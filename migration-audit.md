# Migration Audit: `5f064ad`

Status: not ready to ship. Static Windhawk clang syntax validation passes, but the following correctness issues remain.

## High

### [x] Sign-in-only installs migrate to a permanently empty configuration

Windhawk persists `Mods\<id>\Settings` registry values only when the user saves settings through the Windhawk settings UI; `Wh_GetStringSetting` never falls back to schema defaults (verified against ramensoftware/windhawk v1.7.3: `LoadedMod::GetStringSetting` reads `GetModConfig(..., L"Settings")`, absent values return nothing). Users who signed in on the default `anthropic/A` / `openai/O` accounts without touching Windhawk's settings therefore have no readable legacy settings values - only `hiddenAccounts` and `auth_*` in LocalStorage.

The import probe accepts only three keys - `accounts[0].provider`, `taskbarMonitorMode`, `barLength` - and treats an all-empty result as a fresh install: accounts reset to none and that empty `version:1` document is saved to `settings_v1`. Every later start takes the `Loaded` path and never consults the legacy channel again, so stored sign-ins and hide state become unreachable and the loss does not self-heal (bars are replaced by the AI+ tile). Users whose only legacy customization was hiding an account hit the same path. Not indistinguishable from genuinely fresh installs because default-identity tokens may exist. Fix direction: extend detection to `hiddenAccounts` and default-identity token presence, avoid persisting the outcome of a suspected missed import, and plan recovery for configurations already carrying an empty blob (customized installs that did save once import correctly; REG_DWORD values stringify via `RawItemToString`).

References: `local@taskbar-ai-quota.wh.cpp:5059-5062`, `5210-5217`, `1094-1142`.

### [x] Completed fetches can be discarded and polling suppressed

A settings-generation change can reject an in-flight fetch after its next poll deadline has already advanced. The deadline is preserved across the generation change, and a manual refresh is also cleared despite publishing no result. Old quota data can remain visible until the next configured poll, up to 1440 minutes.

Trigger: change any scalar setting, visibility, bar selection, account order, or reset settings while a fetch is in flight.

References: `local@taskbar-ai-quota.wh.cpp:2891-2917`, `2992-3014`, `3036-3054`.

## Medium

### [x] Legacy whitespace normalization changes account identity

Migration trims leading and trailing whitespace from labels. A legacy label such as `" Work "` becomes `"Work"`, but its token remains under the old identity hash. If `"Work"` already exists, duplicate normalization silently drops one account. The native editor cannot recreate the old spaced identity.

References: `local@taskbar-ai-quota.wh.cpp:411-418`, `4824-4846`, `5046-5152`.

### [x] Rename persistence and token movement are non-atomic

Same-provider rename saves and publishes the new account identity before moving its token. A concurrent fetch can publish a false signed-out state until the next poll. If Explorer terminates in this window, the new identity persists while the only token remains under the old identity.

References: `local@taskbar-ai-quota.wh.cpp:1164-1184`, `6462-6491`.

### [x] Token refresh racing a rename strands the rotated refresh token

`FetchAccount` captures the auth epoch before refreshing (2641-2642). A concurrent same-provider rename moves the blob to the new identity key and bumps the old key's epoch (`MoveStoredTokenForRename`, 1177-1184). When the refresh then succeeds, `SaveStoredTokenIfCurrent` rejects the save as stale and discards the just-rotated credentials (2658-2664 proactive, 2709-2719 reactive); the moved blob under the new identity still holds the pre-rotation refresh token, which providers with single-use rotation have already invalidated. The renamed column then fails its next refresh with "session expired" and requires a full re-sign-in, even though no termination occurred. Fix direction: let an epoch-stale post-rename save re-home onto the new key, or serialize renames against in-flight refreshes.

References: `local@taskbar-ai-quota.wh.cpp:2641-2664`, `2703-2719`, `1164-1185`.

### [x] Renaming onto a removed account's retained identity silently adopts stale credentials

Removing an account can leave its token stored when the user chooses to retain it. Duplicate validation runs against live rows only, so renaming a same-provider account onto that freed identity passes. `MoveStoredTokenForRename` refuses to overwrite the populated destination key and returns false, the caller suppresses its move-failure warning whenever the destination still holds a token, and the renamed column then authenticates with the retained sign-in - possibly a different account or a since-revoked grant. The renamed account's actual token stays stranded under the old identity key, and a reverse rename cannot recover it because the symmetric move also refuses to overwrite.

