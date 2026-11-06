# Settings suite coverage

What each target, fixture and probe of `tests/settings` covers, for finding where a case belongs.
The fixtures are authoritative for individual cases; update this map when adding a target, phase
or fixture file, or when a probe's observed native facts change. Rules and running:
[AGENTS.md](AGENTS.md).

## Targets

`extract.py`'s `TARGETS` lists each target's sections in declaration order; each run writes them
to `<target>.cpp` with a `.manifest.json`.

| Target | Phases | Driver | Production code |
| --- | --- | --- | --- |
| model | `extract`, `model-compile`, `model`, `render` | `fixtures/main.inc` | localization engine and catalogs, `core/model.h`, account and journal errors, threshold notification text, sign-in layout, logos, meter model, pure toolkit, settings edit controller, view, preview, pages and shell |
| history | `extract-history`, `history-compile`, `history`, `history-render` | `fixtures/history-main.inc` | the same engine, model, meter model and toolkit; `history/` types, continuity, series, CSV and the window's view, chart, controller, chart painter, content and shell |
| status | `extract-status`, `status-compile`, `status` | `fixtures/status-main.inc` | `fetch/status-feeds.h` with the model, JSON accessors and date parsing (Windows.Data.Json) |
| host | `extract-host`, `host-compile`, `probe-compile`, `host-probe` | `fixtures/host-dll.inc` in `ui-host.dll`, driven by `host-probe.cpp` | the toolkit with its native host, `history/modal.h` and a stub client with window icons |
| popup | `extract-popup`, `popup-compile`, `popup-probe` | `fixtures/popup-probe.inc` (`popup-automated.inc`, `popup-interactive.inc`) | the same host owning a hand-written probe popup |
| signin | `extract-signin`, `signin-compile`, `signin-probe` | `fixtures/signin-probe.inc` | `auth/login-layout.h`, `auth/login-controls.h`, the catalogs and account errors |

## Model fixtures

- `support.inc`: synthetic inputs, among them `TestMeasure` (0.6 em a character) and
  `TestNumbers` (regional formats from Windows' strings: de-DE, fr-FR, ru-RU, hi-IN, de-CH,
  grouping off, one group). `check.inc`: assertions shared by every target.
- `i18n.inc`: the localization engine on test-only catalogs: grammar, escapes, limits, plural
  categories per language, every fallback case, reordered links, argument kinds, list patterns,
  the registry, Automatic matching, the pseudo-locale, lookup with and without context.
- `meter.inc`, `logos.inc`: the meter model (bars, tiles, account state, headline countdown,
  identity, sparklines and their history requests, projections, matrix cells) and logo paths.
