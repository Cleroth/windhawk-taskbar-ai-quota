Windhawk mod that draws AI subscription quota bars on the Windows 11 taskbar; it runs injected into
`explorer.exe`.

# Documentation

- Mod code rules (fragments, threading, locks, stability, credentials, settings):
  [src/AGENTS.md](src/AGENTS.md). Read it before planning, reviewing, or changing mod code,
  including diff-only or generated-file work. Reading a `src/` file attaches it automatically;
  Grep, shell output, and the generated root file don't.
- User-facing behavior, settings list, sign-in flows and limitations: [README.md](README.md), the
  GitHub readme. The catalog readme is the `WindhawkModReadme` block in
  `src/taskbar-ai-quota.cpp`; it hot-links `ss1.png`-`ss3.png` from `master`.
- Usage-history behavior, storage and manual verification:
  [README > Usage history](README.md#usage-history).
- History regression suite: [tests/history/AGENTS.md](tests/history/AGENTS.md).
- Contributors: when a commit credits someone new (`Co-authored-by`, or work taken from their
  PR), add them alphabetically (ignoring case) to [README > Contributors](README.md#contributors)
  and the About page's Contributors card (`src/settings/page-about.h`; its test's URL list and
  focus-stop count in `tests/settings/fixtures/ui.inc`). Add new work by existing contributors to
  their README line.
- Building, checks, install, testing and catalog publishing: below.

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

## App icon files

`python render-icon.py` regenerates `icon.png` (256 px) and `icon.ico` (16-256 px, PNG entries)
from `SettingsAppIcon` (`src/settings/page-about.h`); rerun after changing it. It fails on a stale
generated file, extracts the icon from it, renders each size natively with bundled clang, Direct2D
and WIC (transparent; tracks in a fixed grey instead of the theme's ink) and assembles the ICO.
Build files go to `.out/icon/`. Nothing references the two files.

# Testing

- Run `check-mod.ps1` for syntax/metadata and `python tests/history/run.py` for isolated
  usage-history domain regressions (model, continuity, burn-up, journal memory, CSV). Both test
  suites use bundled clang, not `msbuild`/`vstest.console`.
- `python tests/settings/run.py` tests the provider status pages' parser and read rules against
  captured pages, the extracted meter model, UI toolkit, settings edit
  controller (against an in-memory store with failure injection), settings builders and the usage
  history window's view, chart, controller (against in-memory ops) and frame builders the same way,
  renders both windows (and the settings
  taskbar preview) offscreen, probes the toolkit's native
  window host (popup, tooltip, text boxes, sliders and the color dialog included) and the dropdown
  popup's capture/activation (`--popup-interactive` and the tooltip's pointer routing,
  `--tooltip-interactive`: by hand); its full run includes `check-mod.ps1`'s check; guide:
  [tests/settings/AGENTS.md](tests/settings/AGENTS.md).
- A full integration check still requires Windhawk loading the mod into `explorer.exe`.
- When changed native-control behavior is hidden by stubs, verify the assumed API/message contract
  with an isolated real-control probe under relevant, verified host contexts. Report caller-logic,
  native-contract, and Explorer integration results separately; a stub pass does not verify
  native behavior.
- For changes to persisted settings or identity keys, runtime-test an upgrade from the current
  catalog version with non-default accounts, visibility, configuration, and existing sign-ins.

## Storage audit

`pwsh storage-audit.ps1 snapshot` saves a read-only JSON snapshot of the mod's stored state to
`%LOCALAPPDATA%\TaskbarAIQuota-audit\` (`-Folder`; `-ModId`, default `local@taskbar-ai-quota`):
Windhawk LocalStorage (`settings_v1` parsed, `hiddenAccounts`, each `auth_*` sign-in as presence
plus a SHA-256 of its encrypted value; never decrypted, copied or printed), the HKCU
`RecordHistory` consent, and history journal metadata (files, header, per-day and per-history-ID
sample counts). It writes only the snapshot. Take one before installing a step that writes
settings or sign-ins.

`pwsh storage-audit.ps1 compare -Baseline <snapshot.json>` saves a new snapshot (or reads
`-Current`) and classifies each difference: expected (`-Edited` fields: top-level names,
`<label>.<field>`, `accountOrder`, `consent`; `-Disposable` account labels), tolerated (sign-in hash
changed while present, journal growth, retention pruning, storage-cap compaction; neither of the
last two while both snapshots keep history forever) or unexpected
(anything else, including history ID changes and sign-ins appearing or disappearing). Exit 1 means
something unexpected.

It's an audit, not a backup. Restore only compatible scalar settings, by hand, with the mod
disabled; restoring an old `settings_v1` after a rename can orphan the moved sign-in. Sign-in
hashes can't restore sign-ins (refresh tokens rotate), and journal counts can't restore pruned
history. Rename/remove/add tests use disposable accounts only.

# Publishing to the windhawk-mods catalog

Updates go by PR to `ramensoftware/windhawk-mods`. Rules (their README):

- The PR must change **exactly one file**: `mods/taskbar-ai-quota.wh.cpp`, a copy of the generated
  file without the local-dev `local@` filename prefix. `@id` (`taskbar-ai-quota`) never has it.
- `@github` is **required** and must be the PR author's profile URL: `https://github.com/Cleroth`.
  You can only update mods whose `@github` is yours.
- No `@license` means MIT; we set `@license MIT` explicitly. Keep metadata in
  `src/taskbar-ai-quota.cpp`.
- Every update bumps `@version`.

Catalog PRs are squash-merged, so the squash commit and public changelog use the **PR title and
body**, not the branch's commit messages. There is no `@changelog` metadata field or in-file
changelog section.

- Title: `Update taskbar-ai-quota: <summary> (vX.Y.Z)`.
- Body: stable and user-facing. Short, plain hyphen bullets starting with Add, Fix, or Improve,
  describing user-visible changes; no headings, implementation details, API-field explanations,
  or long parentheticals. Example: `- Fix quota bars not appearing on newer Windows 11 taskbars.`
  Add a brief `Note:` after the list only when users need migration or compatibility information.
- Put AI-review responses, design rationale, and minor-fix details in PR comments.

## Workflow (gh, authed as Cleroth)

`pwsh catalog-update.ps1` runs `check-mod.ps1`, then syncs a persistent clone of the
`Cleroth/windhawk-mods` fork with upstream `main`, checks out `taskbar-ai-quota-<version>`
(`-Branch` overrides), copies the mod, and verifies it is byte-identical, newer than the catalog
version, and the only changed file. It never commits, pushes, or edits PR text; `-Verify`
rechecks the worktree without changing it. Then:

```pwsh
$wt = "$env:LOCALAPPDATA\windhawk-taskbar-ai-quota\windhawk-mods"
git -C $wt add mods/taskbar-ai-quota.wh.cpp   # stage ONLY this file
git -C $wt commit -m "<msg>"
git -C $wt push -u origin <branch>
gh pr create -R ramensoftware/windhawk-mods -B main -H Cleroth:<branch>
```

`pwsh pr-review-status.ps1` (`-PrNumber` optional) reports checks and whether the latest AI review
matches the PR head. Workbench actions in `.agents/actions` run generate, check, install, catalog
prep, and PR status, plus pull/push of this repo's `master` (`.agents/git_*_master.bat`).

AI reviews are bound to the exact PR head SHA; any push invalidates readiness. The reviewer bot
currently allows three reviews per 24 hours. Request `/ai-review` only after the branch settles,
answer incorrect findings in comments instead of changing working code, and use
`/ready-for-reviewer` only after checks pass and the current head has been reviewed.
