# Taskbar AI Quota Bars

Shows Anthropic Claude, OpenAI/Codex, and Google Antigravity AI agent and LLM subscription quota usage as compact bars on the Windows 11 taskbar.
Can show on the primary taskbar only, all taskbars, or one specific monitor.

![Taskbar AI Quota Bars](https://i.imgur.com/LD0K31E.png)
![Taskbar AI Quota Bars Detail](https://i.imgur.com/H7agnz2.png)

## What It Shows

Each configured account gets one compact taskbar column. Choose its 5-hour, weekly, and Anthropic monthly extra-usage bars independently; selected bars auto-hide when the provider does not return that quota.

- stacked layout: selected bars stack horizontally and fill left-to-right
- vertical layout: selected bars sit side-by-side and fill bottom-up

Hover for exact percentages and reset times. Click a column to refresh that account or open its provider dashboard, depending on settings and provider support. Right-click a column for Settings, Refresh all, provider actions, and show/hide toggles.

Bars use configurable green/yellow/orange/red thresholds, with an optional colorblind palette. Optional pace ticks compare quota usage with elapsed time in each reset window and have a configurable color. Stale errors can mark labels and tooltips with `!`.

It can also fire a Windows notification when an account first crosses the red threshold on a selected bar, so you don't have to keep glancing at the bars. The notification re-arms once usage drops back below the threshold.

## Setup

Install the Windhawk mod from `local@taskbar-ai-quota.wh.cpp`. On a fresh install, click the **AI +** taskbar tile to open the native settings window and add an account. Existing Windhawk settings are imported once when upgrading. Settings autosave without re-querying providers unless an account identity changes.

Configure a provider, unique label, and visible quota bars for each account. Sign in to Anthropic/OpenAI from the settings window or a quota column's right-click menu. Antigravity reads quota from its running app.

## Signing In

The mod runs its own OAuth sign-in and refreshes the access token itself, so the bars keep working without re-running any CLI. A column that needs authentication shows "click to sign in" - just left-click it to start the flow. You can also right-click a column, open **Sign in**, and pick the account:

- **Anthropic**: a browser opens to claude.ai. After you approve, the page shows a code like `abc...#xyz...`; paste it into the prompt the mod shows.
- **OpenAI**: a browser opens to chatgpt.com; the mod catches the redirect on `localhost:1455` (falling back to `1457`) automatically, so there's nothing to paste. If the Codex CLI is signing in at the same time the port may be busy - close it and retry.
- **Google Antigravity**: no separate mod sign-in is needed. Sign in to Antigravity, open a workspace, and keep the app running so the mod can query its local language server.

Use **Sign out** in the same menu to delete an Anthropic/OpenAI stored token. Renaming a label preserves its stored sign-in when possible. Removing an account asks whether to retain or delete it.

## Settings

Right-click any quota column and choose **Settings...**. Useful settings include:

- provider (Anthropic, OpenAI, or Google Antigravity) per account
- account labels
- account ordering and taskbar visibility
- per-account 5-hour, weekly, and Anthropic monthly extra-usage bar selection
- bar length, thickness, and layout
- bar mode: used (fills as quota is consumed) or remaining (fills with quota left, tooltips show "X% remaining")
- pace ticks comparing quota usage with elapsed time (or quota remaining with time remaining), with a configurable color
- label position: hidden, left, top, right, or bottom
- label font size
- account, label, bar, and tray spacing
- compact percent text
- click action: refresh account or open provider dashboard (Antigravity always refreshes)
- cloud poll interval presets plus a custom interval (Antigravity polls its local server every minute)
- taskbar monitor mode: primary, all, or a detected display with its resolution
- color thresholds with palette-matched previews
- threshold notifications (toast when an account crosses the red threshold)
- colorblind palette
- stale-warning marker

Bounded visual sizes use sliders with precise numeric spinner controls. Other numeric settings use spinners. Concise hover help explains polling, pace ticks, Codex Spark, and stale warnings. Each non-account page can be reset independently, or all appearance and behavior settings can be reset together, without removing accounts or credentials.

## Security Notes

For Anthropic and OpenAI, the mod owns its OAuth credentials end to end: it signs in, stores the access and refresh tokens, and refreshes them itself. Tokens are stored encrypted with Windows DPAPI (current user) in the mod's own Windhawk storage; they are never written to disk in plaintext.

The mod never reads or writes the OpenCode, Claude Code, or Codex credential files. Refresh tokens are used only against the provider token endpoints and are never sent as bearer tokens to the quota endpoints.

Signing in uses the public OAuth clients of the official CLIs (Claude Code for Anthropic, Codex for OpenAI) with PKCE. Antigravity uses only its authenticated loopback language server and stores no Google token.

## Limitations

- Windows 11 taskbar only.
- Specific displays use detected taskbar order: `Display 1` is primary and later entries are secondary taskbars.
- Anthropic/OpenAI require signing in once from the right-click menu.
- Antigravity requires its signed-in app to remain running with a workspace open.
- OpenAI sign-in needs `localhost:1455` (or `1457`) free for the browser redirect.
- Anthropic access tokens are short-lived but the mod refreshes them automatically; you only re-sign-in if the refresh token is revoked.
