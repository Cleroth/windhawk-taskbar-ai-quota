# Custom-drawn window toolkit

Direct2D 1.0 + DirectWrite windows for the settings and usage history threads. Never XAML; never
a taskbar thread. Mod-wide rules: [../AGENTS.md](../AGENTS.md).

## Pure vs native

- Pure (`tokens.h`, `draw.h`, `svg-path.h`, `rich-text.h`, `items.h`, `paint.h`, `input.h`,
  `tooltip-layout.h`, the settings page/shell builders and the history window's `history/view.h`,
  `chart.h`, `controller.h`, `chart-paint.h`, `content.h`, `shell.h`): explicit inputs only; no
  HWND, registry, WinRT, globals or `Wh_Log`. `tests/settings` extracts and renders them; fix
  impurities here, never in tests.
- Native (`system-theme.h`, `surface.h`, `window-icon.h`, `popup.h`, `tooltip.h`, `text-box.h`,
  `host.h`, `color-dialog.h`): window, registry and WinRT calls; covered by the `tests/settings`
  native-host probe DLL, never by the render harness. `popup.h`, `tooltip.h` and `text-box.h`
  hold component state and mechanics; their window procedures live in `host.h`, which routes
  everything to them.
- Client-drawn items (`Preview`, a `Gallery` card's thumbnail, `Chart`, `Custom`) paint through
  `UiHostClient::PaintCustom` with the target in item coordinates and the surface's paint state
  (hot, pressed, focus); the windows' painters are pure (`settings/preview.h`,
  `history/chart-paint.h`) and the render harness calls them directly. Navigation pane items are
  never client-drawn.
- Colors a client derives from the theme (`UiRun::color`, a usage color) are built content: a
  theme change rebuilds the frame. High contrast ignores them.

## Lifetime and teardown

- `UiHostRun` owns one scope with every D2D/DWrite/WinRT object (factories, surfaces, caches,
  `UISettings`); the window is destroyed inside it on every path, and it ends before the caller
  uninitializes the apartment. Never keep these objects in globals.
- Host state outlives the windows: parent `WM_DESTROY` precedes child destruction, main
  `WM_NCDESTROY` comes last and posts quit. Each surface releases its target on its window's
  `WM_NCDESTROY`. The tooltip window and its hover timer die in the main window's `WM_DESTROY`.
- Target-bound resources (brushes, layouts with drawing effects) live in the surface's
  `UiTargetCache`, never shared between targets. Factory-bound ones (formats, effect-free
  layouts, geometries) live in `UiGraphics`.
- Target creation: hardware, then software once, else fault close. `Resize` failure or
  `D2DERR_RECREATE_TARGET`: discard target and cache, repaint.
- Window icons (`UiHostConfig::icon`): drawn by the host at the window's DPI and in its palette,
  redrawn on DPI and theme changes (each replaced icon destroyed after its successor is set),
  destroyed in `UiHostRun` after the window, never while it can still show them.
- Window classes are host-specific (`AiQuotaSettingsUi*_<mod id>`, `AiQuotaHistoryUi*_<mod id>`);
  another host uses its own.

## Reentrancy and close paths

- Every window procedure runs inside the host's try/catch: an exception logs and fault-closes
  (no saving; tracked modals are cancelled and the window is destroyed after they unwind).
  `catch (...)` doesn't make dangling references safe.
- Copy an item's action before any call that can pump messages, rebuild or show a modal
  (`SetFocus`, message boxes, client calls). Structural rebuilds are requested with
  `UiHostRequestRebuild` and run when the outermost dispatch unwinds.
