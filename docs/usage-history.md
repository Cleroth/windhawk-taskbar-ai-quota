# Usage history

Recording is on by default. Turn it off or back on with **Settings > History > Record usage
history**, or click **Enable recording** in the history window's disabled banner. The choice
applies to the current Windows user without changing per-account recording selections or
hidden-account rules. Account recording defaults on; turn off **Record history** on an account's
card in **Settings > Accounts** to exclude it. Recording starts with the next poll. The details
flyout's usage charts and the usage graph tiles use these recordings; without them a usage
graph shows only the current reading and its projection.

Projections run at the average rate over the last 24 hours; during a window's first day, for
5-hour windows, for monthly extra usage, without recordings, and when recorded usage dropped in
that span, at the window's average. A recording gap at the 24-hour mark starts the span at the
reading before the gap. The flyout names the rate when a projection reaches the limit before the
reset.

## Viewer

Use **Usage history** from a column's right-click menu or **Open usage history** in
**Settings > History** to open the native viewer.
It reads local samples, not provider APIs. **Refresh / F5** updates the displayed snapshot.
**Info...** explains line styles, statistics, recording rules, and export behavior. Recording
state and storage errors stay visible in the main window.

- **All accounts** overlays separately labeled quota lines on one graph; quotas are never
  summed or reduced to a maximum. The view selector offers **Short-term**, **Weekly**,
  **Extra usage**, and **All meters**, defaulting to **Weekly**. Ranges include 24 hours, 7 days,
  30 days, 90 days, and **All retained**; the window opens on **Settings > History > Default time
  span** (7 days by default).
  - **Short-term** includes regular short-window quotas and Antigravity Gemini and Claude/GPT
    short pools. Labels distinguish reported durations (1h, 3h, or 5h), unknown durations, and
    windows that vary within the displayed range.
  - **Weekly** includes regular weekly quotas and Antigravity Gemini, Gemini Argon and
    Claude/GPT weekly pools. Anthropic follows each account's taskbar weekly selections: normal, Fable, both,
    or neither. **Refresh / F5** picks up changed selections. Removed accounts retain both
    weeklies because they no longer have taskbar settings.
  - **Extra usage** plots Anthropic extra-usage budget used (%), GitHub Copilot AI credits
    used (%) and OpenCode Go's monthly used (%) (left, in the Remaining bar mode) on the left
    axis and OpenAI credits remaining on the right. OpenCode Go's monthly line is its plan's
    included monthly allowance, not OpenCode's separately billed Extra Usage. USD spending
    appears in details, not on this graph. GitHub Copilot records only
    this percentage, so its accounts appear in Extra usage and All meters only; its pooled
    credits and additional usage are not recorded. In the sidebar, a Copilot account shows its
    latest AI credits used (%) where other accounts show their weekly one.
- Selecting one account defaults to **All meters**, with the same three groups also available.
  All-account and single-account modes remember their view selections separately until the window
  closes. **All meters** includes weeklies hidden on the taskbar and plots Anthropic USD
  spending. Percentages, USD, and credits use separately labeled axes.
- A compact legend wraps above the graph; line names use the account label without its provider
  prefix. Hover an entry to emphasize its matching line.
  Hover the graph to identify all nearby or coincident lines by name, with recorded sample
  values and timestamps distinguished as current or previous. Details and per-line statistics
  stay below the graph.
- Unsupported meters and meters without readings in the displayed period(s) are omitted.
  Accounts with no readings get a compact status, with paused/recording-off status shown once
  per account rather than repeated per meter. An account with only USD spending still shows
  those details in Extra usage; it is not reported as having no data. Dollar details show the
  last and peak sampled spending, not total spending across the selected range.
- **Compare previous week** overlays the last seven days and the preceding seven days. It shifts by
  exactly 168 elapsed hours; local clock times can differ across daylight-saving changes.
- All accounts uses a color family per account, with tints distinguishing its quotas;
  single-account colors stay tied to each meter. Connected samples use rounded, DPI-scaled 3px
  primary and 2px scoped lines. Current lines are solid; dashes are reserved strictly for the
  previous week, not for distinguishing meters. Only isolated readings get dots (filled for
  current, hollow for previous). Genuine resets, missing or failed readings, recording gaps,
  restarts, and recording changes stay disconnected; idle OpenAI 0% readings with a rolling
  future reset timestamp stay connected. No event marks or ticks appear above or below the plot,
  including reset, missing, or gap marks.
