"""Extract production fragments, named definitions and constants verbatim for the settings suite."""
import argparse
import hashlib
import json
from pathlib import Path
import re

# Declaration order: each section may use the ones before it.
# The localization engine with its catalogs, ahead of everything that may localize.
I18N = [("fragment", f"i18n/{name}.h") for name in
        ("template", "catalog", "lang-de", "lang-es", "lang-fr", "lang-ja", "lang-pt-BR", "lang-ru", "lang-zh-Hans",
         "languages", "pseudo", "format")]
UI_PURE = [("fragment", f"ui/{name}.h") for name in
           ("tokens", "draw", "svg-path", "rich-text", "items", "paint", "input", "tooltip-layout")]

# Pure model, toolkit and page builders: tests and offscreen renders.
MODEL = [
    *I18N,
    ("fragment", "core/model.h"),
    ("definition", "ApplyCreditsMax"),
    ("fragment", "taskbar/logo-paths.h"),
    ("fragment", "taskbar/meter-model.h"),
    *UI_PURE,
    ("fragment", "settings/edits.h"),
    ("fragment", "settings/view.h"),
    ("fragment", "settings/preview.h"),
    ("fragment", "settings/page-about.h"),
    ("fragment", "settings/page-behavior.h"),
    ("fragment", "settings/page-history.h"),
    ("fragment", "settings/page-layout.h"),
    ("fragment", "settings/page-display.h"),
    ("fragment", "settings/page-accounts.h"),
    ("fragment", "settings/shell.h"),
]

# The usage history window's view, formats, chart, controller, chart painter and frame builders
# on the shared history model and CSV writer: tests and offscreen renders. No journal, settings
# storage, production ops or window code.
HISTORY = [
    *I18N,
    ("fragment", "core/model.h"),
    ("definition", "ApplyCreditsMax"),
    ("definition", "Utf8ToWide"),
    ("definition", "WideToUtf8"),
    ("fragment", "taskbar/logo-paths.h"),
    ("fragment", "taskbar/meter-model.h"),
    *UI_PURE,
    ("fragment", "history/types.h"),
    ("fragment", "history/continuity.h"),
    ("fragment", "history/series.h"),
    ("fragment", "history/csv.h"),
    ("fragment", "history/view.h"),
    ("fragment", "history/chart.h"),
    ("fragment", "history/controller.h"),
    ("fragment", "history/chart-paint.h"),
    ("fragment", "history/content.h"),
    ("fragment", "history/shell.h"),
]

# Toolkit with its native host (window icons, popup, tooltip, text boxes and color dialog
# included) and the history window's dialog scope: the probe DLL. No settings, storage or account
# code.
HOST = [
    ("definition", "WindowDpi"),
    *UI_PURE,
    ("fragment", "ui/system-theme.h"),
    ("fragment", "ui/surface.h"),
    ("fragment", "ui/window-icon.h"),
    ("fragment", "ui/popup.h"),
    ("fragment", "ui/tooltip.h"),
    ("fragment", "ui/text-box.h"),
    ("fragment", "ui/host.h"),
    ("fragment", "ui/color-dialog.h"),
    ("fragment", "history/modal.h"),
]

# Provider status pages: their parser, on Windows.Data.Json (the one WinRT API allowed here), and
# the read rules. No network, globals or windows.
STATUS = [
    ("fragment", "core/model.h"),
    ("constant", "kFileTimeUnixEpochOffsetMs"),
    ("definition", "Utf8ToWide"),
    ("definition", "ParseIso8601Ms"),
    ("definition", "GetObj"),
    ("definition", "GetNum"),
    ("definition", "GetStr"),
    ("definition", "GetBool"),
    ("fragment", "fetch/status-feeds.h"),
]

TARGETS = {
    "model": (MODEL, "harness.hpp", "fixtures/main.inc"),
    "history": (HISTORY, "harness.hpp", "fixtures/history-main.inc"),
    "status": (STATUS, "status.hpp", "fixtures/status-main.inc"),
    "host": (HOST, "host.hpp", "fixtures/host-dll.inc"),
    # The same host as the owner window of the capture/foreground popup probe.
    "popup": (HOST, "host.hpp", "fixtures/popup-probe.inc"),
}


def balanced(text, start):
    """Find the closing brace, ignoring comments and ordinary quoted literals.

    Not a C++ parser: raw literals in a scanned definition fail closed.
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


def definition(text, name):
    matches = list(re.finditer(r"(?m)^static [^\n;{}]+?\b" + re.escape(name) + r"\([^;{}]*\)\s*\{", text))
    if len(matches) != 1:
        raise ValueError(f"Expected one definition of {name}, found {len(matches)}")
    start = matches[0].start()
    return start, balanced(text, start)


def constant(text, name):
    """A one-line `static constexpr` constant, with its line break."""
    matches = list(re.finditer(r"(?m)^static constexpr [^\n=;{}]+?\b" + re.escape(name) + r" = [^\n;{}]+;\n", text))
    if len(matches) != 1:
        raise ValueError(f"Expected one constant {name}, found {len(matches)}")
    return matches[0].start(), matches[0].end()


def fragment(text, path):
    """A whole fragment: after its `// src/<path>` marker line, up to the next marker line."""
    starts = [m.end() for m in re.finditer(r"(?m)^// src/" + re.escape(path) + r"\n", text)]
    if len(starts) != 1:
        raise ValueError(f"Expected one marker for src/{path}, found {len(starts)}")
    end = re.compile(r"(?m)^// src/").search(text, starts[0])
    if not end:
        raise ValueError(f"No marker after src/{path}")
    return starts[0], end.start()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def generate(source, output, expected_hash, target="model"):
    sections, header, fixture = TARGETS[target]
    raw = source.read_bytes()
    if digest(raw) != expected_hash:
        raise ValueError("Source changed before extraction; rerun against settled source")
    text = raw.decode("utf-8-sig")
    manifest = {"source": str(source), "sha256": digest(raw), "target": target, "extractions": []}
    chunks = ["// GENERATED: production sections below are extracted verbatim.\n",
              f'#include "{header}"\n']
    for kind, name in sections:
        a, b = {"fragment": fragment, "definition": definition, "constant": constant}[kind](text, name)
        code = text[a:b]
        line = text.count("\n", 0, a) + 1
        manifest["extractions"].append({"kind": kind, "name": name, "line": line,
                                        "sha256": digest(code.encode("utf-8")), "characters": len(code)})
        # Diagnostics point at the generated mod file without modifying the code.
        chunks.append(f'\n#line {line} {json.dumps(source.as_posix())}\n{code}\n')
    chunks.append(f'\n#include "{fixture}"\n')
    if source.read_bytes() != raw:
        raise ValueError("Source changed during extraction; rerun against settled source")
    output.write_bytes("".join(chunks).encode("utf-8"))
    manifest["generated_sha256"] = digest(output.read_bytes())
    output.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Generated {output}\nProduction SHA256: {manifest['sha256']}\nVerbatim sections: {len(manifest['extractions'])}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-hash", required=True)
    parser.add_argument("--target", choices=sorted(TARGETS), default="model")
    args = parser.parse_args()
    try:
        generate(args.source.resolve(), args.output.resolve(), args.expected_hash, args.target)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"Extraction refused: {error}\n")
