# Taskbar, flyout and notifications

What the quota columns show and how they behave. Overview: [README](../README.md); every
setting: [settings reference](settings.md).

## Quotas by provider

Each visible account gets one compact taskbar column with the provider's bars you select;
selected bars auto-hide when the provider doesn't return that quota.

- **Anthropic:** 5-hour, weekly, Fable weekly and monthly extra usage.
- **OpenAI:** 5-hour, weekly, and prepaid credits measured against a max you set (the API reports
  only the balance).
- **Google Antigravity:** the available short-window and weekly Gemini pools and the Gemini Argon
  weekly pool (keyed `Ar`, on by default). Claude/GPT pool limits and the other quotas it reports
  (such as Argon's other windows, other model groups, or Argon's current limit on Antigravity
  servers without pooled-quota support) appear in the details flyout. Before 2.6.0 Argon counted
  toward the Gemini bars, so earlier Gemini history can include it.
- **GitHub Copilot:** one bar, the monthly AI credits allowance (keyed `Mo`; Copilot Free's
  included), resetting on the 1st of each month. The details flyout shows the credits used of the
  allowance (to two decimals), an additional-usage line when GitHub reports usage past the
  allowance (only what GitHub reports as additional usage, not a bill), and, for an
  organization's pooled seat without a per-user limit, the pooled credits used (or that the pool
  is used up) instead of a bar. The flyout and notifications name the allowance's unit: AI
  credits; premium requests on plans GitHub doesn't bill by tokens; chat requests for a Copilot
  Free account that GitHub still reports in the older form.
- **OpenCode Go:** 5-hour, weekly and monthly usage limits (the monthly keyed `Mo`, all three on by
  default). The 5-hour window starts at its first use, the weekly is a calendar week and the
  monthly resets on your subscription's day of the month; the details flyout marks a window whose
  limit is reached (with Extra Usage turned on in OpenCode, Go then keeps working from your
  balance). The monthly window's start is estimated from its reset: for subscriptions on the 29th
  to 31st it can be up to three days early when the reset falls in a shorter month, which moves
  its chart, pace and projection (and the window length history records); usage and reset times
  are as reported.

## Columns

Each column shows the account's identity beside its bars: the provider logo (with a number badge
when several accounts share a provider, hidden ones included) or the account's label. Stacked
bars fill left to right, stacked vertically; vertical bars sit side by side and fill bottom-up;
the other tile styles are listed in the [settings reference](settings.md). Bars can show compact
quota keys and percentage text, and use configurable green/yellow/orange/red thresholds with an
optional colorblind or custom palette. Optional pace ticks compare quota usage with elapsed time
in each reset window. Stale errors can mark the identity with `!` and the details flyout with a
warning icon.

Click a column to refresh it. Right-click it for Settings, Usage history, Refresh all, provider
actions, **Hide**, and show/hide toggles that keep the menu open while you toggle several
accounts. Hidden accounts are not polled by default, and the last visible account can't be
hidden. History recording can explicitly enable hidden-account polling without showing taskbar
bars or firing threshold notifications for those accounts.

On a taskbar at the left or right of the screen (Windows 11's own taskbar position setting), the
accounts stack down the taskbar and each style rearranges to fit its width: identities sit above
their meters (rings keep theirs inside), a usage graph's charts stack, the quota matrix takes one
column, stacked bars, charts and rings shorten to the width, and an account still wider than the
taskbar (long labels, large text) shrinks as a whole. Your settings stay as they are, so moving
the taskbar back restores the layout across. The bars follow the taskbar when it moves between
edges, and the details flyout opens beside it (below a taskbar at the top).

## Details flyout

Hover a column to highlight it and open its details flyout: percentages, reset times, pace, when
usage is projected to reach the limit, plan and provider details, errors, and retry countdowns,
with buttons for usage history, the provider dashboard, refresh, and settings. It also lists the
limits that have no taskbar bar, each with its percentage, pace and reset: Anthropic's per-model
and per-surface limits (such as Sonnet and Opus weeklies), Antigravity's other reported quotas
(named by the app) and, with their Display setting, OpenAI's additional rate limits. It shows
Anthropic's and OpenAI's free rate-limit resets with the next one's expiry, the Claude plan (Pro,
Max 5x, Max 20x, Team, ...), and "Not yet started" for a Claude window that hasn't begun; past
the screen's height its quota list scrolls.

## Provider outages

While a provider's public status page reports a problem with a service the mod watches, every
account of that provider shows a filled warning triangle in place of the stale `!` (beside the
logo, at a ring's corner, or after a label): in the usage palette's orange for degraded
performance, a partial outage or maintenance, its red for a major outage (so the colorblind and
custom palettes apply). When everything is operational nothing shows: there is no "all good"
state. The details flyout adds a line under its header naming the component (and how many others
are affected), its state, the incident's title when the page gives one, and since when, in local
time; click it to open the status page. Nothing is notified. With **Identity position** hidden on
stacked or vertical bars there is no mark, only the flyout line.

| Accounts | Status page read | Watched |
| --- | --- | --- |
| Anthropic | `status.claude.com/api/v2/summary.json` | claude.ai, Claude API, Claude Code |
| OpenAI | `status.openai.com/proxy/status.openai.com` | every component of its Codex group (Codex Web, Codex API, CLI, VS Code extension) |
| GitHub Copilot | `www.githubstatus.com/api/v2/summary.json` | Copilot, Copilot AI Model Providers |
| OpenCode Go | `status.opencode.ai/api/status` | Go |

Google Antigravity has no status page, so it never shows a mark. Only the Explorer process that
runs the taskbar reads the pages, only for providers with an account shown on the taskbar, about
every 5 minutes (OpenCode every 10, its page being larger), on its own schedule apart from the
quota polls. OpenCode's page changes often for a few minutes at a time, so its mark shows only
once a second read, 3 minutes after the first, agrees; a single blip between reads can still
pass. A mark clears with the first read that finds the service operational, or that fails: a page
that can't be read or understood, or a reading older than two of its intervals, shows nothing.
Turn it off with **Settings > Display > Show provider outages**: no status page is read then.

## Notifications

The mod can show a Windows notification when a quota selected for the taskbar first reaches the
threshold chosen in **Settings > Behavior**: yellow, orange or red (the default). It re-arms once
usage drops back below the threshold; changing the level or the thresholds never notifies by
itself. Notifications show under the mod's own name and icon and, when usage is projected to
reach the limit before the reset, say when, as the details flyout does. Clicking one opens the
provider's dashboard (Antigravity has none), and a repeat for the same account and quota
replaces the earlier one.

At its first notification after starting, the mod registers its name and icon with Windows under
`HKCU\Software\Classes\AppUserModelId\Windhawk.TaskbarAIQuota.<mod ID>`, a volatile key Windows
drops when you sign out, the icon being `notification-icon.png` in your folder of the mod's
Windhawk storage, beside the usage history (see
[usage history > Recording and storage](usage-history.md#recording-and-storage)). Turning
notifications off or disabling the mod removes the registry key; the icon file stays until
Windhawk removes the mod's storage with the mod. Windows keeps its
own entry for the mod in **Settings > System > Notifications**, and turning the mod off there
silences it. Only when Windows fails to show a notification does the mod fall back to a tray
balloon, which Windows attributes to Windows Explorer.
