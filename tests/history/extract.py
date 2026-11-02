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


def fragment(text, path):
    """A whole fragment: after its `// src/<path>` marker line, up to the next marker line. The
    last fragment runs to the end of the file, where only the entry's own code follows it."""
    starts = [m.end() for m in re.finditer(r"(?m)^// src/" + re.escape(path) + r"\n", text)]
    if len(starts) != 1:
        raise ValueError(f"Expected one marker for src/{path}, found {len(starts)}")
    end = re.compile(r"(?m)^// src/").search(text, starts[0])
    return starts[0], end.start() if end else len(text)


def within(text, path, find, *args):
    """Run a boundary search inside one fragment only, so neither another fragment's code nor
    include order can extend or satisfy it."""
    a, b = fragment(text, path)
    start, end = find(text[a:b], *args)
    return a + start, a + end


def constant(text, name):
    matches = list(re.finditer(r"(?m)^static constexpr [^\n]*\b" + re.escape(name) + r"\b[^\n]+", text))
    if len(matches) != 1:
        raise ValueError(f"Expected one {name} constant, found {len(matches)}")
    return matches[0].span()


def branch(text, opening):
    if text.count(opening) != 1:
        raise ValueError(f"Ambiguous branch: {opening!r}")
    a = text.index(opening)
    return a, balanced(text, a)


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

    # Every section is bounded by its own fragment. Pure fragments are taken whole: the account
    # model, sample types, continuity rules, the flyout's burn-up curves and the shared history
    # model; so is the journal's coordination state (a lock, counters and flags). Journal, store
    # and window fragments only contribute named definitions.
    emit("core/model.h", fragment(text, "core/model.h"))
    emit("history/types.h", fragment(text, "history/types.h"))
    # The journal's memory-only parts: its in-memory state (never the storage state after it),
    # the status notifier and the history window's peek. Nothing that loads or writes storage.
    journal = "history/journal.h"
    emit("history/state.h", fragment(text, "history/state.h"))
    emit("journal memory state", within(text, journal, between, "static std::deque<HistorySample> g_historySamples;", "static std::wstring g_historyPath;"))
    for name in ["SetHistoryStatus", "PeekHistorySnapshot"]:
        emit(name, within(text, journal, definition, name))
    emit("filetime epoch", within(text, "core/helpers.h", constant, "kFileTimeUnixEpochOffsetMs"))
    for name in ["Utf8ToWide", "WideToUtf8", "UnixMsToLocalSystemTime", "ProviderDisplayName"]:
        emit(name, within(text, "core/helpers.h", definition, name))
    emit("ScaleForDpi", within(text, "settings/native.h", definition, "ScaleForDpi"))
    for path in ["history/continuity.h", "history/burn-up.h", "history/series.h"]:
        emit(path, fragment(text, path))
    # Classic window: its view types, then view-model, export, paint and window definitions.
    view = "history/view-model.h"
    emit("history view definitions", within(text, "history/window-state.h", between, "static constexpr int kHistorySeriesHeight", "static void HistoryLayout(HistoryWindowState& state, bool newFont);"))
    for name in ["HistoryLocalDate", "HistoryUtcDate", "HistoryReducePeriod", "HistoryPlotRightPadding", "HistoryClearHover", "HistoryPlotPoint", "HistoryLineColor", "HistoryRebuildRenderCache"]:
        emit(name, within(text, view, definition, name))
    emit("HistoryHoverHit", within(text, view, between, "struct HistoryHoverHit {", "// Picking boundary:"))
    for name in ["HistoryHitTest", "HistoryUpdateHover", "HistoryScroll", "HistoryRebuildView"]:
        emit(name, within(text, view, definition, name))
    # Storage/export dialogs are not linked. Only the serializer writes to this memory sink.
    chunks.append('\n#line 1 "harness-csv-wrapper"\nstruct CapturedCsv {\nstd::string bytes;\nbool Write(const std::string& text) { bytes += text; return true; }\n')
    emit("HistoryCsvFile::WriteSnapshot", within(text, "history/export.h", definition, "WriteSnapshot", True))
    chunks.append("\n};\n")
    emit("history/paint.h", fragment(text, "history/paint.h"))
    for name in ["HistoryLayout", "HistoryGraphWndProc"]:
        emit(name, within(text, "history/window.h", definition, name))
    # Extract the real selection branch, not an imitation of its state transitions.
    chunks.append('\n#line 1 "harness-selection-wrapper"\nstatic void HistoryLoad(HistoryWindowState&) { throw std::runtime_error("HistoryLoad deliberately unavailable in isolated harness"); }\nstatic void DispatchSelection(HistoryWindowState* state, int id, int event = CBN_SELCHANGE) {\n')
    emit("HistoryWindowProc selection branch", within(text, "history/window.h", branch, "if (event == CBN_SELCHANGE) {"))
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
