"""Verify the installed user package omits maintainer executables."""
from argparse import ArgumentParser
from pathlib import Path


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("installation", type=Path)
    args = parser.parse_args()
    root = args.installation.resolve(strict=True)
    forbidden = {"owh_info.exe", "owh_controller.exe", "owh_info_v2.exe", "owh_controller_v2.exe"}
    unexpected = [str(p.relative_to(root)) for p in root.rglob("*")
                  if p.is_file() and p.name.lower() in forbidden]
    unexpected += [str(p.relative_to(root)) for p in root.rglob("*")
                   if p.is_dir() and p.name.lower() in {"docs", "starters"}]
    if unexpected:
        raise AssertionError(f"Maintainer tools or offline docs leaked into user package: {unexpected}")
    print("PASS installed package excludes maintainer tools and offline docs")


if __name__ == "__main__":
    main()
