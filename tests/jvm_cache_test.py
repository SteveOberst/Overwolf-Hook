"""Two JVMs can extract/load the same packaged build concurrently without overwriting a loaded DLL."""
import subprocess
import sys
import tempfile


def main():
    java, classpath, capture = sys.argv[1:]
    children = []
    with tempfile.TemporaryDirectory(prefix="owh-shared-cache-") as directory:
        command = [java, "--enable-native-access=ALL-UNNAMED", "-Djava.io.tmpdir=" + directory,
                   "--class-path", classpath, "JvmSmokeTest", "transport", capture]
        try:
            children = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(2)]
            for child in children:
                stdout, stderr = child.communicate(timeout=15)
                assert child.returncode == 0, (stdout, stderr)
        finally:
            for child in children:
                if child.poll() is None:
                    child.kill(); child.communicate(timeout=5)


if __name__ == "__main__":
    main()
