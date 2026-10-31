# Custom-drawn window toolkit

Direct2D 1.0 + DirectWrite windows for the settings thread (and the history thread later). Never
XAML; never a taskbar thread. Mod-wide rules: [../AGENTS.md](../AGENTS.md).

## Pure vs native

- Pure (`tokens.h`, `draw.h`, `svg-path.h`, `rich-text.h`, `items.h`, `paint.h`, `input.h`, and
  the settings page/shell builders): explicit inputs only; no HWND, registry, WinRT, globals or
  `Wh_Log`. `tests/settings` extracts and renders them; fix impurities here, never in tests.
- Native (`system-theme.h`, `surface.h`, `popup.h`, `text-box.h`, `host.h`, `color-dialog.h`):
  window, registry and WinRT calls; covered by the `tests/settings` native-host probe DLL, never by
  the render harness. `popup.h` and `text-box.h` hold component state and mechanics; their window
  procedures live in `host.h`, which routes everything to them.
- Client-drawn items (`Preview`, a `Gallery` card's thumbnail) paint through
  `UiHostClient::PaintCustom` with the target in item coordinates; the settings window's painter is
  pure (`settings/preview.h`) and the render harness calls it directly.

## Lifetime and teardown

- `UiHostRun` owns one scope with every D2D/DWrite/WinRT object (factories, surfaces, caches,
  `UISettings`); the window is destroyed inside it on every path, and it ends before the caller
  uninitializes the apartment. Never keep these objects in globals.
- Host state outlives the windows: parent `WM_DESTROY` precedes child destruction, main
  `WM_NCDESTROY` comes last and posts quit. Each surface releases its target on its window's
  `WM_NCDESTROY`.
- Target-bound resources (brushes, layouts with drawing effects) live in the surface's
  `UiTargetCache`, never shared between targets. Factory-bound ones (formats, effect-free
  layouts, geometries) live in `UiGraphics`.
- Target creation: hardware, then software once, else fault close. `Resize` failure or
  `D2DERR_RECREATE_TARGET`: discard target and cache, repaint.
- Window classes are host-specific (`AiQuotaSettingsUi*_<mod id>`); another host uses its own.

## Reentrancy and close paths

- Every window procedure runs inside the host's try/catch: an exception logs and fault-closes
  (no saving; tracked modals are cancelled and the window is destroyed after they unwind).
  `catch (...)` doesn't make dangling references safe.
- Copy an item's action before any call that can pump messages, rebuild or show a modal
  (`SetFocus`, message boxes, client calls). Structural rebuilds are requested with
  `UiHostRequestRebuild` and run when the outermost dispatch unwinds.
- `WM_CLOSE`: cancel a tracked modal (message box, `UiChooseColor`'s dialog) and retry; fault and
  unload close without saving; a deliberate close asks the client (`PrepareClose`). Nothing
  commits once `WM_DESTROY` starts.

## Input

- Focus owner is the focused HWND (main, viewport or a number box's EDIT); no `IsDialogMessage`.
  Tab follows item order across surfaces; the focus ring shows only after keyboard input.
- Interactive controls keep fixed item ids across rebuilds: focus and EDITs follow ids.
- Dropdown popup: owned, `WS_EX_NOACTIVATE`, shown without activation, holding capture; keyboard
  focus stays on the field, whose keys drive it. `UiPopupClose` is the only close routine
  (closed first, capture released only if still held, never restores focus or foreground).
  Every trigger closes it: capture loss, `WM_CANCELMODE` (popup or main), deactivation, focus
  leaving the field, `WM_SYSCOMMAND`/Alt (before default handling), size/move/minimize/DPI,
  scroll, rebuild, Esc/Tab, a client modal (`UiHostClosePopup` first), close. An outside press
  hides it but keeps capture until its button-up. These rest on `tests/settings` popup probe
  facts; re-run them when changing this.
- Number box EDITs: one per item id, text set only when the client's text changed and no IME
  composition runs (composition: native handling only). They report text changes, Enter, Esc,
  Up/Down and focus moving elsewhere in the window as actions; focus leaving the window
  (deactivation, a modal) reports nothing, so a half-typed value never commits on its own. Never
  commit inside the focus-loss notification.
- Sliders: a press focuses and jumps (unless it grabs the thumb, which hit-tests beyond the
  track's ends), drags report `Slide` values, the release, a capture loss or `WM_CANCELMODE`
  reports `SlideEnd`; arrows, Page Up/Down and Home/End `Step`. Clients draft on `Slide` and queue
  the commit on `SlideEnd`, also when a modal's capture loss caused it (the queue runs after it).
- Preview strips: drag and the sideways wheel (or Shift+wheel) report `Pan`; never a tab stop and
  a press doesn't move focus.

## Evidence

- `PrintWindow` captures of D2D windows are unreliable (black): never use them as evidence.
  Render evidence comes from WIC targets in `tests/settings`.
