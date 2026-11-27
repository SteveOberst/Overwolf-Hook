"""Generate FFM sources only; Gradle owns compilation, tests and packaging."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tarfile
import tempfile
import urllib.request
import sys

def publish_generated(generated: Path, output: Path, temporary: Path):
    """Replace an owned generated tree only after generation succeeds."""
    assert generated.resolve().is_relative_to(temporary.resolve())
    assert temporary.resolve().parent == output.parent.resolve()
    previous = temporary / "previous"
    if output.exists():
        if not output.is_dir() or getattr(output.lstat(), "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0):
            raise RuntimeError("Generated output must not be a reparse path")
        for path in output.rglob("*"):
            if path.is_symlink() or getattr(path.lstat(), "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0):
                raise RuntimeError("Generated output contains a symlink")
            if path.is_file() and path != output / ".owh-ffm-owned":
                valid_package = any(path.is_relative_to(output / "dev/sxlver/overwolfhook" / name)
                                    for name in ("bindings", "model"))
                if path.suffix != ".java" or not valid_package:
                    raise RuntimeError(f"Refusing to replace non-generated content: {path}")
        output.rename(previous)
    try:
        generated.rename(output)
    except BaseException:
        if previous.exists():
            previous.rename(output)
        raise


def prepare_jextract(config: dict, cache: Path) -> Path:
    """Cache the complete pinned archive by digest, publishing only after extraction."""
    cache = cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    destination = cache / config["sha256"]
    executable = Path(config["directory"]) / "bin/jextract.bat"
    if not (destination / executable).is_file():
        with tempfile.TemporaryDirectory(prefix="jextract-", dir=cache) as scratch:
            temporary = Path(scratch)
            archive = temporary / "jextract.tar.gz"
            urllib.request.urlretrieve(config["url"], archive)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != config["sha256"]:
                raise RuntimeError("jextract checksum mismatch")
            extracted = temporary / "extracted"
            with tarfile.open(archive) as package:
                package.extractall(extracted, filter="data")
            if not (extracted / executable).is_file():
                raise RuntimeError("jextract archive does not contain its configured executable")
            extracted.rename(destination)
    return destination / executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jextract", type=Path)
    args = parser.parse_args()
    tools = Path(__file__).resolve().parent
    root = tools.parent.parent
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    generator = args.jextract
    if generator is None:
        config = json.loads((tools / "jextract.json").read_text())
        generator = prepare_jextract(config, output.parent / "toolchain")
    include = root / "include"
    headers = [include / "overwolf_hook" / name for name in
               (root / "cmake/public-headers.txt").read_text(encoding="utf-8").splitlines() if name]
    generator_command = [str(generator.resolve())]
    if generator.suffix.lower() in {".bat", ".cmd"}:
        # CreateProcess cannot launch a batch file directly. Route through cmd
        # while keeping each generated argument as a separate subprocess arg.
        generator_command.insert(0, "/c")
        generator_command.insert(0, "cmd.exe")
    common = generator_command + ["--header-class-name", "OverwolfNative", "-I", str(include)]
    with tempfile.TemporaryDirectory(prefix="owh-ffm-", dir=output.parent) as scratch:
        temporary = Path(scratch)
        generated = temporary / "generated"
        sys.path.insert(0, str(root))
        from tools.bindings.ast import parse
        from tools.bindings.generate import generate_models
        type_headers = [header for header in headers if header.relative_to(include / "overwolf_hook").as_posix().startswith("types/")]
        if not type_headers:
            raise RuntimeError("No public types/*.h headers found")
        runtime = generator.resolve().parent.parent / "runtime"
        native_ast = parse(root, type_headers, runtime)
        model_output = generated / "dev/sxlver/overwolfhook/model"
        model_output.mkdir(parents=True, exist_ok=True)
        generate_models(native_ast, model_output)
        inventory = temporary / "includes.txt"
        # libclang writes precompiled-header scratch files. Keep them in the
        # same writable build tree as the generator's other temporary output.
        environment = os.environ.copy()
        environment.update(TEMP=str(temporary), TMP=str(temporary), TMPDIR=str(temporary))
        subprocess.run(common + ["--dump-includes", str(inventory)] + list(map(str, headers)),
                       check=True, env=environment)
        symbols = []
        for line in inventory.read_text(encoding="utf-8").splitlines():
            match = re.match(r"(--include-\w+)\s+((?:owh_|OWH_)\w+)\s", line)
            if match:
                symbols.append(" ".join(match.groups()))
        if not symbols:
            raise RuntimeError("No public Overwolf Hook symbols found")
        selected = temporary / "public-symbols.txt"
        selected.write_text("\n".join(symbols) + "\n", encoding="utf-8")
        subprocess.run(common + ["@" + str(selected), "--target-package", "dev.sxlver.overwolfhook.bindings",
                                "--output", str(generated)] + list(map(str, headers)),
                       check=True, env=environment)
        (generated / ".owh-ffm-owned").write_text("generated by jextract/libclang; replaced as a complete tree\n")
        publish_generated(generated, output, temporary)


if __name__ == "__main__":
    main()
