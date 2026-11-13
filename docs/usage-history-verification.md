# Usage history: manual runtime checks

Runtime checks of usage-history recording, the usage history window and retention in Explorer.
Expected behavior: [usage history](usage-history.md). The automated suites
(`tests/history`, `tests/settings`) don't cover Windhawk, Explorer, real storage or upgrades.

Prerequisites: the mod installed (`pwsh install-windhawk.ps1`) and loaded by Windhawk; at least
one signed-in account, several with retained readings for the view checks, and disposable
accounts for removal. Take a storage-audit snapshot first (root `AGENTS.md` > Storage audit).

Side effects: recording writes the user's real journal; CSV export writes files; **Clear all
history** permanently removes every account's samples, and removal with **Also delete its usage
history** or **Delete history...** removes that account's.

## Recording and persistence

- Enable history for one signed-in account, then refresh it or wait for its poll. Open Usage
  history and refresh: a recorded point appears. Two successful polls in the same uninterrupted
  window give a line and nonzero observed coverage.
- Hidden accounts pause under the default global restriction and resume only when it is off and
  recording is enabled.
- Test CSV export, persistence across a restart, and Clear; none changes sign-ins.
- Export a large range: the progress count rises while the window scrolls, repaints and resizes;
  other controls stay disabled until the notice. Close the window mid-export: it closes at once,
  no `*.quota-*.tmp` remains beside the destination and an existing destination is unchanged.
  Disable the mod mid-export: Explorer stays responsive and the unload completes. Export to a
  share that stalls mid-write, then close the window and disable the mod: both complete.
- Remove a disposable recorded account with **Also delete its usage history** checked, once with
  each kind of history window open: its samples leave the open window, CSV export and a restart;
  other accounts keep theirs.
- Remove one without the checkbox: it stays under **Removed**. Select it there and use **Delete
  history...**: it leaves the list (All accounts is selected), and its samples leave CSV export
  and a restart while other accounts keep theirs.

## Retention

- Upgrade with an existing day count: it stays selected; a fresh install and **Reset** show
  **Forever**.
- Kept forever: the header reads **Recording · forever**, **All retained** starts at the oldest
  sample, and after a restart and an hour of maintenance no sample is gone.
- Choosing a day count or **Custom** from **Forever** asks first (No keeps Forever); **Custom**
  then shows 3650 days.

## Memory (a journal spanning more than 35 days)

- After a restart, note Explorer's private bytes (Task Manager's details, Memory (private working
  set) or Process Explorer). Open the viewer on **All retained**: the oldest sample shows, and the
  number rises while it is open; close it: it falls back toward the earlier value (Windows' heap
  may keep part of it), and reopening on 7 days doesn't raise it again.
- The details flyout's charts still cover each current window whole (monthly windows included),
  also for an account whose last successful poll is older than 35 days.
- With the viewer open on **All retained**, lengthen **Keep history** (or set the clock back a
  day): the shown range keeps its start until **Refresh**, then reaches back.
- Remove a recorded account with **Also delete its usage history** while the viewer shows a
  range older than 35 days: its samples leave the open viewer without a refresh.
- End Explorer (Task Manager) during maintenance of a large journal (the first hour after a
  start): after the restart the journal loads whole, and no `*.staging` file stays beside it.

## Views (several accounts with retained readings)

- A newly opened viewer defaults to All accounts, Weekly and the Default time span (7 days after
  an upgrade or a fresh install). Another span chosen in Settings leaves the open viewer's range
  alone and applies to a newly opened one; Reset History restores 7 days.
- Short-term, Weekly, Extra usage and All meters list their quota groups with separate labels,
  including known and unknown short durations. All meters keeps percentages, USD and credits on
  separate axes. Quotas are never summed or collapsed to a maximum.
- Anthropic: enable normal weekly, Fable weekly, both, then neither on the taskbar, refreshing
  history after each. Weekly matches each account's selection in both account modes; All meters
  and CSV still include every recorded meter. Removed accounts keep both weeklies; other
  providers are unaffected.
- Antigravity with a Gemini Argon pool: Weekly and All meters show an Argon weekly line apart
  from Gemini weekly, and CSV exports it as `argon_weekly`.
