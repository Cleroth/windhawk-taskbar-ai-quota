# Mod source rules

Rules for code under `src/`. Generation, checks, testing and publishing: the root `AGENTS.md`.

## Source map

Each file opens with a comment naming what it holds.

- `core/`: the shared model, settings and runtime state, JSON, crypto (DPAPI), the UI language,
  this Windows user's storage (`core/user-storage.h`), account errors, forward declarations.
- `net/`: WinHTTP with handle tracking. `providers/`: each provider's usage requests and parsing.
- `auth/`: OAuth clients, the token store, the sign-in thread and window.
- `fetch/`: the polling loop, threshold notifications, provider status pages.
- `taskbar/`: XAML injection and placement, quota grids and tiles, the meter model, the details
  flyout, logos, marshaling.
- `settings/`: the settings window (pages, edit controller, store, account commands, preview).
- `history/`: the journal, series model, burn-up curves and the usage history window.
- `ui/`: the custom-drawn window toolkit ([ui/AGENTS.md](ui/AGENTS.md)); `i18n/`: localization
  ([i18n/AGENTS.md](i18n/AGENTS.md)).

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

- **Fetch thread** (`FetchThreadProc`): usage HTTP, JSON parsing, retry/backoff and token refresh;
  after each round, the providers' public status pages (`PollProviderStatuses`: its own cadence,
  unauthenticated, rereading the setting and visibility before each request). It owns the
  notifications: their HKCU app identity (a volatile key, registered once per fetch thread,
  removed while they are off and at unload) and the fallback tray icon. NEVER XAML. It runs
  rounds only while `IsTaskbarOwnerProcess()`; other Explorer processes never poll, refresh tokens
  or notify. Once it owns the taskbar, it first moves what older versions kept in HKCU and
  LocalAppData (`MigrateUserPreferences`, `PrepareHistoryStorage`), recording or not.
- **Login thread** (`LoginThreadProc`, one at a time, guarded by `g_loginInProgress`; started from
  a taskbar UI thread by `StartLogin`): network and token storage only; NEVER XAML. It runs each
  sign-in in the sign-in window (`g_loginWnd`, own message loop), which offers the authorize link
  to copy or open; never launch a browser unprompted.
  - The Anthropic (any free port) and OpenAI (1455/1457) loopback listener (`g_loginSocket`)
    notifies the window via `WSAAsyncSelect`. Anthropic's "Use a code" closes it for a second
    window, with the paste redirect, that takes the pasted code#state; OpenCode Go's window takes
    its API key (masked).
  - GitHub Copilot's device flow shows its code in the window and polls the token endpoint from
    the message loop between pumps, never from the window procedure, discarding a result once the
    window closed; the loop also sets the status line. A sign-in that ends without a token keeps
    the window open, not polling, until closed.
  - The window is laid out by its text's measure (`auth/login-controls.h`; the pure layout in
    `auth/login-layout.h`), all built before the window: neither the layout nor the loop
    allocates.
- **Settings thread** (`SettingsWindowThreadProc`, one at a time): owns the custom-drawn settings
  window (`settings/ui-window.h`, a `ui/` toolkit client), persists autosaved configuration to mod
  LocalStorage and requests taskbar UI rebuilds; it NEVER touches XAML directly. The window writes
  settings only through the edit controller (`settings/edits.h`: drafts, revisioned commit queue,
  navigation and close flushes) and its store (`settings/store.h`, one transaction per edit);
  notifications reload its baseline and never commit drafts. Account edits and actions run the
  UI-free account commands (`settings/account-commands.h`: the rename's sign-in move, removal,
  reorder, visibility).
- **History thread** (`HistoryUiThreadProc`, one at a time; `OpenHistoryWindow` starts it, or
  hands the open window its request): runs the custom-drawn usage history window
  (`history/ui-window.h`, a `ui/` toolkit client), whose controller (`history/controller.h`) owns
  all window state and reaches the journal, settings and live readings only through the ops in
  `history/ops.h`. `NotifySettingsWindowChanged` also reaches it: its refresh reads the
  journal only through the memory-only `PeekHistorySnapshot`; only its explicit loads may
  initialize or retry storage. Its dialogs (message boxes, the save dialog and its overwrite
  prompt) run in the scope of `history/modal.h` (the close latch, then unload, ends them, nested
  and late ones included); a Yes/No prompt ignores Cancel, so the scope answers No there. NEVER
  XAML. Before unloading, cancel its tracked modal dialogs and join it. Its CSV export writes on
  a worker the ops own, reading only its job (a frozen snapshot, the destination, a cancel flag)
  and the unload flags, never posting to the window (a timer polls it); the ops' destruction
  cancels and joins it on every exit of the history thread, so that join covers it.
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
- `g_providerStatusMutex` (`fetch/provider-status.h`) is a leaf guarding what each provider's
  status shows: copy in or out holding no other lock. Status reads take `g_settingsMutex` and
  `g_refreshMutex` only while holding nothing.
