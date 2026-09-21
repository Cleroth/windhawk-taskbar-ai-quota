# History verification

Maintained source-extracted model/UI/GDI regressions plus isolated native tooltip
contract probes. This is not a Windhawk or Explorer integration test.

## Run

From the repo root:

```text
python tests/history/run.py
python tests/history/run.py --capture
```

Prerequisites: Windows x64, Python 3.10+, PowerShell 7 (`pwsh`), and Windhawk's bundled
clang++ with its MinGW headers/libraries. No Python packages or installs. Use an
interactive Windows session with GDI/common controls available.

Options: `--source PATH`, `--compiler PATH`, `--output DIRECTORY`. Defaults are
repo-relative source, `C:\Program Files\Windhawk\Compiler\bin\clang++.exe`, and
`tests/history/.out/`. Paths supplied by the caller are relative to the caller's cwd.
`--output` changes the parent, never reuses a prior run directory. No environment
variable or session scratch files are required. `check-mod.ps1` remains the independent
fast whole-mod metadata/syntax check.

When the source is the repo-root generated file (the default), a `freshness` phase runs
`pwsh generate-mod.ps1 -Check` before extraction and fails the run if that file is stale
relative to `src/`; it never regenerates. Any other `--source` path skips it.

## Outputs and side effects

Each invocation creates a unique `.out/run-.../` directory, retained without deletion:

- `history.cpp`: generated TU containing verbatim source sections and fixture includes.
- `history.manifest.json`: extraction boundaries, section hashes and generated TU hash.
- Executables, a log per phase, and `results.json` with commands, exit codes, source and
  harness hashes before/after, test totals, GDI resources and native control observations.
- `--capture`: synthetic offscreen GDI BMPs and PNGs at 96/144/192 DPI. These are render
  evidence, not desktop screenshots or automated pixel-golden comparisons.

Capture runs can occupy tens to hundreds of megabytes. Old `run-*` directories can be
removed after their runners exit; the tool never prunes them automatically.

Compile/run phases are serial and bounded by timeouts. A failed compile never runs an
old executable. Concurrent invocations have separate executables, logs and captures.
The native probe creates hidden owner/child windows and an activated tooltip kept at
`-32000,-32000` by a tooltip-only subclass; it never activates the owner's window.
No Explorer messages/injection, Windhawk loading, network, credentials, consent,
registry or real journal/storage access. CSV serialization uses an in-memory sink.
Never add user samples, account identities or replay paths to fixtures.

## Success and evidence limits

Readiness exit 0 requires every positive phase to pass: tooling checks, freshness (default
source), extraction, model compile, model/UI/GDI tests, native compile, default-context
probe, explicit-v6 probe. Missing summaries, source/harness changes, assertion failures,
timeouts and GDI resource growth/deletion failures fail the run. Inspect `results.json` and phase logs; captures
are optional human review. A fresh successful invocation is the readiness evidence,
not a result from an older source hash.

- **Extracted model/UI:** grouping, selection, continuity, reduction, statistics,
  in-memory CSV, layout, picking, hover, cancellation and paint logic are actual source.
  Fake HWND/tooltip seams assert observable calls, text lifetime and failure handling.
  GDI rendering/resource counts are real. Stubs do not prove native control semantics.
- **Native contracts:** the separate positive probe exercises V2 `TOOLINFO`, swap-backed
  active text changes without hide/show, offscreen first activation then `GetWindowRect`,
  and no preactivation `TTM_GETBUBBLESIZE`. It records the actual control context/version;
  default context is not assumed to be v5 or to match Explorer. Explicit v6 must load v6.
  This protocol probe is hand-written, not an extracted production function.
- **Host integration:** real taskbar threads, XAML, Windhawk unload, storage, OAuth,
  upgrades and Explorer's activation context are outside this suite. Follow root/README
  guidance for separately authorized runtime verification. Passing here proves none of
  those host behaviors.

## Opt-in unsafe diagnostic observations

`python tests/history/run.py --diagnose-native-preactivation` additionally runs the
existing preactivation `TTM_GETBUBBLESIZE` diagnostic. Normal and `--capture` commands
never execute it. Do not run it as routine verification or to establish readiness.

Only after this run's native compile succeeds, the runner reuses that executable in
two separate child processes: `--preactivation` in default and explicit-v6 contexts.
There is no injection or new probe protocol. The same offscreen controls, unique run
directory and per-child timeout apply. No stale executable is used after compile failure.

Abnormal exits are expected possibilities: existing default-context v5.82 evidence
ended in access violation `0xC0000005` (unsigned 3221225477, signed -1073741819).
Existing explicit-v6 evidence exited 0 but returned an unusably small preactivation
size. Different systems may behave differently; even exit 0 does not prove the unsafe
call is usable or safe in Explorer. No diagnostic result is a positive test pass.

`results.json` records diagnostic commands, logs, exit codes and raw observations under
`diagnostics`, labeled `observations_not_readiness_tests`; they never enter `phases`
or its evidence parser. A timeout/start error is recorded there too. The runner's
readiness status/exit still depends on mandatory positive phases and integrity checks,
not diagnostic crashes, zero exits or missing positive summaries in diagnostic output.
Use mocked tooling tests to verify this separation; deliberately crashing execution
is not needed when existing native evidence suffices.

## Maintenance rules

- `extract.py` has explicit unique anchors/definitions, not a general C++ parser. It
  refuses missing/ambiguous boundaries, unsupported raw literals in scanned definitions,
  and source changes. Preserve exact extracted text, including line endings; never patch
  generated code to make a test pass. Review extraction when production structure changes.
  Extraction reads the generated single file, so a section may span `// src/<path>` marker
  lines; fix production code in `src/` and regenerate, never edit the generated file.
- Fixtures are split by domain under `fixtures/`. Add independent expected behavior, not
  Python/C++ copies of production decision rules or source-text call-count audits.
- Preserve synthetic continuity coverage for clock rollback, gaps, real resets,
  idle rolling deadlines and first-use anchoring. No real CSV replay.
- Update safe fake-window seams when extracted code gains Win32 calls; fake HWNDs must
  never reach real window APIs. Keep native contract probes separate from stubs and align
  their protocol with deliberate production changes.
- Keep generated artifacts under `.out/` by default, never promote them as source.
