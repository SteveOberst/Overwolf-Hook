"""Mutate one canonical C field and check C++, jextract, Java, and Kotlin consumers."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

from tools.bindings.ast import parse
from tools.bindings.generate import generate_models
from tools.bindings.tests.toolchain import java_tools, kotlin_classpath, tool_path


ROOT = Path(__file__).resolve().parents[3]
EVENTS_HEADER = ROOT / "include/overwolf_hook/types/events.h"
MUTATION_FIELD = "generation_probe"
OLD_FIELD = "    uint64_t process_started, session_id;"
OLD_SIZE = "sizeof(owh_event_capabilities) == 48"
NEW_SIZE = "sizeof(owh_event_capabilities) == 56"


def mutate_event_header(work: Path) -> tuple[Path, Path]:
    include = work / "include/overwolf_hook/types"
    shutil.copytree(ROOT / "include/overwolf_hook/types", include)
    mutated = include / "events.h"
    original = mutated.read_text(encoding="utf-8")
    if original.count(OLD_FIELD) != 1 or original.count(OLD_SIZE) != 1:
        raise AssertionError("canonical EventCapabilities declaration changed; update mutation fixture")
    mutated.write_text(original.replace(OLD_FIELD, OLD_FIELD + f"\n    uint32_t {MUTATION_FIELD};")
                        .replace(OLD_SIZE, NEW_SIZE), encoding="utf-8")
    return work / "include", mutated


def find_vcvars64() -> Path | None:
    candidates = [Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"),
                  Path("C:/Program Files/Microsoft Visual Studio/Installer/vswhere.exe")]
    for vswhere in candidates:
        if not vswhere.is_file():
            continue
        result = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                                 "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                 "-property", "installationPath"], capture_output=True, text=True, timeout=10)
        if result.returncode == 0 and result.stdout.strip():
            vcvars = Path(result.stdout.strip()) / "VC/Auxiliary/Build/vcvars64.bat"
            if vcvars.is_file():
                return vcvars
    return None


class PublicHeaderMutationTest(unittest.TestCase):
    def test_cpp_controller_signature_and_field_follow_mutated_header(self):
        vcvars = find_vcvars64()
        if not vcvars:
            self.skipTest("Visual Studio C++ x64 compiler environment is not installed")
        with tempfile.TemporaryDirectory(prefix="owh-header-mutation-cpp-") as temporary:
            work = Path(temporary)
            include, _ = mutate_event_header(work)
            source = work / "field_probe.cpp"
            source.write_text(r'''#include <overwolf_hook/events.hpp>
#include <type_traits>
#include <utility>
static_assert(std::is_same_v<decltype(std::declval<overwolf_hook::EventController>().capabilities()),
                             owh_event_capabilities>);
int main() {
    owh_event_capabilities value{};
    value.generation_probe = 73;
    return value.generation_probe == 73 ? 0 : 1;
}
''', encoding="utf-8")
            executable = work / "field_probe.exe"
            command = (f'call "{vcvars}" >nul && cl.exe /nologo /std:c++20 /EHsc '
                       f'/I"{include}" /I"{ROOT / "include"}" "{source}" /Fe:"{executable}"')
            compiled = subprocess.run(command, shell=True, cwd=work,
                                      capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, command + "\n" + compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(executable)], cwd=work, capture_output=True, text=True, timeout=15)
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)

    def prepare_mutated_java(self, work: Path):
        javac, java = java_tools()
        runtime = tool_path("OWH_JEXTRACT_RUNTIME")
        jextract = runtime.parent / "bin/jextract.bat"
        include, mutated = mutate_event_header(work)
        ast = parse(ROOT, [mutated], runtime)
        ast["records"] = {name: ast["records"][name]
                          for name in ("owh_event_capabilities", "owh_result")}
        generated = work / "src"
        models = generated / "dev/sxlver/overwolfhook/model"
        models.mkdir(parents=True)
        generate_models(ast, models)
        temp = work / "temp"
        temp.mkdir()
        env = os.environ.copy()
        env.update(TEMP=str(temp), TMP=str(temp), TMPDIR=str(temp),
                   JAVA_TOOL_OPTIONS=f"-Djava.io.tmpdir={temp} -XX:-UsePerfData")
        jextract = ["cmd.exe", "/c", str(jextract), "--header-class-name", "MutationNative",
                    "-I", str(include.parent.parent.parent), "-I", str(ROOT / "include"),
                    "--target-package", "dev.sxlver.overwolfhook.bindings", "--output", str(generated),
                    "--include-struct", "owh_result", "--include-struct", "owh_event_capabilities",
                    str(mutated)]
        extracted = subprocess.run(jextract, cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(extracted.returncode, 0, extracted.stdout + extracted.stderr)
        classes = work / "classes"
        classes.mkdir()
        probe = work / "MutationProbe.java"
        probe.write_text(r'''import dev.sxlver.overwolfhook.bindings.owh_event_capabilities;
import dev.sxlver.overwolfhook.model.EventCapabilities;
import java.lang.foreign.Arena;
public final class MutationProbe {
    public static void main(String[] args) {
        try (Arena arena = Arena.ofConfined()) {
            var raw = owh_event_capabilities.allocate(arena);
            owh_event_capabilities.generation_probe(raw, 73);
            var model = EventCapabilities.copyOf(raw);
            if (model.getGenerationProbe() != 73) throw new AssertionError("generated Java field access mismatch");
        }
        System.out.println("PASS mutated canonical header reaches Java model");
    }
}
''', encoding="utf-8")
        sources = [str(path) for path in generated.rglob("*.java")] + [str(probe)]
        compiled = subprocess.run([str(javac), "--release", "25", "-d", str(classes), *sources],
                                  cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        return java, classes, probe, javac

    def test_jextract_java_model_field_follows_mutated_header(self):
        with tempfile.TemporaryDirectory(prefix="owh-header-mutation-java-") as temporary:
            work = Path(temporary)
            java, classes, _, _ = self.prepare_mutated_java(work)
            executed = subprocess.run([str(java), "--enable-native-access=ALL-UNNAMED", "-cp", str(classes),
                                       "MutationProbe"], cwd=ROOT, capture_output=True, text=True, timeout=30)
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
            self.assertIn("PASS mutated canonical header reaches Java model", executed.stdout)

    def test_kotlin_synthetic_property_follows_mutated_header(self):
        compiler_classpath = kotlin_classpath()
        with tempfile.TemporaryDirectory(prefix="owh-header-mutation-kotlin-") as temporary:
            work = Path(temporary)
            java, classes, _, _ = self.prepare_mutated_java(work)
            kotlin_source = work / "MutationProbe.kt"
            kotlin_source.write_text("""import dev.sxlver.overwolfhook.model.EventCapabilities
fun generationProbe(value: EventCapabilities): Long = value.generationProbe
""", encoding="utf-8")
            stdlib, *compiler_jars = compiler_classpath
            kotlin_cp = os.pathsep.join(map(str, [classes, stdlib]))
            compiler_cp = os.pathsep.join(map(str, [*compiler_classpath]))
            compiled = subprocess.run(
                [str(java), "-cp", compiler_cp, "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler",
                 "-no-stdlib", "-classpath", kotlin_cp, "-d", str(classes), str(kotlin_source)],
                cwd=ROOT, capture_output=True, text=True, timeout=90)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)


if __name__ == "__main__":
    unittest.main()
