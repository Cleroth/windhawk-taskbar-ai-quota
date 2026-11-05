# Settings verification

Source-extracted tests of the provider status pages' parser and read rules, and of the pure
fragments behind both custom-drawn windows (meter model,
`src/ui/` toolkit, the settings edit controller, shell and page builders; the usage history
window's view, chart, controller and frame builders on the shared history model), offscreen renders of
both windows, a native-host probe of the toolkit's window host (with its dropdown popup, tooltip
and number box text boxes, and the history window's dialog scope), and a capture/foreground probe
for the dropdown popup. Not a Windhawk,
Explorer or XAML test. The history domain (journal memory, series, continuity, burn-up, CSV) stays in
`tests/history`.

## Run

From the repo root:

```text
python tests/settings/run.py
python tests/settings/run.py --popup-interactive
python tests/settings/run.py --tooltip-interactive
```

`--popup-interactive` builds only the popup probe, then runs its interactive mode by hand,
attached to the console: a probe window opens on your desktop; follow the printed steps (open the
dropdown, do the action, F9 ends the step, F8 skips it, F10 is the activate request). The log is
`popup-interactive.log` in the run directory.

`--tooltip-interactive` builds only the host probe, then runs its tooltip routing mode the same
way: the probe window (a chart under the dropdown and slider, an icon button with a tooltip at the
top) opens activated on your desktop; follow the printed steps (hover the chart, move onto the
readout, click beside it, the icon's tooltip, other apps, Alt+Tab, the wheel, screen corners and
the taskbar, a second monitor, the Windows key), F9 ends a step, F8 skips it. Each step logs the
readout's shows and hides, hover reports, mouse and activation messages that reached the tooltip,
presses that reached the window and the foreground window. The log is `tooltip-interactive.log`
in the run directory; it is the evidence for tooltip pointer routing.

Prerequisites: Windows x64, Python 3.10+, PowerShell 7 (`pwsh`), Windhawk's bundled clang++ with
its MinGW headers/libraries, and an interactive desktop session (the probe creates windows). No
Python packages or installs.

Options: `--source PATH` (default: the repo-root generated file), `--compiler PATH` (default:
`C:\Program Files\Windhawk\Compiler\bin\clang++.exe`), `--output DIRECTORY` (default:
`tests/settings/.out/`; the parent for a new run directory). With the default source, a
`freshness` phase runs `pwsh generate-mod.ps1 -Check` first and fails on a stale file; it never
regenerates. The default source also gets a `mod-check` phase (`check-mod.ps1`, failing on any
compiler diagnostic), so a full run replaces a separate `check-mod.ps1`.

Order: setup, then all extractions together, then all builds (and `mod-check`) together, then
the tests one at a time; a failed group stops the run. No build overlaps a test.

## Phases

- `model`: pure fixtures (meter model with ring tile outlines, sparkline geometry (the long
  windows' span around now included) and its history requests, usage projections (the tiles' and the flyout's), quota matrix cells, logos, toolkit palette/input/focus/
  hit testing/scroll, controls, popup rows/placement/keys, settings shell and pages).
  `fixtures/i18n.inc`: the localization engine (`src/i18n/`, also extracted into the history
  target) on test-only catalogs: grammar and escapes, shapes and limits, plural categories and
  formatting per language, each fallback case with English plural results, reordered links,
  literal, numeric and rich arguments, list patterns per production catalog, the registry,
  explicit choices and the context, Automatic matching over tag lists, the pseudo-locale, lookup
  with and without context. `fixtures/i18n-settings.inc`: localization in the settings window on
  the production catalogs (`i18n/ui-runs.h` is extracted after the toolkit): localized runs bound
  to actions, the History page's two-link sentence with its links reordered by a test catalog
  (actions and focus order follow the IDs), the language field and its edits (a failed save, the
  Behavior reset), the Behavior page in German, Russian and Japanese (plurals, the widened Show on
  field) and the pseudo-locale, and the locale in the toolkit's text caches.
  `fixtures/i18n-taskbar.inc`: the meter model's localized text on the production catalogs: the
  quota keys and the headline countdown's forms and width reserve in zh-Hans, ru and the
  pseudo-locale, the preview strip drawing them (the matrix's localized key reserve), an account
  error's text per language with its English log rendering, and the Accounts page's state for
  the coded error.
  `fixtures/controls.inc`: the
  toolkit controls the history window builds on (segment geometry, hits and keys, ellipsis
  trimming, the navigation pane's stops, scrollbar and scroll-into-view, the leading switch,
  status pills, info bar severities with a button). `fixtures/tooltip.inc`: the readout layout
  search (forms, columns, widest titles, the "+N more" hint measured on its own, nothing below
  one row), the pointer-excluding placement in adversarial geometries (centre, corners, edges, a
  readout larger than every region, a negative origin, a cursor partly outside, a large image off
  its hotspot, 144 DPI, side order), complete-text blocks, the tooltip's items, content and
  placement dedupe, and the chart item's hits, stop and scroll keys. The edit controller
  (`settings/edits.h`) runs against `TestStore` (`fixtures/edits.inc`): an in-memory store with
  an operation log, deterministic save failures and unload after N transactions; number parsing
  and the render projection get their own fixtures. `fixtures/accounts.inc`: the controller's
  account edits and actions against the same store (keys and slots, label validation, drafts
  committed before an account action and an invalid label blocking it, removal invalidation,
  removal's sign-in and history deletion with a kept history's notice, the
  Add account label choice skipping stored sign-ins, an added account's sign-in waiting for its
  naming (label errors, failed saves, page changes, card actions, Sign in, removal), desired
  visibility and moves, vanished
  accounts) and the Accounts page (cards with their logos' brand colors, header controls, expanded rows per provider, empty
  state, a lifted dragged card, the reorder target, a drag's in-place card moves matching a rebuild
  of nine accounts). `fixtures/layout.inc`: slider input, the
  Layout fields and reset group, the projection the preview reads, the preview strip model
  (`settings/preview.h`: column geometry, the bar columns' tile box and hover fill, each identity
  form (label, badged logo, logo) at every identity position in Stacked and Vertical with its box
  and gap (the badge's overhang, the stale mark per form, no quotas, a logo above or below the
  bars keeping a label's distance at two label sizes), the logo size per style and the outlined
  badge's size, offsets (the logo staying centred across its bars) and color with its contrasting
  outline, the logos' brand colors per provider, pace marks,
  bar styles with their shaded fills and tick overhang, the time track, ring tiles' model shapes,
  gauge, clock pie and rounded square faces, sparkline charts (side by side per quota, shapes in
  drawing order, strokes, the key and the weekly chart's extra usage corner moved clear of the
  reading, the corner clipped, the now line and caret yielding to text, each current position cue
  and the outline on and off, the text size; painted strokes), quota matrix cells, headline fills and the reset
  countdown,
  the theme-aware pace color, overflow, extreme settings; the mock taskbar's pieces (Start, apps,
  chevron, network, clock and bell, Show desktop) and every Position's slot order, overflow,
  scroll range and home, with hover matching paint), the
  Layout page per tile style, its Position dropdown and fallback note, and the pinned/unpinned
  preview header. `fixtures/display.inc`: the
  Display fields and reset group (with every scalar setting in exactly one page reset), the
  Material dropdown's saves and its transparency and Tinted wallpaper notes, the usage palette
  and Custom's color cards (swatches, Custom... per band, dots, wording, dark text), threshold
  ordering in the projection and after a save (the test store's `normalize` hook), the Display
  page per tile style and what its settings change in the preview strip.
- `extract-status`, `status-compile`, `status`: `fetch/status-feeds.h` (with `core/model.h`, the
  JSON accessors, `Utf8ToWide`, `ParseIso8601Ms` and its epoch constant) in its own executable
  (`fixtures/status-main.inc`, `fixtures/status.inc`, header `status.hpp`), given
  `fixtures/status/` as its argument: the captured pages (Claude's and GitHub's Statuspage
  summaries, OpenAI's incident.io feed and OpenCode's status API, all operational, and Supabase's
  incident.io feed with a recovered incident still in monitoring, update texts removed; read
  2026-11-05) parsed as is and with one entry replaced (each level, incidents and maintenances
  with their titles and starts, HashiCorp's live incident.io incident moved onto a Codex
  component, unwatched components, unknown statuses, strict dates, untrusted pages), the
  two-read confirmation and the reading's age. No network.
- `render`: the same executable with `--render-dir` paints the settings client area (1000 x 820
  DIP, the mockups' window without its caption) into WIC bitmaps: About, Accounts (collapsed
  cards), Behavior, History, Layout (every tile style; stacked and ring at every DPI) and Display
  (segmented ring, as its mockup) at 96/144/192 DPI in light, dark and high contrast (Aquatic
  colors), plus interaction-state sheets (`render-states-*`, `render-controls-*`,
  `render-history-off-*`, `render-layout-states-*`, `render-layout-identity-*` (the Identity and
  text section with its link to Accounts), `render-layout-badge-*` (the logo and badge cards, a
  badge color swatch focused), `render-display-states-*`,
  `render-accounts-states-*`, `render-accounts-drag-*` with brand-colored logos), Accounts as its mockups
  (`render-accounts-expanded-*`, `render-accounts-add-*` with the menu painted where the host
  opens it, `render-accounts-empty-*`), Display at the mockup's full height
  (`render-display-full-*`, `render-display-bars-*`, `render-display-custom-*` with the Custom
  palette's color cards), the preview's overflow, scrolled and
  unpinned (`render-layout-overflow-*`, `-unpinned-*`), every Position on the preview strip
  (`render-placement-sheet-*`), the Layout page's Position with its fallback note
  (`render-layout-placement-*`), preview strip sheets with one captioned
  row per layout and option (`render-preview-sheet-*`) or per Display option
  (`render-display-preview-sheet-*`; 1:1 at 100/125/150%) and the dropdown popup's rows
  (`render-popup-*`), and the localization review's Behavior and History pages, whole, in
  zh-Hans and the pseudo-locale (`render-i18n-<page>-<tag>-light-96/144`), with a preview strip
  sheet of the taskbar's localized text (keys, the headline's countdown, chart and matrix keys;
  `render-i18n-preview-sheet-<tag>-light-96/144`). `render-backdrop-*`
  (Accounts, Layout, Display) and
  `render-history-backdrop-*` (light and dark, 144 DPI) draw the windows over a stand-in backdrop
  with grayscale text, as the host does when DWM draws one. Client-drawn previews use the
  production painter. The history window's
  controls sheet (`render-controls-history-*`, every theme at 96/144/192 DPI,
  `fixtures/render-controls.inc`): two scrolled navigation panes through the production pane
  painter (logos, some brand-colored, usage-colored values, a cut label, selected, disabled, the Removed group,
  focus, hover, thin and expanded scrollbars), then segmented controls (hover, focus, pressed,
  disabled), leading switches, pills, a cut label, info bar severities with buttons and an
  icon-only menu button holding its one-line menu open (glyph rows, a separator). PNGs for
  review, no pixel goldens. Shared render support (themes, WIC targets, PNG output):
  `fixtures/render-support.inc`.
- `extract-history`, `history-compile`, `history`, `history-render`: the history window's own
  executable (`history.cpp`, `fixtures/history-main.inc`) on `core/model.h`, the meter model,
  logos, the pure toolkit, `history/types.h`, `continuity.h`, `series.h`, `csv.h`, `view.h`,
  `chart.h`, `controller.h`, `chart-paint.h`, `content.h` and `shell.h`. `history` runs
  `fixtures/history-view.inc` (compact
  formats in synthetic time zones, number rounding and abbreviation with carry, hours; summary
  cards and No readings entries built through `HistoryCollectAccounts` and `HistoryBuildSeries`
  from synthetic readings, as the window's controller does), `fixtures/history-controller.inc`
  (the controller against `TestHistoryOps`: in-memory request, journal with its stamp and peek
  contract, settings with polls, clock, unload, posted refresh messages, the deferral timer and
  the consent transaction, with an operation log; open requests before and after the window is
  published, repeated and unlisted requests, removed accounts, per-mode views, the settings'
  default range on open, the range under week comparison, close and unload with a request pending, and the view it fills; orchestration
  only, never the production ops or the opener-to-window bridge),
  `fixtures/history-refresh.inc` (live refresh: one post per batch of notifications, the live and
  sample parts by the journal's stamp, empty versus no replacement, status-only changes, failed
  storage initialization, pruning, widened ranges, modal, popup and inspection deferral (pointer
  or readout, an unreported leave ended by the timer, a chart scrolled under a still pointer) with
  their resume paths and the no-stranding invariant after every event, explicit loads clearing the
  work, raw rows rebuilt with a replacement while the laid-out chart keeps its own snapshot,
  unload during collection changing nothing;
  Enable recording's success, failure notice, and unload or close during it),
  `fixtures/history-operations.inc` (the test ops' dialogs, write, Clear and folder: Export's scope
  frozen across an activation, a poll and clicks delivered inside the large-export warning and the
  save dialog, the write posted with its progress shown, progress reports changing only the row
  count, cancellations, failed dialogs and writes, the close latch or unload inside each dialog and
  during the write; Clear's success, failure (curves dropped either way, no load), cancel and
  waiting loads; a removed account's Delete history (confirmation naming it, success leaving the
  list, failure keeping it selected, cancel, the close latch or unload, waiting loads; configured
  or unlisted accounts start nothing); About these charts; one operation at a time with only the
  legend acting) and
  `fixtures/history-frame.inc` (the frame per
  state: navigation entries and their ids per history ID, values and inks, logo brand fills, the Removed group;
  header title, status, pill, icon buttons, the narrow rule; segments, Compare disabling range;
  info bars (a removed account's with Delete history); the chart's card (legend chips with their ids, actions and tooltips, wrapping; the
  key line; the chart item) and the empty states; card grid and contents; one-line menu rows
  with separators and the More menu; an icon-only button's centered glyph, measured in pixels;
  the icon buttons' tooltips; an export's progress in the status slot with every control but the
  chips disabled), `fixtures/history-tooltip.inc` (the single-account readout guarantee: the widest
  mandatory parts the production readout rows print, then ten such rows placed at every pointer
  position of an 800 x 590 DIP work area with 32 and 256 DIP pointers at 96 and 144 DPI, again in
  zh-Hans and the pseudo-locale, where only the pointer's clearance is checked and the rows placed
  are reported; complete-text tooltips placed whole there; `OBSERVE` lines report widths and
  capacities), `fixtures/i18n-history.inc` (compact dates and chart ticks per language, the
  status pill's plural, and the controller's language switch: status, cards, ticks and readout
  re-labeled, a modal keeping its language) and
  `fixtures/history-chart.inc` (the chart through the controller on synthetic journals in the
  mockups' shapes: axes, the plot's layout on device pixels, local time ticks per range, across
  daylight saving and over ten years; picking (every nearby or coincident line in both periods at
  a recorded endpoint, breaks never picked, extremes kept within a pixel) and readout rows;
  emphasis; the legend's filter; the scene's draw list; the Remaining bar mode's shares left; the
  reduction cache's key). `history-render` paints `render-history-*` (every theme at 96/144/192
  DPI, `fixtures/history-render.inc`) as the host composes the window (main surface, navigation
  pane at its scroll, viewport, scrollbar, the More menu and a chart's readout where the host
  shows them; the chart and its chips by the production painter, laid out by a controller over
  a synthetic journal), modeled on the approved phase-3 mockups: `default` (s-light), `account`
  (s-account, plus a removed account's Delete history bar), `compare`, `states` (recording off, Enable recording's failure notice, storage
  failing, waiting with focus and hover, an export's progress, also at the minimum width),
  `notices` (each operation's notice), `empty` (loading, no accounts, unsupported view), `empty-readings` (no readings with brand-colored logos,
  dollars only, the No readings card beside cards), `menu`, `min` (the minimum window scrolled),
  `nav` (the scrolled sidebar, brand-colored logos), `band` (above 100%), `custom-colors` (sidebar values in a
  Custom usage palette), `chart-views` (every view, both extra axes),
  `chart-ticks` (each range's ticks) and `chart-states` (comparing, above 100%, the Remaining bar
  mode with its cards, every line hidden, chips off, hot and focused, chart focus, a readout on the chart). `render-history-tooltips-*`
  (same themes and DPIs): the readout placed on each side of the pointer by the production
  placement, row forms, a shortened name, the production time forms (a previous year's, this
  year's), two columns, the "+N more" hint, and the complete-text tooltips (a chip's, the icon
  buttons'); also in zh-Hans and the pseudo-locale, light at 96 and 144 DPI
  (`render-history-tooltips-<tag>-light-*`), with `render-history-i18n-<tag>-light-*`: the
  default view, the 30-day and All retained ticks and a readout on the chart as the controller
  composes them in that language.
- `host-compile`, `probe-compile`, `host-probe`: the toolkit with its native host
  (`ui/system-theme.h`, `surface.h`, `window-icon.h`, `popup.h`, `text-box.h`, `host.h`,
  `color-dialog.h`), the history window's dialog scope (`history/modal.h`) and a stub client with
  window icons form `ui-host.dll`; `host-probe.cpp`
  (hand-written) loads it, opens the window on the DLL's worker thread, closes it from another
  thread, joins and unloads, repeatedly, and checks classes, windows and GDI/USER handle counts;
  then client creation failure, a conflicting class registration, render-target loss through the
  production discard path, the window icons (both sizes at the window's DPI, redrawn at the new
  sizes on synthetic DPI changes and on theme notifications sent to the window only, the replaced
  ones destroyed and no handle growth, destroyed after close), and an unload close during a
  message box. Handle counts are process-wide: a window case reads them on the window thread once
  its pending messages and paints are handled (`kHandles`), never from the driver while the
  window may still be painting or redrawing; the icons case logs each step's counts (`STEP`
  lines; `ANOMALY` with the process's windows if a settled count moves) and what a driver-thread
  read catches mid-redraw. On a hidden desktop: the production
  popup (owned, never active, capture, close triggers, outside press swallowed until release,
  keys and clicks choosing), the number
  box EDIT (kept across rebuilds, text/Enter/Esc/arrow/Tab events, focus loss then rebuild), a
  slider (press jump and focus, drag values, release and capture loss ending the drag, keys; live
  rebuilds once per frame repainting the main window only when named, values compared with the
  last report, the viewport painted in the main window's cycle), a preview strip (drag and
  sideways wheel pan, no focus taken), a drag handle (owner focus, drag threshold, drop, Esc and
  capture loss cancelling, Alt+Up/Down; later drags moving items in place without a rebuild,
  repainting only the viewport, whose paint moves the text box; a declined close cancelling the
  drag first; the auto-scroll timer ticking while the pointer moves in the edge zone, and moves
  there scrolling on time with the timer starved), a free text
  box (limit, Up/Down native) and a menu button (keys open and choose), the color dialog
  (`ui/color-dialog.h`) cancelled by a user close and by unload's close, and unload with the popup
  open, with a focused EDIT, and during a message box over the popup. In the probe client's
  history mode (a navigation pane, segmented controls in the header): segment keys and clicks
  through the host (one stop, ends, the selected segment, a disabled control), keys the host
  doesn't use reaching the client from either surface (not the popup's, focus moves or control
  keys), the activation request reaching the client after the popup closed, theme messages
  rebuilding, and the pane's wheel routing, hits at its scroll, keyboard and selection
  scroll-into-view and scrollbar drag. Pointer state is read in the command that moved the
  pointer: the real cursor is elsewhere, so a leave follows. In the probe client's tooltip mode
  (a chart in the content, an icon button with a tooltip; its moves drop that leave, so the
  pointer stays): chart hover reports without focus, repaint scope, focus on press, scroll keys;
  the readout owned, never active, without capture, clear of the pointer, `HTTRANSPARENT`, moved
  without new rows, hidden by every trigger (one by one), never with the popup open or the owner
  disabled; the delayed item tooltip and what cancels it; target loss; repeated shows without
  USER/GDI handle growth; unload with the readout showing (gone before the main window's
  `WM_NCDESTROY`); creation failure and retry; a conflicting class registration; thread message
  hooks count mouse and activation messages that reach the tooltip. The progress mode: a
  client-drawn main item repainted from inside a handler through `UiHostPaintNow` (found by id
  each time, no rebuild, a lost target recreated, nothing after a fault). The dialog mode (the
  client latches and cancels as the history client does): message boxes in the production scope
  ended by unload's close from the driver thread (as `CloseHistoryWindow`), a Yes/No box inside
  one (Cancel alone ignored, observed; No through the scope), a box activated after the latch, a
  user close during an OK box, scopes refused once cancelled or nested; last, the save dialog as
  an observation. Off the hidden desktop: the cursor rectangle of the real cursor (read only)
  without GDI handle growth.
- `extract-popup`, `popup-compile`, `popup-probe`: the same host as the owner of a probe popup
  (owned, `WS_EX_NOACTIVATE`, shown without activation; `fixtures/popup-*.inc`, hand-written).
  Thread-local message hooks record what each window receives while capture is on the popup, on
  the viewport (the dropdown field's surface), or nowhere, for: opening, `WM_CANCELMODE` to the
  main window or the popup, another window taking capture, release, focus moving to an EDIT,
  owner deactivation, minimize/restore, a disabled owner, a message box and destruction. Prints
  `OBSERVE` lines and checks the native facts the step-4 popup design was derived from (a failure
  means revisiting the design, not the probe).

## Outputs and side effects

Each run creates a retained `.out/run-.../` with the extracted `settings.cpp`, `history.cpp`,
`status.cpp`, `ui-host.cpp` and `popup.cpp` (plus `.manifest.json` section boundaries and hashes), executables, the DLL,
`render-*.png`, per-phase logs and `results.json`. The tool never prunes old runs. The host probe
shows off-screen, never-activated windows and briefly a message box, and reads the user's theme
(registry, `UISettings`); its popup, text box, slider, preview, chart, tooltip and color dialog
cases (each on its own) and the popup probe run on hidden desktops they create and never switch to, so their
windows, focus, capture, tooltips and dialogs never reach the user's screen, focus or pointer. The
interactive modes are the exception: an activated window on the user's desktop, and F8/F9 (the
popup probe also F10) registered as global hotkeys until it exits. No storage, network,
credential or Explorer access.

## Success and evidence limits

Exit 0 requires every phase to pass: tooling self-tests, freshness and the mod check (default
source), the extractions, compiles, exactly one successful `RESULT` summary from `model`, `render`, `history`,
`history-render`, `status`, `host-probe` and `popup-probe`, every reported render present as a PNG, and
source and harness unchanged during the run. A pass covers extracted logic, offscreen rendering and the host's
window/thread/teardown contract in a test process (the controller against a test store, the
popup and EDITs against a stub client); Explorer, the settings thread's real client and store,
DWM caption colors, how the title bar and Alt+Tab show the window icon, live theme switches and
multi-monitor DPI need the runtime check in the root guide. The history controller fills
its view from in-memory ops: the production ops (journal reads and the peek, settings and poll
reads, the consent transaction, the refresh message and deferral timer), the opener, the window
client (its notification, inspection and popup inputs) and its activation path are outside this
suite; `tests/history` covers the production peek's contract. The popup probe's automated part covers thread-local activation and programmatic events
only; real clicks on other apps or the taskbar, Alt+Tab, Start and wheel routing need the
interactive mode, and neither runs inside Explorer (where the taskbar shares the process). The
same holds for the tooltip: on the hidden desktop the cursor image isn't readable (the fallback
square stands in) and `WindowFromPoint` is only an observation; pointer routing through the
readout, real cursor sizes, multi-monitor DPI and z-order next to the taskbar need
`--tooltip-interactive` and Explorer.

## Maintenance rules

- `extract.py` takes whole fragments between `// src/<path>` marker lines of the generated file,
  plus named definitions and one-line constants; it refuses missing or ambiguous boundaries and
  source changes. Model- and history-target fragments must stay pure (no XAML, WinRT, window,
  registry, storage or global state); the status target's likewise, except Windows.Data.Json: fix
  impurities in `src/`, never patch extracted code. The only stub allowed is `Wh_Log`, in the host
  target.
- `fixtures/status/` holds captured public status pages only (no account data); replace a capture
  rather than edit it, and keep expected values read off it by hand.
- Never extract the production store, storage, auth, account commands, settings client, history
  journal, history ops (`history/ops.h`) or history window code (`history/ui-window.h`,
  `window.h`).
- Fixtures live by domain under `fixtures/`: synthetic inputs with hand-derived expected values,
  not copies of production rules.
- Keep generated artifacts under `.out/`.
