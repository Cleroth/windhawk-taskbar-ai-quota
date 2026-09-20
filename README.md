# Taskbar AI Quota Bars

Shows Anthropic Claude, OpenAI/Codex, and Google Antigravity AI agent and LLM subscription quota usage as compact bars on the Windows 11 taskbar.
Can show on the primary taskbar only, all taskbars, or one specific monitor.

![Quota tooltip](ss1.png)
![Compact taskbar bars](ss2.png)
![Threshold notification](ss3.png)

## What It Shows

Each visible account gets one compact taskbar column. Choose among the provider's supported bars: 5-hour, weekly, Anthropic Fable weekly, Anthropic monthly extra usage, and OpenAI prepaid credits measured against a max you set (the API reports only the balance). Antigravity maps available short-window and weekly Gemini pools to its bars; Claude/GPT pool limits appear in the tooltip. Selected bars auto-hide when the provider does not return that quota.

- stacked layout: horizontal bars stack vertically and fill left-to-right
- vertical layout: selected bars sit side-by-side and fill bottom-up

Hover for percentages, reset times, plan and provider details, errors, and retry countdowns. Click a column to refresh that account or open its provider dashboard, depending on settings and provider support. Right-click a column for Settings, Usage history, Refresh all, provider actions, and show/hide toggles. Hidden accounts are not polled by default, and the last visible account cannot be hidden. History recording can explicitly enable hidden-account polling without showing taskbar bars or firing threshold notifications for those accounts.

Bars can show compact quota labels (`5h`, `7d`, `Fa`, `Ex`, `Cr`) and percentage text with never, hover, or always visibility plus adaptive, left, center, or right alignment. They use configurable green/yellow/orange/red thresholds, with an optional colorblind palette. Optional pace ticks compare quota usage with elapsed time in each reset window and have caret, full-line, edge-notch, and dot styles with a configurable color. Stale errors can mark labels and tooltips with `!`.

It can also fire a Windows notification when an account first crosses the red threshold on a selected bar, so you don't have to keep glancing at the bars. The notification re-arms once usage drops back below the threshold.

## Setup

Install **Taskbar AI Quota Bars** from the Windhawk catalog. For local development, `install-windhawk.ps1` installs `local@taskbar-ai-quota.wh.cpp`. On a fresh install, click the **AI +** taskbar tile to open the native settings window and add an account. Existing Windhawk settings are imported once when upgrading. Settings autosave; most appearance and behavior edits do not immediately re-query providers, while new or identity-changed accounts and stale accounts made visible can fetch.

Configure a provider and label, then select at least one supported quota bar. Each provider-and-label pair must be unique. Sign in to Anthropic/OpenAI from the settings window or a quota column's right-click menu. Antigravity reads quota from its running app or CLI session.

## Signing In

The mod runs its own OAuth sign-in and refreshes the access token itself, so the bars keep working without re-running any CLI. A column that needs authentication shows "click to sign in" - just left-click it to start the flow. You can also right-click a column, open **Sign in**, and pick the account:

- **Anthropic**: a browser opens to claude.ai. After you approve, the page shows a code like `abc...#xyz...`; paste it into the prompt the mod shows.
- **OpenAI**: a browser opens to OpenAI's authorization page; the mod catches the redirect on `localhost:1455` (falling back to `1457`) automatically, so there's nothing to paste. If the Codex CLI is signing in at the same time the port may be busy - close it and retry.
- **Google Antigravity**: no separate mod sign-in is needed. Keep the signed-in Antigravity app or CLI running so the mod can query its local language server. Older IDE builds also need an open workspace.

Use **Sign out** in the same menu to delete an Anthropic/OpenAI stored token. Renaming a label preserves its stored sign-in when possible. Removing an Anthropic/OpenAI account asks whether to retain or delete its stored sign-in; removing an Antigravity account only confirms the removal.

Antigravity servers without pooled-quota support can show only their current limiting quota. Weekly stays unavailable and pace ticks are disabled for that fallback.

## Settings

Right-click any quota column and choose **Settings...**. Useful settings include:

