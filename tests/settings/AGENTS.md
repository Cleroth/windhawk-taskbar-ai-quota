# Settings verification

Source-extracted tests of the pure fragments behind both custom-drawn windows (meter model,
`src/ui/` toolkit, the settings edit controller, shell and page builders; the usage history
window's view, controller and frame builders on the shared history model), offscreen renders of
both windows, a native-host probe of the toolkit's window host (with its dropdown popup and
number box text boxes), and a capture/foreground probe for the dropdown popup. Not a Windhawk,
Explorer or XAML test. The history domain (journal, series, continuity, burn-up, the classic window) stays in
`tests/history`.

## Run

From the repo root:

```text
python tests/settings/run.py
python tests/settings/run.py --popup-interactive
```

`--popup-interactive` builds only the popup probe, then runs its interactive mode by hand,
attached to the console: a probe window opens on your desktop; follow the printed steps (open the
dropdown, do the action, F9 ends the step, F8 skips it, F10 is the activate request). The log is
`popup-interactive.log` in the run directory.

Prerequisites: Windows x64, Python 3.10+, PowerShell 7 (`pwsh`), Windhawk's bundled clang++ with
its MinGW headers/libraries, and an interactive desktop session (the probe creates windows). No
Python packages or installs.

Options: `--source PATH` (default: the repo-root generated file), `--compiler PATH` (default:
`C:\Program Files\Windhawk\Compiler\bin\clang++.exe`), `--output DIRECTORY` (default:
`tests/settings/.out/`; the parent for a new run directory). With the default source, a
`freshness` phase runs `pwsh generate-mod.ps1 -Check` first and fails on a stale file; it never
regenerates.

## Phases

