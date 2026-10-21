"""Generate bindings from public headers, compile the JVM facade and package matching native binaries.

Only needed by library builders. The resulting JAR needs Java 25, no jextract or third-party JVM libraries.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--java-home", type=Path, default=os.environ.get("JAVA_HOME"))
    parser.add_argument("--jextract", type=Path, help="Override the pinned downloaded generator")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.java_home is None:
        parser.error("Set JAVA_HOME to JDK 25 or pass --java-home")
    java = Path(args.java_home).resolve() / "bin"
    generator = args.jextract
    if generator is None:
        config = json.loads((root / "jvm/toolchain.json").read_text())
        tools = output / "toolchain"
        tools.mkdir(exist_ok=True)
        generator = tools / "jextract-25/bin/jextract.bat"
        if not generator.exists():
            archive = tools / "jextract.tar.gz"
            urllib.request.urlretrieve(config["url"], archive)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != config["sha256"]:
                raise RuntimeError("jextract checksum mismatch")
            with tarfile.open(archive) as package:
                package.extractall(tools, filter="data")
    generator = generator.resolve()
    headers = [root / f"include/overwolf_hook/{name}.h" for name in ("state", "discovery", "probe")]
    common = [str(generator), "--header-class-name", "OverwolfNative", "-I", str(root / "include")]
    subprocess.run(common + ["--dump-includes", str(output / "includes.txt")] + list(map(str, headers)), check=True)
    # Derive the filter from the header inventory, not a second list of ABI registrations.
    symbols = []
    for line in (output / "includes.txt").read_text().splitlines():
        match = re.match(r"(--include-\w+)\s+((?:owh_|OWH_)\w+)\s", line)
        if match:
            symbols.append(" ".join(match.groups()))
    if not symbols:
        raise RuntimeError("No public Overwolf Hook symbols found")
    selected = output / "public-symbols.txt"
    selected.write_text("\n".join(symbols) + "\n", encoding="utf-8")
    generated = output / "generated"
    classes = output / "classes"
    for target in (generated, classes):
        if target.resolve().parent != output:
            raise RuntimeError("Build cleanup escaped its output directory")
        if target.exists():
            shutil.rmtree(target)
    subprocess.run(common + ["@" + str(selected), "--target-package", "dev.sxlver.overwolfhook.bindings",
                            "--output", str(generated)] + list(map(str, headers)), check=True)
    sources = sorted(generated.rglob("*.java")) + sorted((root / "jvm/src/main/java").rglob("*.java"))
    classes.mkdir(exist_ok=True)
    arguments = output / "javac.args"
    arguments.write_text("\n".join('"' + str(path).replace("\\", "/") + '"' for path in sources), encoding="utf-8")
    subprocess.run([str(java / "javac.exe"), "--release", "25", "-d", str(classes), "@" + str(arguments)], check=True)
    native = classes / "overwolf/native/windows-x64"
    native.mkdir(parents=True, exist_ok=True)
    for name in ("owh_client.dll", "owh_capture.dll", "owh_probe.exe", "owh_info.exe"):
        (native / name).write_bytes((args.native_dir / name).read_bytes())
    licenses = classes / "META-INF/overwolf-hook"
    licenses.mkdir(parents=True, exist_ok=True)
    for source in ("LICENSE", "THIRD_PARTY_NOTICES.md", "third_party/MinHook.LICENSE.txt"):
        (licenses / Path(source).name).write_bytes((root / source).read_bytes())
    version = (root / "version.txt").read_text().strip()
    manifest = output / "MANIFEST.MF"
    manifest.write_text(f"Manifest-Version: 1.0\nAutomatic-Module-Name: dev.sxlver.overwolfhook\nImplementation-Version: {version}\n\n")
    package = output / f"overwolf-hook-{version}-windows-x64.jar"
    subprocess.run([str(java / "jar.exe"), "--create", "--file", str(package), "--manifest", str(manifest), "-C", str(classes), "."], check=True)
    test_classes = output / "test-classes"
    test_classes.mkdir(exist_ok=True)
    examples = sorted((root / "examples/java").glob("*.java"))
    tests = sorted((root / "jvm/src/test/java").glob("*.java"))
    subprocess.run([str(java / "javac.exe"), "--release", "25", "--class-path", str(package), "-d", str(test_classes)] + list(map(str, examples + tests)), check=True)
    print(package)


if __name__ == "__main__":
    main()
