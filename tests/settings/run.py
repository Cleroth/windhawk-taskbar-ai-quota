"""Run source-extracted settings tests, offscreen renders and native-host and popup probes."""
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
SUMMARY_PHASES = ("model", "render", "host-probe", "popup-probe")


def hashes():
    paths = sorted(p for p in HERE.iterdir() if p.is_file() and p.suffix in {".py", ".hpp", ".cpp"})
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


def renders(root, text):
    """Every reported render is a PNG in this run's directory."""
    names = [Path(line.removeprefix("RENDER ").strip()).name for line in text.splitlines()
             if line.startswith("RENDER ")]
    if not names:
        raise ValueError("render: no renders reported")
    for name in names:
        path = root / name
        if not name.startswith("render-") or not path.is_file() or path.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"render: missing or invalid PNG {name}")
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE,
                        help="Mod file to extract; the default generated file is first checked against src/")
    parser.add_argument("--compiler", type=Path, default=DEFAULT_COMPILER, help="Windhawk-compatible clang++ executable")
    parser.add_argument("--output", type=Path, default=HERE / ".out", help="Parent for a new unique run directory (never cleaned)")
    parser.add_argument("--popup-interactive", action="store_true",
                        help="Only build the popup probe, then run its interactive mode by hand (logs to the run directory)")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Windows is required")
    source, compiler = args.source.resolve(), args.compiler.resolve()
    parent = args.output.resolve()
    parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=datetime.now(timezone.utc).strftime("run-%Y%m%dT%H%M%SZ-"), dir=parent))
    print(f"Output: {root}", flush=True)
    results = {"source": str(source), "compiler": str(compiler), "output": str(root), "phases": [], "ok": False,
               "scope": "isolated source-extracted model tests, offscreen renders, a native-host probe DLL and a "
                        "popup capture probe on a hidden desktop; not Explorer, XAML or the mod's settings thread"}
    try:
        results["source_sha256_before"] = hashlib.sha256(source.read_bytes()).hexdigest()
        results["harness_sha256_before"] = hashes()
        cpp, exe = root / "settings.cpp", root / "settings.exe"
        host_cpp, host_dll, probe = root / "ui-host.cpp", root / "ui-host.dll", root / "host-probe.exe"
        popup_cpp, popup_exe = root / "popup.cpp", root / "popup-probe.exe"
        flags = [str(compiler), "--target=x86_64-w64-mingw32", "-std=c++23", "-DUNICODE", "-D_UNICODE",
                 "-Wall", "-Wextra", "-O0", "-g", "-static"]
        # The probes mirror Windhawk's Windows 10 targeting and MSVC-style wide printf.
        native = flags + ["-D_WIN32_WINNT=0x0A00", "-DWINVER=0x0A00", "-D__USE_MINGW_ANSI_STDIO=0"]
        host_libs = ["-ld2d1", "-ldwrite", "-ldwmapi", "-lole32", "-loleaut32", "-lruntimeobject", "-lwindowsapp",
                     "-luser32", "-lgdi32", "-ladvapi32"]
        extract = [sys.executable, "-B", str(HERE / "extract.py"), "--source", str(source),
                   "--expected-hash", results["source_sha256_before"]]
        setup = [
            ("tooling", [sys.executable, "-B", str(HERE / "test_tooling.py")]),
            # A stale generated file would test code that src/ no longer contains.
            *([("freshness", ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                              str(REPO / "generate-mod.ps1"), "-Check"])]
              if source == DEFAULT_SOURCE.resolve() else []),
        ]
        popup_build = [
            ("extract-popup", extract + ["--output", str(popup_cpp), "--target", "popup"]),
            ("popup-compile", native + ["-municode", "-I", str(HERE), str(popup_cpp), "-o", str(popup_exe),
                                        *host_libs]),
        ]
        commands = [*setup, *popup_build] if args.popup_interactive else [
            *setup,
            ("extract", extract + ["--output", str(cpp)]),
            ("extract-host", extract + ["--output", str(host_cpp), "--target", "host"]),
            ("model-compile", flags + ["-I", str(HERE), str(cpp), "-o", str(exe), "-ld2d1", "-ldwrite",
                                       "-lwindowscodecs", "-lole32", "-loleaut32", "-lruntimeobject"]),
            ("model", [str(exe)]),
            ("render", [str(exe), "--render-dir", str(root)]),
            ("host-compile", native + ["-shared", "-I", str(HERE), str(host_cpp), "-o", str(host_dll), *host_libs]),
            ("probe-compile", native + ["-municode", str(HERE / "host-probe.cpp"), "-o", str(probe),
                                        "-lole32", "-luser32"]),
            ("host-probe", [str(probe), str(host_dll)]),
            *popup_build,
            ("popup-probe", [str(popup_exe)]),
        ]
        for name, command in commands:
            step, text = phase(root, name, command)
            results["phases"].append(step)
            if name in SUMMARY_PHASES:
                try:
                    step["tests"] = summary(name, text)
                    if name == "render":
                        results["renders"] = renders(root, text)
                    if name.endswith("-probe"):
                        step["observations"] = re.findall(r"^(?:HANDLES|LOSS|LOG|CONTEXT|OBSERVE|NOTE) [^\n]+",
                                                          text, re.M)
                except ValueError as error:
                    step.update(ok=False, evidence_error=str(error))
                    print(error, flush=True)
            if name.startswith("extract") and step["ok"]:
                extracted = {"extract": cpp, "extract-host": host_cpp, "extract-popup": popup_cpp}[name]
                manifest = extracted.with_suffix(".manifest.json")
                results.setdefault("extraction", {})[name] = json.loads(manifest.read_text(encoding="utf-8"))
            if not step["ok"]:
                break
        results["ok"] = len(results["phases"]) == len(commands) and all(step["ok"] for step in results["phases"])
        if args.popup_interactive and results["ok"]:
            # By hand, attached to this console, no timeout; the log is the evidence.
            log = root / "popup-interactive.log"
            print(f"\n=== popup-interactive ===\nFollow the steps below; the log is {log}", flush=True)
            process = subprocess.run([str(popup_exe), "--interactive", "--log", str(log)], cwd=root)
            results["interactive"] = {"log": str(log), "exit_code": process.returncode}
            print(f"Interactive log: {log}", flush=True)
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