- `g_placementMutex` (`taskbar/placement.h`) is a leaf guarding the placement records. Only a
  record's owner taskbar thread changes it or uses its weak reference; the settings thread reads
  plain fields only.
- Hold `g_authEpochMutex` only for short local auth/settings transactions, never across network
  I/O, dialogs, notifications, UI marshaling or `FinishSettingsApply`.
- `UserPreferencesLock` (`core/user-storage.h`, a named mutex per Windows user) is a leaf
  serializing consent and language writes and their migration across processes and sessions:
  acquire nothing while holding it. It waits up to 2 s; a timeout fails the write.
- `g_historyMutex` is an independent leaf: never hold settings/auth/UI locks across journal
  operations, or acquire them inside history operations. Clear invalidates in-flight batches via
  `g_historyEpoch`; settings publication updates the history admission generation and the
  retention journal operations read once loaded (`g_historyRetentionDays`), never a caller's copy.
- Burn-up curves (`history/burn-up-store.h`): only the fetch thread publishes; settings
  publication and Clear history reset it via `ResetBurnUpCurvesLocked` (under `g_dataMutex`).
  The fetch thread caches per-account journal tails and the history window keeps a snapshot by
  the journal's stamp, so any change to `g_historySamples` other than `AppendHistory` must bump
  `g_historyEpoch` (Clear) or `g_historyRevision` (load, cap eviction, retention pruning,
  Antigravity thinning, an account's deletion). An account's deletion also notifies the windows
  (`NotifySettingsWindowChanged`).
- `g_settingsGeneration` gates published data. Code that publishes to `g_data` must re-check the
  generation under lock (see the existing publish block).
- Manual refreshes queue in `g_refreshRequests` (under the leaf `g_refreshMutex`): one entry per
  identity (0: every account), stamped with `g_refreshGeneration`. A round removes only the
  entries it started with; never replace or drop another account's request.
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
  threads (e.g. taskbar restart) only request removal/injection from the retry worker (and wake
  the fetch thread); they never marshal themselves. The taskbar edge watch (`WatchTaskbarEdge`,
  owner-thread XAML handlers) may only give back the room beside Start, close the flyout and
  collapse the bars before requesting a rebuild the same way.
- Check `g_unloading` in loops and before slow/marshaled work.
- The current UI language (`core/language.h`: `g_language`, an immutable registry entry) is read
  lock-free by any thread and written only under `g_configEditMutex` (and at init). Native code
  snapshots one `I18nLocale` per frame, notification or dialog (`CurrentLocale`, with the user's
  regional number format read anew); text built from it is cached under that locale. A
  language-only edit stores the preference and switches it, publishing nothing else; callers
  then rebuild the taskbar (only when the shown language changed) without waking polling and
  notify the windows. A regional format change (`WM_SETTINGCHANGE` "intl") updates the taskbar
  and recomposes the open windows.
- A taskbar grid keeps the language it was built in (`QuotaUiInstance::buildLocale`) for its
  updates, menus and flyouts; a switch rebuilds it. Each update refreshes its regional number
  format: no width measured at build holds a separator. `SetXamlLanguage` gives a non-English
  language to our XAML roots (the grid, the flyout card, each menu item, localized tooltips
  through `SetXamlToolTip`) and to detached measurement probes before they measure; English
  keeps Explorer's inherited language.

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
  `HookTaskbarDllSymbols` and `TryGetTaskbarElementAbi` first. Positions depend on taskbar element
  names (tray anchors, `TaskbarFrame`/`RootGrid`/`TaskbarFrameRepeater`, Start's automation ID).
- Windows' native taskbar position setting docks a taskbar at any edge: RootGrid's
  `DockingStates` names it, and a move re-lays out the same tree (no `TrayUI::StartTaskbar`), so
  the edge watch rebuilds. Only that state picks the side layout, never the window's shape: a
  taskbar another mod rotates stays laid out across. A side layout needs a StackPanel tray; a
  grid tray there keeps a watch-only state (no grid) so moving back rebuilds.
- Beside Start the mod writes native margins (the repeater's left, Start's right). Go through the
  placement records: add or give back only our own contribution, never touch a margin another mod
  took, and write only from owner-thread code that teardown revokes (`LayoutUpdated`, injection,
  removal), never from deferred dispatcher work that could run after unload. Revoke callbacks
  before giving the margin back.

## Credentials and history storage

The mod owns its OAuth credentials: it signs in (PKCE with the public Claude Code / Codex clients;
GitHub's device flow with copilot.vim's public client), stores access+refresh tokens, and
refreshes/rotates them itself. OpenCode Go has no OAuth: the user pastes an API key, stored as an
access token without refresh or expiry and sent only to its usage endpoint.

- Never read or write the OpenCode/Claude Code/Codex credential files, or any other app's GitHub
  or Copilot credentials (Visual Studio, GitHub CLI, Credential Manager). Tokens live only in the
  mod's Windhawk storage (`auth_<identityHash>`), DPAPI-encrypted for the current user via
  `DpapiProtect`/`DpapiUnprotect`; never persist them in plaintext.
- Don't revisit borrowing other apps' sessions without new evidence: they go stale while the app
  is idle (Claude Code 8 h, Codex 10 days; gh can opt into expiring tokens), single-use refresh
  tokens mean renewing one signs the app out (launching it to renew races its refresh), a borrowed
  account follows the app's current login (one per source), and gh tokens carry repo/workflow
  scopes. Cut setup friction in the mod's own sign-in instead.
- Refresh tokens go only to provider token endpoints, never as a bearer token to a quota endpoint.
- The token store is keyed by `AccountIdentityHash` (provider+label). Keep it stable or sign-ins
  get orphaned.
- Details read through a sign-in (Anthropic plan, Codex free resets' expiry) are cached under its
  sign-in generation. Any new path that replaces, deletes or moves a sign-in must call
  `BumpSignInGenerationLocked` in the same `g_authEpochMutex` section, after the token write.
- A poll's result belongs to the sign-in generation `FetchAccount` read (and returns) with its
  token, which it reads only for a configured identity. If that changed, the fetch thread polls
  once more; publication (under `g_authEpochMutex`) drops a polled result whose generation changed,
  with its history sample and notifications; deadlines set under an earlier generation don't apply.
- Auth epochs make saves of an earlier sign-in's token stale: the same section must also
  `BumpAuthEpochLocked` the identity whose sign-in it replaces or deletes (a rename: the source,
  plus a redirect). Save tokens only through `SaveStoredTokenIfCurrent`: a refresh with the epoch
  read in one `g_authEpochMutex` section with the token it refreshes, a sign-in with the epoch read
  when it started.
- Per-user data lives in the mod's Windhawk storage, keyed by the user's SID
  (`core/user-storage.h`), so Windhawk removes it with the mod: the journal, its lock and the
  notifications' icon in `UserStorageFolder()` (`<Wh_GetModStoragePath>\<SID>`); recording consent
  and the language preference as `recordHistory_<SID>` and `language_<SID>`
  (`Settings::historyEnabled`, `Settings::language`), never fields in machine-shared `settings_v1`.
- Windhawk's storage is writable by every user. Refuse a storage root or user folder that is a
  reparse point; open every handle written through there with `OpenUserStorageFile` (no reparse
  point followed, plain file with one link) and write replacements to `CreateUserStagingFile`'s
  unique `CREATE_NEW` names, then move them into place. It's a mitigation, not a guarantee.
- Preference readers fall back to the HKCU values older versions wrote and never write; writes go
  only to Windhawk storage, under `UserPreferencesLock`. Only the shell's fetch thread migrates
  the HKCU values and the old LocalAppData journal (`MoveLegacyHistoryLocked`: copied under the
  old lock, provenance recorded, an old journal deleted only when it matches the provenance or the
  new journal, never merged).
- Keep the journal's size cap (maintenance and appends apply it with a number of days; kept
  forever, nothing may prune or cap it but Antigravity thinning, `history/thinning.h`), checksum
  validation, atomic compaction and exclusive writer handle. Taking that handle is separate from
  loading samples: nothing may treat a journal owned but not loaded as empty. Loads read the whole
  file whatever the retention; one that stops part-way (out of memory, unload) leaves the journal
  unwritable until Clear. Only the shell process records; save pending history IDs before
  recording. History IDs are separate from auth identities and follow configured entries, not
  verified logins.
- User-facing recording/storage/export behavior: [docs/usage-history.md](../docs/usage-history.md);
  its manual runtime checks: [docs/usage-history-verification.md](../docs/usage-history-verification.md).

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
  [settings reference](../docs/settings.md). A per-user one (consent, language) skips JSON:
  deserialization keeps the caller's value, `LoadSettings` reads it in every branch and
  `ApplyOwnedSettings` writes it.
- Wide strings throughout; `swprintf`'s `%s` is wide here.
- UI text: wrap it for localization and update every catalog in the same change; before adding
  or changing UI text, read [i18n/AGENTS.md](i18n/AGENTS.md).
