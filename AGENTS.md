# Building

Windhawk loads the mod as one file, `local@taskbar-ai-quota.wh.cpp`, generated from `src/`. There
is no `.sln`/`.vcxproj`. Do NOT use `msbuild`. Windhawk compiles the `.wh.cpp` itself with its
bundled clang when the mod is loaded.

## Source layout

- `src/` is the source of truth. The root `local@taskbar-ai-quota.wh.cpp` is generated and
  committed; never edit it directly. Edit `src/`, then run `pwsh generate-mod.ps1`.
  `generate-mod.ps1 -Check` verifies freshness without writing; `check-mod.ps1` and
  `install-windhawk.ps1` run it for the root file. `.ignore` hides the root file from searches;
  read it by path when you need the generated output.
- Edits made in Windhawk's editor, or PRs against the root file, must be ported into `src/`. Each
  fragment starts after a `// src/<path>` marker line in the generated file.
- `src/taskbar-ai-quota.cpp` is the entry: mod metadata and readme, system includes, the fragment
  includes, and the `Wh_Mod*` callbacks. Fragments form one translation unit, and the entry's
  include order is declaration order.
- Fragments contain no local includes. Every file under `src/` except the entry must be included
  exactly once; the generator fails otherwise. Put forward declarations for calls into a later
  fragment in `src/core/forward.h`. Keep file-scope functions and globals `static`.
- `src/` files are UTF-8 without BOM, LF, and end with exactly one newline.
- `pwsh generate-mod.ps1 -Locate <line>` maps a line number from Windhawk compile errors or logs
  back to `src/<file>:<line>`.

## Local compile / syntax check

Windhawk's editor compiles with `-DWH_EDITING` (placeholder macros, no internal header, no
link). Replicate that for a fast local check with the bundled clang:

```pwsh
& "C:\Program Files\Windhawk\Compiler\bin\clang++.exe" `
  --target=x86_64-w64-mingw32 -std=c++23 `
  -DWH_EDITING -DWH_MOD -DUNICODE -D_UNICODE -municode -Wall `
  -I"C:\Program Files\Windhawk\Compiler\include" `
  -fsyntax-only "local@taskbar-ai-quota.wh.cpp"
```

Exit code 0 with no diagnostics = clean. This checks the whole translation unit (winrt + Win32
headers included) but does not link, so it won't catch missing libs.

Toolchain paths (Windhawk install): compiler `C:\Program Files\Windhawk\Compiler\bin\clang++.exe`,
mod API headers (`windhawk_api.h`, `windhawk_utils.h`) in `C:\Program Files\Windhawk\Compiler\include`.

## When adding Win32/WinRT APIs

Keep the `@compilerOptions` line in the mod header in sync with the libs you call (e.g. adding
`Shell_NotifyIconW` requires `-lshell32`). A `-fsyntax-only` check passes regardless of libs, so
linking errors only surface when Windhawk actually builds the mod.

Runtime OS support doesn't guarantee that Windhawk's bundled MinGW headers and import libraries
expose an API. Verify with the bundled toolchain before replacing dynamic `GetProcAddress`
lookups/fallbacks with direct calls.

## Install / runtime test

`pwsh install-windhawk.ps1` copies the mod into `C:\ProgramData\Windhawk\ModsSource`. It refuses
to overwrite an installed copy that changed since its last install (e.g. edited in Windhawk's
editor); port those edits into `src/`, or pass `-Force` to discard them. A full runtime test
requires Windhawk loading the mod into `explorer.exe`; it can't be tested from a plain compile.

# Publishing to the windhawk-mods catalog

Mods are published via PR to `ramensoftware/windhawk-mods`. Rules (their README):

- PR must change **exactly one file**: `mods/<mod-id>.wh.cpp`. Here `<mod-id>` is `taskbar-ai-quota`
  (the `@id`), so the file is `mods/taskbar-ai-quota.wh.cpp`. The local `local@` filename prefix is
  a Windhawk local-dev convention and is **dropped** in the catalog; `@id` itself never has it.
- `@github` is **required** and must be the PR author's profile URL: `https://github.com/Cleroth`.
- No `@license` => MIT by default; we set `@license MIT` explicitly.
- New mod: any `@version` is fine. Update: bump `@version`. You can only update mods whose
  `@github` is yours.

Catalog PRs are squash-merged, so the squash commit and public changelog use the **PR title and
body**, not the branch's individual commit messages. Use
`Update taskbar-ai-quota: <summary> (vX.Y.Z)` for the title. Keep the body stable and user-facing;
put AI-review responses, design rationale, and minor-fix details in PR comments. There is no
`@changelog` metadata field or in-file changelog section.