- Charts show recorded peaks, coverage, and estimated time above the current
  red threshold. Near-limit estimates interpolate only connected samples, not gaps or time after
  the last sample. Short and weekly limits overlap and must not be added together.
- In the **Remaining** bar mode (**Settings > Display**), percentage lines, the percent axis,
  readouts and cards show quota left (100% minus used, below 0% past the limit, as Used mode
  goes above 100%): lines fall as usage rises, the red-threshold band sits at the bottom, and
  cards give the lowest share left and the estimated time at or below what the threshold leaves.
  The sidebar's current values stop at 0%, like the taskbar. Dollar spending and credits keep
  their recorded values, and the CSV always holds the recorded values.
- OpenAI credits are not dollars or a spending ledger; the user-set credits ceiling is not
  stored as historical usage. View selection does not change the journal or CSV contents.
- **Export CSV...** exports all meters for the selected account or all accounts in the selected
  range (both periods in comparison mode). Times are UTC; opaque IDs preserve account identity
  without exporting labels. The file is written in the background: the window keeps responding
  and shows the rows written so far, its other controls stay unavailable and refreshes wait until
  the export ends. An existing destination is replaced only after a successful write. Closing the
  window or unloading the mod cancels an export still writing and leaves the destination
  unchanged; one that already reached its final step can still complete.
- **Clear all history...** permanently removes every account's local samples after confirmation.
  It leaves settings and sign-ins untouched; future polls record again if recording remains on.
  To delete one account's samples, remove the account with **Also delete its usage history**
  checked. If they can't be deleted (damaged or inaccessible storage, or another Explorer
  session holding it), the account is still removed and you are told its history was kept.
- To delete an already removed account's samples, select it under **Removed** and click
  **Delete history...** in its banner. After confirmation every sample of that account goes, not
  only the selected range; other accounts keep theirs.

## Recording and storage

Recording reuses normal polls; opening the viewer does not increase polling. Missing/failed
polls, sleep, restarts, and recording/settings changes break graph continuity. No past usage is
backfilled. With **Do not poll hidden accounts** on, hidden accounts have no new samples. Turn
it off to poll hidden accounts only when both global and per-account recording are enabled.

History follows the **configured account entry**, not a verified provider login. Renaming keeps
its history. Use a new entry for a different provider or login, including an Antigravity login
changed in its app. Removing an entry retains anonymous history until expiry, Clear, or
**Delete history...** on its page in the viewer, unless you check **Also delete its usage
history** when removing it; re-adding it starts fresh.
Duplicate entries for the same login remain separate lines. Antigravity graphs track the
displayed limiting pools, which can change; they are not cumulative consumption meters.

Samples are stored in the mod's Windhawk storage, in a folder named after your Windows account's
SID: `<Windhawk data>\ModsWritable\mod-storage\<mod ID>\<SID>\usage-history-v1.bin` (in a
default install, `<Windhawk data>` is `C:\ProgramData\Windhawk\Engine`; **Settings > History**
shows the folder), beside `history.lock` and the notifications' icon. The journal contains
quota/amount readings, timestamps, provider and opaque IDs—not tokens, labels, prompts, or raw
responses. It is not encrypted, and the folder keeps the permissions of Windhawk's storage, which
gives every user and app on the PC full access: other users can read or change it. The mod
refuses a storage folder that is a link (a junction or symbolic link), and writes only to plain
files under fresh random names, moved into place; this limits what another user's files in the
shared folder can redirect, but is no protection against a hostile user of the PC. When Windhawk
removes the mod it deletes the mod's storage, history included, as far as it can (a file in use
may stay); use Clear first to be sure. Local and catalog mod IDs use separate histories.

The recording choice is stored per user in the mod's Windhawk storage as `recordHistory_<SID>`
(missing means on, `0` means off), and so is the interface language, as `language_<SID>`
(`auto`: Automatic); another Windows user's choice cannot change yours. Other settings, including
per-account selection and retention, follow Windhawk's existing machine-shared configuration.
Opening an empty viewer does not create files.