References: `local@taskbar-ai-quota.wh.cpp:1164-1185`, `6440-6491`, `6505-6563`.

### [x] Requested credential deletion can silently fail

Token clearing ignores `Wh_SetStringValue` failure. Account removal and provider-change flows can report success while retaining credentials the user requested deleted. Rename can also report success after copying the token even if clearing the source key fails.

References: `local@taskbar-ai-quota.wh.cpp:1139-1175`, `6485-6486`, `6553-6563`.

### [ ] Cross-thread settings activation can stall the taskbar

A taskbar thread synchronously calls `ShowWindow(SW_RESTORE)` on the settings-thread-owned window. At the same time, the settings thread can synchronously marshal quota UI removal to that taskbar thread. The timeout and fallback path can freeze the taskbar for several seconds.

References: `local@taskbar-ai-quota.wh.cpp:4592-4607`, `5194-5197`, `7249-7255`.

### [ ] Unload can destroy the settings window beneath the color picker

`ChooseColorW` runs a modal message pump on the settings thread. Unload posts `WM_CLOSE` to the owner settings window, whose handler immediately destroys it, then waits for the thread indefinitely. The common dialog can therefore continue with a destroyed owner and return into code holding dead window handles. Other blocking settings flows have explicit unload handling; the color picker does not.

References: `local@taskbar-ai-quota.wh.cpp:6980-6995`, `7124-7127`, `7373-7386`.

### [ ] User-paced settings dialogs can block unload indefinitely

Unload waits indefinitely for the settings thread after posting `WM_CLOSE` only to the main settings window. If that thread is inside a synchronous `MessageBoxW`, unload cannot complete until the user dismisses the dialog. This is not a lock deadlock, but it can stall mod update or removal indefinitely.

References: `local@taskbar-ai-quota.wh.cpp:6413-6425`, `6511-6525`, `6640-6644`, `6694-6698`, `7373-7386`.

### [ ] Specific monitor selection is not stable across topology changes

The persisted monitor number is a position in a newly enumerated, primary-first taskbar list rather than a stable display identity. Reordering, unplugging, or reconnecting displays can silently move quota bars to a different physical monitor. The one-based settings, combo data, and runtime filter are internally consistent; the problem is identity instability rather than an off-by-one error.

References: `local@taskbar-ai-quota.wh.cpp:3216-3273`, `3289-3294`, `5896-5918`.

### [ ] Slider keyboard input and window close commit synchronously

Spin-button edits defer commits to the 250 ms autosave timer, but slider line/page/track-end actions call `CommitScalarSettings` immediately on every key or click repeat, and `WM_CLOSE` commits before destroying the window. Any effective change tears down and re-injects the quota UI on every taskbar, so holding a slider arrow key or closing the window with a pending edit can stall repeatedly for the cross-thread marshal duration per event.

References: `local@taskbar-ai-quota.wh.cpp:6874-6879`, `6919-6925`, `7124-7127`.

## Low

### [ ] Empty-label legacy accounts lose hidden state

The legacy hidden-account hash is checked while the imported label is still empty. Normalization later changes it to the provider default (`A`, `O`, or `G`), so the old hash does not match and the account becomes visible.

References: `local@taskbar-ai-quota.wh.cpp:4834-4836`, `5074-5109`.

### [ ] Migration truncates legacy accounts after index 63

The parent version read accounts until the first missing provider. Migration stops after 64 entries and permanently persists the truncated list, orphaning later account configuration and tokens.

References: `local@taskbar-ai-quota.wh.cpp:5065-5091`.

### [ ] Unknown provider strings drop legacy accounts instead of defaulting

The legacy loader treated any unrecognized provider string as `anthropic`. Migration skips unrecognized providers instead, permanently dropping such accounts and orphaning their tokens. Only reachable through registry values outside the original dropdown choices.

References: `local@taskbar-ai-quota.wh.cpp:5066-5078`.

### [ ] Clamp ceilings tightened versus legacy values