- `WM_CLOSE`: cancel a tracked modal (message box, `UiChooseColor`'s dialog) or a handle drag
  (DragCancel, capture released) and retry; fault and unload close without saving; a deliberate
  close asks the client (`PrepareClose`). Nothing commits once `WM_DESTROY` starts.
- A handler that keeps the thread busy (the history window's CSV write) shows progress only
  through `UiHostPaintNow`: a main-surface item found by id each time and painted nested (never
  a rebuild; a lost target is recreated), nothing once closing or faulted. Keep no item
  reference across it.

## Input

- Focus owner is the focused HWND (main, viewport or a number box's EDIT); no `IsDialogMessage`.
  Tab follows item order across surfaces (navigation pane, main, content); the focus ring shows
  only after keyboard input.
- Interactive controls keep fixed item ids across rebuilds: focus and EDITs follow ids.
- `WM_KEYDOWN` that neither surface uses (no focus move, scroll, activation, popup, drag or
  control key) goes to `UiHostClient::Message` before default handling: window shortcuts (F5,
  Esc) live in the client. The activation request reaches the client after the host closed the
  popup and brought the window (or its modal) back.
- Navigation pane (`UiFrame::nav`, `navPane`, `navHeight`; surface `Nav`): main-surface items in
  main coordinates at offset 0, clipped to the pane, with its own pixel-aligned scroll and thin
  scrollbar (widened under the pointer, thumb drag, page on the track). Inside the pane the
  pointer hits only its items, at its scroll; the vertical wheel over it scrolls it, elsewhere in
  the page column the content. Keyboard focus scrolls an entry into view (6 DIP margin), and so
  does a rebuild whose selected entry changed; its scroll closes the popup and re-targets the
  pointer, like the content's.
- Segmented control: one tab stop; the pointer targets each segment (`UiFocusRef::link`), a press
  focuses the control as a whole; Left/Right (stopping at the ends) and Home/End select; the
  selected segment's activation reports nothing.
- Dropdown popup: owned, `WS_EX_NOACTIVATE`, shown without activation, holding capture; keyboard
  focus stays on the field, whose keys drive it. `UiPopupClose` is the only close routine
  (closed first, capture released only if still held, never restores focus or foreground).
  Every trigger closes it: capture loss, `WM_CANCELMODE` (popup or main), deactivation, focus
  leaving the field, `WM_SYSCOMMAND`/Alt (before default handling), size/move/minimize/DPI,
  scroll, rebuild, Esc/Tab, a client modal (`UiHostClosePopup` first), close. An outside press
  hides it but keeps capture until its button-up. These rest on `tests/settings` popup probe
  facts; re-run them when changing this.
- Number and text box EDITs: one per item id, text set only when the client's text changed and no
  IME composition runs (composition: native handling only). They report text changes, Enter, Esc,
  Up/Down (number boxes only) and focus moving elsewhere in the window as actions; focus leaving
  the window (deactivation, a modal) reports nothing, so a half-typed value never commits on its
  own. Never commit inside the focus-loss notification. A text box's limit is its item's maximum,
  its placeholder the native cue banner.
- A Button with options is a menu button: activating it opens the dropdown popup (same rules),
  right-aligned under it. Options with glyphs make one-line menu rows; a separator belongs to
  the row under it (`UiOption::separator`), never a row of its own, so the popup's keys and
  pointer only ever reach choosable rows.
- Drag handles (not tab stops): a press focuses the handle's owner; past 4 DIP the drag reports
  Drag (the pointer's content y), then Drop on release, or DragCancel on Esc, capture loss or
  `WM_CANCELMODE`; near the viewport's top or bottom it scrolls a step every 30 ms: a viewport
  timer (started on entering that zone, never reset within it) steps a still pointer, the drag's
  reports step a moving one (input and paints outrank `WM_TIMER`). Alt+Up/Down on the owner Step
  the handle. Clients change nothing persistent before Drop.
- Pointer-rate updates do no per-move rebuild: a handle's Drag moves built items with
  `UiHostMoveContent` (text boxes follow at the viewport's paint), a slider's Slide requests
  `UiHostRequestLiveRebuild` (once per frame before the paint, the main window repainted only when
  named), a chart's hover repaints only its surface and only when the client says its emphasis
  changed, and a leave during a press (repeated on every move outside the window) repaints only
  its surface. The viewport paints in the main window's paint cycle. Each present blocks on the
  display and input outranks `WM_PAINT`: per-move main window invalidations (painted before the
  child) or slow per-move work would starve the viewport while the pointer moves.
- Charts (`Chart`): hot-tracked and a tab stop without an action, never activated (Enter and the
  release do nothing); a press focuses them. Each pointer move that moved reports to
  `UiHostClient::ChartHover` with the point, an ended hover (leaving it, a press, any tooltip hide
  trigger, a rebuild before the client builds) with none. The client keeps its pick: `readout`
  arrives with the shown readout's key, and an unchanged key keeps the shown rows. While focused,
  Up/Down scroll the content a line (Page Up/Down and Home/End as everywhere).
- `Custom` items are decoration, unless they carry an action: then a control the client draws
  whole from the paint state (the history window's legend chips), hot, pressed, a tab stop,
  activated and with its tooltip like a button; the toolkit adds only its focus ring, rounded at
  its ends. A disabled one blocks the pointer like any disabled control.
- Tooltip (`ui/tooltip.h`, layout and placement in `ui/tooltip-layout.h`; `UiHostConfig::
  tooltipClass`, none: no tooltips or readouts): one owned popup, `WS_EX_NOACTIVATE |
  WS_EX_TOOLWINDOW`, `CS_DROPSHADOW`, shown with `SWP_NOACTIVATE`, never capture or focus;
  `WM_NCHITTEST` answers `HTTRANSPARENT` (the pointer reaches the window under it on this thread),
  `WM_MOUSEACTIVATE` `MA_NOACTIVATE`, any mouse message that still arrives hides it. Its own
  surface at its monitor's DPI, target-bound resources in its surface cache (inked layouts cleared
  with each new content). A chart's readout shows at once; an item's `tooltip` (complete text,
  wrapped, never cut) after the double-click time (the main window's timer `0x7E02`; client
  timers use other ids), only if its item is still hot in the same frame generation. Neither shows during a press, with the popup open or with the owner disabled.
  Placement (`UiTooltipPlacement`) keeps clear of the cursor image and hotspot 8 DIP out and is
  never clamped afterwards; nothing that fits shows nothing. The same content at the same
  pointer, cursor image and work area does nothing; a moved pointer moves it (no new rows).
  `UiHostHideTooltip` (it ends a chart's hover) runs on every trigger: leave from the pointer's
  surface, any press, key (also in text boxes), wheel, scroll, rebuild, popup open, a disabled
  owner, Alt, theme, and every popup close trigger (`UiHostClosePopup` hides it first: cancel
  mode, deactivation, focus loss, size/move/minimize/hide, `WM_SYSCOMMAND`, activation request,
  DPI, client modals, close). `UiTooltipHide` is the one idempotent hide: no focus or foreground
  change, the hover timer killed, the content forgotten. Creation or drawing failure: hidden,
  retried on the next show; class registration failure: no tooltips. These rest on
  `tests/settings` host-probe tooltip cases; pointer routing on its `--tooltip-interactive` mode.
- Hit testing: an enabled item without an action is decoration (the pointer passes through; a
  chart needs none); a disabled interactive one blocks.
- Sliders: a press focuses and jumps (unless it grabs the thumb, which hit-tests beyond the
  track's ends), drags report `Slide` values (each differing from the previous report, not from
  the possibly older frame), the release, a capture loss or `WM_CANCELMODE`
  reports `SlideEnd`; arrows, Page Up/Down and Home/End `Step`. Clients draft on `Slide` and queue
  the commit on `SlideEnd`, also when a modal's capture loss caused it (the queue runs after it).
- Preview strips: drag and the sideways wheel (or Shift+wheel) report `Pan`; never a tab stop and
  a press doesn't move focus.

## Evidence

- `PrintWindow` captures of D2D windows are unreliable (black): never use them as evidence.
  Render evidence comes from WIC targets in `tests/settings`.
