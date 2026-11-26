"""Compile the documentation's complete JVM and native examples as consumers."""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / "docs" / "assets" / "examples"


def copy_starter(source: Path, destination: Path) -> None:
    shutil.copytree(
        source,
        destination,
        ignore=shutil.ignore_patterns("build", ".gradle", ".kotlin", "CMakeFiles"),
    )


def run(command: list[str], cwd: Path, report: dict) -> None:
    item = {"command": command, "cwd": str(cwd), "returncode": None}
    report["commands"].append(item)
    completed = subprocess.run(command, cwd=cwd, check=False)
    item["returncode"] = completed.returncode
    if completed.returncode:
        raise subprocess.CalledProcessError(completed.returncode, command)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-dir", required=True, type=Path, help="extracted native SDK prefix")
    parser.add_argument("--ivy-repository", required=True, type=Path, help="local staged Ivy repository")
    parser.add_argument("--work-dir", type=Path, help="fresh directory (default: build/docs-smoke)")
    parser.add_argument("--generator", default="Visual Studio 17 2022")
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--offline", action="store_true", help="pass --offline to Gradle")
    args = parser.parse_args()

    sdk = args.sdk_dir.resolve()
    ivy = args.ivy_repository.resolve()
    work = (args.work_dir or ROOT / "build" / "docs-smoke").resolve()
    report_path = work / "report.json"
    report = {
        "started_at": datetime.now(timezone.utc).isoformat(),
        "sdk_dir": str(sdk), "ivy_repository": str(ivy), "work_dir": str(work),
        "generator": args.generator, "configuration": args.configuration,
        "commands": [], "status": "failed",
    }
    work_owned = False
    try:
        if not sdk.is_dir():
            raise FileNotFoundError(f"SDK directory does not exist: {sdk}")
        if not ivy.is_dir():
            raise FileNotFoundError(f"Ivy repository does not exist: {ivy}")
        if work.exists() and any(work.iterdir()):
            raise FileExistsError(f"Work directory must be fresh and empty: {work}")
        work.mkdir(parents=True, exist_ok=True)
        work_owned = True

        for language in ("java", "kotlin"):
            app = work / language
            copy_starter(ROOT / "starters" / language, app)
            source_dir = app / "src" / "main" / language
            source_dir.mkdir(parents=True, exist_ok=True)
            example_dir = source_dir / "docs-examples"
            example_dir.mkdir()
            for source in sorted((EXAMPLES / language).glob("*.java" if language == "java" else "*.kt")):
                # Keep identically named example and starter files independent.
                shutil.copy2(source, example_dir / source.name)
            # Compile the installation's actual load-check source as well.
            suffix = ".java" if language == "java" else ".kt"
            for source in (ROOT / "docs/assets/integration" / language).glob("*" + suffix):
                shutil.copy2(source, source_dir / source.name)
            # Keep starter entry points unchanged: they intentionally attach and
            # render. A dedicated installed consumer checks loading without a host.
            if language == "java":
                (source_dir / "DocsLoadCheck.java").write_text(
                    "import dev.sxlver.overwolfhook.OverwolfHook;\n"
                    "public final class DocsLoadCheck {\n"
                    " public static void main(String[] args) throws Exception {\n"
                    "  Class.forName(\"FirstOverlay\", false, DocsLoadCheck.class.getClassLoader()).getMethod(\"main\", String[].class);\n"
                    "  try (var sdk = OverwolfHook.open()) { sdk.gameState().sources(); }\n"
                    " }\n}\n", encoding="utf-8")
                load_entry = "DocsLoadCheck"
            else:
                (source_dir / "DocsLoadCheck.kt").write_text(
                    "package smoke\n"
                    "import dev.sxlver.overwolfhook.OverwolfHook\n"
                    "import dev.sxlver.overwolfhook.kotlin.gameState\n"
                    "fun main() {\n"
                    " Class.forName(\"FirstOverlayKt\", false, Thread.currentThread().contextClassLoader)\n"
                    "  .getMethod(\"main\", Array<String>::class.java)\n"
                    " OverwolfHook.open().use { it.gameState.sources() }\n}\n",
                    encoding="utf-8")
                load_entry = "smoke.DocsLoadCheckKt"
            gradle = str(app / "gradlew.bat")
            command = [gradle, "installDist", f"-PsdkRepository={ivy}"]
            if args.offline:
                command.append("--offline")
            run(command, app, report)
            distribution = app / "build" / "install" / f"first-overlay-{language}"
            java = Path(os.environ["JAVA_HOME"]) / "bin/java.exe" if "JAVA_HOME" in os.environ else "java"
            run([str(java), "--enable-native-access=ALL-UNNAMED", "-cp",
                 str(distribution / "lib/*"), load_entry], app, report)
            run([str(java), "--enable-native-access=ALL-UNNAMED", "-cp",
                 str(distribution / "lib/*"), "SdkCheck" if language == "java" else "SdkCheckKt"],
                app, report)

        native = work / "native"
        copy_starter(ROOT / "starters" / "native", native)
        c_dir = native / "docs-examples"
        c_dir.mkdir()
        for source in sorted((EXAMPLES / "c").glob("*.c")):
            shutil.copy2(source, c_dir / source.name)
        for source in sorted((EXAMPLES / "cpp").glob("*.cpp")):
            shutil.copy2(source, c_dir / source.name)
        shutil.copy2(ROOT / "docs/assets/integration/native/main.c",
                     c_dir / "installation_check.c")
        (c_dir / "load_check.c").write_text(
            '#include <overwolf_hook/abi/sdk.h>\n'
            'int main(void) { owh_sdk *sdk = 0;\n'
            ' if (owh_sdk_open(&sdk).status != OWH_OK) return 1;\n'
            ' owh_sdk_close(sdk);\n'
            ' return owh_sdk_abi_version() == OWH_ABI_VERSION ? 0 : 2; }\n',
            encoding="utf-8")
        with (native / "CMakeLists.txt").open("a", encoding="utf-8") as cmake:
            cmake.write('\nenable_language(CXX)\n')
            for source in sorted(c_dir.iterdir()):
                target = "docs_" + source.stem
                relative = source.relative_to(native).as_posix()
                standalone = bool(re.search(r"\bint\s+main\s*\(", source.read_text()))
                cmake.write(f'add_executable({target} "{relative}")\n' if standalone else
                            f'add_library({target} OBJECT "{relative}")\n')
                language = "cxx_std_20" if source.suffix == ".cpp" else "c_std_17"
                library = "OverwolfHook::Cpp" if source.suffix == ".cpp" else "OverwolfHook::Client"
                cmake.write(f'target_compile_features({target} PRIVATE {language})\n'
                            f'target_compile_definitions({target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)\n'
                            f'target_link_libraries({target} PRIVATE {library})\n')
                if source.name == "shared_texture_overlay.cpp":
                    cmake.write(f'target_link_libraries({target} PRIVATE d3d11 dxgi)\n')
        build = native / "build"
        run(["cmake", "-S", str(native), "-B", str(build), "-G", args.generator,
             "-A", "x64", f"-DCMAKE_PREFIX_PATH={sdk}"], work, report)
        run(["cmake", "--build", str(build), "--config", args.configuration], work, report)
        run([str(build / args.configuration / "docs_load_check.exe")], work, report)
        run([str(build / args.configuration / "docs_installation_check.exe")], work, report)
        run([str(build / args.configuration / "docs_sdk_check.exe")], work, report)
        report["status"] = "passed"
        return 0
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        print(report["error"], file=sys.stderr)
        return 1
    finally:
        report["finished_at"] = datetime.now(timezone.utc).isoformat()
        if work_owned:
            report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            print(f"Smoke report: {report_path}")


if __name__ == "__main__":
    raise SystemExit(main())