Legacy clamps bounded only below: bar length `std::max(x, 10)` and margins/gaps `std::max(x, 0)` with no upper bounds. Normalization now caps bar length at 1000 and margins/gaps at 100 (4857-4863), and legacy import normalizes immediately (5151), so oversized legacy values are silently shrunk at migration time and the truncated values persist. Other clamps match the old ones exactly.

References: `local@taskbar-ai-quota.wh.cpp:4857-4863`, `5151`.

### [ ] Renames discard the column's cached quota data

`PublishSettings` preserves `g_data` only where account identities match, and a label rename always mints a new identity while the token move happens only afterwards. The rebuilt column renders empty/uninitialized for roughly one fetch cycle even though data existed. Self-heals via the fetch wake in `FinishSettingsApply`, but is visible on every rename.

References: `local@taskbar-ai-quota.wh.cpp:5156-5174`, `6473-6492`.

### [ ] Typed out-of-range numeric values map inconsistently

The up-down controls do not clamp manually typed text before `EN_KILLFOCUS`. Normalization interprets non-positive values as missing defaults instead of clamping them to the control minimum. For example, bar length `0` becomes `100`, while `1` becomes `10`; custom polling `0` becomes the 10-minute preset.

References: `local@taskbar-ai-quota.wh.cpp:4854-4859`, `5830-5835`, `5979-6046`.

### [ ] Bar-length slider and spinner disagree above 300 pixels

The bar-length row registers a spin range of 10-1000 but limits its companion slider to 10-300. Typed values above 300 persist correctly, then the first slider interaction silently snaps the stored value back into the slider range while the spinner keeps accepting 1000.

References: `local@taskbar-ai-quota.wh.cpp:5468-5476`, `6792-6793`.

### [ ] Trackbar mouse-wheel input defers the autosave commit

Mouse wheel over a slider sends `TB_THUMBPOSITION`, which syncs the companion spin control (6916-6918) but is absent from the commit list (6919-6924). The change persists only on the next unrelated commit trigger or window close instead of on the wheel gesture like every other trackbar input.

References: `local@taskbar-ai-quota.wh.cpp:6909-6925`.

### [ ] Account editor forgets the extra-usage selection across provider switches

`UpdateAccountEditorProvider` unchecks the Anthropic monthly extra-usage box whenever the provider combo leaves anthropic and never restores it when switching back (6100-6106), so an Anthropic account whose editor is opened, switched away, and switched back loses its bar selection unless re-checked before saving.

References: `local@taskbar-ai-quota.wh.cpp:6100-6106`.

### [ ] Duplicate-account rejection destroys the editor

The account editor accepts and closes before the parent validates provider/label uniqueness. A duplicate warning appears only after all form edits have been discarded.

References: `local@taskbar-ai-quota.wh.cpp:6190-6224`, `6374-6378`, `6455-6459`.

### [ ] Double-click bypasses disabled account editing during sign-in

The Edit button is disabled while a login is active, but list double-click invokes editing unconditionally. Bar-only edits can save through the disabled state; identity edits close the editor and are then rejected, losing the changes.

References: `local@taskbar-ai-quota.wh.cpp:5729-5731`, `6407-6410`, `6882-6886`.

### [ ] Double-click edits nest modal loops inside the ListView notification

The `NM_DBLCLK` handler invokes account editing directly. While the ListView's own double-click handling is still on the stack, this enters the editor's modal message loop and later rebuilds the list via `ListView_DeleteAllItems`; a posted refresh during the loop can destroy items concurrently with deferred notifications. Posting a self-directed edit command would sequence identically without re-entering the control.

References: `local@taskbar-ai-quota.wh.cpp:5771`, `6324-6337`, `6882-6887`, `7097-7099`.

### [ ] Reordering during refresh highlights the wrong account

The fetch thread resolves the requested stable identity against the new order, but the UI refresh indicator continues using the index captured before reordering. The correct account is fetched while another account appears to be refreshing.

References: `local@taskbar-ai-quota.wh.cpp:732-750`, `2944-2956`, `4092-4122`.

### [ ] Account growth can exceed the owned-settings storage limit

The native editor has no account-count limit, while `SaveOwnedSettings` rejects serialized JSON at 65,535 characters. Enough accounts make every later autosave fail until accounts are removed, leaving the controls unable to persist otherwise valid edits.