- `model`: pure fixtures (meter model, logos, toolkit palette/input/focus/hit testing/scroll,
  controls, popup rows/placement/keys, settings shell and pages). `fixtures/controls.inc`: the
  toolkit controls the history window builds on (segment geometry, hits and keys, ellipsis
  trimming, the navigation pane's stops, scrollbar and scroll-into-view, the leading switch,
  status pills, info bar severities with a button). The edit controller
  (`settings/edits.h`) runs against `TestStore` (`fixtures/edits.inc`): an in-memory store with
  an operation log, deterministic save failures and unload after N transactions; number parsing
  and the render projection get their own fixtures. `fixtures/accounts.inc`: the controller's
  account edits and actions against the same store (keys and slots, label validation, drafts
  committed before an account action and an invalid label blocking it, removal invalidation, the
  Add account label choice skipping stored sign-ins, desired visibility and moves, vanished
  accounts) and the Accounts page (cards, header controls, expanded rows per provider, empty
  state, a lifted dragged card, the reorder target, a drag's in-place card moves matching a rebuild
  of nine accounts). `fixtures/layout.inc`: slider input, the
  Layout fields and reset group, the projection the preview reads, the preview strip model
  (`settings/preview.h`: column geometry, hover, pace marks, overflow, extreme settings), the
  Layout page per tile style and the pinned/unpinned preview header. `fixtures/display.inc`: the
  Display fields and reset group (with every scalar setting in exactly one page reset), threshold
  ordering in the projection and after a save (the test store's `normalize` hook), the Display
  page per tile style and what its settings change in the preview strip.
- `render`: the same executable with `--render-dir` paints the settings client area (1000 x 820
  DIP, the mockups' window without its caption) into WIC bitmaps: About, Accounts (collapsed
  cards), Behavior, History, Layout (every tile style; stacked and ring at every DPI) and Display
  (segmented ring, as its mockup) at 96/144/192 DPI in light, dark and high contrast (Aquatic
  colors), plus interaction-state sheets (`render-states-*`, `render-controls-*`,
  `render-history-off-*`, `render-layout-states-*`, `render-display-states-*`,
  `render-accounts-states-*`, `render-accounts-drag-*`), Accounts as its mockups
  (`render-accounts-expanded-*`, `render-accounts-add-*` with the menu painted where the host
  opens it, `render-accounts-empty-*`), Display at the mockup's full height
  (`render-display-full-*`, `render-display-bars-*`), the preview's overflow, scrolled and
  unpinned (`render-layout-overflow-*`, `-unpinned-*`), preview strip sheets with one captioned
  row per layout and option (`render-preview-sheet-*`) or per Display option
  (`render-display-preview-sheet-*`; 1:1 at 100/125/150%) and the dropdown popup's rows
  (`render-popup-*`). Client-drawn previews use the production painter. The history window's
  controls sheet (`render-controls-history-*`, every theme at 96/144/192 DPI,
  `fixtures/render-controls.inc`): two scrolled navigation panes through the production pane
  painter (logos, usage-colored values, a cut label, selected, disabled, the Removed group,
  focus, hover, thin and expanded scrollbars), then segmented controls (hover, focus, pressed,
  disabled), leading switches, pills, a cut label, info bar severities with buttons and an
  icon-only menu button holding its one-line menu open (glyph rows, a separator). PNGs for
  review, no pixel goldens. Shared render support (themes, WIC targets, PNG output):
  `fixtures/render-support.inc`.
- `extract-history`, `history-compile`, `history`, `history-render`: the history window's own
  executable (`history.cpp`, `fixtures/history-main.inc`) on `core/model.h`, the meter model,
  logos, the pure toolkit, `history/types.h`, `continuity.h`, `series.h`, `view.h`,
  `controller.h`, `content.h` and `shell.h`. `history` runs `fixtures/history-view.inc` (compact
  formats in synthetic time zones, number rounding and abbreviation with carry, hours; summary
  cards and No readings entries built through `HistoryCollectAccounts` and `HistoryBuildSeries`
  from synthetic readings, as the window's controller does), `fixtures/history-controller.inc`
  (the controller against `TestHistoryOps`: in-memory request, journal, settings with polls,
  clock and unload, with an operation log; open requests before and after the window is
  published, repeated and unlisted requests, removed accounts, per-mode views, the range under
  week comparison, close and unload with a request pending, and the view it fills; orchestration
  only, never the production ops or the opener-to-window bridge) and
  `fixtures/history-frame.inc` (the frame per
  state: navigation entries and their ids per history ID, values and inks, the Removed group;
  header title, status, pill, icon buttons, the narrow rule; segments, Compare disabling range;
  info bars; the chart's placeholder and empty states; card grid and contents; one-line menu
  rows with separators; an icon-only button's centered glyph, measured in pixels).
  `history-render` paints `render-history-*` (every theme at 96/144/192 DPI,
  `fixtures/history-render.inc`) as the host composes the window (main surface, navigation pane
  at its scroll, viewport, scrollbar, the More menu where the host opens it), modeled on the
  approved phase-3 mockups: `default` (s-light), `account` (s-account), `compare`, `states`
  (recording off, storage failing, waiting with focus and hover), `empty` (loading, no accounts,
  unsupported view), `empty-readings` (no readings, dollars only, the No readings card beside
  cards), `menu`, `min` (the minimum window scrolled), `nav` (the scrolled sidebar) and `band`
  (above 100%). The chart's card is an empty placeholder of its size.
- `host-compile`, `probe-compile`, `host-probe`: the toolkit with its native host
  (`ui/system-theme.h`, `surface.h`, `window-icon.h`, `popup.h`, `text-box.h`, `host.h`,
  `color-dialog.h`) and a stub client with window icons form `ui-host.dll`; `host-probe.cpp`
  (hand-written) loads it, opens the window on the DLL's worker thread, closes it from another
  thread, joins and unloads, repeatedly, and checks classes, windows and GDI/USER handle counts;
  then client creation failure, a conflicting class registration, render-target loss through the
  production discard path, the window icons (both sizes at the window's DPI, redrawn at the new
  sizes on synthetic DPI changes with the replaced ones destroyed and no handle growth, destroyed
  after close), and an unload close during a message box. On a hidden desktop: the production
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
  pointer: the real cursor is elsewhere, so a leave follows.
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
`ui-host.cpp` and `popup.cpp` (plus `.manifest.json` section boundaries and hashes), executables, the DLL,
`render-*.png`, per-phase logs and `results.json`. The tool never prunes old runs. The host probe
shows off-screen, never-activated windows and briefly a message box, and reads the user's theme
(registry, `UISettings`); its popup, text box, slider, preview and color dialog cases, like the
popup probe, run on a hidden desktop it creates and never switches to, so their windows, focus,
capture and dialogs never reach the user's screen, focus or pointer. The popup probe's interactive mode is the exception:
an activated window on the user's desktop, and F8/F9/F10 registered as global hotkeys until it
exits. No storage, network, credential or Explorer access.

## Success and evidence limits

Exit 0 requires every phase to pass: tooling self-tests, freshness (default source), the
extractions, compiles, exactly one successful `RESULT` summary from `model`, `render`, `history`,
`history-render`, `host-probe` and `popup-probe`, every reported render present as a PNG, and
source and harness unchanged during the run. A pass covers extracted logic, offscreen rendering and the host's
window/thread/teardown contract in a test process (the controller against a test store, the
popup and EDITs against a stub client); Explorer, the settings thread's real client and store,
DWM caption colors, how the title bar and Alt+Tab show the window icon, live theme switches and
multi-monitor DPI need the runtime check in the root guide. The history controller fills
its view from in-memory ops: the production ops (journal, settings and poll reads), the opener,
the window client and its activation path are outside this suite. The popup probe's automated part covers thread-local activation and programmatic events
only; real clicks on other apps or the taskbar, Alt+Tab, Start and wheel routing need the
interactive mode, and neither runs inside Explorer (where the taskbar shares the process).

## Maintenance rules

- `extract.py` takes whole fragments between `// src/<path>` marker lines of the generated file,
  plus named definitions; it refuses missing or ambiguous boundaries and source changes.
  Model- and history-target fragments must stay pure (no XAML, WinRT, window, registry, storage
  or global state): fix impurities in `src/`, never patch extracted code. The only stub allowed
  is `Wh_Log`, in the host target.
- Never extract the production store, storage, auth, account commands, settings client, history
  journal, history ops (`history/ops.h`) or history window code (`history/ui-window.h`,
  `window.h`).
- Fixtures live by domain under `fixtures/`: synthetic inputs with hand-derived expected values,
  not copies of production rules.
- Keep generated artifacts under `.out/`.
