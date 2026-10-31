"""Extract a bounded set of production history definitions without rewriting them."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def balanced(text, start):
    """Find the closing brace, ignoring comments and ordinary quoted literals.

    This is intentionally not a C++ parser. Unsupported raw literals in an extracted
    definition fail closed; changed syntax needs a reviewed extraction update.
    """
    opening = text.index("{", start)
    depth = 0
    token = re.compile(r'//[^\n]*|/\*.*?\*/|R"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    for match in token.finditer(text, opening):
        item = match.group()
        if item == 'R"':
            raise ValueError("Raw C++ literals need an explicit extraction update")
        if item == "{":
            depth += 1
        elif item == "}":
            depth -= 1
            if depth == 0:
                return match.end()
    raise ValueError("Unbalanced production definition")


def definition(text, name, method=False):
    pattern = (r"(?m)^    bool " if method else r"(?m)^static [^\n;{}]+?\b")
    pattern += re.escape(name) + r"\([^;{}]*\)\s*\{"
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1:
        raise ValueError(f"Expected one definition of {name}, found {len(matches)}")
    start = matches[0].start()
    return start, balanced(text, start)


def between(text, start, end):
    if text.count(start) != 1 or text.count(end) != 1:
        raise ValueError(f"Ambiguous source boundary: {start!r} / {end!r}")
    a, b = text.index(start), text.index(end)
    if b <= a:
        raise ValueError(f"Reversed source boundary: {start!r} / {end!r}")
    return a, b


def digest(data):
    return hashlib.sha256(data).hexdigest()


def generate(source, output, expected_hash):
    raw = source.read_bytes()
    if digest(raw) != expected_hash:
        raise ValueError("Source changed before extraction; rerun against settled source")
    # Preserve original line endings too: these exact strings are emitted as UTF-8 bytes.
    text = raw.decode("utf-8-sig")
    here = Path(__file__).resolve().parent
    manifest = {"source": str(source), "sha256": digest(raw), "extractions": []}
    chunks = ["// GENERATED: production sections below are extracted verbatim.\n",
              '#include "win32-stubs.hpp"\n']

    def emit(label, bounds):
        a, b = bounds
        code = text[a:b]
        line = text.count("\n", 0, a) + 1
        manifest["extractions"].append({"name": label, "line": line,
                                        "sha256": digest(code.encode("utf-8")), "characters": len(code)})
        # Keep compiler diagnostics on the actual source without modifying the definition.
        chunks.append(f'\n#line {line} {json.dumps(source.as_posix())}\n{code}\n')

    emit("raw history definitions", between(text, "static constexpr size_t kHistoryMeterCount", "static std::mutex g_historyMutex"))
    emit("history view definitions", between(text, "static constexpr int kHistorySeriesHeight", "static void HistoryLayout(HistoryWindowState& state, bool newFont);"))
    constants = list(re.finditer(r"(?m)^static constexpr ULONGLONG kFileTimeUnixEpochOffsetMs[^\n]+", text))
    if len(constants) != 1:
        raise ValueError(f"Expected one filetime constant, found {len(constants)}")
    emit("filetime epoch", constants[0].span())
    for name in ["ScaleForDpi", "WideToUtf8", "UnixMsToLocalSystemTime", "HistoryNumber", "HistoryLocalDate", "HistoryUtcDate", "HistoryCsvId", "HistoryHours"]:
        emit(name, definition(text, name))
    # Whole pure fragments: continuity rules and the flyout's burn-up curve model. The store
    # fragment after them (locks, settings, journal) is not extracted.
    emit("history continuity", between(text, "// src/history/continuity.h\n", "// src/history/burn-up.h\n"))
    emit("burn-up model", between(text, "// src/history/burn-up.h\n", "// src/history/burn-up-store.h\n"))
    for name in ["HistoryReducePeriod", "HistoryPlotRightPadding", "HistoryClearHover", "HistoryPlotPoint", "HistoryLineColor", "HistoryRebuildRenderCache"]:
        emit(name, definition(text, name))
    emit("HistoryHoverHit", between(text, "struct HistoryHoverHit {", "// Picking boundary:"))
    for name in ["HistoryHitTest", "HistoryUpdateHover", "HistoryAnalyze", "HistoryScroll", "HistoryRebuildView"]:
        emit(name, definition(text, name))
    # Storage/export dialogs are not linked. Only the serializer writes to this memory sink.
    chunks.append('\n#line 1 "harness-csv-wrapper"\nstruct CapturedCsv {\nstd::string bytes;\nbool Write(const std::string& text) { bytes += text; return true; }\n')
    emit("HistoryCsvFile::WriteSnapshot", definition(text, "WriteSnapshot", method=True))
    chunks.append("\n};\n")
    emit("production paint", between(text, "struct HistoryPaint {", "static void HistoryLayout(HistoryWindowState& state, bool newFont) {"))
    for name in ["HistoryLayout", "HistoryGraphWndProc"]:
        emit(name, definition(text, name))
    # Extract the real selection branch, not an imitation of its state transitions.
    branch = "if (event == CBN_SELCHANGE) {"
    if text.count(branch) != 1:
        raise ValueError("Ambiguous selection branch")
    a = text.index(branch)
    chunks.append('\n#line 1 "harness-selection-wrapper"\nstatic void HistoryLoad(HistoryWindowState&) { throw std::runtime_error("HistoryLoad deliberately unavailable in isolated harness"); }\nstatic void DispatchSelection(HistoryWindowState* state, int id, int event = CBN_SELCHANGE) {\n')
    emit("HistoryWindowProc selection branch", (a, balanced(text, a)))
    chunks.append('\n}\n#include "fixtures/main.inc"\n')
    if source.read_bytes() != raw:
        raise ValueError("Source changed during extraction; rerun against settled source")
    output.write_bytes("\n".join(chunks).encode("utf-8"))
    manifest["generated_sha256"] = digest(output.read_bytes())
    output.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Generated {output}\nProduction SHA256: {manifest['sha256']}\nVerbatim sections: {len(manifest['extractions'])}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-hash", required=True)
    args = parser.parse_args()
    try:
        generate(args.source.resolve(), args.output.resolve(), args.expected_hash)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"Extraction refused: {error}\n")
