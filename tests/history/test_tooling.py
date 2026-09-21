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
    def invoke_runner(self, arguments=(), compile_exit=0, positive_exit=0, diagnostic_exit=3221225477,
                      freshness_exit=0):
        """Exercise real orchestration with no filesystem writes or child execution."""
        def fake_phase(root, name, command):
            code = compile_exit if name == "native-compile" else positive_exit if name == "native-default" else 0
            if name == "freshness":
                code = freshness_exit
            if name.startswith("diagnostic-"):
                code = diagnostic_exit
            return {"name": name, "command": command, "log": f"{name}.log",
                    "exit_code": code, "ok": code == 0}, "no positive summary\n"

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
        return code, json.loads(saved.call_args.args[0]), phases.call_args_list, parser.call_args_list, output.getvalue()

    def test_diagnostics_never_enter_default_phases(self):
        code, results, calls, parsed, _ = self.invoke_runner()
        expected = ["tooling", "freshness", "extract", "model-compile", "model-ui-gdi", "native-compile", "native-default", "native-v6"]
        self.assertEqual(code, 0)
        self.assertEqual([call.args[1] for call in calls], expected)
        self.assertEqual([step["name"] for step in results["phases"]], expected)
        self.assertFalse(results["diagnostics"]["requested"])
        self.assertEqual(results["diagnostics"]["steps"], [])
        self.assertTrue(all("--preactivation" not in call.args[2] for call in calls))
        self.assertEqual([call.args[0] for call in parsed], ["model-ui-gdi", "native-default", "native-v6"])

    def test_opt_in_diagnostic_exits_are_observations_not_readiness(self):
        for diagnostic_exit in (0, 3221225477, -1073741819):
            with self.subTest(exit=diagnostic_exit):
                code, results, calls, parsed, output = self.invoke_runner(
                    ["--diagnose-native-preactivation"], diagnostic_exit=diagnostic_exit)
                self.assertEqual(code, 0)
                self.assertTrue(results["ok"])
                self.assertEqual(len(results["phases"]), 8)
                self.assertEqual(len(calls), 10)
                self.assertEqual(results["diagnostics"]["role"], "observations_not_readiness_tests")
                self.assertIn("Abnormal exits can be expected", output)
                self.assertIn("not evidence that the unsafe call is usable", output)
                diagnostics = results["diagnostics"]["steps"]
                self.assertEqual(len(diagnostics), 2)
                for step in diagnostics:
                    self.assertEqual(step["exit_code"], diagnostic_exit)
                    self.assertEqual(step["observations"], ["no positive summary"])
                    self.assertNotIn("ok", step)
                    self.assertNotIn("tests", step)
                    self.assertNotIn("evidence_error", step)
                default = results["phases"][-2]["command"]
                v6 = results["phases"][-1]["command"]
                self.assertEqual(diagnostics[0]["command"], default + ["--preactivation"])
                self.assertEqual(diagnostics[1]["command"], v6 + ["--preactivation"])
                self.assertTrue(all("--preactivation" not in step["command"] for step in results["phases"]))
                self.assertEqual([call.args[0] for call in parsed], ["model-ui-gdi", "native-default", "native-v6"])

    def test_diagnostics_skipped_after_native_compile_failure(self):
        code, results, calls, _, _ = self.invoke_runner(["--diagnose-native-preactivation"], compile_exit=1)
        self.assertEqual(code, 1)
        self.assertFalse(results["ok"])
        self.assertEqual(calls[-1].args[1], "native-compile")
        self.assertEqual(results["diagnostics"]["steps"], [])
        self.assertIn("native compilation did not succeed", results["diagnostics"]["skipped"])
        self.assertTrue(all("--preactivation" not in call.args[2] for call in calls))

    def test_stale_generated_source_stops_before_extraction(self):
        code, results, calls, _, _ = self.invoke_runner(freshness_exit=1)
        self.assertEqual(code, 1)
        self.assertFalse(results["ok"])
        self.assertEqual([call.args[1] for call in calls], ["tooling", "freshness"])
        self.assertEqual(calls[-1].args[2][-2:], [str(run.REPO / "generate-mod.ps1"), "-Check"])

    def test_explicit_other_source_skips_freshness(self):
        code, results, calls, _, _ = self.invoke_runner(["--source", "other.wh.cpp"])
        self.assertEqual(code, 0)
        self.assertTrue(results["ok"])
        self.assertEqual([call.args[1] for call in calls],
                         ["tooling", "extract", "model-compile", "model-ui-gdi", "native-compile", "native-default", "native-v6"])

    def test_zero_diagnostic_exits_cannot_rescue_positive_failure(self):
        code, results, _, _, _ = self.invoke_runner(
            ["--diagnose-native-preactivation"], positive_exit=1, diagnostic_exit=0)
        self.assertEqual(code, 1)
        self.assertFalse(results["ok"])
        self.assertEqual(len(results["diagnostics"]["steps"]), 2)

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

    def test_raw_literals_refused_instead_of_misparsed(self):
        with self.assertRaisesRegex(ValueError, "Raw C\\+\\+"):
            extract.definition('static int Example() { auto x = R"tag(})tag"; }', "Example")

    def test_source_changed_before_extraction_writes_nothing(self):
        with patch.object(Path, "read_bytes", return_value=b"changed"), patch.object(Path, "write_bytes") as write:
            with self.assertRaisesRegex(ValueError, "Source changed before"):
                extract.generate(Path("source.cpp"), Path("output.cpp"), "old hash")
            write.assert_not_called()

    def test_summary_missing_or_duplicated_is_not_success(self):
        for text in ("", "RESULT 1 passed, 0 failed; 2 assertions\n" * 2):
            with self.subTest(text=text), self.assertRaises(ValueError):
                run.observations("model-ui-gdi", text)

    def test_gdi_failure_is_not_success(self):
        summary = "RESULT 1 passed, 0 failed; 2 assertions\n"
        for resource in ("", "GDI_RESOURCES before=2 after=3 paints=120 delete_failures=0\n",
                         "GDI_RESOURCES before=2 after=2 paints=120 delete_failures=1\n"):
            with self.subTest(resource=resource), self.assertRaises(ValueError):
                run.observations("model-ui-gdi", summary + resource)

    def test_native_context_and_positive_evidence_required(self):
        summary = "ACTIVE_RESULT 1 passed, 0 failed; 60 assertions\n"
        control = "NATIVE_CONTROL context=v6 version=6.10 build=1 full_size=72 v2_size=64\n"
        rects = "".join(f"NATIVE_ACTIVE text={i} width=80 height=20\n" for i in range(3))
        finish = "NATIVE_V2_DEACTIVATE hidden=1 getbubblesize_calls=0\n"
        text = summary + control + rects + finish
        self.assertEqual(run.observations("native-v6", text)["control"]["version"], "6.10")
        for invalid in (text.replace("6.10", "5.82"), text.replace("context=v6", "context=default"),
                        summary + control + rects, text + "NATIVE_PREACTIVATION\n"):
            with self.subTest(text=invalid), self.assertRaises(ValueError):
                run.observations("native-v6", invalid)


if __name__ == "__main__":
    unittest.main(verbosity=2)
