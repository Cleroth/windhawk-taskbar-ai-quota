"""Small fail-closed checks for the extraction/runner boundary."""
from contextlib import redirect_stderr, redirect_stdout
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
    PASSING = "RESULT 3 passed, 0 failed; 9 assertions\n"

    def invoke_runner(self, arguments=(), freshness_exit=0, model_output="no summary\n", mod_check_output="",
                      child=None, reads=None):
        """Exercise real orchestration with no filesystem writes; child execution only through `child`.

        Returns the exit code, the saved results and the phases run, in results order. `reads` collects
        the names of the files read as text (extraction manifests)."""
        def read_text(path, **_):
            if reads is not None:
                reads.append(path.name)
            return "{}"

        def fake_phase(root, name, command):
            code = freshness_exit if name == "freshness" else 0
            text = (model_output if name == "model" else mod_check_output if name == "mod-check" else
                    self.PASSING if name in run.SUMMARY_PHASES else "")
            return {"name": name, "command": command, "log": f"{name}.log",
                    "exit_code": code, "ok": code == 0}, text

        output = io.StringIO()
        with (patch.object(sys, "argv", ["run.py", *arguments]),
              patch.object(Path, "mkdir"),
              patch.object(run.tempfile, "mkdtemp", return_value=str(Path.cwd())),
              patch.object(Path, "read_bytes", return_value=b"settled source"),
              patch.object(Path, "read_text", autospec=True, side_effect=read_text),
              patch.object(Path, "write_text") as saved,
              patch.object(run, "hashes", return_value={"fixture": "stable"}),
              patch.object(run, "renders", return_value=["render-a.png"]),
              patch.object(run, "phase", side_effect=fake_phase),
              patch.object(run.subprocess, "run",
                           side_effect=child or AssertionError("Child execution forbidden in tooling tests")),
              redirect_stdout(output)):
            code = run.main()
        results = json.loads(saved.call_args.args[0])
        return code, results, [step["name"] for step in results["phases"]]

    EXTRACTS = ["extract", "extract-history", "extract-status", "extract-host", "extract-popup", "extract-signin"]
    BUILDS = ["model-compile", "history-compile", "status-compile", "host-compile", "probe-compile", "popup-compile",
              "signin-compile"]
    TESTS = ["model", "render", "history", "history-render", "status", "host-probe", "popup-probe", "signin-probe"]

    def test_default_phases_and_summary_required(self):
        reads = []
        code, results, names = self.invoke_runner(model_output=self.PASSING, reads=reads)
        self.assertEqual(code, 0)
        self.assertEqual(names, ["tooling", "freshness", *self.EXTRACTS, *self.BUILDS, "mod-check", *self.TESTS])
        self.assertEqual(reads, ["settings.manifest.json", "history.manifest.json", "status.manifest.json",
                                 "ui-host.manifest.json", "popup.manifest.json", "signin.manifest.json"])
        self.assertEqual(results["phases"][-1]["tests"], {"passed": 3, "failed": 0, "assertions": 9})
        # Every render phase reports its own: the settings window's, the history window's, then the
        # sign-in window's.
        self.assertEqual(results["renders"], ["render-a.png", "render-a.png", "render-a.png"])
        code, results, _ = self.invoke_runner()
        self.assertEqual(code, 1)
        self.assertIn("evidence_error", results["phases"][-1])
        self.assertEqual(results["phases"][-1]["name"], "model")

    def test_stale_generated_source_stops_before_extraction(self):
        code, results, names = self.invoke_runner(freshness_exit=1)
        self.assertEqual(code, 1)
        self.assertFalse(results["ok"])
        self.assertEqual(names, ["tooling", "freshness"])

    def test_popup_interactive_builds_only_the_popup_probe_then_runs_it_attached(self):
        launched = []

        def child(command, **_):
            launched.append(command)
            return run.subprocess.CompletedProcess(command, 0)

        reads = []
        code, results, names = self.invoke_runner(["--popup-interactive"], child=child, reads=reads)
        self.assertEqual(code, 0)
        self.assertEqual(names, ["tooling", "freshness", "extract-popup", "popup-compile"])
        self.assertEqual(reads, ["popup.manifest.json"])
        self.assertEqual(len(launched), 1)
        self.assertEqual(launched[0][1:3], ["--interactive", "--log"])
        self.assertEqual(results["interactive"]["exit_code"], 0)
        _, _, names = self.invoke_runner(["--popup-interactive"], freshness_exit=1)
        self.assertEqual(names, ["tooling", "freshness"])

    def test_tooltip_interactive_builds_only_the_host_probe_then_runs_it_attached(self):
        launched = []

        def child(command, **_):
            launched.append(command)
            return run.subprocess.CompletedProcess(command, 0)

        reads = []
        code, results, names = self.invoke_runner(["--tooltip-interactive"], child=child, reads=reads)
        self.assertEqual(code, 0)
        self.assertEqual(names, ["tooling", "freshness", "extract-host", "host-compile", "probe-compile"])
        self.assertEqual(reads, ["ui-host.manifest.json"])
        self.assertEqual(len(launched), 1)
        self.assertTrue(launched[0][1].endswith("ui-host.dll"))
        self.assertEqual(launched[0][2:4], ["--tooltip-interactive", "--log"])
        self.assertTrue(launched[0][4].endswith("tooltip-interactive.log"))
        self.assertEqual(results["interactive"]["exit_code"], 0)
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            self.invoke_runner(["--tooltip-interactive", "--popup-interactive"])

    def test_mod_check_diagnostics_stop_before_tests(self):
        code, results, names = self.invoke_runner(
            model_output=self.PASSING, mod_check_output="x.cpp:1:2: warning: unused [-Wunused]\n1 warning generated.\n")
        self.assertEqual(code, 1)
        self.assertEqual(names, ["tooling", "freshness", *self.EXTRACTS, *self.BUILDS, "mod-check"])
        self.assertEqual(results["phases"][-1]["evidence_error"], "mod-check: 1 compiler diagnostic(s)")

    def test_explicit_other_source_skips_freshness_and_mod_check(self):
        _, _, names = self.invoke_runner(["--source", "other.wh.cpp"], model_output=self.PASSING)
        self.assertEqual(names, ["tooling", *self.EXTRACTS, *self.BUILDS, *self.TESTS])

    def test_renders_require_reported_png_files(self):
        with self.assertRaises(ValueError):
            run.renders(Path("missing-run"), "RESULT 1 passed, 0 failed; 1 assertions\n")
        with self.assertRaises(ValueError):
            run.renders(Path("missing-run"), "RENDER C:\\missing-run\\render-about.png\n")

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

    def test_constant_takes_one_line_and_refuses_ambiguity(self):
        text = "static constexpr int kOther = 2;\nstatic constexpr ULONGLONG kValue = 11ULL;\nint after;\n"
        a, b = extract.constant(text, "kValue")
        self.assertEqual(text[a:b], "static constexpr ULONGLONG kValue = 11ULL;\n")
        for text in ("", "static constexpr int kValue = 1;\nstatic constexpr int kValue = 2;\n",
                     "static constexpr int kValueX = 1;\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                extract.constant(text, "kValue")

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
