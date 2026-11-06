# History verification

Source-extracted regressions of the usage-history domain: the history model (grouping, labels,
reduction, statistics, account collection, selection, cancellation), continuity, Antigravity
thinning, the flyout's burn-up curves, the journal's memory parts and the CSV writer. Not a Windhawk or Explorer
integration test. The usage history window itself (view, chart, controller, frames, renders, its
native host and dialog scope) is tested in `tests/settings`.

## Run

From the repo root:

```text
python tests/history/run.py
```

Prerequisites: Windows x64, Python 3.10+, PowerShell 7 (`pwsh`) and Windhawk's bundled clang++
with its MinGW headers/libraries. No Python packages or installs.

Options: `--source PATH` (default: the repo-root generated file), `--compiler PATH` (default:
`C:\Program Files\Windhawk\Compiler\bin\clang++.exe`), `--output DIRECTORY` (default:
`tests/history/.out/`; the parent for a new run directory, never a reused one). Caller-supplied
paths resolve against the caller's cwd. No environment variables or session scratch files are
needed. `check-mod.ps1` remains the separate fast whole-mod metadata/syntax check.

With the default source, a `freshness` phase runs `pwsh generate-mod.ps1 -Check` before
extraction and fails the run if the file is stale relative to `src/`; it never regenerates.
Other `--source` paths skip it.

## Outputs and side effects

Each invocation creates a unique, retained `.out/run-.../` directory:

- `history.cpp`: generated TU of verbatim source sections and fixture includes.
- `history.manifest.json`: extraction boundaries, section hashes and generated TU hash.
- The executable, per-phase logs, and `results.json`: commands, exit codes, source/harness hashes
  before and after, test totals.

The tool never prunes; old `run-*` directories can be removed after their runners exit.

Phases are serial with timeouts. A failed compile never runs an old executable; concurrent
invocations keep separate executables and logs. No windows, Explorer messages/injection,
Windhawk loading, network, credentials, consent, registry, or real journal/storage access; CSV
serialization uses an in-memory sink. Never add user samples, account identities or replay paths
to fixtures.

## Success and evidence limits

Readiness (exit 0) requires every phase to pass: tooling checks, freshness (default source),
extraction, compile and the tests, with exactly one successful `RESULT` summary. Missing
summaries, source/harness changes, assertion failures and timeouts fail the run. Readiness
evidence is a fresh successful run, not a result from an older source hash.

- **Extracted model:** grouping, account collection, selection, continuity, Antigravity thinning
  (with the journal's record checksum), reduction, statistics, burn-up curves and in-memory CSV
  are actual source. Thinning's call from maintenance (its cadence, swap and revision bump) is not
  extracted.
- **Journal memory parts** (`fixtures/journal.inc`): the in-memory journal state, the status
  notifier (`SetHistoryStatus`, its window notification a counting stub), the history window's
  peek (`PeekHistorySnapshot`) and an account deletion's memory step
  (`EraseHistorySamplesLocked`, with the peek and burn-up tails it must refresh) are actual
  source; nothing that loads or writes storage is extracted (nor the deletion's rewrite and
  window notification), so the peek compiles only while it never loads. Its lock is the real
  `g_historyMutex`. A refresh-cost observation at the 250,000-sample cap prints one `OBSERVE
  refresh-cost` line (copy, sort, account collection, series analysis, reduction; `-O0` like the
  rest of the build): an observation, not a readiness condition. The tail cache's reaction to
  retention pruning is `burn-up.inc`'s; the prune's revision bump in `MaintainHistory` is not
  extracted.
- **Host integration:** real taskbar threads, XAML, Windhawk unload, storage, OAuth, upgrades and
  Explorer are outside this suite, and passing proves none of them. Separately authorized runtime
  checks: [docs/usage-history-verification.md](../../docs/usage-history-verification.md).

## Maintenance rules

- `extract.py` uses explicit unique anchors/definitions, not a general C++ parser. It refuses
  missing/ambiguous boundaries, unsupported raw literals in scanned definitions, and source
  changes. Preserve extracted text exactly, including line endings; never patch generated code to
  pass a test. Review extraction when production structure changes. Extraction reads the
  generated file, but every section is bounded by its own `// src/<path>` fragment: pure
  fragments (the localization engine with its catalogs for the model's titles, `core/model.h`,
  `history/types.h`, `continuity.h`, `thinning.h`, `burn-up.h`, `series.h`, `csv.h`) and the journal's
  coordination state (`history/state.h`) whole, other fragments by
  named definitions or anchors searched inside that fragment only, so include order can't change
  what is extracted. `history/journal.h` keeps its in-memory state before its storage state
  (`g_historyPath` ends the extracted block). Fix production code in `src/` and regenerate.
- `fixtures/model.inc` tests the history model (`history/series.h`) through its explicit inputs.
  `fixtures/csv.inc` tests the CSV writer (`history/csv.h`, whole) with frozen scopes:
  hand-derived bytes, 64 KiB pieces, cancellation, a failing sink, and the model's titles in
  another language (and de-DE's number format) beside the CSV's unchanged English bytes.
- Fixtures are split by domain under `fixtures/`. Add independent expected behavior, not
  Python/C++ copies of production decision rules or source-text call-count audits.
- Preserve synthetic continuity coverage for clock rollback, gaps, real resets, idle rolling
  deadlines and first-use anchoring. No real CSV replay.
- `fixtures/thinning.inc` tests Antigravity thinning (`history/thinning.h`) on minute readings
  with hand-derived kept times: steady runs, reset drift, gaps, failed readings, meters appearing
  or disappearing, real resets, the 15-day boundary, other accounts and providers, and daily
  re-thinning converging to a single pass.
- `win32-stubs.hpp` holds only the prelude and the globals the extracted sections read; extracted
  code that gains window or storage calls doesn't belong in this suite.
- Keep generated artifacts under `.out/` by default; never promote them to source.
