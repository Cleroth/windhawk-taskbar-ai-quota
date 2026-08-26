# Building

This is a single-file Windhawk mod (`local@taskbar-ai-quota.wh.cpp`). There is no
`.sln`/`.vcxproj`. Do NOT use `msbuild`. Windhawk compiles the `.wh.cpp` itself with its
bundled clang when the mod is loaded.

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

## Install / runtime test

`pwsh install-windhawk.ps1` copies the mod into `C:\ProgramData\Windhawk\ModsSource`. A full
runtime test requires Windhawk loading the mod into `explorer.exe`; it can't be tested from a
plain compile.

# Publishing to the windhawk-mods catalog

Mods are published via PR to `ramensoftware/windhawk-mods`. Rules (their README):

- PR must change **exactly one file**: `mods/<mod-id>.wh.cpp`. Here `<mod-id>` is `taskbar-ai-quota`
  (the `@id`), so the file is `mods/taskbar-ai-quota.wh.cpp`. The local `local@` filename prefix is
  a Windhawk local-dev convention and is **dropped** in the catalog; `@id` itself never has it.
- `@github` is **required** and must be the PR author's profile URL: `https://github.com/Cleroth`.
- No `@license` => MIT by default; we set `@license MIT` explicitly.
- New mod: any `@version` is fine. Update: bump `@version` and put the version note in the commit
  message (it becomes the catalog changelog). You can only update mods whose `@github` is yours.

The mod's Changelog tab in the Windhawk app/catalog is generated entirely from the **update PR's
commit message** — there is NO `@changelog` metadata field and NO changelog section in the
`.wh.cpp`. So the only way to add a changelog entry is the commit message of the update PR (the
`version` must change). Confirmed by the catalog README ("Submitting a Mod Update") and by the
reference mod `taskbar-clock-customization.wh.cpp`, which carries no in-file changelog.

This repo's `local@taskbar-ai-quota.wh.cpp` is the source of truth; the catalog file is a copy with
the `local@` filename prefix stripped. Keep the metadata (incl. `@github`/`@license`) in sync here.

## Workflow (gh, authed as Cleroth)

```pwsh
# pre-flight: clang -fsyntax-only check above must exit 0 first
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

# Architecture (threading model)

Single `.wh.cpp` injected into `explorer.exe`. Execution domains:

- **Fetch thread** (`FetchThreadProc`): usage HTTP, JSON parsing, retry/backoff, token refresh,
  and owns the tray notify icon. It must NEVER touch XAML.
- **Login thread** (`LoginThreadProc`, one at a time, guarded by `g_loginInProgress`): runs an
  OAuth sign-in - opens the browser, then either shows the Anthropic paste window (`g_loginWnd`,
  with its own message loop) or runs the OpenAI loopback listener (`g_loginSocket`). It does
  network + token storage but must NEVER touch XAML. Spawned from a taskbar UI thread by
  `StartLogin`.
- **Settings thread** (`SettingsWindowThreadProc`, one at a time): owns the native Win32 settings
  window and account editor. It persists autosaved configuration to mod LocalStorage and requests
  taskbar UI rebuilds, but must NEVER touch XAML directly.
- **Taskbar UI threads**: all XAML lives here. One `QuotaUiInstance` per taskbar window in
  `g_uiInstances`.

Cross-thread UI work is marshaled with `RunFromWindowThread` (a `WH_CALLWNDPROC` hook +
`SendMessageTimeout`). Resolve XAML refs only on the target UI thread: `PostUiUpdate` marshals
first, then reads/updates XAML. Never resolve XAML refs on the fetch or login thread.

# Concurrency rules

- Lock order is `g_configEditMutex` before `g_settingsMutex` before `g_dataMutex`. Never take them
  in the reverse order. Release `g_configEditMutex` before marshaling to taskbar UI threads or
  showing user-paced dialogs.
  `g_authMutex` (token store) is independent and only held inside `LoadStoredToken`/
  `SaveStoredToken`/`ClearStoredToken`; don't call other locked code while holding it.
- `g_settingsGeneration` gates published data; `g_refreshGeneration` drives manual single-account
  refresh. Code that publishes to `g_data` must re-check the generation under lock (existing
  pattern in the publish block).
- In-flight WinHTTP handles are tracked (`TrackHttpHandle`) so `Wh_ModUninit` can cancel them via
  `CloseActiveHttpHandles`. New network code must follow this or unload can hang.
- A sign-in can block on a window message loop (Anthropic) or `select`/`accept` (OpenAI). Unload
  unblocks it by posting `WM_CLOSE` to `g_loginWnd` and closing `g_loginSocket`, then joins
  `g_loginThread`. Keep this path intact when changing the login flow.
- Check `g_unloading` in loops and before slow/marshaled work.

# Stability invariants (runs inside explorer.exe)

A crash takes down the shell. Keep it defensive:

- Wrap WinRT/XAML calls in `try/catch` (existing pattern) and bail when `g_unloading`.
- Unload must stay clean: set `g_unloading`, signal `g_stopEvent`, close+join the settings window,
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

# Conventions

- Bump `@version` in the mod header on user-facing changes.
- Settings are owned by the native dialog and stored as versioned JSON in `settings_v1`; Windhawk
  settings are legacy import data only. A new setting must be kept in sync in `Settings`, JSON
  serialization/deserialization, normalization, the native controls, and the README list.
- Wide strings throughout; `swprintf`'s `%s` is wide here.

# Testing

No automated tests. Verify with the `-fsyntax-only` clang check above, then a Windhawk runtime
load into `explorer.exe`. The global `msbuild`/`vstest.console` guidance does not apply to this
repo.
