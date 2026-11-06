"""Small fail-closed checks for the maintained extraction/runner boundary."""
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
    def invoke_runner(self, arguments=(), exit_codes=None):
        """Exercise real orchestration with no filesystem writes or child execution."""
        exit_codes = exit_codes or {}

        def fake_phase(root, name, command, timeout=120):
            code = exit_codes.get(name, 0)
            return {"name": name, "command": command, "log": f"{name}.log",
                    "exit_code": code, "ok": code == 0}, "no summary\n"

        output = io.StringIO()
        with (patch.object(sys, "argv", ["run.py", *arguments]),
              patch.object(Path, "mkdir"),
              patch.object(run.tempfile, "mkdtemp", return_value=str(Path.cwd())),
              patch.object(Path, "read_bytes", return_value=b"settled source"),
              patch.object(Path, "read_text", return_value="{}"),
              patch.object(Path, "write_text") as saved,
              patch.object(run, "hashes", return_value={"fixture": "stable"}),
              patch.object(run, "phase", side_effect=fake_phase) as phases,
              patch.object(run, "observations", return_value={"passed": 1}) as parser,
              patch.object(run.subprocess, "run", side_effect=AssertionError("Child execution forbidden in tooling tests")),
              redirect_stdout(output)):
            code = run.main()
        return code, json.loads(saved.call_args.args[0]), phases.call_args_list, parser.call_args_list

    def test_default_phases_in_order_with_one_evidence_parse_per_test(self):
        code, results, calls, parsed = self.invoke_runner()
        expected = ["tooling", "freshness", "extract", "extract-journal", "model-compile", "model", "journal-compile",
                    "journal"]
        self.assertEqual(code, 0)
        self.assertTrue(results["ok"])
        self.assertEqual([call.args[1] for call in calls], expected)
        self.assertEqual([step["name"] for step in results["phases"]], expected)
        self.assertEqual([call.args[0] for call in parsed], ["model", "journal"])

    def test_failed_compile_never_runs_the_tests(self):
        for compile_phase, tests in (("model-compile", []), ("journal-compile", ["model"])):
            with self.subTest(compile_phase=compile_phase):
                code, results, calls, parsed = self.invoke_runner(exit_codes={compile_phase: 1})
                self.assertEqual(code, 1)
                self.assertFalse(results["ok"])
                self.assertEqual(calls[-1].args[1], compile_phase)
                self.assertEqual([call.args[0] for call in parsed], tests)

    def test_stale_generated_source_stops_before_extraction(self):
        code, results, calls, _ = self.invoke_runner(exit_codes={"freshness": 1})
        self.assertEqual(code, 1)
        self.assertFalse(results["ok"])
        self.assertEqual([call.args[1] for call in calls], ["tooling", "freshness"])
        self.assertEqual(calls[-1].args[2][-2:], [str(run.REPO / "generate-mod.ps1"), "-Check"])

    def test_explicit_other_source_skips_freshness(self):
        code, results, calls, _ = self.invoke_runner(["--source", "other.wh.cpp"])
        self.assertEqual(code, 0)
        self.assertTrue(results["ok"])
        self.assertEqual([call.args[1] for call in calls],
                         ["tooling", "extract", "extract-journal", "model-compile", "model", "journal-compile", "journal"])

    def test_definition_preserves_bytes_and_ignores_quoted_braces(self):
        code = 'static int Example() {\r\n // }\r\n const char* s = "}"; /* { */ return 1;\r\n}'
        text = "// prefix\r\n" + code + "\r\n// suffix\r\n"
        a, b = extract.definition(text, "Example")
        self.assertEqual(text[a:b].encode(), code.encode())

    def test_missing_duplicate_and_unbalanced_definitions_refused(self):
        for text in ("", "static int Example() {}\nstatic int Example() {}", "static int Example() {"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                extract.definition(text, "Example")

    def test_missing_duplicate_and_reversed_anchors_refused(self):
        for text in ("begin only", "begin begin end", "begin end end", "end begin"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                extract.between(text, "begin", "end")

    def test_sections_stay_inside_their_fragment(self):
        text = ("// src/a.h\nstatic int Example() { return 1; }\nbegin end\n"
                "// src/b.h\nstatic int Other() { return 2; }\nbegin\n")
        a, b = extract.within(text, "a.h", extract.definition, "Example")
        self.assertEqual(text[a:b], "static int Example() { return 1; }")
        a, b = extract.within(text, "a.h", extract.between, "begin", "end")
        self.assertEqual(text[a:b], "begin ")
        a, b = extract.fragment(text, "b.h")  # The last fragment runs to the end of the file.
        self.assertEqual(text[a:b], "static int Other() { return 2; }\nbegin\n")
        crossing = "// src/a.h\nstatic int Open() {\n// src/b.h\n}\n"
        for source, path, find, args in ((text, "a.h", extract.definition, ("Other",)),
                                         (text, "b.h", extract.between, ("begin", "end")),
                                         (crossing, "a.h", extract.definition, ("Open",))):
            with self.subTest(path=path, args=args), self.assertRaises(ValueError):
                extract.within(source, path, find, *args)
        for source, path in ((text, "c.h"), (text + "// src/a.h\n", "a.h")):
            with self.subTest(path=path), self.assertRaises(ValueError):
                extract.fragment(source, path)

    def test_raw_literals_refused_instead_of_misparsed(self):
        with self.assertRaisesRegex(ValueError, "Raw C\\+\\+"):
            extract.definition('static int Example() { auto x = R"tag(})tag"; }', "Example")

    def test_source_changed_before_extraction_writes_nothing(self):
        with patch.object(Path, "read_bytes", return_value=b"changed"), patch.object(Path, "write_bytes") as write:
            with self.assertRaisesRegex(ValueError, "Source changed before"):
                extract.generate(Path("source.cpp"), Path("output.cpp"), "old hash")
            write.assert_not_called()

    def test_summary_missing_duplicated_or_failed_is_not_success(self):
        for text in ("", "RESULT 1 passed, 0 failed; 2 assertions\n" * 2, "RESULT 1 passed, 1 failed; 2 assertions\n",
                     "RESULT 0 passed, 0 failed; 0 assertions\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                run.observations("model", text)
        self.assertEqual(run.observations("model", "RESULT 3 passed, 0 failed; 9 assertions\n"),
                         {"passed": 3, "failed": 0, "assertions": 9})


if __name__ == "__main__":
    unittest.main(verbosity=2)