References: `local@taskbar-ai-quota.wh.cpp:5018-5024`, `6358-6390`.

### [ ] Spurious Missing read overwrites owned settings without backup

Per the Windhawk API contract, `Wh_GetStringValue` returns an empty string when the buffer is too small or on error - indistinguishable from the value not existing. A stored blob of 64Ki or more characters (or a transient storage read failure) therefore reports Missing, and `LoadSettings` runs legacy import and saves its result to `settings_v1`, destroying the real configuration with no `settings_v1_invalid` backup. Unreachable today because saves are capped below 65,535 characters, but any future cap raise plus downgrade triggers silent loss. The Invalid path handles this correctly by preserving and backing up.

References: `local@taskbar-ai-quota.wh.cpp:5033-5044`, `5018-5024`, `5207-5217`.

### [ ] Anthropic extra-usage bars never show pace ticks

The parser stores the extra-usage percentage and reset time but never sets `windowDurationMs`. Pace-tick visibility requires a positive duration, so the extra-usage tick is permanently hidden. This behavior predates `5f064ad`, but remains a correctness gap in the audited result.

References: `local@taskbar-ai-quota.wh.cpp:2007-2013`, `4195-4204`.

### [ ] Bar-selection-only changes can miss an injection request

Changing only an account's selected bars calls `PostUiUpdate` rather than the rebuild path. `PostUiUpdate` returns immediately while `g_uiInjected` is false, so an edit during startup or a rebuild window schedules neither repaint nor injection. The persisted selection appears only when another event eventually injects or updates the UI.

References: `local@taskbar-ai-quota.wh.cpp:2769-2780`, `6488-6492`.

### [ ] Settle budget exhaustion drops a requested rebuild

The retry thread consumes `g_rebuildQuotaUiBeforeInject` with an exchange before settling (4678-4681). During the 2 second settle every wakeup - including each drained posted message - costs one attempt against the fixed 600 budget (4678, 4684-4692); exhausting the budget before the settle deadline exits without removing the grids and with the flag already cleared. The later inject pass keeps surviving grids (4429-4434), so stale-layout bars persist until the next display or taskbar event. Requires more than 600 wakeups in 2 seconds (message storm), so unlikely; consume-before-act is the fragile pattern.

References: `local@taskbar-ai-quota.wh.cpp:4677-4696`, `4429-4434`.

### [ ] Removal marshal timeout leaves stale-settings bars

Settings applies remove grids via non-forced `RemoveAllQuotaGrids`, whose cross-thread marshals time out at 2 seconds and only log failure (4584-4627), while `InjectQuotaGrid` keeps any window whose grid survived (4429-4434). An apply issued while taskbar threads are busy can therefore leave bars rendered from the previous settings until a later event forces removal. The retry semantics predate this commit; autosave now fires applies far more often, raising exposure.

References: `local@taskbar-ai-quota.wh.cpp:5194-5197`, `4584-4627`, `4429-4434`.

### [ ] A stale sign-in request can leave settings controls temporarily disabled

`StartLoginByIdentity` claims `g_loginInProgress` before confirming that the selected identity still exists. The missing-account path clears the flag without notifying the settings window. If that window refreshes during the brief claimed interval, it can retain disabled account actions until another refresh occurs.

References: `local@taskbar-ai-quota.wh.cpp:1903-1934`, `5725-5753`.

### [ ] Legal layout values can exceed taskbar bounds

The injected column has no aggregate width cap. Long labels, many accounts, a 1,000-pixel bar length, and 100-pixel margins can crowd or clip neighboring tray content. Extreme stacked-layout thickness and gaps can likewise exceed the taskbar frame height. The exact visual failure depends on taskbar XAML arrange and clipping behavior.

References: `local@taskbar-ai-quota.wh.cpp:3541-3610`, `3632-3708`, `4148-4164`, `4495-4505`.

### [ ] UI update bursts are not coalesced

Each `PostUiUpdate` synchronously fans out a full update to every eligible taskbar. Bursts from fetch publication, visibility changes, and settings actions can run redundant XAML passes instead of collapsing into one latest-state update. The result is correct but can add taskbar-thread latency.

References: `local@taskbar-ai-quota.wh.cpp:2769-2780`, `3047-3088`.

### [ ] Late account-edit dialogs can lose their owner

