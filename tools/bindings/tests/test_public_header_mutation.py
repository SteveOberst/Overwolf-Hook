"""Mutate one canonical C field and check C++, jextract, Java, and Kotlin consumers."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from tools.bindings.ast import parse
from tools.bindings.generate import generate_models


ROOT = Path(__file__).resolve().parents[3]
JEXTRACT_ROOT = (Path(os.environ["OWH_JEXTRACT_RUNTIME"]).parent
                 if os.environ.get("OWH_JEXTRACT_RUNTIME")
                 else ROOT / "jvm/build/generated/sources/toolchain/jextract-25")
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


def jdk_25():
    candidates = []
    for variable in ("JAVA_HOME", "JDK_HOME"):
        if os.environ.get(variable):
            candidates.append(Path(os.environ[variable]))
    candidates.extend(Path("C:/Program Files/Java").glob("jdk-25*"))
    candidates.extend(Path.home().glob(".jdks/openjdk-25*"))
    for home in candidates:
        javac, java = home / "bin/javac.exe", home / "bin/java.exe"
        if not javac.is_file() or not java.is_file():
            continue
        try:
            version = subprocess.run([str(javac), "--version"], capture_output=True,
                                     text=True, timeout=5, check=True).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        if "javac 25" in version:
            return javac, java
    return None


def kotlin_version() -> str:
    build = (ROOT / "jvm/build.gradle.kts").read_text(encoding="utf-8")
    match = re.search(r'kotlin\("jvm"\)\s+version\s+"([^"]+)"', build)
    if not match:
        raise AssertionError("could not read Kotlin JVM plugin version from jvm/build.gradle.kts")
    return match.group(1)


def gradle_artifact(group: str, artifact: str, version: str) -> Path | None:
    # Gradle caches the dotted Maven group as a literal directory name.
    base = Path.home() / ".gradle/caches/modules-2/files-2.1" / group / artifact / version
    if not base.is_dir():
        return None
    return next((path for path in base.glob("*/*.jar")
                 if not path.name.endswith(("-sources.jar", "-javadoc.jar"))), None)


def kotlin_compiler_classpath(version: str) -> tuple[Path, ...] | None:
    # Gradle resolves the compiler's runtime dependencies, including kotlin-reflect.
    # Cache/POM probing remains useful when running this test outside Gradle.
    if configured := os.environ.get("OWH_KOTLIN_COMPILER_CLASSPATH"):
        jars = tuple(Path(item) for item in configured.split(os.pathsep))
        stdlib = next((path for path in jars if path.name.startswith("kotlin-stdlib-")), None)
        if not stdlib or not all(path.is_file() for path in jars):
            raise AssertionError("incomplete Gradle Kotlin compiler classpath")
        return stdlib, *(path for path in jars if path != stdlib)
    compiler = gradle_artifact("org.jetbrains.kotlin", "kotlin-compiler-embeddable", version)
    stdlib = gradle_artifact("org.jetbrains.kotlin", "kotlin-stdlib", version)
    if not compiler or not stdlib:
        return None
    jars = {compiler, stdlib}
    queue = [("org.jetbrains.kotlin", "kotlin-compiler-embeddable", version)]
    visited = set()
    namespace = {"m": "http://maven.apache.org/POM/4.0.0"}
    while queue:
        group, artifact, current_version = queue.pop()
        identity = (group, artifact, current_version)
        if identity in visited:
            continue
        visited.add(identity)
        base = Path.home() / ".gradle/caches/modules-2/files-2.1" / group / artifact / current_version
        pom = next(base.glob("*/*.pom"), None) if base.is_dir() else None
        if not pom:
            continue
        document = ET.parse(pom).getroot()
        for dependency in document.findall("m:dependencies/m:dependency", namespace):
            scope = dependency.findtext("m:scope", default="compile", namespaces=namespace)
            optional = dependency.findtext("m:optional", default="false", namespaces=namespace)
            if scope not in {"compile", "runtime"} or optional == "true":
                continue
            dep_group = dependency.findtext("m:groupId", namespaces=namespace)
            dep_artifact = dependency.findtext("m:artifactId", namespaces=namespace)
            dep_version = dependency.findtext("m:version", namespaces=namespace)
            if not dep_group or not dep_artifact or not dep_version or "${" in dep_version:
                continue
            jar = gradle_artifact(dep_group, dep_artifact, dep_version)
            if jar:
                jars.add(jar)
                queue.append((dep_group, dep_artifact, dep_version))
    return stdlib, compiler, *sorted(jars - {stdlib, compiler})


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
        toolchain = jdk_25()
        jextract = JEXTRACT_ROOT / "bin/jextract.bat"
        runtime = JEXTRACT_ROOT / "runtime"
        if not (toolchain and jextract.is_file() and (runtime / "bin/libclang.dll").is_file()):
            self.skipTest("pinned jextract and Java 25 toolchain are not installed")
        javac, java = toolchain
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
        version = kotlin_version()
        toolchain = jdk_25()
        compiler_classpath = kotlin_compiler_classpath(version)
        if not toolchain or not compiler_classpath:
            self.skipTest(f"JDK 25 and cached Kotlin compiler {version} are required for this subtest")
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
