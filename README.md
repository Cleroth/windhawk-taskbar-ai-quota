# Taskbar AI Quota Bars

Shows Anthropic Claude and OpenAI/Codex AI agent and LLM subscription quota usage as compact bars on the Windows 11 taskbar.
Can show on the primary taskbar only, all taskbars, or one specific monitor.

![Taskbar AI Quota Bars](https://i.imgur.com/LD0K31E.png)
![Taskbar AI Quota Bars Detail](https://i.imgur.com/H7agnz2.png)

## What It Shows

Each configured account gets one compact taskbar column:

- stacked layout: top bar = 5-hour usage, bottom bar = weekly usage
- vertical layout: side-by-side 5h | weekly bars, both filling bottom-up

Hover for exact percentages and reset times. Click a column to refresh that account or open the provider dashboard, depending on settings. Right-click a column for Refresh all and Open dashboard.

Bars use configurable green/yellow/orange/red thresholds, with an optional colorblind palette. Stale errors can mark labels and tooltips with `!`.

It can also fire a Windows notification when an account first crosses the red threshold (5-hour or weekly), so you don't have to keep glancing at the bars. The notification re-arms once usage drops back below the threshold.

## Setup

Install the Windhawk mod from `local@taskbar-ai-quota.wh.cpp`, then configure accounts in the mod settings.

Default accounts use OpenCode credentials for Anthropic and OpenAI. To use Claude Code or Codex credentials, change the account provider in settings.

## Credential Sources

The mod reads existing local auth files from OpenCode, Claude Code, or Codex.

| Provider | Source | File | Token field |
| --- | --- | --- | --- |
| Anthropic | OpenCode | `%USERPROFILE%\.local\share\opencode\auth.json` | `anthropic.access` |
| OpenAI | OpenCode | `%USERPROFILE%\.local\share\opencode\auth.json` | `openai.access` |
| Anthropic | Claude Code | `%USERPROFILE%\.claude\.credentials.json` | `claudeAiOauth.accessToken` |
| OpenAI | Codex | `%USERPROFILE%\.codex\auth.json` | `tokens.access_token` |

For OpenCode, the auth key defaults to `anthropic` or `openai`. You can override it if your `auth.json` uses different keys.

## Settings

Useful settings include:

- provider and credential source per account
- account labels
- bar length, thickness, and layout
- bar mode: used (fills as quota is consumed) or remaining (fills with quota left, tooltips show "X% remaining")
- label position and font size
- account, label, bar, and tray spacing
- compact percent text
- click action: refresh account or open provider dashboard
- taskbar monitor mode: primary, all, or specific monitor number (`1` = primary, `2+` = secondary taskbars)
- color thresholds
- threshold notifications (toast when an account crosses the red threshold)
- colorblind palette
- stale-warning marker

## Security Notes

Credentials are read-only. The mod never refreshes, rewrites, or rotates tokens. Refresh tokens are not bearer tokens and are not sent to quota endpoints.

OpenCode, Claude Code, and Codex are responsible for refreshing their own auth files. If a token expires, run the owning app so it can refresh credentials, then refresh the taskbar quota display.

Do not share auth files. They contain access tokens.

## Limitations

- Windows 11 taskbar only.
- Specific monitor numbers use taskbar order: `1` is primary, `2+` are secondary taskbars in monitor order.
- Uses local auth files from supported tools.
- Expired access tokens can cause `401` until the owning app refreshes them.
