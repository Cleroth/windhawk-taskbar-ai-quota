# History verification

Source-extracted model/UI/GDI regressions plus isolated native tooltip contract probes. Not a
Windhawk or Explorer integration test.

## Run

From the repo root:

```text
python tests/history/run.py
python tests/history/run.py --capture
```

Prerequisites: Windows x64, Python 3.10+, PowerShell 7 (`pwsh`), Windhawk's bundled clang++ with
its MinGW headers/libraries, and an interactive session with GDI/common controls. No Python
packages or installs.

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
- Executables, per-phase logs, and `results.json`: commands, exit codes, source/harness hashes
  before and after, test totals, GDI resources and native control observations.
- `--capture`: synthetic offscreen GDI BMPs/PNGs at 96/144/192 DPI. Render evidence, not desktop
  screenshots or automated pixel-golden comparisons.

Capture runs can take tens to hundreds of MB. The tool never prunes; old `run-*` directories can
be removed after their runners exit.

Compile/run phases are serial with timeouts. A failed compile never runs an old executable;
concurrent invocations keep separate executables, logs and captures. The native probe creates
hidden owner/child windows and an activated tooltip held at `-32000,-32000` by a tooltip-only
subclass; it never activates the owner window. No Explorer messages/injection, Windhawk loading,
network, credentials, consent, registry, or real journal/storage access; CSV serialization uses an
in-memory sink. Never add user samples, account identities or replay paths to fixtures.

## Success and evidence limits

Readiness (exit 0) requires every positive phase to pass: tooling checks, freshness (default
source), extraction, model compile, model/UI/GDI tests, native compile, default-context probe,
explicit-v6 probe. Missing summaries, source/harness changes, assertion failures, timeouts and
GDI resource growth/deletion failures fail the run. Inspect `results.json` and phase logs;
captures are optional human review. Readiness evidence is a fresh successful run, not a result
from an older source hash.

- **Extracted model/UI:** grouping, selection, continuity, reduction, statistics, in-memory CSV,
  layout, picking, hover, cancellation and paint logic are actual source. Fake HWND/tooltip seams
  assert observable calls, text lifetime and failure handling. GDI rendering/resource counts are
  real. Stubs don't prove native control semantics.
- **Native contracts:** a separate hand-written positive probe (not extracted production code)
  exercises V2 `TOOLINFO`, swap-backed active text changes without hide/show, offscreen first
  activation then `GetWindowRect`, and no preactivation `TTM_GETBUBBLESIZE`. It records the actual
  control context/version; the default context isn't assumed to be v5 or to match Explorer.
  Explicit v6 must load v6.
- **Host integration:** real taskbar threads, XAML, Windhawk unload, storage, OAuth, upgrades and
  Explorer's activation context are outside this suite, and passing proves none of them. Follow
  root/README guidance for separately authorized runtime verification.

## Opt-in unsafe diagnostic

`python tests/history/run.py --diagnose-native-preactivation` also runs the preactivation
`TTM_GETBUBBLESIZE` diagnostic. Normal and `--capture` runs never execute it. Never run it as
routine verification or to establish readiness.

Only after this run's native compile succeeds, the runner reuses that executable in two child
processes: `--preactivation` in default and explicit-v6 contexts, with the same offscreen
controls, run directory and per-child timeout. No injection, new probe protocol, or stale
executable after a compile failure.

Abnormal exits are possible. Existing evidence: the default context (v5.82) ended in access
violation `0xC0000005` (unsigned 3221225477, signed -1073741819); explicit v6 exited 0 but
returned an unusably small preactivation size. Other systems may differ; even exit 0 doesn't
prove the call is usable or safe in Explorer. No diagnostic result is a positive pass.

`results.json` records diagnostic commands, logs, exit codes, raw observations and any
timeout/start error under `diagnostics`, labeled `observations_not_readiness_tests`; they never
enter `phases` or its evidence parser. Readiness status/exit depends only on mandatory positive
phases and integrity checks, not on diagnostic crashes, zero exits or missing positive summaries.
Verify this separation with mocked tooling tests; deliberate crashes aren't needed when existing
native evidence suffices.

## Maintenance rules

- `extract.py` uses explicit unique anchors/definitions, not a general C++ parser. It refuses
  missing/ambiguous boundaries, unsupported raw literals in scanned definitions, and source
  changes. Preserve extracted text exactly, including line endings; never patch generated code to
  pass a test. Review extraction when production structure changes. Extraction reads the
  generated file, so a section may span `// src/<path>` markers; fix production code in `src/`
  and regenerate.
- Fixtures are split by domain under `fixtures/`. Add independent expected behavior, not
  Python/C++ copies of production decision rules or source-text call-count audits.
- Preserve synthetic continuity coverage for clock rollback, gaps, real resets, idle rolling
  deadlines and first-use anchoring. No real CSV replay.
- Update the safe fake-window seams when extracted code gains Win32 calls; fake HWNDs must never
  reach real window APIs. Keep native contract probes separate from stubs and align their protocol
  with deliberate production changes.
- Keep generated artifacts under `.out/` by default; never promote them to source.
