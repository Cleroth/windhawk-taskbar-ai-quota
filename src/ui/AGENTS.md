# Custom-drawn window toolkit

Direct2D 1.0 + DirectWrite windows for the settings thread (and the history thread later). Never
XAML; never a taskbar thread. Mod-wide rules: [../AGENTS.md](../AGENTS.md).

## Pure vs native

- Pure (`tokens.h`, `draw.h`, `svg-path.h`, `rich-text.h`, `items.h`, `paint.h`, `input.h`, and
  the settings page/shell builders): explicit inputs only; no HWND, registry, WinRT, globals or
  `Wh_Log`. `tests/settings` extracts and renders them; fix impurities here, never in tests.
- Native (`system-theme.h`, `surface.h`, `host.h`): window, registry and WinRT calls; covered by
  the `tests/settings` native-host probe DLL, never by the render harness.

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
- `WM_CLOSE`: cancel a tracked modal and retry; fault and unload close without saving; a
  deliberate close asks the client (`PrepareClose`). Nothing commits once `WM_DESTROY` starts.

## Input

- Focus owner is the focused HWND (main or viewport); no `IsDialogMessage`. Tab follows item
  order across surfaces; the focus ring shows only after keyboard input.

## Evidence

- `PrintWindow` captures of D2D windows are unreliable (black): never use them as evidence.
  Render evidence comes from WIC targets in `tests/settings`.