- One account selected: All meters is the default and all three groups stay available. Views
  chosen in all-account and single-account modes are each restored when switching back.
- All accounts uses matching account color families in the graph and legend, with tinted scoped
  lines, 3px primary and 2px scoped lines. Single-account meter colors stay stable across groups.
- With several legend entries, narrow the window: the legend wraps above the graph, details stay
  below. Layout and hover targets hold after scrolling, resizing and DPI changes; hovering a
  legend entry emphasizes its line.
- Hovering nearby and coincident lines names all of them with recorded values and times; in week
  comparison, current and previous are distinct.
- Compare previous week switches to 7 days and restores the chosen range when unchecked. Check
  previous-week-only meters, solid current versus dashed previous lines, and filled/hollow
  isolated points; connected curves have no dots. Genuine resets, missing readings and gaps stay
  disconnected, with no event ticks above or below the plot; idle OpenAI 0% readings with a
  rolling future reset timestamp stay connected without hiding genuine resets.
- Unsupported or unread meters have no empty legend entries. Accounts without readings show a
  compact status, paused/recording-off only once per account; repeat with readings missing in
  one or both comparison periods.
- Extra usage: a left percentage axis for Anthropic budget used, a right credits axis for OpenAI
  balances, USD spending in details only. A USD-only Anthropic account shows spending details,
  not a no-data status, and its single-account All meters view keeps the dollar graph. No
  provider needs more than two axes in All meters.
- Switching groups changes neither recorded history nor CSV meters: exports of the same account
  scope and range from different groups hold the same meters.

## Anthropic extra usage (an account with a spending limit, recorded since before 2.21.0)

- The details flyout's extra usage row gives a reset at the next month's start (00:00 UTC on the
  1st, in local time) and charts this month from its first recorded reading on, those recorded
  before 2.21.0 included, with its pace line.
- **Settings > Display > Extra usage chart**: **Week** spans the weekly quota's window, its now
  line under the weekly chart's; **Off** shows the bar with its pace marker. Both keep the reset
  line.
- In the viewer, the extra usage line breaks at each month's start, not where 2.21.0 was
  installed. A CSV export has no reset for extra usage, before or after it.
- Across a month's start (or with the clock set past it): usage returns to 0, the reset moves a
  month on, and the chart starts over.

## Antigravity thinning

- With an Antigravity account recorded for more than 15 days, after a restart and its first
  maintenance: readings older than 15 days are about 10 minutes apart (CSV `poll_interval_ms`
  600000, the last older one and those beside gaps 60000), newer ones a minute apart; lines over
  the thinned period connect and break where they did before (compare a CSV export or a
  screenshot taken first), and a storage-audit compare reports the removed readings as thinning.
- Other providers' readings and the journal's other days are unchanged.

## Upgrades

Before enabling recording on an upgrade, verify existing non-default accounts, visibility,
appearance and sign-ins.

From a version before 2.18.0, with a recorded journal, recording turned off or a language chosen
(take a storage-audit snapshot first, and compare after each step):

- Migration: after the upgrade, the journal sits in the History page's folder (under Windhawk's
  `mod-storage`), with every sample; the old `%LOCALAPPDATA%\TaskbarAIQuota` folder and the
  `HKCU\Software\Windhawk\TaskbarAIQuota` key are gone; recording and the language are as they
  were; the Windhawk log names the move.
- Interrupted migration: with an older version still holding the old folder (another signed-in
  session running it), or Explorer ended during the copy, the old journal stays as it was, the
  viewer reports the storage problem, nothing records, and the next start (or **Refresh**)
  completes the move without losing or duplicating samples. No `*.staging` file stays behind.
- Concurrent preference migration: change the language or recording in Settings while Explorer
  restarts (or from a second session): the change holds, and neither value returns to its old
  one.
- Downgrade and upgrade again: the older version starts with recording on and Automatic and
  records a journal in the old folder; after updating again that journal stays there (logged),
  the moved journal keeps its samples, and nothing merges.
- Removal while loaded: remove the mod in Windhawk with Explorer running: Explorer stays up, and
  the mod's storage folder (journal, lock, icon) goes, unless a file was in use. The notifications'
  registry key is gone after unload, and also after a sign-out without unload (kill Explorer, then
  sign out and in).