- `ui.inc`: toolkit palette, input, focus, hit testing, scroll, controls, popup rows, placement and
  keys, the settings shell and pages (the About page's links and focus stops).
- `controls.inc`: the controls the history window builds on (segments, ellipsis, dropdowns, the
  navigation pane, leading switch, pills, info bars).
- `tooltip-support.inc`, `tooltip.inc`: readout layout search, pointer-excluding placement in
  adversarial geometries, complete-text blocks, content and placement dedupe, chart item hits
  and keys.
- `edits.inc`: the edit controller against `TestStore` (in-memory, with an operation log,
  deterministic save failures and unload after N transactions), number parsing, the render
  projection.
- `accounts.inc`: account edits and actions against `TestStore` (labels, drafts, removal with its
  sign-in and history deletion, Add account, sign-in after naming, visibility, moves) and the
  Accounts page (cards, expanded rows, empty state, drag and reorder).
- `layout.inc`: sliders, Layout fields and reset group, the preview strip model
  (`settings/preview.h`: every tile style, identity form and position, pace marks, sparklines,
  the mock taskbar and every Position), the Layout page per tile style.
- `display.inc`: Display fields and reset group (every scalar setting in exactly one page reset),
  Material, usage palettes and Custom colors, threshold ordering, the Display page and what it
  changes in the preview.
- `i18n-settings.inc`: the settings window on the production catalogs (localized runs bound to
  actions, plurals, notices, draft errors, per-page language cases).
- `i18n-taskbar.inc`: the meter model's localized text, measured taskbar text on synthetic measures
  (`TestWideMeasure`: CJK an em), the preview strip per language, account errors and flyout
  templates.
- `i18n-errors.inc`: coded account errors in every form, threshold notifications' text and toast
  XML, the sign-in templates, outage lines.
- `i18n-numbers.inc`: regional number formats, integer arguments, amounts and number boxes (format
  and parse round trips, a draft across a regional change).
- `layout-audit.inc` (shared with the history target): audits a frame for labels wider than their
  control, text cut without a tooltip holding it whole where the pointer hits, wrapped text taller
  than its slot or breaking a word, items over each other or out of their card, popup rows.
- `i18n-layout.inc`: every settings page and state in every language and the pseudo-locale at the
  default and minimum size through the audit, then measured layout cases; its cases are also the
  localization review renders'.
- `signin-layout.inc`: the sign-in layout from widths and lines the sign-in probe measured
  (English's fixed geometry at 96 to 192 DPI, languages widening it, reserves).
- `render-support.inc`, `render.inc`, `render-controls.inc`: renders (below).

## History fixtures

- `history-view.inc`: compact formats in synthetic time zones, number rounding, summary cards and
  No readings entries built from synthetic readings.
- `history-controller.inc`: the controller against `TestHistoryOps` (in-memory request, journal
  with its stamp and peek contract, settings with polls, clock, unload, posted refresh messages,
  deferral timer, consent transaction, operation log). Orchestration only: never the production
  ops or the opener-to-window bridge.
- `history-refresh.inc`: live refresh, deferral (modal, popup, inspection) and its resume paths,
  the no-stranding invariant after every event, Enable recording.
- `history-operations.inc`: Export, Clear, Delete history and About these charts through the test
  ops' dialogs, with cancellations, failures, the close latch and unload inside each.
- `history-frame.inc`: the frame per state (navigation, header, segments, info bars, the chart's
  card and legend chips, cards, menus, export progress).
- `history-tooltip.inc`: the single-account readout guarantee at every pointer position, in every
  language; `history-chart.inc`: axes, ticks, picking, readout rows, emphasis, the legend's
  filter, the draw list, Remaining mode, the reduction cache's key.
- `i18n-history.inc`, `i18n-journal.inc`, `i18n-history-layout.inc`: dates and ticks per
  language, language switches, regional formats, the journal's coded errors, and the window in
  every language through the layout audit (also the localization review renders' cases).

## Status pages

`fixtures/status/` holds captured public pages, read 2026-11-05 with update texts removed:
Claude's and GitHub's Statuspage summaries, OpenAI's incident.io feed and OpenCode's status API
(all operational), and Supabase's incident.io feed with a recovered incident still in monitoring.
`fixtures/status.inc` parses them as is and with one entry replaced (each level, incidents and
maintenances, a live incident moved onto a Codex component, unwatched components, unknown
statuses, strict dates, untrusted pages), plus the two-read confirmation and the reading's age.

## Renders

`render` (the model executable with `--render-dir`) and `history-render` paint into WIC bitmaps
with the production painters: PNGs for review, no pixel goldens. Names:

- `render-<page|sheet>-<theme>-<dpi>`: the settings client area (1000 x 820 DIP, the mockups'
  window without its caption) per page, tile style and interaction state, in light, dark and high
  contrast at 96/144/192 DPI; preview strip, placement, popup and controls sheets
  (`render-controls-history-*`: the history window's controls).
- `render-i18n-<page|sheet>-<tag>-light-96[-min]` (some also 144): the localization review in the
  pseudo-locale, de, ru, ja and zh-Hans (preview sheets also `en`), at the default and minimum
  width; `-de-DE-` variants render under de-DE's number format.
- `render-backdrop-*`, `render-history-backdrop-*`: windows over a stand-in backdrop with grayscale
  text, as the host draws them when DWM draws one.
- `render-history-<sheet>-*`: the history window as the host composes it, laid out by a controller
  over synthetic journals and modeled on the mockups; `render-history-tooltips-*`; and
  `render-history-i18n-<sheet>-<tag>-light-96[-min]`.
- `render-signin-<tag>-<96|144>` (en, qps-ploc, de, ru, ja, zh-Hans): from the sign-in probe.

The run directory holds the exact set; every reported render must exist as a PNG.

## Native probes

### Host probe

`host-probe.cpp` loads `ui-host.dll`, opens the window on the DLL's worker thread, closes it from
another thread, joins and unloads, repeatedly, checking classes, windows and GDI/USER handle
counts; then client creation failure, a conflicting class registration, render-target loss
through the production discard path, the minimum size, window icons across DPI and theme changes,
and an unload close during a message box.

- Handle counts are process-wide: a window case reads them on the window thread once its pending
  messages and paints are handled (`kHandles`), never from the driver while the window may still
  paint. The icons case logs each step's counts (`STEP` lines; `ANOMALY` with the process's windows
  if a settled count moves).
- On hidden desktops, one case each: the production popup, number box EDIT, slider, preview strip,
  drag handle (with the auto-scroll timer), free text box, menu button, color dialog, and unload
  with the popup open, a focused EDIT or a message box over the popup.
- History mode: segmented controls and keys through the host, keys reaching the client, the
  activation request after the popup closed, theme rebuilds, the navigation pane's wheel, hits,
  scroll-into-view and scrollbar drag.
- Pointer state is read in the command that moved the pointer: the real cursor is elsewhere, so a
  leave follows. Tooltip mode's moves drop that leave, so the pointer stays: chart hover, the
  readout (owned, never active, no capture, `HTTRANSPARENT`, hidden by every trigger one by one),
  the delayed item tooltip, target loss, handle growth, unload while showing, creation failure
  and retry; thread message hooks count what reaches the tooltip.
- Progress mode: a client-drawn item repainted from inside a handler through `UiHostPaintNow`.
- Dialog mode (latching and cancelling as the history client does): message boxes in the
  production scope ended by unload's close from the driver thread; a Yes/No box ignores Cancel
  alone (observed), so the scope answers No; boxes after the latch, a user close, refused nested
  or cancelled scopes; last, the save dialog as an observation.
- Off the hidden desktop: the real cursor's rectangle, read only.

### Popup probe

Thread-local message hooks record what each window receives while capture is on the popup, on the
viewport or nowhere: opening, `WM_CANCELMODE` to either window, another window taking capture,
release, focus moving to an EDIT, owner deactivation, minimize/restore, a disabled owner, a
message box, destruction. It prints `OBSERVE` lines and checks the native facts the popup design
rests on: a failure means revisiting the design, not the probe.

### Sign-in probe

The production controls in never-shown probe windows, with comctl32 6 activated from a manifest
written to the run directory (as Explorer's gives the sign-in window), in every language and the
pseudo-locale at 96 and 144 DPI, for each flow's texts: buttons' ideal sizes (`BCM_GETIDEALSIZE`)
for every label, statics' texts as painted (`WM_PRINTCLIENT`) inside their rectangles, boxes a line
tall, nothing overlapping or outside the client; English's fixed geometry at 96 to 192 DPI; a DPI
change through the production handler. `WM_PRINT` leaves out a box's themed border, so the renders
draw a plain one; themes draw at the probe monitor's DPI. `OBSERVE` lines report sizes, widths
and lines.