- provider (Anthropic, OpenAI, or Google Antigravity) per account
- account labels
- account ordering and taskbar visibility
- per-account 5-hour, weekly, Anthropic Fable weekly, Anthropic monthly extra-usage, and OpenAI credits bar selection
- OpenAI credits max: the balance the credits bar treats as 100% (credits have no server-side ceiling)
- bar length, thickness, and layout
- bar mode: used (fills as quota is consumed) or remaining (fills with quota left and shows "X% remaining"); threshold colors always represent consumed quota
- pace ticks comparing quota usage with elapsed time (or quota remaining with time remaining), with caret, full-line, edge-notch, and dot styles and a configurable color
- label position: hidden, left, top, right, or bottom
- account-label and bar-text font sizes
- account, label, bar, and tray spacing
- compact bar labels (`5h`, `7d`, `Fa`, `Ex`, `Cr`), hidden by default
- percentage text: never show, show on hover (default), or always show, with adaptive, left, center, or right alignment
- amounts on extra/credits bars: show `$` spent or left (Anthropic extra usage) or credits (OpenAI) instead of a percentage, following the bar mode
- optional additional OpenAI rate limits in tooltips (Codex Spark, hidden model lanes such as `gpt-reserve`), hidden by default
- click action: refresh account or open provider dashboard (Antigravity always refreshes)
- cloud poll interval presets plus a custom interval (Antigravity polls its local server every minute)
- opt-in local history, retention in days (90 by default, configurable from 1 to 3650), and per-account recording
- do not poll hidden accounts (on by default; overrides per-account history recording)
- taskbar monitor mode: primary, all, or a detected display with its resolution
- color thresholds with palette-matched previews
- threshold notifications (toast when an account crosses the red threshold)
- colorblind palette
- stale-warning marker
- temporary threshold-spanning test accounts for previewing current taskbar visuals

Bar dimensions and font sizes use sliders with precise numeric spinner controls. Spacing, thresholds, and custom polling use spinners. Concise hover help explains polling, pace ticks, additional OpenAI rate limits, and stale warnings. Each non-account page can be reset independently without removing accounts or credentials.

## Usage history

Click **Enable recording** in the history window's disabled banner, or use
**Settings > Behavior > Enable local history**. This opts in the current Windows user without
changing per-account recording selections or hidden-account rules. Account recording defaults on;
change **Record history** in an account editor to exclude it. Recording starts with the next poll.
Use **Usage history...** from the taskbar context menu or Behavior page to open the native viewer.
It reads local samples, not provider APIs. **Refresh / F5** updates the displayed snapshot.
**Info...** explains line styles, statistics, recording rules, and export behavior without
cluttering the charts. Recording state and storage errors stay visible in the main window.

- **All accounts** overlays separately labeled quota lines on one graph; quotas are never
  summed or reduced to a maximum. The view selector offers **Short-term**, **Weekly**,
  **Extra usage**, and **All meters**, defaulting to **Weekly** over **24 hours**. Ranges include
  24 hours, 7 days, 30 days, 90 days, and **All retained**.
  - **Short-term** includes regular short-window quotas and Antigravity Gemini and Claude/GPT
    short pools. Labels distinguish reported durations (1h, 3h, or 5h), unknown durations, and
    windows that vary within the displayed range.
  - **Weekly** includes regular weekly quotas and Antigravity Gemini and Claude/GPT weekly
    pools. Anthropic follows each account's taskbar weekly selections: normal, Fable, both,
    or neither. **Refresh / F5** picks up changed selections. Removed accounts retain both
    weeklies because they no longer have taskbar settings.
  - **Extra usage** plots Anthropic extra-usage budget used (%) on the left axis and OpenAI
    credits remaining on the right. USD spending appears in details, not on this graph.