Earlier versions kept the journal in `%LOCALAPPDATA%\TaskbarAIQuota\<mod ID>\` and the two choices
in `HKCU\Software\Windhawk\TaskbarAIQuota\<mod ID>` (`RecordHistory`, `Language`), which every
Explorer still reads until they move. The taskbar's Explorer moves them when it starts, recording
or not:

- The choices: a value already in Windhawk storage wins; otherwise the old one is copied, then
  deleted. The emptied registry keys go too.
- The journal, if the new folder has none yet: copied whole into it (under the old folder's lock,
  so a running earlier version can't write meanwhile), then the old journal, its lock, the old
  icon and the emptied folders are deleted. If the old folder is in use or the copy fails, nothing
  old changes and the viewer reports the storage problem; recorded polls try again (a failed copy
  at most hourly), as do **Refresh** in the viewer and the next start, and recording waits until
  the move succeeds.
- If both folders hold a journal, the old one is deleted only when it is the one already moved
  or identical to the new one; any other is kept where it is, never merged and never deleted.
  While such a moved copy can't be deleted yet, **Clear all history...** and history deletion
  first retry it and, if it stays, fail as in use rather than leave a copy that could come back.

History is kept **forever** by default: nothing is pruned by age and there is no size limit, so
the journal keeps growing on disk (for example, six accounts polled every 10 minutes around the
clock add about 75 MB a year). Settings saved before this option keep their day count. With a number
of days, hourly maintenance prunes older samples while recording or when refreshing the viewer
(including with recording off); loading a journal also permits maintenance. Retention edits do
not bypass that hourly cadence. A **250,000-sample cap (about 60 MB)** then also evicts oldest records in
batches; many accounts or frequent polls can shorten effective retention. Leaving **Forever**
asks first, since both limits apply again. Only the taskbar-owning Explorer records, with
exclusive journal ownership to prevent competing writers. Storage failures appear in the viewer.

Explorer normally keeps only about the last 35 days of readings in memory, what the details
flyout's usage charts need; the journal file holds the rest. Memory also holds the range the open
viewer shows (**All retained**: the whole journal, besides the viewer's own copy) until it closes,
readings not saved yet while storage fails, and, while they run, what refilling an older chart
window from the file and maintenance need. The viewer reads older readings from the file only
when it loads (opening it, **Refresh / F5**, or a range or comparison change); its live updates
don't, so a span reaching further back (a longer retention, a clock set back) shows after
**Refresh**. Maintenance, thinning, **Clear all history...** and history deletion write the
journal anew from the file into a new file that replaces it: if any step fails, the previous
journal stays as it was. If the journal file is changed, shortened or removed outside the mod,
recording stops saving to it, and the viewer says so, until **Clear all history...**.

Antigravity is read every minute whatever the poll interval, so whatever the retention its
readings older than 15 days are thinned to about one every 10 minutes (at the first maintenance
after loading, then daily). Kept are the first and last of each account's older readings, failed
readings, and both readings around every gap, reset, failure and meter appearing or disappearing,
so lines still connect and break where they did. Thinned readings are exported with
`poll_interval_ms` 600000. Statistics over thinned periods (coverage at a range's edges, peaks,
time near the limit) are estimates from the readings kept and can differ from before thinning.

## Downgrading

- A version without history support can discard saved history IDs when settings are edited; old
  samples then appear as anonymous retained history.
- Versions before 2.4.0 don't know GitHub Copilot: they report a journal with Copilot samples as
  damaged (until Clear) and drop Copilot accounts from their settings.
- From 2.7.1 on, a journal with samples of a provider added by a later version still loads: that
  version drops those accounts from its settings, lists their history under **Removed** (provider
  unknown, meters under generic names) and exports them with provider `unknown`.
- Versions before 2.8.0 don't know OpenCode Go: 2.7.1 handles its accounts and history that way,
  earlier versions also report the journal as damaged (until Clear). A dropped account's key stays
  stored: after updating back, renaming a new OpenCode Go account to its old label takes it over.
- Versions before 2.6.0 ignore Antigravity's Argon weekly and its recorded history, and saving
  settings there drops its selection (an account with only Argon selected falls back to its short
  window), so it is on again after updating back.
- Versions before 2.12.0 notify only at red: with Yellow or Orange chosen they notify at red, and
  saving settings there drops the choice, so it is Red after updating back.
- Versions before 2.13.0 show the Custom usage palette as the default one, and saving settings
  there drops the custom colors.
- A version without **Forever** keeps 3650 days but applies its size cap, permanently discarding
  samples beyond the newest 250,000.
- Versions before 2.18.0 read the journal, recording choice and language from their old places
  (`%LOCALAPPDATA%`, HKCU). Once this version moved them, such a version starts with recording on
  and the language on Automatic, and records a new journal in the old folder. Updating again keeps
  that journal there as it is: never merged into the moved one, never deleted.

## Verification

Manual runtime checks: [usage history verification](usage-history-verification.md).
