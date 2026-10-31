# Settings verification

Source-extracted tests of the pure fragments shared by the taskbar and the settings window,
starting with the meter model. Not a Windhawk, Explorer, XAML or settings window test.

## Run

From the repo root:

```text
python tests/settings/run.py
```

Prerequisites: Windows x64, Python 3.10+, PowerShell 7 (`pwsh`), Windhawk's bundled clang++ with
its MinGW headers/libraries. No Python packages, installs or interactive session.

Options: `--source PATH` (default: the repo-root generated file), `--compiler PATH` (default:
`C:\Program Files\Windhawk\Compiler\bin\clang++.exe`), `--output DIRECTORY` (default:
`tests/settings/.out/`; the parent for a new run directory). With the default source, a
`freshness` phase runs `pwsh generate-mod.ps1 -Check` first and fails on a stale file; it never
regenerates.

## Outputs and side effects

Each run creates a retained `.out/run-.../` with `settings.cpp` (verbatim extracted sections and
fixture includes), `settings.manifest.json` (section boundaries and hashes), the executable,
per-phase logs and `results.json`. The tool never prunes old runs. No registry, storage,
network, credential, window or Explorer access.

## Success and evidence limits

Exit 0 requires every phase to pass: tooling self-tests, freshness (default source), extraction,
compile and exactly one successful `RESULT` summary, with source and harness unchanged during
the run. A pass covers the extracted model logic only; XAML application, theme detection,
measuring and Explorer behavior need the runtime check in the root guide.

## Maintenance rules

- `extract.py` takes whole fragments between `// src/<path>` marker lines of the generated file,
  plus named definitions; it refuses missing or ambiguous boundaries and source changes.
  Extracted fragments must stay pure (no XAML, WinRT, window, registry, storage or global state):
  fix impurities in `src/`, never patch extracted code. The only stub allowed is `Wh_Log`.
- Never extract the production store, storage, auth or account commands.
- Fixtures live by domain under `fixtures/`: synthetic inputs with hand-derived expected values,
  not copies of production rules.
- Keep generated artifacts under `.out/`.
