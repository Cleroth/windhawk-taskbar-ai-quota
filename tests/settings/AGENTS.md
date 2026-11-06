# Settings verification

Source-extracted tests of the pure fragments behind the settings, usage history and sign-in
windows (meter model, `src/ui/` toolkit, the settings edit controller, shell and pages; the history
window's view, chart, controller and frames on the shared history model) and of the provider
status pages' parser; offscreen renders of both windows; native probes of the toolkit's window
host (popup, tooltip, text boxes, the history window's dialog scope), the dropdown popup's
capture/foreground behavior and the sign-in window's controls. Not a Windhawk, Explorer or XAML
test. The history domain (journal memory, series, continuity, burn-up, CSV) stays in
`tests/history`. Which target and fixture covers what, render names and probe cases:
[coverage.md](coverage.md).

## Run

From the repo root:

```text
python tests/settings/run.py
python tests/settings/run.py --popup-interactive
python tests/settings/run.py --tooltip-interactive
```

Prerequisites: Windows x64, Python 3.10+, PowerShell 7 (`pwsh`), Windhawk's bundled clang++ with
its MinGW headers/libraries, and an interactive desktop session (the probes create windows). No
Python packages or installs.

Options: `--source PATH` (default: the repo-root generated file), `--compiler PATH` (default:
`C:\Program Files\Windhawk\Compiler\bin\clang++.exe`), `--output DIRECTORY` (default:
`tests/settings/.out/`; the parent for a new run directory). With the default source, a
`freshness` phase runs `pwsh generate-mod.ps1 -Check` first and fails on a stale file (it never
regenerates), and a `mod-check` phase runs `check-mod.ps1`, failing on any compiler diagnostic, so
a full run replaces a separate `check-mod.ps1`.

Order: setup, then all extractions together, then all builds (and `mod-check`) together, then
the tests one at a time; a failed group stops the run. No build overlaps a test.

The interactive modes build only their probe and run by hand, attached to the console: a probe
window opens on your desktop; follow the printed steps (F9 ends a step, F8 skips it; in the popup
probe F10 is the activate request). `--popup-interactive` covers the dropdown popup's capture and
activation under real input; `--tooltip-interactive` the readout's pointer routing (the chart, an
icon button's tooltip, other apps, Alt+Tab, the wheel, screen corners, the taskbar, a second
monitor, the Windows key). Their logs, `popup-interactive.log` and `tooltip-interactive.log` in the
run directory, are the evidence.

## Outputs and side effects

Each run creates a retained `.out/run-.../` with the extracted sources (each with a
`.manifest.json` of section boundaries and hashes), executables, the DLL, `render-*.png`, the
sign-in probe's `signin-probe.manifest`, per-phase logs and `results.json`. The tool never prunes
old runs.

The host probe shows off-screen, never-activated windows and briefly a message box, and reads the
user's theme (registry, `UISettings`). Its popup, text box, slider, preview, chart, tooltip and
color dialog cases (each on its own) and the popup probe run on hidden desktops they create and
never switch to, so their windows, focus, capture, tooltips and dialogs never reach the user's
screen, focus or pointer. The interactive modes are the exception: an activated window on the
user's desktop, and F8/F9 (the popup probe also F10) registered as global hotkeys until it exits.
No storage, network, credential or Explorer access.

## Success and evidence limits

Exit 0 requires every phase to pass: tooling self-tests, freshness and the mod check (default
source), the extractions, compiles, exactly one successful `RESULT` summary from each of `model`,
`render`, `history`, `history-render`, `status`, `host-probe`, `popup-probe` and `signin-probe`,
every reported render present as a PNG, and source and harness unchanged during the run.

A pass covers extracted logic, offscreen rendering and the host's window/thread/teardown contract
in a test process (the controllers against test stores and ops, the popup and EDITs against a
stub client). These need the root guide's runtime check, or the interactive modes where named:

- Explorer, the settings thread's real client and store, DWM caption colors, how the title bar and
  Alt+Tab show the window icon, live theme switches and multi-monitor DPI.
- The history window's production ops (journal reads and the peek, settings and poll reads, the
  consent transaction, the refresh message and deferral timer, the export's worker, its poll
  timer, cancellation and join), its opener, client (notification, inspection and popup inputs)
  and activation path; `tests/history` covers the production peek's contract and the CSV writer.
- The popup probe's automated part covers thread-local activation and programmatic events only;
  real clicks on other apps or the taskbar, Alt+Tab, Start and wheel routing need
  `--popup-interactive`. Neither mode runs inside Explorer, where the taskbar shares the process.
- On the hidden desktop the cursor image isn't readable (a fallback square stands in) and
  `WindowFromPoint` is only an observation: pointer routing through the readout, real cursor
  sizes, multi-monitor DPI and z-order next to the taskbar need `--tooltip-interactive` and
  Explorer.
- The sign-in probe covers what the controls report in a test process with comctl32 6 from its
  manifest; Explorer's activation context, the caption, a real DPI change (its suggested
  rectangle, a move between monitors), the message loop and the sign-in flows need the runtime
  check.

## Maintenance rules

- `extract.py` takes whole fragments between `// src/<path>` marker lines of the generated file,
  plus named definitions and one-line constants; it refuses missing or ambiguous boundaries and
  source changes. Model- and history-target fragments must stay pure (no XAML, WinRT, window,
  registry, storage or global state); the status target's likewise, except Windows.Data.Json. Fix
  impurities in `src/`, never patch extracted code. The only stub allowed is `Wh_Log`, in the host
  target.
- Never extract the production store, storage, auth (except the sign-in window's
  `auth/login-layout.h` and `auth/login-controls.h`: text, layout and controls, no network or
  tokens), account commands, settings client, history journal, history ops (`history/ops.h`) or
  history window code (`history/ui-window.h`, `window.h`).
- Fixtures live by domain under `fixtures/`: synthetic inputs with hand-derived expected values,
  not copies of production rules.
- `fixtures/status/` holds captured public status pages only (no account data); replace a capture
  rather than edit it, and keep expected values read off it by hand.
- Update [coverage.md](coverage.md) when adding a target, phase or fixture file.
- Keep generated artifacts under `.out/`.
