"""Small fail-closed checks for the extraction/runner boundary."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import extract
import run


class ToolingTests(unittest.TestCase):
    def invoke_runner(self, arguments=(), freshness_exit=0, model_output="no summary\n"):
        """Exercise real orchestration with no filesystem writes or child execution."""
        def fake_phase(root, name, command):
            code = freshness_exit if name == "freshness" else 0
            text = model_output if name == "model" else ""
            return {"name": name, "command": command, "log": f"{name}.log",
                    "exit_code": code, "ok": code == 0}, text

        output = io.StringIO()
        with (patch.object(sys, "argv", ["run.py", *arguments]),
              patch.object(Path, "mkdir"),
              patch.object(run.tempfile, "mkdtemp", return_value=str(Path.cwd())),
              patch.object(Path, "read_bytes", return_value=b"settled source"),
              patch.object(Path, "read_text", return_value="{}"),
              patch.object(Path, "write_text") as saved,
              patch.object(run, "hashes", return_value={"fixture": "stable"}),
              patch.object(run, "phase", side_effect=fake_phase) as phases,
              patch.object(run.subprocess, "run", side_effect=AssertionError("Child execution forbidden in tooling tests")),
              redirect_stdout(output)):
            code = run.main()
        return code, json.loads(saved.call_args.args[0]), [call.args[1] for call in phases.call_args_list]

    def test_default_phases_and_summary_required(self):
        passing = "RESULT 3 passed, 0 failed; 9 assertions\n"
        code, results, names = self.invoke_runner(model_output=passing)
        self.assertEqual(code, 0)
        self.assertEqual(names, ["tooling", "freshness", "extract", "model-compile", "model"])
        self.assertEqual(results["phases"][-1]["tests"], {"passed": 3, "failed": 0, "assertions": 9})
        code, results, _ = self.invoke_runner()
        self.assertEqual(code, 1)
        self.assertIn("evidence_error", results["phases"][-1])

    def test_stale_generated_source_stops_before_extraction(self):
        code, results, names = self.invoke_runner(freshness_exit=1)
        self.assertEqual(code, 1)
        self.assertFalse(results["ok"])
        self.assertEqual(names, ["tooling", "freshness"])

    def test_explicit_other_source_skips_freshness(self):
        _, _, names = self.invoke_runner(["--source", "other.wh.cpp"])
        self.assertEqual(names, ["tooling", "extract", "model-compile", "model"])

    def test_unsuccessful_or_duplicate_summary_is_not_success(self):
        for text in ("", "RESULT 1 passed, 1 failed; 2 assertions\n",
                     "RESULT 1 passed, 0 failed; 2 assertions\n" * 2):
            with self.subTest(text=text), self.assertRaises(ValueError):
                run.summary("model", text)

    def test_fragment_preserves_bytes_up_to_next_marker(self):
        text = "// src/a.h\nint a;\r\n\n// src/b.h\nint b;\n// src/c.h\n"
        a, b = extract.fragment(text, "a.h")
        self.assertEqual(text[a:b], "int a;\r\n\n")
        a, b = extract.fragment(text, "b.h")
        self.assertEqual(text[a:b], "int b;\n")

    def test_missing_duplicate_and_unterminated_fragments_refused(self):
        for text in ("// src/b.h\n", "// src/a.h\n// src/a.h\n// src/b.h\n", "// src/a.h\nint a;\n",
                     "// src/a.hx\n// src/b.h\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                extract.fragment(text, "a.h")

    def test_definition_ignores_quoted_braces_and_refuses_ambiguity(self):
        code = 'static int Example() {\n const char* s = "}"; /* { */ return 1;\n}'
        a, b = extract.definition("// x\n" + code + "\n", "Example")
        self.assertEqual(("// x\n" + code)[a:b], code)
        for text in ("", "static int Example() {}\nstatic int Example() {}", "static int Example() {"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                extract.definition(text, "Example")

    def test_source_changed_before_extraction_writes_nothing(self):
        with patch.object(Path, "read_bytes", return_value=b"changed"), patch.object(Path, "write_bytes") as write:
            with self.assertRaisesRegex(ValueError, "Source changed before"):
                extract.generate(Path("source.cpp"), Path("output.cpp"), "old hash")
            write.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
