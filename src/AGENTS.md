# Mod source rules

Rules for code under `src/`. Generation, checks, testing and publishing are in the root
`AGENTS.md`.

## Fragments

- `taskbar-ai-quota.cpp` is the entry: mod metadata, catalog readme, system includes, fragment
  includes and the `Wh_Mod*` callbacks. Fragments form one translation unit; the entry's
  include order is declaration order.
- Fragments contain no local includes. Every other file under `src/` except `AGENTS.md` must be
  included exactly once; the generator fails otherwise. Put forward declarations for calls into a
  later fragment in `core/forward.h`. Keep file-scope functions and globals `static`.
- Files are UTF-8 without BOM, LF, ending with exactly one newline.
- Keep the `@compilerOptions` line in sync with the libs you call (e.g. `Shell_NotifyIconW` needs
  `-lshell32`); syntax checks can't catch a missing lib.
- Runtime OS support doesn't guarantee that Windhawk's bundled MinGW headers and import libraries
  expose an API. Verify with the bundled toolchain before replacing dynamic `GetProcAddress`
  lookups/fallbacks with direct calls.

## Threading model

The mod is injected into `explorer.exe`. Execution domains:

- **Fetch thread** (`FetchThreadProc`): usage HTTP, JSON parsing, retry/backoff, token refresh;
  owns the tray notify icon. It must NEVER touch XAML.
- **Login thread** (`LoginThreadProc`, one at a time, guarded by `g_loginInProgress`; spawned from
  a taskbar UI thread by `StartLogin`): runs an OAuth sign-in in the sign-in window (`g_loginWnd`,
  own message loop), which offers the authorize link to copy or open; never launch a browser
  unprompted. Anthropic codes are pasted there; the OpenAI loopback listener (`g_loginSocket`)
  notifies that window via `WSAAsyncSelect`. Network and token storage only; NEVER XAML.
- **Settings thread** (`SettingsWindowThreadProc`, one at a time): owns the custom-drawn settings
  window (`settings/ui-window.h`, a client of the `ui/` toolkit host; toolkit rules:
  [ui/AGENTS.md](ui/AGENTS.md)), persists autosaved configuration to mod LocalStorage and
  requests taskbar UI rebuilds. It must NEVER touch XAML directly. The window writes settings
  only through the edit controller (`settings/edits.h`: drafts, revisioned commit queue,
  navigation and close flushes) and its store (`settings/store.h`, one transaction per edit);
  notifications reload its baseline and never commit drafts. Account edits and actions run the
  UI-free account commands (`settings/account-commands.h`: the rename's sign-in move, removal,
  reorder, visibility).
- **History thread** (`HistoryUiThreadProc`, one at a time; `OpenHistoryWindow` starts it, or
  hands the open window its request): runs the custom-drawn usage history window
  (`history/ui-window.h`, a client of the `ui/` toolkit host), whose controller
  (`history/controller.h`) owns all window state and reaches the journal, settings and live
  readings only through the stateless ops in `history/ops.h`. `NotifySettingsWindowChanged` also
  reaches it: its refresh reads the journal only through the memory-only `PeekHistorySnapshot`;
  only its explicit loads may initialize or retry storage. Its dialogs (message boxes, the save
  dialog and its overwrite prompt) run in the scope of `history/modal.h` (the close latch, then
  unload, ends them, nested and late ones included); a Yes/No prompt ignores Cancel, so the scope
  answers No there. No XAML. Cancel tracked modal dialogs and join it before unloading.
- **Link launch threads** (`OpenUrl`, one per browser launch, tracked under the leaf
  `g_urlLaunchMutex`): `ShellExecuteExW` in an STA with error dialogs suppressed. `Wh_ModUninit`
  joins them after the login thread, which can start them; a stuck launch delays unload.