Write the body as short, plain hyphen bullets starting with Add, Fix, or Improve, describing
user-visible changes.
No headings, implementation details, API-field explanations, or long parentheticals. For example:
`- Fix quota bars not appearing on newer Windows 11 taskbars.` Add a brief `Note:` after the list
only when users need migration or compatibility information.

The catalog file is a copy of the generated `local@taskbar-ai-quota.wh.cpp` with the `local@`
filename prefix stripped. Keep the metadata (incl. `@github`/`@license`) in sync in
`src/taskbar-ai-quota.cpp`.

## Workflow (gh, authed as Cleroth)

```pwsh
# pre-flight: check-mod.ps1 (freshness + clang check) must pass first
$dest = "$env:TEMP\windhawk-mods"
gh repo sync Cleroth/windhawk-mods --source ramensoftware/windhawk-mods --branch main
git clone --single-branch --branch main https://github.com/Cleroth/windhawk-mods.git $dest
git -C $dest checkout -b <branch>
Copy-Item "local@taskbar-ai-quota.wh.cpp" "$dest\mods\taskbar-ai-quota.wh.cpp" -Force
git -C $dest add mods/taskbar-ai-quota.wh.cpp   # stage ONLY this file
git -C $dest commit -m "<msg>"                  # update => include version note
git -C $dest push -u origin <branch>
gh pr create -R ramensoftware/windhawk-mods -B main -H Cleroth:<branch>
```

## Release helpers

- `pwsh check-mod.ps1` checks freshness, runs the canonical bundled-clang check, and validates
  source metadata.
- `pwsh catalog-update.ps1` prepares a persistent catalog worktree without committing, pushing,
  or editing PR text. `-Verify` checks the current worktree without changing it.
- `pwsh pr-review-status.ps1` reports checks and whether the latest AI review matches the PR head.

Matching Workbench actions live in `.agents/actions`.

AI reviews are bound to the exact PR head SHA; any push invalidates readiness. The reviewer bot
currently allows three reviews per 24 hours. Request `/ai-review` only after the branch settles,
answer incorrect findings in comments instead of changing working code, and use
`/ready-for-reviewer` only after checks pass and the current head has been reviewed.

# Architecture (threading model)

Single `.wh.cpp` injected into `explorer.exe`. Execution domains:

- **Fetch thread** (`FetchThreadProc`): usage HTTP, JSON parsing, retry/backoff, token refresh,
  and owns the tray notify icon. It must NEVER touch XAML.
- **Login thread** (`LoginThreadProc`, one at a time, guarded by `g_loginInProgress`): runs an
  OAuth sign-in - shows the sign-in window (`g_loginWnd`, own message loop) offering the authorize
  link to copy or open; never launch a browser unprompted. Anthropic pastes the code there; the
  OpenAI loopback listener (`g_loginSocket`) notifies that window via `WSAAsyncSelect`. It does
  network + token storage but must NEVER touch XAML. Spawned from a taskbar UI thread by
  `StartLogin`.
- **Settings thread** (`SettingsWindowThreadProc`, one at a time): owns the native Win32 settings
  window and account editor. It persists autosaved configuration to mod LocalStorage and requests
  taskbar UI rebuilds, but must NEVER touch XAML directly.
- **History thread**: owns the native history window, graph caches, and export/confirmation
  dialogs. No XAML. Cancel tracked modal dialogs and join this worker before unloading.
- **Taskbar UI threads**: all XAML lives here. One `QuotaUiInstance` per taskbar window in
  `g_uiInstances`.

Cross-thread UI work is marshaled with `RunFromWindowThread` (a `WH_CALLWNDPROC` hook +
`SendMessageTimeout`). Resolve XAML refs only on the target UI thread: `PostUiUpdate` marshals
first, then reads/updates XAML. Never resolve XAML refs on the fetch or login thread.

# Concurrency rules

- Lock order is `g_configEditMutex` before `g_authEpochMutex` before `g_settingsMutex` before
  `g_dataMutex`. Never take them in the reverse order. Release `g_configEditMutex` before
  marshaling to taskbar UI threads or showing user-paced dialogs.
  `g_authMutex` is a leaf used only by token-store operations, including raw rename copies; never
  acquire another lock while holding it. Hold `g_authEpochMutex` only for short local auth/settings
  transactions, never across network I/O, dialogs, notifications, UI marshaling, or
  `FinishSettingsApply`.
- `g_settingsGeneration` gates published data; `g_refreshGeneration` drives manual single-account
  refresh. Code that publishes to `g_data` must re-check the generation under lock (existing
  pattern in the publish block).