- Selecting one account defaults to **All meters**, with the same three groups also available.
  All-account and single-account modes remember their view selections separately until the window
  closes. **All meters** includes weeklies hidden on the taskbar and plots Anthropic USD
  spending. Single-account colors stay tied to each meter; All accounts keeps account color
  families. Percentages, USD, and credits use separately labeled axes.
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
- All accounts uses a color family per account, with tints distinguishing its quotas. Connected
  samples use rounded, DPI-scaled 3px primary and 2px scoped lines. Current lines are solid;
  dashes are reserved strictly for the previous week, not for distinguishing meters. Only
  isolated readings get dots (filled for current, hollow for previous). Genuine resets, missing
  or failed readings, recording gaps, restarts, and recording changes stay disconnected.
  No event marks or ticks appear above or below the plot, including reset, missing, or gap marks.
  OpenAI's idle 0% case with a rolling future reset timestamp no longer fragments the line;
  genuine resets still break it.
- Charts show recorded peaks, coverage, and estimated time above the current
  red threshold. Near-limit estimates interpolate only connected samples, not gaps or time after
  the last sample. Short and weekly limits overlap and must not be added together.
- OpenAI credits are not dollars or a spending ledger; the user-set credits ceiling is not
  stored as historical usage. View selection does not change the journal or CSV contents.
- **Export CSV...** exports all meters for the selected account or all accounts in the selected
  range (both periods in comparison mode). Times are UTC; opaque IDs preserve account identity
  without exporting labels. An existing destination is replaced only after a successful write.
- **Clear all history...** permanently removes every account's local samples after confirmation.
  It leaves settings and sign-ins untouched; future polls record again if recording remains on.

### Recording and storage

Recording reuses normal polls; opening the viewer does not increase polling. Missing/failed
polls, sleep, restarts, and recording/settings changes break graph continuity. No past usage is
backfilled. With **Do not poll hidden accounts** on, hidden accounts have no new samples. Turn
it off to poll hidden accounts only when both global and per-account recording are enabled.

History follows the **configured account entry**, not a verified provider login. Renaming keeps
its history; changing provider starts a new identity. Use a new entry for a different login,
including an Antigravity login changed in its app. Removing an entry retains anonymous history
until expiry or Clear; re-adding it starts fresh. Duplicate entries for the same login remain
separate lines. Antigravity graphs track the displayed limiting pools, which can change; they
are not cumulative consumption meters.

Samples are stored under `%LOCALAPPDATA%\TaskbarAIQuota\<Windhawk mod ID>\usage-history-v1.bin`,
with the user's inherited folder permissions. The journal contains quota/amount readings,
timestamps, provider and opaque IDs—not tokens, labels, prompts, or raw responses. It is not
encrypted: processes running as you can read it. It survives mod removal; use Clear first if
you want to remove recorded usage. Local and catalog mod IDs use separate histories.

The recording opt-in is stored per user in
`HKCU\Software\Windhawk\TaskbarAIQuota\<mod ID>\RecordHistory`; another Windows user's opt-in
cannot enable yours. Other settings, including per-account selection and retention, follow
Windhawk's existing machine-shared configuration. Opening an empty viewer does not create files.

Retention defaults to **90 days**. Hourly maintenance prunes older samples while recording or
when refreshing the viewer (including with recording off); loading a journal also permits
maintenance. Retention edits do not bypass that hourly cadence. A **250,000-sample cap
(about 60 MB)** also evicts oldest records in batches;
many accounts or frequent polls can shorten effective retention. Only the taskbar-owning
Explorer records, with exclusive journal ownership to prevent competing writers. Storage
failures appear in the viewer. Downgrading to a version without history support can discard
saved history IDs when settings are edited; old samples then appear as anonymous retained history.

### Verification

After loading the updated mod, enable history for one signed-in account, then refresh that
account or wait for its normal poll. Open Usage history and refresh the snapshot: a recorded
point should appear. Two successful polls in the same uninterrupted window should produce a
line and nonzero observed coverage. Check that hidden accounts pause with the default global
restriction, and resume only when that restriction is off and recording is enabled. Test CSV
export, restart persistence, and Clear without changing sign-ins.

With multiple accounts and retained readings, check:

- A newly opened viewer defaults to All accounts, Weekly, and 24 hours. Switch among Short-term,
  Weekly, Extra usage, and All meters; verify the listed quota groups and separate labels,
  including known and unknown short durations. All meters must keep percentages, USD, and
  credits on separate axes. Quotas must not be summed or collapsed to a maximum.
