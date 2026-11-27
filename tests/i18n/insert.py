"""Insert new entries into the language catalogs (src/i18n/lang-<tag>.h) at their sorted positions.

Usage: python tests/i18n/insert.py ENTRIES.json [--src DIR] [--dry-run]

ENTRIES.json: {"<tag>": [["<context>", "<English>", "<translation>"], ...], ...}; an empty context
is "". Strings are written into the C++ wide literals as given, so an escape such as \\u00A0 must
reach this script as the characters backslash-u (in JSON: "\\\\u00A0"). English keys must be plain
ASCII without C++ escapes. It only adds: an entry already present (same context and English) stops
the run before that catalog is written. Run the checker (check.py) afterwards.
"""
import argparse
import json
from pathlib import Path
import re
import sys

REPO = Path(__file__).resolve().parents[2]
ENTRY_START = re.compile(r'^    \{L"((?:[^"\\]|\\.)*)", L"((?:[^"\\]|\\.)*)"')
WIDTH = 120


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("entries", type=Path, help="JSON file of the entries to insert, per language tag")
    parser.add_argument("--src", type=Path, default=REPO / "src", help="Source root (default: the repo's src/)")
    parser.add_argument("--dry-run", action="store_true", help="List the insertions without writing")
    args = parser.parse_args()
    data = json.loads(args.entries.read_text(encoding="utf-8"))
    for tag, entries in data.items():
        path = args.src / "i18n" / f"lang-{tag}.h"
        raw = path.read_bytes()
        newline = "\r\n" if b"\r\n" in raw else "\n"
        lines = raw.decode("utf-8").split(newline)
        for context, english, translation in entries:
            # Entry starts, in file order: (line index, context, English). Multi-line English
            # literals only give their first part, which is enough to order ASCII keys unless a
            # new key shares that whole part.
            starts = [(i, m.group(1), m.group(2)) for i, line in enumerate(lines)
                      if (m := ENTRY_START.match(line))]
            if any(c == context and e == english for _, c, e in starts):
                sys.exit(f"{path}: already has {context!r} / {english!r}")
            after = [i for i, c, e in starts if (c, e) > (context, english)]
            if after:
                at = after[0]
            else:
                # After the last entry: the line closing it.
                at = max(i for i, line in enumerate(lines) if line.endswith("},")) + 1
            one = f'    {{L"{context}", L"{english}", L"{translation}"}},'
            new = [one] if len(one) <= WIDTH else [f'    {{L"{context}", L"{english}",', f'     L"{translation}"}},']
            lines[at:at] = new
            print(f"{path.name}:{at + 1}: {new[0]}")
        if not args.dry_run:
            path.write_bytes(newline.join(lines).encode("utf-8"))


if __name__ == "__main__":
    main()
