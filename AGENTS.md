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

# Architecture (threading model)

Single `.wh.cpp` injected into `explorer.exe`. Two execution domains:

- **Fetch thread** (`FetchThreadProc`): HTTP, JSON parsing, retry/backoff, and owns the tray
  notify icon. It must NEVER touch XAML.
- **Taskbar UI threads**: all XAML lives here. One `QuotaUiInstance` per taskbar window in
  `g_uiInstances`.

Cross-thread UI work is marshaled with `RunFromWindowThread` (a `WH_CALLWNDPROC` hook +
`SendMessageTimeout`). Resolve XAML refs only on the target UI thread: `PostUiUpdate` marshals
first, then reads/updates XAML. Never resolve XAML refs on the fetch thread.

# Concurrency rules

- Lock order is `g_settingsMutex` before `g_dataMutex`. Never take them in the reverse order.
- `g_settingsGeneration` gates published data; `g_refreshGeneration` drives manual single-account
  refresh. Code that publishes to `g_data` must re-check the generation under lock (existing
  pattern in the publish block).
- In-flight WinHTTP handles are tracked (`TrackHttpHandle`) so `Wh_ModUninit` can cancel them via
  `CloseActiveHttpHandles`. New network code must follow this or unload can hang.
- Check `g_unloading` in loops and before slow/marshaled work.

# Stability invariants (runs inside explorer.exe)

A crash takes down the shell. Keep it defensive:

- Wrap WinRT/XAML calls in `try/catch` (existing pattern) and bail when `g_unloading`.
- Unload must stay clean: set `g_unloading`, signal `g_stopEvent`, join threads, remove injected
  UI, delete the tray icon.

# Symbol hooks are version-fragile

The mod hooks private `taskbar.dll` symbols and byte-pattern-scans `TaskbarHost::FrameHeight` for
an offset. These can break on Windows updates. If bars stop showing, suspect these first
(`HookTaskbarDllSymbols`, `TryGetTaskbarElementAbi`).

# Security invariant

Credentials are read-only. Never write, refresh, or rotate tokens, and never send a refresh token
as a bearer token. Preserve this when extending auth handling.

# Conventions

- Bump `@version` in the mod header on user-facing changes.
- A new setting must be kept in sync in three places: the `==WindhawkModSettings==` block, the
  `Settings` struct + `LoadSettings`, and the README settings list.
- Wide strings throughout; `swprintf`'s `%s` is wide here.

# Testing

No automated tests. Verify with the `-fsyntax-only` clang check above, then a Windhawk runtime
load into `explorer.exe`. The global `msbuild`/`vstest.console` guidance does not apply to this
repo.