- **Taskbar UI threads**: all XAML lives here; one `QuotaUiInstance` per taskbar window in
  `g_uiInstances`. Their mod code (XAML handlers, timers, the subclass proc, marshaled payloads)
  must never pump messages or wait on other threads: no `ShellExecute*`, message boxes or dialogs,
  synchronous cross-apartment COM, cross-thread `SendMessage` without `SMTO_BLOCK`, clipboard or
  joins. A nested pump runs marshaled teardown (including unload's) and XAML input inside the
  caller's frame; hand such work to a joined worker, as `OpenUrl` does.

Marshal cross-thread UI work with `RunFromWindowThread` (a `WH_CALLWNDPROC` hook +
`SendMessageTimeout`). Resolve XAML refs only on the target UI thread: `PostUiUpdate` marshals
first, then reads/updates XAML.

## Concurrency

- Lock order: `g_configEditMutex` before `g_authEpochMutex` before `g_settingsMutex` before
  `g_dataMutex`; never reverse it. Release `g_configEditMutex` before marshaling to taskbar UI
  threads or showing user-paced dialogs.
- `g_authMutex` is a leaf used only by token-store operations, including raw rename copies; never
  acquire another lock while holding it.
- Hold `g_authEpochMutex` only for short local auth/settings transactions, never across network
  I/O, dialogs, notifications, UI marshaling or `FinishSettingsApply`.
- `g_historyMutex` is an independent leaf: never hold settings/auth/UI locks across journal
  operations, or acquire them inside history operations. Clear invalidates in-flight batches via
  `g_historyEpoch`; settings publication updates the history admission generation.
- Burn-up curves (`history/burn-up-store.h`): only the fetch thread publishes; settings
  publication and Clear history reset it via `ResetBurnUpCurvesLocked` (under `g_dataMutex`).
  The fetch thread caches per-account journal tails and the history window keeps a snapshot by
  the journal's stamp, so any change to `g_historySamples` other than `AppendHistory` must bump
  `g_historyEpoch` (Clear) or `g_historyRevision` (load, cap eviction, retention pruning, an
  account's deletion). An account's deletion also notifies the windows
  (`NotifySettingsWindowChanged`).
- `g_settingsGeneration` gates published data; `g_refreshGeneration` drives manual
  single-account refresh. Code that publishes to `g_data` must re-check the generation under
  lock (see the existing publish block).
- Track in-flight WinHTTP handles with `TrackHttpHandle` so `Wh_ModUninit` can cancel them via
  `CloseActiveHttpHandles`; untracked network code can hang unload.
- A sign-in blocks on the sign-in window's message loop with no timeout. Unload unblocks it by
  posting `WM_CLOSE` to `g_loginWnd` and closing `g_loginSocket`, then joins `g_loginThread`.
  A new sign-in starts only after the previous login thread has exited. Keep this path intact
  when changing the login flow.
- Teardown lifetime barrier: join every worker that can issue finite marshals before
  `RemoveAllQuotaGrids(true)`. A finite marshal may discard only an unclaimed payload; once the
  target claims it, wait for completion. Never add a timeout that lets the DLL unload while a
  worker, hook, subclass or XAML callback can still execute mod code. Hook callbacks on Explorer
  threads (e.g. taskbar restart) only request removal/injection from the retry worker; they never
  marshal themselves.
- Check `g_unloading` in loops and before slow/marshaled work.

## Stability (a crash takes down the shell)

- Wrap WinRT/XAML calls in `try/catch` (existing pattern) and bail when `g_unloading`.
- A global that transitively owns WinRT/XAML references must follow the `g_uiInstances` pattern:
  `[[clang::no_destroy]]`, nullable storage, owner-thread cleanup and explicit reset during
  controlled unload. `Wh_ModUninit` isn't guaranteed to run at process shutdown, so a normal
  global destructor must never release XAML.
- Unload must stay clean: set `g_unloading`, signal `g_stopEvent`, close+join history and
  settings, unblock+join the login thread, join link launches, join the retry/fetch threads,
  remove injected UI, delete the tray icon, `WSACleanup`.
- Hooks on private `taskbar.dll` symbols and the byte-pattern scan for the
  `TaskbarHost::FrameHeight` offset can break on Windows updates. If bars stop showing, suspect
  `HookTaskbarDllSymbols` and `TryGetTaskbarElementAbi` first.

## Credentials and history storage

The mod owns its OAuth credentials: it signs in (PKCE, public Claude Code / Codex clients), stores
access+refresh tokens, and refreshes/rotates them itself.

- Never read or write the OpenCode/Claude Code/Codex credential files. Tokens live only in the
  mod's Windhawk storage (`auth_<identityHash>`), DPAPI-encrypted for the current user via
  `DpapiProtect`/`DpapiUnprotect`; never persist them in plaintext.
- Refresh tokens go only to provider token endpoints, never as a bearer token to a quota endpoint.
- The token store is keyed by `AccountIdentityHash` (provider+label). Keep it stable or sign-ins
  get orphaned.
- Usage history belongs in per-user LocalAppData, not Windhawk's machine-shared writable storage.
  Recording consent is a separate HKCU setting, never a field in machine-shared `settings_v1`.
- Keep the journal size/read bounds, checksum validation, atomic compaction and exclusive writer
  handle. A load that stops part-way (out of memory, unload) leaves the journal unwritable until
  Clear. Only the shell process records; save pending history IDs before recording. History IDs
  are separate from auth identities and follow configured entries, not verified logins.
- User-facing recording/storage/export behavior and its manual verification:
  [README > Usage history](../README.md#usage-history).

## Conventions

- Bump `@version` in the mod header on user-facing changes.
- The mod's settings window is intentionally the sole settings surface; don't restore or split
  out a declarative Windhawk settings block unless explicitly requested. Settings are versioned
  JSON in `settings_v1`; Windhawk settings are legacy import data only. Preserve
  `LoadLegacySettings`: migration relies on `Wh_Get*Setting` reading persisted legacy keys
  without a current settings block.
- Keep a new setting in sync across `Settings`, JSON serialization/deserialization,
  normalization, the settings window (its `SettingsField` value, assignment, range and page
  reset in `settings/edits.h`, its page builder) and the
  [README settings list](../README.md#settings).
- Wide strings throughout; `swprintf`'s `%s` is wide here.
