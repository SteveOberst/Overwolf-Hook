"""Prepare website downloads and build/preview the authored docs directly.

Zensical owns page rendering and live rebuilds. This helper only prepares starter
archives and versioned dependency downloads; it never publishes a site.
"""
from __future__ import annotations

import argparse
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile
from reference import stage_reference

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docs"
SDK_VERSION_TOKEN = "@OWH_SDK_VERSION@"
IGNORED_PARTS = {"build", ".gradle", ".kotlin", "__pycache__"}
STARTER_NAMES = {"README.md", "LICENSE", ".gitignore", "CMakeLists.txt", "main.c",
                 "gradlew", "gradlew.bat", "gradle.properties", "settings.gradle.kts",
                 "build.gradle.kts", "gradle-wrapper.jar", "gradle-wrapper.properties",
                 "GRADLE-LICENSE.txt", "GRADLE-NOTICE.txt", "FirstOverlay.java", "FirstOverlay.kt"}


def write_changed(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def render_version(data: bytes, version: str) -> bytes:
    """Fill the documentation's SDK version without touching its authored templates."""
    return data.replace(SDK_VERSION_TOKEN.encode(), version.encode())


def starter_files(language: str) -> list[Path]:
    base = ROOT / "starters" / language
    return sorted(p for p in base.rglob("*") if p.is_file()
                  and not IGNORED_PARTS.intersection(p.relative_to(base).parts)
                  and p.name in STARTER_NAMES)


def stage(source_ref: str, release_tag: str) -> Path:
    version = (ROOT / "version.txt").read_text().strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?", version):
        raise ValueError("version.txt must contain a semantic SDK version")
    if release_tag and release_tag != "v" + version:
        raise ValueError(f"Release tag {release_tag!r} does not match version.txt ({version})")
    if not source_ref or any(c.isspace() for c in source_ref):
        raise ValueError("Source ref must be an explicit Git ref without whitespace")
    def put(path: Path, data: bytes) -> None:
        write_changed(path, data)

    stage_reference(put)

    # Archives contain only the independent project's source, wrapper and licenses.
    for language in ("native", "java", "kotlin"):
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in starter_files(language):
                name = path.relative_to(ROOT / "starters" / language).as_posix()
                entry = zipfile.ZipInfo(f"{language}-starter/{name}", (1980, 1, 1, 0, 0, 0))
                entry.external_attr = (0o755 if name == "gradlew" else 0o644) << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                data = render_version(path.read_bytes(), version)
                archive.writestr(entry, data)
                # Source files can also be viewed directly from a tutorial link.
                if path.suffix != ".md":
                    put(SOURCE / "downloads" / language / name, data)
        put(SOURCE / "downloads" / f"{language}-starter.zip", buffer.getvalue())

    # Only versioned dependency snippets need a generated counterpart. Pages,
    # examples and images are read directly from docs/ by Zensical.
    for path in sorted((ROOT / "docs/assets/integration").rglob("*")):
        if path.is_file():
            put(SOURCE / "assets/generated/integration" / path.relative_to(ROOT / "docs/assets/integration"),
                render_version(path.read_bytes(), version))
    put(SOURCE / "build-info.json", (json.dumps({"sdk_version": version,
                  "source_ref": source_ref, "release_tag": release_tag or None}, indent=2) + "\n").encode())
    return ROOT / "zensical.toml"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "serve"))
    parser.add_argument("--source-ref", default="main", help="Source ref recorded in website build metadata")
    parser.add_argument("--release-tag", default="", help="Published tag; must match version.txt")
    parser.add_argument("--dev-addr", default="127.0.0.1:8000")
    args = parser.parse_args()
    config = stage(args.source_ref, args.release_tag)
    command = [sys.executable, "-m", "zensical", args.command, "--config-file", str(config)]
    if args.command == "build":
        command += ["--clean", "--strict"]
    else:
        command += ["--dev-addr", args.dev_addr]
    result = subprocess.call(command, cwd=ROOT)
    if result == 0 and args.command == "build":
        return subprocess.call([sys.executable, str(ROOT / "tools/docs/check.py")], cwd=ROOT)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
