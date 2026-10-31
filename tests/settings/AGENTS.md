# Settings verification

Source-extracted tests of the pure fragments shared by the taskbar and the settings window (meter
model, `src/ui/` toolkit, settings shell and page builders), offscreen renders of the settings
window, a native-host probe of the toolkit's window host, and a capture/foreground probe for the
dropdown popup. Not a Windhawk, Explorer or XAML test.

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
  settings shell and About layout).
- `render`: the same executable with `--render-dir` paints the settings client area (1000 x 820
  DIP, the mockups' window without its caption) into WIC bitmaps: About and a placeholder page at
  96/144/192 DPI in light, dark and high contrast (Aquatic colors), plus interaction-state sheets.
  PNGs for review, no pixel goldens.
- `host-compile`, `probe-compile`, `host-probe`: the toolkit with its native host
  (`ui/system-theme.h`, `surface.h`, `host.h`) and a stub client form `ui-host.dll`;
  `host-probe.cpp` (hand-written) loads it, opens the window on the DLL's worker thread, closes it
  from another thread, joins and unloads, repeatedly, and checks classes, windows and GDI/USER
  handle counts; then client creation failure, a conflicting class registration, render-target
  loss through the production discard path, and an unload close during a message box.
- `extract-popup`, `popup-compile`, `popup-probe`: the same host as the owner of a probe popup
  (owned, `WS_EX_NOACTIVATE`, shown without activation; `fixtures/popup-*.inc`, hand-written).
  Thread-local message hooks record what each window receives while capture is on the popup, on
  the viewport (the dropdown field's surface), or nowhere, for: opening, `WM_CANCELMODE` to the
  main window or the popup, another window taking capture, release, focus moving to an EDIT,
  owner deactivation, minimize/restore, a disabled owner, a message box and destruction. Prints
  `OBSERVE` lines and checks the native facts the step-4 popup design was derived from (a failure
  means revisiting the design, not the probe).

## Outputs and side effects

Each run creates a retained `.out/run-.../` with the extracted `settings.cpp`, `ui-host.cpp` and
`popup.cpp` (plus `.manifest.json` section boundaries and hashes), executables, the DLL,
`render-*.png`, per-phase logs and `results.json`. The tool never prunes old runs. The host probe shows off-screen,
never-activated windows and briefly a message box, and reads the user's theme (registry,
`UISettings`). The popup probe runs on a hidden desktop it creates and never switches to, so its
windows, activation and message box never reach the user's screen, focus or pointer. The
interactive mode is the exception: an activated window on the user's desktop, and F8/F9/F10
registered as global hotkeys until it exits. No storage, network, credential or Explorer access.

## Success and evidence limits

Exit 0 requires every phase to pass: tooling self-tests, freshness (default source), the
extractions, compiles, exactly one successful `RESULT` summary from `model`, `render`,
`host-probe` and `popup-probe`, every reported render present as a PNG, and source and harness
unchanged during the run. A pass covers extracted logic, offscreen rendering and the host's
window/thread/teardown contract in a test process; Explorer, the settings thread's real client,
DWM caption colors, live theme switches and multi-monitor DPI need the runtime check in the root
guide. The popup probe's automated part covers thread-local activation and programmatic events
only; real clicks on other apps or the taskbar, Alt+Tab, Start and wheel routing need the
interactive mode, and neither runs inside Explorer (where the taskbar shares the process).

## Maintenance rules

- `extract.py` takes whole fragments between `// src/<path>` marker lines of the generated file,
  plus named definitions; it refuses missing or ambiguous boundaries and source changes.
  Model-target fragments must stay pure (no XAML, WinRT, window, registry, storage or global
  state): fix impurities in `src/`, never patch extracted code. The only stub allowed is
  `Wh_Log`, in the host target.
- Never extract the production store, storage, auth, account commands or settings client.
- Fixtures live by domain under `fixtures/`: synthetic inputs with hand-derived expected values,
  not copies of production rules.
- Keep generated artifacts under `.out/`.
