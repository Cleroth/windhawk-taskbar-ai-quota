// Automated part of the popup probe (included by popup-probe.inc): programmatic and synthetic
// events on the hidden desktop, OBSERVE lines and the checks of the facts the step-4 popup design
// was derived from.

struct ProbeObservation {
    std::vector<ProbeSignal> open, signals;
    ProbeSnapshot opened, after;
    std::string extra;
};

#define CHECK(condition) do { ++g_probe.assertions; if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

static void ProbeCase(const char* name, const std::function<void()>& body) {
    try {
        body();
        ++g_probe.passed;
        std::printf("PASS %s\n", name);
    } catch (const std::exception& e) {
        ++g_probe.failed;
        std::printf("FAIL %s: %s\n", name, e.what());
    }
    std::fflush(stdout);
}

static void CALLBACK ProbeModalTimer(HWND, UINT, UINT_PTR, DWORD) {
    HWND modal = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM found) -> BOOL {
        wchar_t name[16] = {};
        if (IsWindowVisible(window) && GetClassNameW(window, name, 16) && wcscmp(name, L"#32770") == 0) {
            *reinterpret_cast<HWND*>(found) = window;
            return FALSE;
        }
        return TRUE;
    }, LPARAM(&modal));
    if (!modal || !g_probe.modalState.empty()) return;
    g_probe.modalState = "during_modal{" + Describe(Snap(), false) + "}";
    PostMessageW(modal, WM_COMMAND, IDCANCEL, 0);
}

// Baseline: main is the thread's active window with focus on the viewport (the dropdown field's
// surface). Records the popup's opening, then the action.
static ProbeObservation Observe(ProbeCapture capture, const char* name,
                                const std::function<void()>& action, DWORD exStyle = kPopupExStyle,
                                bool showActivating = false) {
    ProbeObservation result;
    if (IsIconic(g_probe.main)) SendMessageW(g_probe.main, WM_SYSCOMMAND, SC_RESTORE, 0);
    EnableWindow(g_probe.main, TRUE);
    SetActiveWindow(g_probe.main);
    SetFocus(g_probe.viewport);
    Pump();
    g_probe.start = GetTickCount();
    g_probe.signals.clear();
    g_probe.recording = true;
    OpenPopup(capture, exStyle, showActivating);
    Pump();
    g_probe.recording = false;
    result.open = std::move(g_probe.signals);
    result.opened = Snap();
    g_probe.signals.clear();
    g_probe.modalState.clear();
    g_probe.recording = true;
    if (action) action();
    Pump();
    g_probe.recording = false;
    result.signals = std::move(g_probe.signals);
    g_probe.signals.clear();
    result.after = Snap();
    result.extra = g_probe.modalState;
    std::printf("OBSERVE %s %s opened{%s} open_signals[%s] signals[%s] after{%s}%s%s fired{%s}\n",
                CaptureName(capture), name, Describe(result.opened, false).c_str(),
                Join(result.open).c_str(), Join(result.signals).c_str(),
                Describe(result.after, false).c_str(), result.extra.empty() ? "" : " ",
                result.extra.c_str(), Fired(result.signals, capture).c_str());
    std::fflush(stdout);
    ClosePopup();
    if (GetCapture()) ReleaseCapture();
    Pump();
    return result;
}

