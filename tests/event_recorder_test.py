"""Full controller against an owned process and the locally reviewed consumer."""
import ctypes
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

spec = importlib.util.spec_from_file_location("inspector", Path(__file__).parents[1] / "tools/inspect_capture.py")
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def wait_for(predicate, message):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.025)
    raise AssertionError(message)


def main():
    probe, host, consumer, capture = map(lambda p: str(Path(p).resolve()), sys.argv[1:5])
    unreviewed = sys.argv[5:] == ["--unreviewed"]
    children = []
    with tempfile.TemporaryDirectory(prefix="owh-events-") as temporary:
        root = Path(temporary)
        if unreviewed:
            # An inert PE overlay changes the file hash without changing loaded
            # instructions/layout. Never modify or redistribute the source DLL.
            changed_consumer = root / "OWGameEventsConsumer.dll"
            changed_consumer.write_bytes(Path(consumer).read_bytes() + b"owh-owned-test-overlay")
            consumer = str(changed_consumer)
        ready = root / "READY"
        fixture = subprocess.Popen([host, "--serve", consumer, str(ready)])
        children.append(fixture)
        try:
            wait_for(ready.exists, "owned host not ready")

            def start(name, seconds="0"):
                path, stop = root / (name + ".bin"), root / (name + ".STOP")
                controller = subprocess.Popen([probe, str(fixture.pid), capture, seconds, str(path), "all", "--stop-file", str(stop)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
                children.append(controller)
                wait_for(lambda: path.exists() and path.stat().st_size > 0 or controller.poll() is not None,
                         "controller produced no data")
                if controller.poll() is not None:
                    raise AssertionError(controller.communicate())
                return controller, path, stop

            def finish(controller, path):
                stdout, stderr = controller.communicate(timeout=15)
                assert controller.returncode == 0, (stdout, stderr)
                assert (b"warning: consumer SHA-256 is unreviewed" in stderr) == unreviewed, stderr
                report = inspector.inspect(path)
                assert report["records"] > 0 and report["channels"] == [900001]
                assert "fixture.synthetic" in report["event_names"]
                return stdout

            # Native long capture remains live until our stop request, not an arbitrary cap.
            controller, path, stop = start("explicit-stop", "600")
            stop.touch()
            stdout = finish(controller, path)
            assert b"enabled=0" in stdout and b"active=0" in stdout

            # Indefinite mode responds to a console signal and is reusable afterward.
            controller, path, _ = start("console-stop")
            os.kill(controller.pid, signal.CTRL_BREAK_EVENT)
            finish(controller, path)

            # Hard controller death triggers the hook's owner watcher; retry
            # start only after its genuine asynchronous cleanup completes.
            controller, _, _ = start("owner-dies")
            controller.kill(); controller.communicate(timeout=5)
            deadline = time.monotonic() + 6
            while True:
                replacement_path = root / ("replacement-" + str(time.monotonic_ns()) + ".bin")
                replacement = subprocess.run([probe, str(fixture.pid), capture, "1", str(replacement_path)],
                    capture_output=True, timeout=15)
                if replacement.returncode == 0:
                    assert inspector.inspect(replacement_path)["records"] > 0
                    break
                assert b"status 7" in replacement.stderr and time.monotonic() < deadline, replacement.stderr
                time.sleep(0.05)

            controller, path, _ = start("host-dies")
            fixture.kill(); fixture.wait(timeout=5)
            stdout = finish(controller, path)
            assert b"target_exited=1" in stdout and b"diagnostics=unavailable" in stdout
        finally:
            for child in reversed(children):
                if child.poll() is None:
                    child.kill()
                if child.stdout is not None:
                    child.communicate(timeout=5)
                else:
                    child.wait(timeout=5)
    print("PASS actual controller, exact frames, long/indefinite capture, stop file, Ctrl-Break, owner death/restart, host death/drain")


if __name__ == "__main__":
    main()