- In-flight WinHTTP handles are tracked (`TrackHttpHandle`) so `Wh_ModUninit` can cancel them via
  `CloseActiveHttpHandles`. New network code must follow this or unload can hang.
- A sign-in blocks on the sign-in window's message loop with no timeout. Unload unblocks it by
  posting `WM_CLOSE` to `g_loginWnd` and closing `g_loginSocket`, then joins
  `g_loginThread`. Keep this path intact when changing the login flow.
- Preserve the teardown lifetime barrier: join every worker that can issue finite marshals before
  `RemoveAllQuotaGrids(true)`. A finite marshal may discard only an unclaimed payload; once the
  target claims it, wait for completion. Never add a timeout that lets the DLL unload while a
  worker, hook, subclass, or XAML callback can still execute mod code.
- Check `g_unloading` in loops and before slow/marshaled work.
- `g_historyMutex` is an independent leaf: never hold settings/auth/UI locks across journal
  operations, or acquire them from inside history operations. Clear invalidates in-flight batches
  with `g_historyEpoch`; settings publication updates the history admission generation.

# Stability invariants (runs inside explorer.exe)

A crash takes down the shell. Keep it defensive:

- Wrap WinRT/XAML calls in `try/catch` (existing pattern) and bail when `g_unloading`.
- A global that transitively owns WinRT/XAML references must use the `g_uiInstances` pattern:
  `[[clang::no_destroy]]`, nullable storage, owner-thread cleanup, and explicit reset during
  controlled unload. `Wh_ModUninit` isn't guaranteed to run during process shutdown, so a normal
  global destructor must never release XAML.
- Unload must stay clean: set `g_unloading`, signal `g_stopEvent`, close+join history and settings,
  unblock+join the login thread, join the fetch/retry threads, remove injected UI, delete the tray
  icon, `WSACleanup`.

# Symbol hooks are version-fragile

The mod hooks private `taskbar.dll` symbols and byte-pattern-scans `TaskbarHost::FrameHeight` for
an offset. These can break on Windows updates. If bars stop showing, suspect these first
(`HookTaskbarDllSymbols`, `TryGetTaskbarElementAbi`).

# Security invariant

The mod owns its OAuth credentials: it signs in (PKCE, using the public Claude Code / Codex
clients), stores access+refresh tokens, and refreshes/rotates them itself. Preserve these rules
when extending auth handling:

- Never read or write the OpenCode/Claude Code/Codex credential files. The mod's tokens live only
  in its own Windhawk storage (`auth_<identityHash>`), DPAPI-encrypted (current user) via
  `DpapiProtect`/`DpapiUnprotect` - never persist tokens in plaintext.
- Refresh tokens go only to the provider token endpoints; never send one as a bearer token to a
  quota endpoint.
- The token store is keyed by `AccountIdentityHash` (provider+label). Keep that stable or sign-ins
  get orphaned.
- Usage history belongs in per-user LocalAppData, not Windhawk's machine-shared writable storage.
  Recording consent is a separate HKCU setting, never a field in machine-shared `settings_v1`.
  Keep the journal size/read bounds, checksum validation, atomic compaction, and exclusive writer
  handle. Only the shell process records; pending history IDs must be saved before recording.
  History IDs are separate from auth identities and follow configured entries, not verified logins.
  Recording/storage/export behavior and verification live in README > Usage history.

# Conventions

- Bump `@version` in the mod header on user-facing changes.
- The native dialog is intentionally the sole settings surface; don't restore or split out a
  declarative Windhawk settings block unless explicitly requested. Settings are stored as
  versioned JSON in `settings_v1`; Windhawk settings are legacy import data only. Preserve
  `LoadLegacySettings`: migration relies on `Wh_Get*Setting` reading persisted legacy keys without
  a current settings block.
- A new setting must be kept in sync in `Settings`, JSON serialization/deserialization,
  normalization, the native controls, and the README list.
- Wide strings throughout; `swprintf`'s `%s` is wide here.

# Testing

Run `check-mod.ps1` for syntax/metadata and `python tests/history/run.py` for isolated history
regressions and native-control probes (`--capture` saves rendered images). The suite uses bundled
clang, not `msbuild`/`vstest.console`; its owning guide is [tests/history/AGENTS.md](tests/history/AGENTS.md).
A full integration check still requires Windhawk loading the mod into `explorer.exe`.

When changed native-control behavior is hidden by stubs, verify the assumed API/message contract
with an isolated real-control probe under relevant, verified host contexts. Report caller-logic,
native-contract, and Explorer integration results separately; a stub pass does not verify native behavior.

For changes to persisted settings or identity keys, runtime-test an upgrade from the current
catalog version with non-default accounts, visibility, configuration, and existing sign-ins.
