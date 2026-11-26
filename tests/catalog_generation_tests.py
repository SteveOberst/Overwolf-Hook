"""Stage an isolated generated-catalog variant for the C++ router regression."""
from argparse import ArgumentParser
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
PROBE = "OWH_METHOD(GeneratedProbe, generated_probe, 97, Empty, ResultResponse)\n"


def main():
    parser = ArgumentParser()
    parser.add_argument("--output", required=True, type=Path, help="generated include root in the build tree")
    args = parser.parse_args()
    output = args.output.resolve()
    core_source = ROOT / "modules/core/include/owh/core"
    core_output = output / "owh/core"
    core_output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(core_source, core_output, dirs_exist_ok=True)

    catalog = core_output / "events.def"
    contents = catalog.read_text(encoding="utf-8")
    if "GeneratedProbe" in contents:
        raise SystemExit("GeneratedProbe must not be present in the product catalog")
    catalog.write_text(contents.rstrip() + "\n" + PROBE, encoding="utf-8", newline="\n")
    print(f"Staged synthetic catalog under {output}")


if __name__ == "__main__":
    main()
