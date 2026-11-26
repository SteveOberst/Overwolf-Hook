"""Generation failures must preserve the old tree; success removes retired classes."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.bindings import ast as binding_ast
from tools.bindings import generate as model_generator

spec = importlib.util.spec_from_file_location("generator", Path(__file__).resolve().parents[1] / "jvm/tools/generate_bindings.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
PACKAGE = Path("dev/sxlver/overwolfhook/bindings")
TEMP_ROOT = None  # Use the invoking process's temporary directory.



class GenerationTests(unittest.TestCase):
    def invoke(self, output, fail=False):
        def run(command, **kwargs):
            if "--dump-includes" in command:
                Path(command[command.index("--dump-includes") + 1]).write_text("--include-function owh_current details\n")
            else:
                generated = Path(command[command.index("--output") + 1]) / PACKAGE
                generated.mkdir(parents=True)
                (generated / "Current.java").write_text("class Current {}")
                if fail:
                    raise subprocess.CalledProcessError(1, command)
        with patch.object(sys, "argv", ["generator", "--output", str(output), "--jextract", "fake-generator.bat"]), \
                patch.object(generator.subprocess, "run", side_effect=run), \
                patch.object(binding_ast, "parse", return_value={"records": {}, "constants": {}}), \
                patch.object(model_generator, "generate_models"):
            generator.main()

    def test_success_removes_retired_binding(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as root:
            output = Path(root) / "ffm"
            (output / PACKAGE).mkdir(parents=True)
            (output / PACKAGE / "Retired.java").write_text("old binding")
            self.invoke(output)
            self.assertFalse((output / PACKAGE / "Retired.java").exists())
            self.assertTrue((output / PACKAGE / "Current.java").exists())

    def test_partial_generation_failure_keeps_previous_tree(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as root:
            output = Path(root) / "ffm"
            (output / PACKAGE).mkdir(parents=True)
            (output / PACKAGE / "Previous.java").write_bytes(b"previous valid binding")
            with self.assertRaises(subprocess.CalledProcessError):
                self.invoke(output, fail=True)
            self.assertEqual((output / PACKAGE / "Previous.java").read_bytes(), b"previous valid binding")
            self.assertFalse((output / PACKAGE / "Current.java").exists())
            self.assertEqual(list(Path(root).iterdir()), [output])

    def test_foreign_output_is_not_replaced(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as root:
            output = Path(root) / "ffm"
            output.mkdir()
            (output / "README.txt").write_bytes(b"user data")
            with self.assertRaises(RuntimeError):
                self.invoke(output)
            self.assertEqual((output / "README.txt").read_bytes(), b"user data")

    def test_failed_directory_swap_restores_previous_tree(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as root:
            output = Path(root) / "ffm"
            (output / PACKAGE).mkdir(parents=True)
            (output / PACKAGE / "Previous.java").write_bytes(b"previous valid binding")
            rename = Path.rename
            def failing_rename(path, target):
                if path.name == "generated":
                    raise OSError("simulated rename failure")
                return rename(path, target)
            with patch.object(Path, "rename", failing_rename), self.assertRaises(OSError):
                self.invoke(output)
            self.assertEqual((output / PACKAGE / "Previous.java").read_bytes(), b"previous valid binding")


if __name__ == "__main__":
    unittest.main()
