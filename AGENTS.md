Windhawk mod that draws AI subscription quota bars on the Windows 11 taskbar; it runs injected into
`explorer.exe`.

# Documentation

- Mod code rules (source map, fragments, threading, locks, stability, credentials, settings):
  [src/AGENTS.md](src/AGENTS.md). Read it before planning, reviewing, or changing mod code,
  including diff-only or generated-file work. Reading a `src/` file attaches it automatically;
  Grep, shell output, and the generated root file don't.
- Custom-drawn settings and usage history windows (Direct2D toolkit: lifetime, input, popups,
  tooltips): [src/ui/AGENTS.md](src/ui/AGENTS.md).
- Localization (message keys, grammar, catalogs, translator notes, the catalog checker):
  [src/i18n/AGENTS.md](src/i18n/AGENTS.md).
- User-facing docs: [README.md](README.md), the GitHub readme (overview, languages, limitations,
  contributors), and its reference pages: [docs/taskbar.md](docs/taskbar.md) (quotas per
  provider, columns, flyout, outages, notifications), [docs/accounts.md](docs/accounts.md)
  (adding accounts, sign-in flows, security), [docs/settings.md](docs/settings.md) (every
  setting), [docs/usage-history.md](docs/usage-history.md) (recording, viewer, storage,
  downgrades) and its manual runtime checks
  [docs/usage-history-verification.md](docs/usage-history-verification.md).
- The catalog readme is the `WindhawkModReadme` block in `src/taskbar-ai-quota.cpp`; it hot-links
  `screenshots/*.png` from `master`. The catalog archives each image URL once and fails a PR whose
  image changed: a replaced screenshot needs a new filename.
- Test suites: [tests/history/AGENTS.md](tests/history/AGENTS.md) (usage-history domain) and
  [tests/settings/AGENTS.md](tests/settings/AGENTS.md) (windows, toolkit host, status pages,
  sign-in controls).
- Catalog updates (PR rules, changelog, workflow, AI review):
  [docs/catalog-publishing.md](docs/catalog-publishing.md).
- Contributors: when a commit credits someone new (`Co-authored-by`, or work taken from their
  PR), add them alphabetically (ignoring case) to [README > Contributors](README.md#contributors)
  and the About page's Contributors card (`src/settings/page-about.h`; its test's URL list and
  focus-stop count in `tests/settings/fixtures/ui.inc`). Add new work by existing contributors to
  their README line.

# Building

Windhawk loads the mod as one file, `local@taskbar-ai-quota.wh.cpp`, and compiles it itself with
its bundled clang. There is no `.sln`/`.vcxproj`. Do NOT use `msbuild`.

- `src/` is the source of truth. The root `local@taskbar-ai-quota.wh.cpp` is generated from it and
  committed; never edit it directly. Edit `src/`, then run `pwsh generate-mod.ps1`. `-Check`
  verifies freshness without writing; `check-mod.ps1` and `install-windhawk.ps1` run it.
- `.ignore` hides the generated file from searches; read it by path when you need it.
- Port edits made in Windhawk's editor, or PRs against the generated file, into `src/`. Each
  fragment starts after a `// src/<path>` marker line.
- `pwsh generate-mod.ps1 -Locate <line>` maps a line number from Windhawk compile errors or logs
  back to `src/<file>:<line>`.

## Check, install, runtime test

- `pwsh check-mod.ps1` checks freshness, runs bundled clang `-fsyntax-only` with Windhawk's editor
  flags (`-DWH_EDITING`: placeholder macros, no internal header), and validates metadata. Exit 0
  with no diagnostics = clean. It never links, so a lib missing from `@compilerOptions` only fails
  when Windhawk builds the mod.
- Toolchain: compiler `C:\Program Files\Windhawk\Compiler\bin\clang++.exe`; mod API headers
  (`windhawk_api.h`, `windhawk_utils.h`) in `C:\Program Files\Windhawk\Compiler\include`.
- `pwsh install-windhawk.ps1` (as Administrator) copies the mod into
  `C:\ProgramData\Windhawk\ModsSource`. It refuses to overwrite an installed copy that changed
  since its last install (e.g. edited in Windhawk's editor); port those edits into `src/`, or pass
  `-Force` to discard them.
- A full runtime test requires Windhawk loading the mod into `explorer.exe`; a compile can't test
  it.
- Workbench actions in `.agents/actions` run generate, check, install, catalog prep and PR status,
  plus pull/push of this repo's `master` (`.agents/git_*_master.bat`).

## App icon files

`python render-icon.py` regenerates `icon.png` (256 px) and `icon.ico` (16-256 px, PNG entries)
from `SettingsAppIcon` (`src/settings/page-about.h`); rerun after changing it. It fails on a stale
generated file, renders each size with bundled clang, Direct2D and WIC (transparent; tracks in a
fixed grey instead of the theme's ink) and writes build files to `.out/icon/`. Nothing references
the two files.

# Testing

Tests compile with bundled clang, never `msbuild`/`vstest.console`.

- `pwsh check-mod.ps1`: freshness, syntax and metadata (above).
- `python tests/history/run.py`: usage-history domain regressions (model, continuity, burn-up,
  journal memory, CSV).
- `python tests/settings/run.py`: extracted logic, offscreen renders and native probes of the
  settings, usage history and sign-in windows, the toolkit host and the status-page parser. Its
  full run includes `check-mod.ps1`'s check; `--popup-interactive` and `--tooltip-interactive`
  run by hand.
- `python tests/i18n/check.py`: catalogs against the message keys in `src/` (self-test:
  `tests/i18n/test_check.py`); guide: [src/i18n/AGENTS.md](src/i18n/AGENTS.md#checker).
- A full integration check still requires Windhawk loading the mod into `explorer.exe`.
- When changed native-control behavior is hidden by stubs, verify the assumed API/message contract
  with an isolated real-control probe under relevant, verified host contexts. Report caller-logic,
  native-contract, and Explorer integration results separately; a stub pass does not verify
  native behavior.
- For changes to persisted settings or identity keys, runtime-test an upgrade from the current
  catalog version with non-default accounts, visibility, configuration, and existing sign-ins.

## Storage audit

`storage-audit.ps1` is a read-only audit of the mod's stored state: settings, each sign-in's
presence and hash (never decrypted or printed), consent, language and journal metadata. Take a
snapshot before installing a step that writes settings or sign-ins.

- `pwsh storage-audit.ps1 snapshot` writes only a JSON snapshot, to
  `%LOCALAPPDATA%\TaskbarAIQuota-audit\` by default.
- `pwsh storage-audit.ps1 compare -Baseline <snapshot.json>` takes a new snapshot and classifies
  each difference as expected (`-Edited` fields, `-Disposable` accounts), tolerated or unexpected
  (including history ID changes and sign-ins appearing or disappearing); exit 1 means something
  unexpected. Snapshot contents, parameters and classification rules: the script's help.

It's an audit, not a backup. Restore only compatible scalar settings, by hand, with the mod
disabled; restoring an old `settings_v1` after a rename can orphan the moved sign-in. Sign-in
hashes can't restore sign-ins (refresh tokens rotate), and journal counts can't restore pruned
history. Rename/remove/add tests use disposable accounts only.
