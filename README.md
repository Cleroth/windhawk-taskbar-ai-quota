# Taskbar AI Quota Bars

Shows Anthropic Claude, OpenAI/Codex, and Google Antigravity AI agent and LLM subscription quota usage as compact bars on the Windows 11 taskbar.
Can show on the primary taskbar only, all taskbars, or one specific monitor.

![Taskbar AI Quota Bars](https://i.imgur.com/LD0K31E.png)
![Taskbar AI Quota Bars Detail](https://i.imgur.com/H7agnz2.png)

## What It Shows

Each configured account gets one compact taskbar column:

- stacked layout: top bar = 5-hour usage, bottom bar = weekly usage
- vertical layout: side-by-side 5h | weekly bars, both filling bottom-up

Hover for exact percentages and reset times. Click a column to refresh that account or open its provider dashboard, depending on settings and provider support. Right-click a column for Refresh all, provider actions, and show/hide toggles.

Bars use configurable green/yellow/orange/red thresholds, with an optional colorblind palette. Stale errors can mark labels and tooltips with `!`.

It can also fire a Windows notification when an account first crosses the red threshold (5-hour or weekly), so you don't have to keep glancing at the bars. The notification re-arms once usage drops back below the threshold.

## Setup

Install the Windhawk mod from `local@taskbar-ai-quota.wh.cpp`. Configure accounts (provider + label) in the mod settings, then sign in to Anthropic/OpenAI accounts from a quota column's right-click menu. Labels must be unique within each provider; duplicate identities are ignored. Antigravity reads quota from its running app.

The default accounts are one Anthropic (`A`) and one OpenAI (`O`). Add a Google Antigravity account (`G`) when needed.

## Signing In

The mod runs its own OAuth sign-in and refreshes the access token itself, so the bars keep working without re-running any CLI. A column that needs authentication shows "click to sign in" - just left-click it to start the flow. You can also right-click a column, open **Sign in**, and pick the account:

- **Anthropic**: a browser opens to claude.ai. After you approve, the page shows a code like `abc...#xyz...`; paste it into the prompt the mod shows.
- **OpenAI**: a browser opens to chatgpt.com; the mod catches the redirect on `localhost:1455` (falling back to `1457`) automatically, so there's nothing to paste. If the Codex CLI is signing in at the same time the port may be busy - close it and retry.
- **Google Antigravity**: no separate mod sign-in is needed. Sign in to Antigravity, open a workspace, and keep the app running so the mod can query its local language server.

Use **Sign out** in the same menu to delete an Anthropic/OpenAI stored token. The label is part of those accounts' identity, so renaming it requires signing in again.

## Settings

Useful settings include:

- provider (Anthropic, OpenAI, or Google Antigravity) per account
- account labels
- bar length, thickness, and layout
- bar mode: used (fills as quota is consumed) or remaining (fills with quota left, tooltips show "X% remaining")
- label position and font size
- account, label, bar, and tray spacing
- compact percent text
- click action: refresh account or open provider dashboard (Antigravity always refreshes)
- cloud poll interval (Antigravity polls its local server every minute)
- taskbar monitor mode: primary, all, or specific monitor number (`1` = primary, `2+` = secondary taskbars)
- color thresholds
- threshold notifications (toast when an account crosses the red threshold)
- colorblind palette
- stale-warning marker

## Security Notes

For Anthropic and OpenAI, the mod owns its OAuth credentials end to end: it signs in, stores the access and refresh tokens, and refreshes them itself. Tokens are stored encrypted with Windows DPAPI (current user) in the mod's own Windhawk storage; they are never written to disk in plaintext.

The mod never reads or writes the OpenCode, Claude Code, or Codex credential files. Refresh tokens are used only against the provider token endpoints and are never sent as bearer tokens to the quota endpoints.

Signing in uses the public OAuth clients of the official CLIs (Claude Code for Anthropic, Codex for OpenAI) with PKCE. Antigravity uses only its authenticated loopback language server and stores no Google token.

## Limitations

- Windows 11 taskbar only.
- Specific monitor numbers use taskbar order: `1` is primary, `2+` are secondary taskbars in monitor order.
- Anthropic/OpenAI require signing in once from the right-click menu.
- Antigravity requires its signed-in app to remain running with a workspace open.
- OpenAI sign-in needs `localhost:1455` (or `1457`) free for the browser redirect.
- Anthropic access tokens are short-lived but the mod refreshes them automatically; you only re-sign-in if the refresh token is revoked.
