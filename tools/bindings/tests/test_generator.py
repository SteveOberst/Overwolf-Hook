import tempfile
import unittest
from pathlib import Path

from tools.bindings.ast import parse
from tools.bindings.generate import generate_models


ROOT = Path(__file__).resolve().parents[3]
FIXTURES = Path(__file__).resolve().parent / "fixtures"


class BindingGeneratorTest(unittest.TestCase):
    def test_ast_drives_mutated_fields_constants_and_nested_views(self):
        ast = parse(ROOT, [FIXTURES / "mutable.h"])
        fixture = ast["records"]["owh_fixture"]
        self.assertEqual([field["name"] for field in fixture["fields"]],
                         ["sequence", "digest", "result", "audit_marker"])
        self.assertEqual(fixture["fields"][1]["array_count"], 4)
        self.assertEqual(ast["constants"]["OWH_FIXTURE_KNOWN"], 17)
        with tempfile.TemporaryDirectory() as temporary:
            generated = generate_models(ast, Path(temporary))
            source = (Path(temporary) / "Fixture.java").read_text(encoding="utf-8")
            self.assertIn("getAuditMarker()", source)
            self.assertIn("Result getResult()", source)
            self.assertEqual(len(generated), 5)  # runtime helpers, nested owh_result, and fixture

    def test_unannotated_pointer_fails_closed(self):
        ast = parse(ROOT, [FIXTURES / "unknown_pointer.h"])
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "Unannotated pointer"):
                generate_models(ast, Path(temporary))

    def test_input_model_preserves_native_defaults_until_a_setter_is_used(self):
        ast = parse(ROOT, [FIXTURES / "input_init.h"])
        with tempfile.TemporaryDirectory() as temporary:
            generate_models(ast, Path(temporary))
            source = (Path(temporary) / "InputFixture.java").read_text(encoding="utf-8")
            init = source.index("OverwolfNative.owh_input_fixture_init(segment)")
            setter = source.index("if (assigned[1])", init)
            self.assertLess(init, setter)
            self.assertIn("public InputFixture()", source)
            self.assertIn("setTimeoutMs(long value)", source)
            self.assertIn("numbers[1] = value; assigned[1] = true;", source)
            self.assertNotIn("setSize(", source)
            # Initialization metadata owns defaults, including the size field.
            self.assertNotIn("layout().byteSize()", source)
            self.assertNotIn("abi_version", source)

    def test_output_allocator_initializes_only_explicit_struct_header_metadata(self):
        ast = parse(ROOT, [FIXTURES / "output_headers.h"])
        with tempfile.TemporaryDirectory() as temporary:
            generate_models(ast, Path(temporary))
            header = (Path(temporary) / "OutputHeaderFixture.java").read_text(encoding="utf-8")
            payload = (Path(temporary) / "PayloadSizeFixture.java").read_text(encoding="utf-8")
            borrowed = (Path(temporary) / "BorrowedOutputFixture.java").read_text(encoding="utf-8")
            self.assertIn("public static MemorySegment allocate(Arena arena)", header)
            self.assertIn("owh_output_header_fixture.size(segment, (int) dev.sxlver.overwolfhook.bindings.owh_output_header_fixture.layout().byteSize());", header)
            self.assertIn("public static MemorySegment allocate(Arena arena)", payload)
            self.assertNotIn("layout().byteSize()", payload)
            self.assertIn("copyInto(owned.owner(), copy, source);", borrowed)
            copy_into = borrowed.index("static void copyInto")
            copy = borrowed.index("copy.copyFrom(source.asSlice(0, copy.byteSize()));", copy_into)
            clear = borrowed.index("owh_borrowed_output_fixture.pixels(copy, MemorySegment.NULL);", copy_into)
            self.assertLess(copy, clear)
            self.assertIn("public MemorySegment getPixels()", borrowed)
        unsafe_ast = parse(ROOT, [FIXTURES / "unsafe_borrowed_output.h"])
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "needs copy=clear metadata"):
                generate_models(unsafe_ast, Path(temporary))

    def test_nested_owned_output_uses_one_owner_and_recursively_sanitizes_pointers(self):
        ast = parse(ROOT, [FIXTURES / "nested_owned.h"])
        with tempfile.TemporaryDirectory() as temporary:
            generate_models(ast, Path(temporary))
            outer = (Path(temporary) / "NestedOwned.java").read_text(encoding="utf-8")
            inner = (Path(temporary) / "NestedBytes.java").read_text(encoding="utf-8")
            self.assertIn("NestedBytes.copyInto(owner, dev.sxlver.overwolfhook.bindings.owh_nested_owned.nested(copy), dev.sxlver.overwolfhook.bindings.owh_nested_owned.nested(source));", outer)
            self.assertIn("dev.sxlver.overwolfhook.bindings.owh_nested_bytes.data(copy, MemorySegment.NULL);", inner)
            self.assertIn("dev.sxlver.overwolfhook.bindings.owh_nested_bytes.borrowed(copy, MemorySegment.NULL);", inner)
            self.assertIn("owner.allocateBytes(n_data, 1)", inner)
            self.assertIn("owner.allocateBytes(n_label + 1, 1)", inner)

    def test_span_of_pointer_bearing_records_fails_closed(self):
        ast = parse(ROOT, [FIXTURES / "unsupported_span_record.h"])
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "Unsupported span element type"):
                generate_models(ast, Path(temporary))

    def test_canonical_public_models_accept_annotated_void_byte_span(self):
        include = ROOT / "include/overwolf_hook"
        headers = [include / name for name in
                   (ROOT / "cmake/public-headers.txt").read_text(encoding="utf-8").splitlines()
                   if name and name.startswith("types/")]
        ast = parse(ROOT, headers)
        state_field = ast["records"]["owh_state_field"]
        byte_field = next(field for field in state_field["fields"] if field["name"] == "bytes")
        self.assertEqual(byte_field["pointee"].replace("const ", "").strip(), "void")
        self.assertIn("span=size", byte_field["metadata"])
        with tempfile.TemporaryDirectory() as temporary:
            generate_models(ast, Path(temporary))
            generated = (Path(temporary) / "StateField.java").read_text(encoding="utf-8")
            self.assertIn("getBytes()", generated)
            self.assertIn("owner.allocateBytes(n_bytes, 1)", generated)
            self.assertIn("owh_state_field.bytes(copy, MemorySegment.NULL);", generated)


if __name__ == "__main__":
    unittest.main()
