# Settings reference

Every setting in the mod's settings window. Overview: [README](../README.md).

Right-click any quota column and choose **Settings**. The window follows the Windows light, dark,
or high-contrast theme and has six pages. Sizes and thresholds combine a slider with a number
box; spacing, polling, retention, and credits use number boxes with step buttons. Changes save as
you make them; a number or text box saves on Enter or when you leave it, and Esc reverts it. A
value that can't be saved stays in its box with the error, and closing the window then asks
before discarding it. Layout, Display, Behavior, and History each have a reset that restores that
page's defaults without touching accounts or sign-ins.

## Accounts

**Add account** (Anthropic, OpenAI, Google Antigravity, GitHub Copilot, or OpenCode Go); drag a
card by its handle (or press Alt+Up/Down) to reorder; its switch shows or hides the account on the
taskbar (the last visible account can't be hidden). Adding, signing in and removal:
[accounts and sign-in](accounts.md). Expand a card for:

- label: unique per provider; renaming keeps the sign-in and history
- quotas on taskbar: 5-hour (Antigravity: short window), weekly, Anthropic Fable weekly and
  monthly extra usage, Antigravity Argon weekly (on for new and upgraded accounts), OpenAI
  credits, OpenCode Go monthly (on for new accounts); GitHub Copilot's AI credits are its only
  quota; unavailable quotas auto-hide
- OpenAI credits max: the balance the credits bar treats as 100% (credits have no server-side
  ceiling)
- identity, shown beside the account's stacked or vertical bars and on tiles: provider logo with a
  number badge when several accounts share a provider, hidden ones included (default), logo only,
  or label (the full label on bars; on tiles up to 4 characters of short text, else the label
  without spaces)
- record history for this account
- **Open dashboard**, **Sign in again**, and **Sign out** (Anthropic, OpenAI, GitHub Copilot,
  OpenCode Go), and **Remove account**

## Layout

With a live preview of the taskbar:

- tile style: usage graph (the default for new installs; a chart per quota of its recorded usage
  across its window, side by side, with an even-use diagonal, the current reading, and a
  projection at its recent average rate, as described under [Usage history](usage-history.md)),
  stacked bars, vertical bars, segmented ring, concentric rings, gauge % (a gauge per quota: its
  arc around its number), clock pies (a pie per quota with an elapsed-time hand), rounded square
  (segmented-ring arcs along a rounded square around the identity), headline %, or quota matrix
  (each quota's key and percentage in up to two rows and columns); settings the chosen style
  ignores are hidden or disabled with the reason
- position on the taskbar: before the notification icons (default), before network and volume,
  before or after the clock, or after Show desktop (the far right); or, experimental, **Left
  side** (the taskbar's left edge, after any space another mod such as Taskbar System Info keeps
  there; Widgets, Start and the apps move right, on a centered taskbar by half the bars' width)
  and **Right of Start** (between Start and the buttons after it, which move over).
  - A secondary taskbar without the chosen tray neighbour uses the next one to its right.
  - For Left side and Right of Start, a taskbar whose buttons aren't found, whose Start is hidden
    (Right of Start only), or where another mod controls the space the bars need shows them at
    the first tray slot instead, with a note under Position; after a conflict, restart Explorer
    to retry. A taskbar on the left or right of the screen also shows them at the first tray slot.
  - A note under Position lists the taskbars on a side, where the preview still shows the layout
    across a bottom taskbar.
- bar style: a full track behind each fill (default) or a thin rail under it
- sizes per style:
  - stacked and vertical bars: bar length (stacked bars, default 100 px), vertical bar length
    (vertical bars' own, 10 to 100 px, default 32 so they fit the taskbar; settings saved with
    vertical bars keep the shared length), thickness, and gap
  - rings, gauge, and rounded square: size and thickness; pies take the size
  - usage graph: each chart's width and its height (50 to 100% of the tile, default 80%), the size
    of its quota keys and extra usage text (6 to 16 px, default 8), a faint outline around each
    chart (on by default), the current position cues, each optional (the window's elapsed part
    lit, a now line and a ring around the current reading by default, and a now caret on the
    bottom edge), and the current reading's size (its ring and dot, or the plain dot; 50 to 200%,
    default 100%)
  - headline %: the number size, a reset countdown (the time until the quota resets under the
    number, instead of its bars), and the length and thickness of the bars under it, with the
    same bar gap; the quota matrix takes the number size
- usage graph spans: charts whose window is longer than a day (7d, Fable weekly, Argon weekly,
  monthly extra usage, AI credits and OpenCode Go's monthly) show the whole window (default), or
  12, 24, 36 or 48 hours before and after now, on the same 0 to 100% scale (kept inside the
  window, so near its start and its reset now moves across); their span **On hover**, while the
  pointer rests on the account, is any of those (24 hours each side by default) or off
- usage graph extra usage: the account's extra usage in the 7d chart's top-left corner
  (bottom-left in remaining mode) while it is selected for the account and the provider reports
  it: the amount (`$` for Anthropic extra usage, credits for OpenAI; default), the percentage of
  the limit (the amount without one), or off, spent in used mode and left in remaining mode.
  OpenAI credits never get a chart of their own; GitHub Copilot's monthly AI credits and OpenCode
  Go's monthly do
- identity position (left, right, top, bottom, or hidden) for stacked and vertical bars, the label
  font size for label identities, and the identity gap
- logo size (50 to 200% of the standard size; ring logos stay inside the hole), and the number
  badge's size (6 to 20 px, default 13), color, and horizontal and vertical offsets; the badge is
  an outlined number centered on the logo's lower-right corner (on rings, pulled inside the hole),
  in the taskbar's text color by default (the white swatch), or another color from the swatches
  or **Custom...**, outlined dark or light to contrast with it
- logo colors: the Claude logo in the taskbar's text color (default) or Claude's orange, and the
  Antigravity (Gemini sparkle) logo in the text color (default) or Gemini's purple-to-blue
  gradient; the details flyout, the Accounts page and the usage history window follow them, and
  high contrast always uses the text color
- percentage text on stacked and vertical bars: never show, show on hover (default), or always
  show, with adaptive, left, center, or right alignment; vertical bars keep their own choice,
  never by default since the text is wider than a thin bar and overlaps its neighbors (settings
  saved with vertical bars keep the shared choice)
- amounts on extra/credits bars: show `$` spent or left (Anthropic extra usage) or credits
  (OpenAI, and GitHub Copilot's AI credits rounded to whole credits) instead of a percentage,
  following the bar mode; from 1,000 on, taskbar amounts are shortened to three digits with `k`
  or `M` (`62.5k`)
- compact quota keys (`5h`, `7d`, `Fa`, `Ar`, `Ex`, `Cr`, `Mo`), hidden by default, and the bar
  text size
- pace markers comparing quota usage with elapsed time (or quota remaining with time remaining),
  with caret, full-line, edge-notch, dot, and time-track (a small bar under each bar, or beside
  each vertical bar, that fills with the elapsed time) styles on bars and a color from the
  swatches or **Custom...** (the default light grey turns dark on a light taskbar)
- account margin and the gap after accounts
- **Show on taskbar**: replaces the taskbar accounts with sample accounts while it is on and the
  window is open

## Display

With the same preview:

- material: the background of the details flyout and the settings and usage history windows,
  which all follow it: **Acrylic** (default; a tinted blur of what's behind), Mica or Mica Alt
  (the blurred desktop wallpaper, tinted as Windows does), Tinted wallpaper (the blurred wallpaper
  under the theme color; the windows use Mica for it), or Solid; all but Solid need Windows
  transparency effects, and show solid without them and in high contrast
- bar mode: used (fills as quota is consumed) or remaining (fills with quota left and shows "X%
  remaining"; usage graphs and the details flyout's charts plot quota left, falling from 100%);
  threshold colors always represent consumed quota
- usage color palette: **Default** (green, yellow, orange, red), **Colorblind** (blue to
  vermilion), or **Custom**: a color for usage below the yellow threshold and one from each
  threshold on, from swatches (that band's default and colorblind colors) or **Custom...**,
  starting from the default colors. Taskbar meters, the details flyout and its charts, the
  preview and the usage history window follow it; stale data stays grey. Then the yellow, orange,
  and red thresholds, each with its color
- stale-data marker (`!`)
- show provider outages (on by default): marks an account while its provider's status page
  reports a problem, as described under [Provider outages](taskbar.md#provider-outages); off reads
  no status pages
- additional OpenAI rate limits as rows in the details flyout, one per window (Codex Spark, hidden
  model lanes such as `gpt-reserve`), hidden by default

## Behavior

- interface language: **Automatic** (the default), English, or another language by its own name,
  per Windows user, as described under [Languages](../README.md#languages)
- show on: the primary display, all displays, or a detected display with its resolution
- show details on hover: resting the pointer on a column opens its details flyout (on by default)
- cloud poll interval presets plus a custom interval for Anthropic, OpenAI, GitHub Copilot and
  OpenCode Go (Antigravity polls its local server every minute); only the Explorer process that
  runs the taskbar polls, refreshes sign-ins and notifies, so another one (such as folder windows
  launched in a separate process) adds no requests or notifications
- do not poll hidden accounts (on by default; overrides per-account history recording)
- threshold notifications: off, or the yellow, orange or red threshold (the default), each listed
  with its percentage from Display; a selected taskbar quota notifies when it first reaches it

## History

Record usage history (on by default, per Windows user, with its live status), how long to keep it
(30 days, 90 days, 1 year, **Forever** by default, or a custom 1 to 3650 days), **Open usage
history**, the time span the usage history window opens on (24 hours, **7 days** by default, 30
days, 90 days, or All retained), and the storage folder. Details: [usage history](usage-history.md).

## About

Version, **Copy version info** (the mod's name and version and the Windows build, with its update
revision, to the clipboard for bug reports), project links, contributors, credits, and license.
