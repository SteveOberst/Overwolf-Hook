"""End-to-end nested-owned copy check using the pinned jextract/JDK toolchain."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import os

from tools.bindings.ast import parse
from tools.bindings.generate import generate_models
from tools.bindings.tests.toolchain import java_tools, tool_path


ROOT = Path(__file__).resolve().parents[3]
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "nested_owned.h"
class NestedCopyRuntimeTest(unittest.TestCase):
    def test_jextract_compiled_models_own_recursive_spans_after_source_arena_closes(self):
        runtime = tool_path("OWH_JEXTRACT_RUNTIME")
        jextract = runtime.parent / "bin/jextract.bat"
        javac, java = java_tools()

        ast = parse(ROOT, [FIXTURE], runtime)
        with tempfile.TemporaryDirectory(prefix="owh-nested-copy-") as temporary:
            work = Path(temporary)
            binding = work / "src/dev/sxlver/overwolfhook/bindings"
            model = work / "src/dev/sxlver/overwolfhook/model"
            binding.mkdir(parents=True)
            model.mkdir(parents=True)
            generate_models(ast, model)
            command = ["cmd.exe", "/c", str(jextract), "--header-class-name", "FixtureNative",
                       "-I", str(ROOT / "include"), "--target-package",
                       "dev.sxlver.overwolfhook.bindings", "--output", str(work / "src"),
                       "--include-struct", "owh_result",
                       "--include-struct", "owh_nested_bytes", "--include-struct", "owh_nested_owned",
                       str(FIXTURE)]
            scratch = work / "temp"
            scratch.mkdir()
            environment = os.environ.copy()
            environment.update(TEMP=str(scratch), TMP=str(scratch), TMPDIR=str(scratch),
                               JAVA_TOOL_OPTIONS=f"-Djava.io.tmpdir={scratch} -XX:-UsePerfData")
            extracted = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True, text=True)
            if extracted.returncode:
                self.fail(f"jextract failed ({extracted.returncode}):\n{extracted.stdout}\n{extracted.stderr}")

            runner = work / "NestedCopyRuntime.java"
            runner.write_text(r'''import dev.sxlver.overwolfhook.bindings.owh_nested_bytes;
import dev.sxlver.overwolfhook.bindings.owh_nested_owned;
import dev.sxlver.overwolfhook.model.NestedOwned;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Arrays;

public final class NestedCopyRuntime {
    public static void main(String[] args) {
        NestedOwned owned;
        NestedOwned emptyOwned;
        long sourceDataAddress;
        try (Arena source = Arena.ofConfined()) {
            MemorySegment bytes = source.allocate(3);
            bytes.setAtIndex(ValueLayout.JAVA_BYTE, 0, (byte) 9);
            bytes.setAtIndex(ValueLayout.JAVA_BYTE, 1, (byte) 8);
            bytes.setAtIndex(ValueLayout.JAVA_BYTE, 2, (byte) 7);
            MemorySegment label = source.allocate(4);
            label.setAtIndex(ValueLayout.JAVA_BYTE, 0, (byte) 'o');
            label.setAtIndex(ValueLayout.JAVA_BYTE, 1, (byte) 'k');
            label.setAtIndex(ValueLayout.JAVA_BYTE, 2, (byte) '!');
            label.setAtIndex(ValueLayout.JAVA_BYTE, 3, (byte) 0);
            MemorySegment borrowed = source.allocate(1);
            MemorySegment inner = owh_nested_bytes.allocate(source);
            owh_nested_bytes.data(inner, bytes);
            owh_nested_bytes.size(inner, 3);
            owh_nested_bytes.label(inner, label);
            owh_nested_bytes.borrowed(inner, borrowed);
            MemorySegment outer = owh_nested_owned.allocate(source);
            owh_nested_owned.nested(outer).copyFrom(inner);
            owh_nested_owned.marker(outer, 42);
            sourceDataAddress = bytes.address();
            owned = NestedOwned.copyOf(outer);
            owh_nested_bytes.size(owh_nested_owned.nested(outer), 0);
            emptyOwned = NestedOwned.copyOf(outer);
            if (owned.getNested().getData().asMemorySegment().address() == sourceDataAddress)
                throw new AssertionError("nested span still aliases source allocation");
            if (!owned.getNested().getBorrowed().equals(MemorySegment.NULL))
                throw new AssertionError("nested borrowed pointer was retained");
        }
        if (!Arrays.equals(owned.getNested().getData().copyBytes(), new byte[]{9, 8, 7}))
            throw new AssertionError("nested span was not readable after source arena closed");
        if (!Arrays.equals(owned.getNested().getLabel().copyBytes(), new byte[]{'o', 'k', '!'}))
            throw new AssertionError("nested c-string was not copied");
        if (!emptyOwned.getNested().getData().asMemorySegment().equals(MemorySegment.NULL))
            throw new AssertionError("zero-length nested span retained its source pointer");
        System.out.println("PASS recursive nested output ownership");
    }
}
''', encoding="utf-8")
            classes = work / "classes"
            classes.mkdir()
            sources = [str(path) for path in (work / "src").rglob("*.java")] + [str(runner)]
            compiled = subprocess.run([javac, "--release", "25", "-d", str(classes), *sources],
                                      cwd=ROOT, capture_output=True, text=True, timeout=120)
            if compiled.returncode:
                self.fail(f"javac failed ({compiled.returncode}):\n{compiled.stdout}\n{compiled.stderr}")
            executed = subprocess.run([java, "--enable-native-access=ALL-UNNAMED", "-cp", str(classes),
                                       "NestedCopyRuntime"], cwd=ROOT, capture_output=True, text=True,
                                      timeout=30)
            if executed.returncode:
                self.fail(f"nested model runtime failed ({executed.returncode}):\n{executed.stdout}\n{executed.stderr}")
            self.assertIn("PASS recursive nested output ownership", executed.stdout)


if __name__ == "__main__":
    unittest.main()