Account editing rechecks the selected identity after its modal editor closes, but several later warning and error dialogs do not recheck whether the main settings window survived an unload-triggered `WM_CLOSE`. `MessageBoxW` tolerates a dead owner in practice, but the dialogs can become desktop-owned or appear with incorrect foreground behavior.

References: `local@taskbar-ai-quota.wh.cpp:6401-6484`, `7124-7127`.

### [ ] Stale settings-window handle survives destruction in the window state

`WM_DESTROY` deletes visuals and clears the global handle but leaves `state.hWnd` set. When unload posts `WM_CLOSE` while a modal dialog is pumping messages, callers resume after the dialog against a dead window. `CommitScalarSettings` guards with a control-existence check; trailing `RefreshSettingsControls` calls do not, and `ListView_GetNextItem` on the destroyed list returns 0 instead of -1, so "no selection" acts like row 0 selected - `UpdateAccountButtons` then decrypts the token store for an account the user never selected. Currently wasted work only, since every other operation no-ops on invalid windows.

References: `local@taskbar-ai-quota.wh.cpp:5703-5712`, `5729-5753`, `5979-5981`, `7128-7139`.

### [ ] Ordinary close can re-commit garbage during child destruction

`WM_CLOSE` commits scalar settings and then destroys the window (7124-7127). An `EN_KILLFOCUS` delivered while children are being destroyed re-enters `CommitScalarSettings` through the catch-all (7008-7013); the only staleness gate is control existence for one combo (5981), so later-created combos can already be gone while that one remains, and failed `SendDlgItemMessageW` calls return 0 - mapping label position to Hidden (6013-6016), bar layout to Stacked (5999), or poll preset index 0 (6031-6036) - and the wrong values overwrite the correct commit and autosave. Unload-triggered closes are protected by the `g_unloading` bail (5980); ordinary user closes are exposed. Whether edits notify during destruction is unproven; a closing flag set after the final commit is cheap hardening.

References: `local@taskbar-ai-quota.wh.cpp:5979-6046`, `7008-7013`, `7124-7127`.

### [ ] Visibility toggles touch XAML while holding configuration locks

The taskbar-thread toggle handler calls `toggle.IsChecked(...)` with `g_configEditMutex` held (and `g_settingsMutex` in the same region), contrary to the documented rule to release it before taskbar-UI work. Safe today only because the mod subscribes to `Click`; adding a `Checked`/`Unchecked` handler that takes either mutex would self-deadlock a shell thread on a non-recursive lock.

References: `local@taskbar-ai-quota.wh.cpp:793-875`.

### [ ] Persisted state includes dead or redundant writes

Both visibility-toggle paths still maintain `hiddenAccounts`, but only the one-time legacy importer reads it, so ongoing writes are dead data; each load failure also rewrites `settings_v1_invalid` with identical content. Harmless, but deletion would avoid implying the legacy storage channel is still live.

References: `local@taskbar-ai-quota.wh.cpp:851-868`, `5038-5041`, `6622-6631`.

## Compatibility Decision

### [ ] Decide whether migration must preserve all-disabled bar selections

Legacy settings can disable all three quota bars. Normalization silently enables the 5-hour bar, changing display and threshold-notification behavior. This matches the new editor invariant, but it is not a behavior-preserving migration.

References: `local@taskbar-ai-quota.wh.cpp:4838-4841`, `5087-5090`.

### [ ] Decide whether an unavailable specific monitor should fall back

Specific-monitor mode returns no taskbar windows when the saved positional target is unavailable. Bars remain absent after the retry series until a topology or settings event occurs; the settings UI explicitly shows the target as unavailable. Decide whether this should remain strict targeting or temporarily fall back to the primary monitor.

References: `local@taskbar-ai-quota.wh.cpp:3289-3294`, `4671-4733`, `5912-5917`.

## Confirmed Clean Areas

- Legacy scalar setting names and options are mapped.
- Canonical non-empty account labels preserve ordering, hidden state, bar selection, and token identity within the 64-account limit.
- Normalized settings round-trip through JSON serialization and deserialization.
- Removing Windhawk settings metadata does not prevent reading persisted legacy values.
- Missing-storage migration retries after a save failure; invalid versioned JSON is backed up.
