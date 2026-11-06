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

## Upgrades

Before enabling recording on an upgrade, verify existing non-default accounts, visibility,
appearance and sign-ins.
