"""Optional end-to-end JVM session ownership test against our own host, never an actual Overwolf process."""
import ctypes
import queue
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def main():
    host, consumer, java, classpath = sys.argv[1:5]
    kotlin_classpath = sys.argv[5] if len(sys.argv) == 6 else None
    with tempfile.TemporaryDirectory(prefix="owh-jvm-test-") as directory:
        ready = Path(directory) / "READY"
        fixture = subprocess.Popen([host, "--serve", consumer, str(ready)])
        jvm = None
        example = None
        try:
            deadline = time.monotonic() + 10
            while not ready.exists():
                if fixture.poll() is not None or time.monotonic() >= deadline:
                    raise AssertionError("Owned host did not start")
                time.sleep(0.01)
            command = [java, "--enable-native-access=ALL-UNNAMED", "--class-path", classpath, "JvmSmokeTest"]
            silent_ready = Path(directory) / "SILENT_READY"
            silent = subprocess.Popen([host, "--serve-silent", consumer, str(silent_ready)])
            try:
                deadline = time.monotonic() + 10
                while not silent_ready.exists():
                    assert silent.poll() is None and time.monotonic() < deadline
                    time.sleep(0.01)
                subprocess.run(command + ["silent", str(silent.pid)], check=True, timeout=20)
                if kotlin_classpath:
                    subprocess.run([java, "--enable-native-access=ALL-UNNAMED", "-cp", kotlin_classpath,
                        "dev.sxlver.overwolfhook.kotlin.OwnedCaptureTestKt", "silent", str(silent.pid)], check=True, timeout=20)
            finally:
                silent.kill(); silent.wait(timeout=5)
            subprocess.run(command + ["capture", str(fixture.pid)], check=True, timeout=20)
            if kotlin_classpath:
                subprocess.run([java, "--enable-native-access=ALL-UNNAMED", "-cp", kotlin_classpath,
                    "dev.sxlver.overwolfhook.kotlin.OwnedCaptureTestKt", "active", str(fixture.pid)], check=True, timeout=20)
            recording = Path(directory) / "explicit.bin"
            subprocess.run(command + ["record", str(fixture.pid), str(recording)], check=True, timeout=20)
            assert recording.stat().st_size > 0
            jvm = subprocess.Popen(command + ["hold", str(fixture.pid)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            line = jvm.stdout.readline().decode().strip()
            assert line.startswith("controller="), (line, jvm.poll())
            controller_pid = int(line.split("=", 1)[1])
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
            kernel.OpenProcess.restype = ctypes.c_void_p
            kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            process = kernel.OpenProcess(0x100000, False, controller_pid)
            assert process
            try:
                jvm.kill(); jvm.communicate(timeout=5)
                assert kernel.WaitForSingleObject(process, 5000) == 0, "Controller survived JVM death"
            finally:
                kernel.CloseHandle(process)
            # Hook shutdown is asynchronous after controller death. Wait for the actual owner watcher.
            time.sleep(1.5)
            subprocess.run(command + ["capture", str(fixture.pid)], check=True, timeout=20)
            if kotlin_classpath:
                example = subprocess.Popen([java, "--enable-native-access=ALL-UNNAMED", "-cp", kotlin_classpath,
                    "ReadStateKt", "events", str(fixture.pid)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    text=True, bufsize=1)
                output = queue.Queue()
                output_reader = threading.Thread(target=lambda: [output.put(line) for line in example.stdout], daemon=True)
                output_reader.start()
                received = [output.get(timeout=10) for _ in range(3)]
                assert all(line.startswith("channel=900001 sequence=") for line in received), received
                fixture.kill(); fixture.wait(timeout=5)
                example.wait(timeout=20)
                output_reader.join(timeout=5)
                errors = example.stderr.read()
                assert example.returncode == 0, errors
        finally:
            if example is not None and example.poll() is None:
                example.kill(); example.communicate(timeout=5)
            if jvm is not None and jvm.poll() is None:
                jvm.kill(); jvm.communicate(timeout=5)
            fixture.kill(); fixture.wait(timeout=5)


if __name__ == "__main__":
    main()
