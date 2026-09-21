"""Run source-extracted history tests and isolated positive native tooltip probes."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

# Local imports in self-tests must not create files beside maintained tooling.
sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DEFAULT_SOURCE = REPO / "local@taskbar-ai-quota.wh.cpp"
DEFAULT_COMPILER = Path(r"C:\Program Files\Windhawk\Compiler\bin\clang++.exe")


def hashes():
    paths = sorted(p for p in HERE.iterdir()
                   if p.is_file() and p.suffix in {".py", ".h", ".hpp", ".cpp", ".inc", ".manifest"})
    paths += sorted((HERE / "fixtures").glob("*.inc"))
    return {p.relative_to(HERE).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def phase(root, name, command, timeout=120):
    print(f"\n=== {name} ===", flush=True)
    step = {"name": name, "command": command, "log": f"{name}.log"}
    try:
        # Always wait for exit before executing a binary or proceeding to another compile.
        process = subprocess.run(command, cwd=root, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=timeout)
        output = process.stdout
        step["exit_code"] = process.returncode
    except subprocess.TimeoutExpired as error:
        # subprocess.run kills and waits for its child on timeout.
        output = (error.stdout or b"") + f"\nTIMEOUT after {timeout}s\n".encode()
        step.update(exit_code=1, error="timeout")
    except OSError as error:
        output = f"Cannot start phase: {error}\n".encode("utf-8")
        step.update(exit_code=1, error=str(error))
    (root / step["log"]).write_bytes(output)
    text = output.decode("utf-8", errors="replace").replace("\r\n", "\n")
    # Console code pages must not turn a completed test into a runner failure.
    print(text.encode(sys.stdout.encoding or "utf-8", errors="replace")
          .decode(sys.stdout.encoding or "utf-8"), end="", flush=True)
    step["ok"] = step["exit_code"] == 0
    return step, text


def observations(name, text):
    """Validate evidence as well as exit status; missing summaries never mean pass."""
    native = name.startswith("native-")
    marker = "ACTIVE_RESULT" if native else "RESULT"
    matches = re.findall(rf"^{marker} (\d+) passed, (\d+) failed; (\d+) assertions$", text, re.M)
    if len(matches) != 1:
        raise ValueError(f"{name}: expected exactly one test summary")
    result = dict(zip(("passed", "failed", "assertions"), map(int, matches[0])))
    if not result["passed"] or result["failed"] or not result["assertions"]:
        raise ValueError(f"{name}: unsuccessful test summary: {result}")
    if native:
        controls = re.findall(r"^NATIVE_CONTROL context=(\S+) version=(\d+\.\d+) build=(\d+) full_size=(\d+) v2_size=(\d+)$", text, re.M)
        if len(controls) != 1:
            raise ValueError(f"{name}: missing/ambiguous native control version")
        context, version, build, full_size, v2_size = controls[0]
        expected_context = "v6" if name == "native-v6" else "default"
        if context != expected_context or (context == "v6" and version.split(".")[0] != "6"):
            raise ValueError(f"{name}: wrong native control context/version: {controls[0]}")
        result["control"] = dict(context=context, version=version, build=int(build),
                                 full_size=int(full_size), v2_size=int(v2_size))
        result["rectangle_measurements"] = re.findall(r"^NATIVE_ACTIVE [^\n]+", text, re.M)
        result["no_bubble_queries"] = bool(re.search(r"^NATIVE_V2_DEACTIVATE .* getbubblesize_calls=0$", text, re.M)) and "NATIVE_PREACTIVATION" not in text
        if len(result["rectangle_measurements"]) != 3 or not result["no_bubble_queries"]:
            raise ValueError(f"{name}: incomplete activate-first native evidence")
    else:
        resources = re.findall(r"^GDI_RESOURCES before=(\d+) after=(\d+) paints=(\d+) delete_failures=(\d+)$", text, re.M)
        if len(resources) != 1:
            raise ValueError(f"{name}: missing/ambiguous GDI resource report")
        before, after, paints, failures = map(int, resources[0])
        if before <= 0 or before != after or paints <= 0 or failures:
            raise ValueError(f"{name}: GDI resource regression: {resources[0]}")
        result["gdi_resources"] = dict(before=before, after=after, paints=paints, delete_failures=failures)
    return result


def bmp_to_png(path):
    """Convert only the harness's top-down, 32-bit uncompressed BMP format."""
    data = path.read_bytes()
    if len(data) < 54:
        raise ValueError(f"Truncated capture: {path}")
    offset = struct.unpack_from("<I", data, 10)[0]
    size, width, height, planes, bits, compression = struct.unpack_from("<IiiHHI", data, 14)
    if (data[:2] != b"BM" or size != 40 or width <= 0 or height >= 0 or
            planes != 1 or bits != 32 or compression != 0 or offset < 54 or
            len(data) != offset + width * -height * 4):
        raise ValueError(f"Unexpected capture bitmap format: {path}")
    rows = bytearray()
    for y in range(-height):
        source = data[offset + y * width * 4: offset + (y + 1) * width * 4]
        row = bytearray(width * 3)
        row[0::3], row[1::3], row[2::3] = source[2::4], source[1::4], source[0::4]
        rows.append(0)
        rows.extend(row)
    png = bytearray(b"\x89PNG\r\n\x1a\n")
    for kind, body in [(b"IHDR", struct.pack(">IIBBBBB", width, -height, 8, 2, 0, 0, 0)),
                       (b"IDAT", zlib.compress(rows)), (b"IEND", b"")]:
        png.extend(struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body)))
    path.with_suffix(".png").write_bytes(png)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", action="store_true", help="Save offscreen GDI BMP/PNG captures")
    parser.add_argument("--diagnose-native-preactivation", action="store_true",
                        help="Opt-in unsafe child-process observations; crashes possible, excluded from readiness")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE,
                        help="Mod file to extract; the default generated file is first checked against src/")
    parser.add_argument("--compiler", type=Path, default=DEFAULT_COMPILER, help="Windhawk-compatible clang++ executable")
    parser.add_argument("--output", type=Path, default=HERE / ".out", help="Parent for a new unique run directory (never cleaned)")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Windows is required for the GDI and native-control phases")
    source, compiler = args.source.resolve(), args.compiler.resolve()
    parent = args.output.resolve()
    parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=datetime.now(timezone.utc).strftime("run-%Y%m%dT%H%M%SZ-"), dir=parent))
    print(f"Output: {root}", flush=True)
    results = {"source": str(source), "compiler": str(compiler), "output": str(root),
               "capture_requested": args.capture, "phases": [], "ok": False,
               "diagnostics": {"requested": args.diagnose_native_preactivation,
                               "role": "observations_not_readiness_tests", "steps": []},
               "scope": "isolated source-extracted tests and native contracts; not Explorer integration"}
    if args.diagnose_native_preactivation:
        print("WARNING: opted-in preactivation diagnostics run only in child processes. "
              "Abnormal exits can be expected diagnostic observations, not readiness failures. "
              "Even exit 0 is not evidence that the unsafe call is usable.", flush=True)
    captured_paths = []
    try:
        results["source_sha256_before"] = hashlib.sha256(source.read_bytes()).hexdigest()
        results["harness_sha256_before"] = hashes()
        cpp, exe, native = root / "history.cpp", root / "history.exe", root / "native-tooltip.exe"
        flags = [str(compiler), "--target=x86_64-w64-mingw32", "-std=c++23", "-DUNICODE", "-D_UNICODE", "-Wall", "-Wextra", "-O0", "-g", "-static"]
        commands = [
            ("tooling", [sys.executable, "-B", str(HERE / "test_tooling.py")]),
            # A stale generated file would test code that src/ no longer contains.
            *([("freshness", ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                              str(REPO / "generate-mod.ps1"), "-Check"])]
              if source == DEFAULT_SOURCE.resolve() else []),
            ("extract", [sys.executable, "-B", str(HERE / "extract.py"), "--source", str(source), "--output", str(cpp), "--expected-hash", results["source_sha256_before"]]),
            ("model-compile", flags + ["-I", str(HERE), str(cpp), "-o", str(exe), "-luser32", "-lgdi32"]),
            ("model-ui-gdi", [str(exe)] + (["--capture-dir", str(root)] if args.capture else [])),
            ("native-compile", flags + ["-municode", str(HERE / "native-tooltip.cpp"), "-o", str(native), "-lcomctl32", "-luser32", "-lgdi32"]),
            ("native-default", [str(native)]),
            ("native-v6", [str(native), str(HERE / "tooltip.manifest")]),
        ]
        for name, command in commands:
            step, text = phase(root, name, command)
            results["phases"].append(step)
            if name in ("model-ui-gdi", "native-default", "native-v6"):
                try:
                    step["tests"] = observations(name, text)
                except ValueError as error:
                    step.update(ok=False, evidence_error=str(error))
                    print(error, flush=True)
            if name == "model-ui-gdi":
                captured_paths = [Path(line.removeprefix("CAPTURE ").strip()) for line in text.splitlines() if line.startswith("CAPTURE ")]
            if name == "extract" and step["ok"]:
                results["extraction"] = json.loads(cpp.with_suffix(".manifest.json").read_text(encoding="utf-8"))
            if not step["ok"] and name not in ("native-default", "native-v6"):
                break
        if args.capture:
            captures = []
            for bmp in captured_paths:
                if bmp.resolve().parent != root or not bmp.name.startswith("grouped-history-") or bmp.suffix != ".bmp":
                    raise ValueError(f"Unexpected capture path: {bmp}")
                bmp_to_png(bmp)
                captures.append(bmp.with_suffix(".png").name)
            results["captures"] = captures
            if not captures:
                raise ValueError("Capture requested but no captures produced")
        results["ok"] = len(results["phases"]) == len(commands) and all(step["ok"] for step in results["phases"])
        # Diagnostics never join commands/phases, the positive evidence parser or readiness.
        if args.diagnose_native_preactivation:
            if any(step["name"] == "native-compile" and step["ok"] for step in results["phases"]):
                for context, arguments in (("default", []), ("v6", [str(HERE / "tooltip.manifest")])):
                    name = f"diagnostic-preactivation-{context}"
                    command = [str(native), *arguments, "--preactivation"]
                    try:
                        step, text = phase(root, name, command)
                        step.pop("ok")  # An exit status is an observation, never a diagnostic pass/fail.
                        step["observations"] = text.splitlines()
                    except OSError as error:
                        step = {"name": name, "command": command, "recording_error": str(error)}
                    results["diagnostics"]["steps"].append(step)
            else:
                results["diagnostics"]["skipped"] = "native compilation did not succeed in this run"
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