// Observes every scenario for each capture holder, then checks the facts the step-4 popup design
// was derived from. A failure means the design needs revisiting, not that the probe is broken.
static void RunAutomated() {
    std::printf("CONTEXT hidden desktop; activation is thread-local; programmatic and synthetic "
                "events only (no real input, no foreground)\n");
    using Capture = ProbeCapture;
    HWND main = g_probe.main;

    // Controls: an owned popup that can activate, and WS_EX_NOACTIVATE with an activating show.
    ProbeObservation activating =
        Observe(Capture::Popup, "control-activating-show", nullptr, WS_EX_TOOLWINDOW, true);
    ProbeObservation noActivateShow =
        Observe(Capture::Popup, "control-noactivate-sw-show", nullptr, kPopupExStyle, true);

    std::map<std::string, std::map<Capture, ProbeObservation>> seen;
    for (Capture capture : {Capture::Popup, Capture::Owner, Capture::None}) {
        auto observe = [&](const char* name, const std::function<void()>& action) {
            seen[name][capture] = Observe(capture, name, action);
        };
        observe("open", nullptr);
        observe("cancelmode-to-owner", [main] { SendMessageW(main, WM_CANCELMODE, 0, 0); });
        observe("cancelmode-to-popup", [] { SendMessageW(g_probe.popup, WM_CANCELMODE, 0, 0); });
        observe("capture-taken", [] { SetCapture(g_probe.edit); });
        observe("release-capture", [] { ReleaseCapture(); });
        observe("focus-edit", [] { SetFocus(g_probe.edit); });
        observe("deactivate-local", [] { SetActiveWindow(g_probe.other); });
        observe("minimize", [main] { SendMessageW(main, WM_SYSCOMMAND, SC_MINIMIZE, 0); });
        observe("minimize-restore", [main] {
            SendMessageW(main, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            Pump();
            SendMessageW(main, WM_SYSCOMMAND, SC_RESTORE, 0);
        });
        observe("disable-owner", [main] { EnableWindow(main, FALSE); });
        observe("modal", [main] {
            UINT_PTR timer = SetTimer(nullptr, 0, 50, ProbeModalTimer);
            MessageBoxW(main, L"Popup probe: closed by the probe.", L"Probe", MB_OKCANCEL);
            KillTimer(nullptr, timer);
        });
        observe("destroy-popup", [] { ClosePopup(); });
    }
    auto at = [&](const char* name, Capture capture = Capture::Popup) -> const ProbeObservation& {
        return seen.at(name).at(capture);
    };

    ProbeCase("an activating show makes the popup the active window and the activate target", [&] {
        for (const ProbeObservation* o : {&activating, &noActivateShow}) {
            CHECK(o->opened.active == "popup" && o->opened.target == "popup");
            CHECK(Has(o->open, "main", WM_NCACTIVATE, false));
        }
    });
    ProbeCase("a no-activate show keeps activation, focus and the activate target on main", [&] {
        for (Capture capture : {Capture::Popup, Capture::Owner, Capture::None}) {
            const ProbeObservation& o = at("open", capture);
            CHECK(o.opened.popupVisible && o.opened.active == "main" && o.opened.focus == "viewport");
            CHECK(o.opened.lastActive == "main" && o.opened.target == "main");
            CHECK(!Has(o.open, "main", WM_NCACTIVATE) && !Has(o.open, "main", WM_ACTIVATE));
        }
        CHECK(at("open").opened.capture == "popup");
    });
    ProbeCase("the popup's capture ends with WM_CAPTURECHANGED when taken, released or cancelled", [&] {
        CHECK(Has(at("capture-taken").signals, "popup", WM_CAPTURECHANGED));
        CHECK(at("capture-taken").after.capture == "edit");
        CHECK(Has(at("release-capture").signals, "popup", WM_CAPTURECHANGED));
        CHECK(at("release-capture").after.capture == "none");
        CHECK(Has(at("cancelmode-to-popup").signals, "popup", WM_CAPTURECHANGED));
        CHECK(at("cancelmode-to-popup").after.capture == "none");
    });
    ProbeCase("destroying the popup ends its capture and visibility", [&] {
        CHECK(at("destroy-popup").after.capture == "none" && !at("destroy-popup").after.popupVisible);
    });
    ProbeCase("WM_CANCELMODE to main (sent, or by disabling it) leaves the popup's capture", [&] {
        for (const char* name : {"cancelmode-to-owner", "disable-owner"}) {
            const ProbeObservation& o = at(name);
            CHECK(Has(o.signals, "main", WM_CANCELMODE));
            CHECK(!Has(o.signals, "popup", WM_CAPTURECHANGED) && o.after.capture == "popup");
        }
    });
    ProbeCase("focus moving to an EDIT and owner deactivation leave the popup's capture", [&] {
        const ProbeObservation& focus = at("focus-edit");
        CHECK(Has(focus.signals, "viewport", WM_KILLFOCUS) && focus.after.focus == "edit");
        CHECK(!Has(focus.signals, "popup", WM_CAPTURECHANGED) && focus.after.capture == "popup");
        const ProbeObservation& deactivate = at("deactivate-local");
        CHECK(Has(deactivate.signals, "main", WM_ACTIVATE, false));
        CHECK(!Has(deactivate.signals, "popup", WM_CAPTURECHANGED) && deactivate.after.capture == "popup");
        CHECK(deactivate.after.target == "main");
    });
    ProbeCase("a modal dialog ends the popup's capture and becomes the activate target", [&] {
        const ProbeObservation& o = at("modal");
        CHECK(Has(o.signals, "main", WM_CANCELMODE) && Has(o.signals, "popup", WM_CAPTURECHANGED));
        CHECK(o.extra.find("activate_target=modal") != std::string::npos);
    });
    ProbeCase("minimize: ignored while capture is held; without capture it hides the popup and "
              "restore shows it again", [&] {
        for (Capture capture : {Capture::Popup, Capture::Owner}) {
            CHECK(!at("minimize", capture).after.iconic && at("minimize", capture).after.popupVisible);
        }
        const ProbeObservation& minimized = at("minimize", Capture::None);
        CHECK(minimized.after.iconic && !minimized.after.popupVisible);
        CHECK(Has(minimized.signals, "main", WM_SIZE) && Has(minimized.signals, "popup", WM_SHOWWINDOW, false));
        const ProbeObservation& restored = at("minimize-restore", Capture::None);
        CHECK(!restored.after.iconic && restored.after.popupVisible);
        CHECK(Has(restored.signals, "popup", WM_SHOWWINDOW, true));
    });
    std::printf("NOTE destroying the popup while it holds capture: popup WM_CAPTURECHANGED %s\n",
                Has(at("destroy-popup").signals, "popup", WM_CAPTURECHANGED) ? "sent" : "not sent");
}
