"""Run source-extracted settings and meter model tests."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

# Local imports in self-tests must not create files beside maintained tooling.
sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_SOURCE = REPO / "local@taskbar-ai-quota.wh.cpp"
DEFAULT_COMPILER = Path(r"C:\Program Files\Windhawk\Compiler\bin\clang++.exe")


def hashes():
    paths = sorted(p for p in HERE.iterdir() if p.is_file() and p.suffix in {".py", ".hpp"})
    paths += sorted((HERE / "fixtures").glob("*.inc"))
    return {p.relative_to(HERE).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def phase(root, name, command, timeout=120):
    print(f"\n=== {name} ===", flush=True)
    step = {"name": name, "command": command, "log": f"{name}.log"}
    try:
        # Always wait for exit before executing a binary or starting another compile.
        process = subprocess.run(command, cwd=root, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=timeout)
        output = process.stdout
        step["exit_code"] = process.returncode
    except subprocess.TimeoutExpired as error:
        output = (error.stdout or b"") + f"\nTIMEOUT after {timeout}s\n".encode()
        step.update(exit_code=1, error="timeout")
    except OSError as error:
        output = f"Cannot start phase: {error}\n".encode("utf-8")
        step.update(exit_code=1, error=str(error))
    (root / step["log"]).write_bytes(output)
    text = output.decode("utf-8", errors="replace").replace("\r\n", "\n")
    print(text.encode(sys.stdout.encoding or "utf-8", errors="replace")
          .decode(sys.stdout.encoding or "utf-8"), end="", flush=True)
    step["ok"] = step["exit_code"] == 0
    return step, text


def summary(name, text):
    """Exactly one successful summary; a missing one never means pass."""
    matches = re.findall(r"^RESULT (\d+) passed, (\d+) failed; (\d+) assertions$", text, re.M)
    if len(matches) != 1:
        raise ValueError(f"{name}: expected exactly one test summary")
    result = dict(zip(("passed", "failed", "assertions"), map(int, matches[0])))
    if not result["passed"] or result["failed"] or not result["assertions"]:
        raise ValueError(f"{name}: unsuccessful test summary: {result}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE,
                        help="Mod file to extract; the default generated file is first checked against src/")
    parser.add_argument("--compiler", type=Path, default=DEFAULT_COMPILER, help="Windhawk-compatible clang++ executable")
    parser.add_argument("--output", type=Path, default=HERE / ".out", help="Parent for a new unique run directory (never cleaned)")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Windows is required")
    source, compiler = args.source.resolve(), args.compiler.resolve()
    parent = args.output.resolve()
    parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=datetime.now(timezone.utc).strftime("run-%Y%m%dT%H%M%SZ-"), dir=parent))
    print(f"Output: {root}", flush=True)
    results = {"source": str(source), "compiler": str(compiler), "output": str(root), "phases": [], "ok": False,
               "scope": "isolated source-extracted model tests; not Explorer, XAML or settings window integration"}
    try:
        results["source_sha256_before"] = hashlib.sha256(source.read_bytes()).hexdigest()
        results["harness_sha256_before"] = hashes()
        cpp, exe = root / "settings.cpp", root / "settings.exe"
        flags = [str(compiler), "--target=x86_64-w64-mingw32", "-std=c++23", "-DUNICODE", "-D_UNICODE",
                 "-Wall", "-Wextra", "-O0", "-g", "-static"]
        commands = [
            ("tooling", [sys.executable, "-B", str(HERE / "test_tooling.py")]),
            # A stale generated file would test code that src/ no longer contains.
            *([("freshness", ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                              str(REPO / "generate-mod.ps1"), "-Check"])]
              if source == DEFAULT_SOURCE.resolve() else []),
            ("extract", [sys.executable, "-B", str(HERE / "extract.py"), "--source", str(source),
                         "--output", str(cpp), "--expected-hash", results["source_sha256_before"]]),
            ("model-compile", flags + ["-I", str(HERE), str(cpp), "-o", str(exe)]),
            ("model", [str(exe)]),
        ]
        for name, command in commands:
            step, text = phase(root, name, command)
            results["phases"].append(step)
            if name == "model":
                try:
                    step["tests"] = summary(name, text)
                except ValueError as error:
                    step.update(ok=False, evidence_error=str(error))
                    print(error, flush=True)
            if name == "extract" and step["ok"]:
                results["extraction"] = json.loads(cpp.with_suffix(".manifest.json").read_text(encoding="utf-8"))
            if not step["ok"]:
                break
        results["ok"] = len(results["phases"]) == len(commands) and all(step["ok"] for step in results["phases"])
    except (OSError, ValueError) as error:
        results["error"] = str(error)
        print(f"Verification failed: {error}", flush=True)
    finally:
        try:
            results["source_sha256_after"] = hashlib.sha256(source.read_bytes()).hexdigest()
            results["harness_sha256_after"] = hashes()
            results["source_unchanged_during_run"] = results.get("source_sha256_before") == results["source_sha256_after"]
            results["harness_unchanged_during_run"] = results.get("harness_sha256_before") == results["harness_sha256_after"]
            if not results["source_unchanged_during_run"] or not results["harness_unchanged_during_run"]:
                results["ok"] = False
                print("Source or harness changed during run: rerun before relying on results", flush=True)
        except OSError as error:
            results.update(ok=False, hash_error=str(error))
        (root / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(f"\nReadiness {'PASS' if results['ok'] else 'FAIL'}: {root / 'results.json'}", flush=True)
    return 0 if results["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
