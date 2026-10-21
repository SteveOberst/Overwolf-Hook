"""Optional end-to-end JVM session ownership test against our own host, never an actual Overwolf process."""
import ctypes
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def main():
    host, consumer, java, classpath = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="owh-jvm-test-") as directory:
        ready = Path(directory) / "READY"
        fixture = subprocess.Popen([host, "--serve", consumer, str(ready)])
        jvm = None
        try:
            deadline = time.monotonic() + 10
            while not ready.exists():
                if fixture.poll() is not None or time.monotonic() >= deadline:
                    raise AssertionError("Owned host did not start")
                time.sleep(0.01)
            command = [java, "--enable-native-access=ALL-UNNAMED", "--class-path", classpath, "JvmSmokeTest"]
            subprocess.run(command + ["capture", str(fixture.pid)], check=True, timeout=20)
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
        finally:
            if jvm is not None and jvm.poll() is None:
                jvm.kill(); jvm.communicate(timeout=5)
            fixture.kill(); fixture.wait(timeout=5)


if __name__ == "__main__":
    main()
