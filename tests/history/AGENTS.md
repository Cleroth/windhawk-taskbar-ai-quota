# History verification

Source-extracted regressions of the usage-history domain, in two targets:

- **model**: the history model (grouping, labels, reduction, statistics, account collection,
  selection, cancellation), continuity, Antigravity thinning, the flyout's burn-up curves and
  tails, the CSV writer and a refresh-cost observation.
- **journal**: the production journal on synthetic files: load, appends and their recovery,
  admission, the rewrite transactions (retention, cap, thinning, Clear, an account's deletion),
  the history window's peek, pin and explicit loads, burn-up tails refilled from the file, and
  storage compatibility (v1 files, the 2.18.0 move, unavailable, owned and linked storage).

Not a Windhawk or Explorer integration test. The usage history window itself (view, chart,
controller, frames, renders, its native host and dialog scope) is tested in `tests/settings`.

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

- `history.cpp`, `journal.cpp`: generated TUs of verbatim source sections and fixture includes,
  each with its `.manifest.json` (extraction boundaries, section hashes, generated TU hash).
- The executables, per-phase logs, and `results.json`: commands, exit codes, source/harness
  hashes before and after, test totals.
- `journal-files/`: the journal target's synthetic files, one folder per case (its storage and
  LocalAppData), removed when the case passes and kept when it fails.

The tool never prunes; old `run-*` directories can be removed after their runners exit.

Phases are serial with timeouts: tooling, freshness, both extractions, then each target's
compile and test. A failed compile never runs an old executable; concurrent invocations keep
separate executables and logs. No windows, Explorer messages/injection, Windhawk loading,
network, credentials, consent, registry, Windhawk values or the user's real journal/storage: the
journal target writes only its synthetic files under the run directory, through stubs that point
the storage folder and LocalAppData there; CSV serialization uses an in-memory sink. Never add
user samples, account identities or replay paths to fixtures.

## Success and evidence limits

Readiness (exit 0) requires every phase to pass: tooling checks, freshness (default source),
extraction, compiles and the tests, with exactly one successful `RESULT` summary per target.
Missing summaries, source/harness changes, assertion failures and timeouts fail the run.
Readiness evidence is a fresh successful run, not a result from an older source hash.

- **Model:** grouping, account collection, selection, continuity, the thinning machine (with the
  journal's record checksum; `fixtures/thinning.inc` applies its fates as the rewrite does),
  reduction, statistics, burn-up curves and tails (an archive reader injected) and in-memory CSV
  are actual source. `OBSERVE refresh-cost` times a refresh's copy, sort, account collection,
  series analysis and reduction at the 250,000-sample cap (`-O0`): an observation, not a
  readiness condition.
- **Journal:** `history/journal.h` up to its recording consent is actual source, with its
  coordination state, the storage folder's file helpers (`core/user-storage.h`), the thinning
  machine and burn-up tails. Stubbed: Windhawk's log, storage path and values, LocalAppData, the
  clock, the windows' notifier (counting) and `g_unloading`. File API failures (`ReadFile`,
  `WriteFile`, `FlushFileBuffers`, `MoveFileExW`, `SetEndOfFile`) are injected by macros over
  the real calls, allocation failures by a replaced `operator new`. `OBSERVE journal-memory`
  lines print memory against the archive (records, MB) at load, maintenance and the history
  window's open and close: observations, not readiness conditions. Not covered: recording
  consent, the fetch thread's calls (`RebuildBurnUpCurves`: its glue to the archive reader is
  repeated in the fixtures), the history window's production ops and real cross-process sharing.
- **Host integration:** real taskbar threads, XAML, Windhawk unload, real storage, OAuth,
  upgrades and Explorer are outside this suite, and passing proves none of them. Separately
  authorized runtime checks: [docs/usage-history-verification.md](../../docs/usage-history-verification.md).

## Maintenance rules

- `extract.py` uses explicit unique anchors/definitions, not a general C++ parser. It refuses
  missing/ambiguous boundaries, unsupported raw literals in scanned definitions, and source
  changes. Preserve extracted text exactly, including line endings; never patch generated code to
  pass a test. Review extraction when production structure changes. Extraction reads the
  generated file, but every section is bounded by its own `// src/<path>` fragment: pure
  fragments (the localization engine with its catalogs for the model's titles, `core/model.h`,
  `history/types.h`, `continuity.h`, `thinning.h`, `burn-up.h`, `series.h`, `csv.h`) and the
  journal's coordination state (`history/state.h`) whole, other fragments by named definitions
  or anchors searched inside that fragment only, so include order can't change what is
  extracted. The journal target takes `history/journal.h` from `struct HistoryFileHeader {` to
  its recording consent. Fix production code in `src/` and regenerate.
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
  re-thinning converging to a single pass. `fixtures/journal-rewrite.inc` streams it through the
  journal's maintenance across chunk boundaries.
- Journal target: `journal-stubs.hpp` holds the prelude, the stubs and the failure injection;
  `fixtures/journal-support.inc` the per-case folders, reset (as `Wh_ModUninit` leaves the
  journal), synthetic record and file helpers and the memory-against-file invariant check;
  `journal-file.inc` (load, appends, recovery, admission, storage), `journal-rewrite.inc`
  (rewrites, failures, out of memory, observations), `journal-window.inc` (peek, pin, explicit
  loads, burn-up tails). Files go only under the run directory, never the user's real storage.
- `win32-stubs.hpp` holds only the model target's prelude and the globals its extracted sections
  read; extracted code that gains window or storage calls doesn't belong in the model target.
- Keep generated artifacts under `.out/` by default; never promote them to source.
