import os
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    controller = Path(sys.argv[1]).resolve()
    pid = str(os.getpid())  # This Python test process is intentionally not Overwolf.
    with tempfile.TemporaryDirectory(prefix="owh-controller-cli-") as folder:
        root = Path(folder)
        cases = [
            [str(controller), pid, "1", str(root / "record.bin"), "--stop-file", str(root / "stop")],
            [str(controller), "stream", pid, "--stop-file", str(root / "stream-stop")],
        ]
        for command in cases:
            result = subprocess.run(command, capture_output=True, text=True, timeout=15)
            output = result.stdout + result.stderr
            if result.returncode == 0 or "Usage:" in output or "Host attach failed" not in output:
                raise AssertionError((command, result.returncode, output))
        if (root / "record.bin").exists() or (root / "stop").exists() or (root / "stream-stop").exists():
            raise AssertionError("Rejected target created controller artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