- For Anthropic, enable normal weekly, Fable weekly, both, then neither on the taskbar;
  refresh history after each change. Weekly should match each account's selection in both
  account modes; All meters and CSV should still include every recorded meter. Removed
  accounts should keep both weeklies, and other providers should be unaffected.
- First select one account: All meters is the default, and all three groups remain available.
  Choose different views in all-account and single-account modes, then switch between them;
  each mode should restore its own selection.
- All accounts uses matching account color families in the graph and legend, with tinted scoped
  lines, 3px primary lines, and 2px scoped lines. Single-account meter colors stay stable when
  switching groups.
- With several legend entries, narrow the window to check wrapping above the graph and details
  below. Check layout and hover targets after scrolling, resizing, and changing DPI; hovering
  each legend entry should emphasize its matching line.
- Hover nearby and coincident lines: all should be identified by name with recorded sample
  values and times. Repeat in week comparison and check that current and previous are distinct.
- Compare previous week switches to 7 days and restores the chosen range when unchecked.
  Check previous-week-only meters, solid current versus dashed previous lines, and filled/hollow
  isolated points. Connected curves need no point dots. Genuine resets, missing readings, and
  gaps stay disconnected, with no event ticks above or below the plot. Check that idle OpenAI
  0% readings with a rolling future reset timestamp stay connected without hiding genuine resets.
- Unsupported or unread meters have no empty legend entries. Accounts without readings show a
  compact status, with paused/recording-off status only once per account. Repeat with missing
  readings in one or both comparison periods.
- Extra usage uses a left percentage axis for Anthropic budget used and a right credits axis
  for OpenAI balances, with USD spending in details only. A USD-only Anthropic account still
  has spending details rather than a false no-data status; its single-account All meters view
  retains the dollar graph. No supported provider needs more than two axes in All meters.
- Switching groups does not change recorded history or filter CSV meters. Export the same
  account scope and range from different groups and confirm all recorded meters remain present.

For upgrades, verify existing non-default accounts, visibility, appearance, and sign-ins before
enabling recording. A local compile alone cannot validate Windhawk injection into Explorer.

## Security Notes

For Anthropic and OpenAI, the mod owns its OAuth credentials end to end: it signs in, stores the access and refresh tokens, and refreshes them itself. Tokens are stored encrypted with Windows DPAPI (current user) in the mod's own Windhawk storage; they are never written to disk in plaintext.

The mod never reads or writes the OpenCode, Claude Code, or Codex credential files. Refresh tokens are used only against the provider token endpoints and are never sent as bearer tokens to the quota endpoints.

Signing in uses the public OAuth clients of the official CLIs (Claude Code for Anthropic, Codex for OpenAI) with PKCE. Antigravity uses only its signed-in loopback language server and stores no Google token.

## Limitations

- Windows 11 taskbar only.
- x86-64 only.
- Specific displays use detected taskbar order: `Display 1` is primary and later entries are secondary taskbars.
- Anthropic/OpenAI require one browser sign-in per configured account.
- Antigravity requires a signed-in app or CLI session to remain running; older IDE builds may also require an open workspace.
- OpenAI sign-in needs `localhost:1455` (or `1457`) free for the browser redirect.
- Anthropic and OpenAI sessions refresh automatically; sign in again if a stored session is missing, invalid, or revoked.

## Building from source

Windhawk and its catalog take a single file, so the source lives in `src/` as fragments of one translation unit, included in order by `src/taskbar-ai-quota.cpp`. `pwsh generate-mod.ps1` combines them into the committed `local@taskbar-ai-quota.wh.cpp`; edit `src/` rather than that generated file, regenerate, then run `pwsh install-windhawk.ps1` to install it locally. Edits made in Windhawk's mod editor need to be ported back into `src/`.

## Suggestions & Bugs

Have a suggestion or found a bug? [Open an issue](https://github.com/Cleroth/windhawk-taskbar-ai-quota/issues/new).
