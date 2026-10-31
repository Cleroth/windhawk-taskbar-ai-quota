// Capture/foreground probe for the settings dropdown popup (hand-written, not production code).
// The owner is the extracted toolkit host (real classes, styles and window procedures); the popup
// is a probe window owned by it (WS_POPUP, WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, shown with
// SWP_NOACTIVATE, MA_NOACTIVATE), painted with the toolkit. Thread-local message hooks record
// what each probe window receives. The probe popup never closes itself: it reports signals.
//
// Automated (default, popup-automated.inc): on a hidden desktop created for the run, so nothing
// reaches the user's foreground, focus or pointer; activation there is thread-local. Programmatic
// and synthetic events only. Prints OBSERVE lines and checks the facts the step-4 popup relies on.
// Interactive (--interactive [--log PATH], popup-interactive.inc): on the user's desktop with real
// input, one step at a time; F9 ends a step, F8 skips it, F10 sends the host's activate message.

static constexpr PCWSTR kPopupProbeClass = L"AiQuotaPopupProbe";
static constexpr PCWSTR kPopupProbeViewportClass = L"AiQuotaPopupProbeView";
static constexpr PCWSTR kProbePopupClass = L"AiQuotaPopupProbePopup";
static constexpr PCWSTR kProbeOtherClass = L"AiQuotaPopupProbeOther";
static constexpr PCWSTR kProbeControlClass = L"AiQuotaPopupProbeControl";
static constexpr UINT kProbeStart = WM_APP + 80;
static constexpr UINT kProbeRefresh = WM_APP + 81;
static constexpr int kOpenDropdown = 100;
static constexpr float kPopupWidth = 280, kPopupHeight = 192;
static constexpr DWORD kPopupExStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
enum ProbeHotkey { kHotkeyDone = 1, kHotkeySkip, kHotkeyActivate };

// Capture holder while the popup is open: the popup, the owner's surface that holds the dropdown
// field (the viewport), or none.
enum class ProbeCapture { Popup, Owner, None };

static const char* CaptureName(ProbeCapture capture) {
    return capture == ProbeCapture::Popup ? "popup" : capture == ProbeCapture::Owner ? "viewport" : "none";
}

// One recorded message, described when it arrived (windows may be gone later).
struct ProbeSignal {
    std::string role;
    UINT message = 0;  // 0: a probe note
    std::string text;
    bool flag = false;  // Button-down on the popup: outside it. WM_ACTIVATE/NCACTIVATE/APP/ENABLE/SHOW: on.
    DWORD time = 0;
};

// Window roles and states as text, so checks never compare handles of destroyed windows.
struct ProbeSnapshot {
    std::string capture, active, focus, lastActive, target, foreground;
    bool popupVisible = false, iconic = false, enabled = true;
};

// Probe-thread state (one UI thread; the console thread only waits).
struct ProbeState {
    bool interactive = false;
    UiHost* host = nullptr;
    HWND main = nullptr, viewport = nullptr, edit = nullptr, popup = nullptr, other = nullptr,
         control = nullptr;
    UiSurface popupSurface;
    std::vector<UiItem> popupItems;
    D2D1_RECT_F dropdown{};  // Content DIPs
    HHOOK sentHook = nullptr, postedHook = nullptr;
    UINT activateMessage = 0;

    // Recording
    bool recording = false;
    std::vector<ProbeSignal> signals;
    DWORD start = 0;

    // Automated results
    int passed = 0, failed = 0, assertions = 0;
    std::string modalState;

    // Interactive
    size_t step = 0;
    int opened = 0;
    FILE* log = nullptr;
    std::array<std::string, 4> keys;  // Registered chord by ProbeHotkey
};
static ProbeState g_probe;

static std::string Utf8(const std::wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr,
                                   nullptr);
    std::string out(size_t(std::max(size, 0)), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr,
                            nullptr);
    }
    return out;
}

static std::string Role(HWND window) {
    if (!window) return "none";
    if (window == g_probe.main) return "main";
    if (window == g_probe.viewport) return "viewport";
    if (window == g_probe.popup) return "popup";
    if (window == g_probe.edit) return "edit";
    if (window == g_probe.other) return "other";
    if (window == g_probe.control) return "control";
    wchar_t name[80] = {};
    GetClassNameW(window, name, ARRAYSIZE(name));
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return "app:" + Utf8(name);
    return wcscmp(name, L"#32770") == 0 ? "modal" : "own:" + Utf8(name);
}

static std::string RootRoleAt(POINT point) {
    HWND under = WindowFromPoint(point);
    return Role(under ? GetAncestor(under, GA_ROOT) : nullptr);
}

static const char* MessageName(UINT message) {
    switch (message) {
        case WM_ACTIVATE: return "WM_ACTIVATE";
        case WM_NCACTIVATE: return "WM_NCACTIVATE";
        case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
        case WM_MOUSEACTIVATE: return "WM_MOUSEACTIVATE";
        case WM_CANCELMODE: return "WM_CANCELMODE";
        case WM_CAPTURECHANGED: return "WM_CAPTURECHANGED";
        case WM_SETFOCUS: return "WM_SETFOCUS";
        case WM_KILLFOCUS: return "WM_KILLFOCUS";
        case WM_ENABLE: return "WM_ENABLE";
        case WM_SHOWWINDOW: return "WM_SHOWWINDOW";
        case WM_WINDOWPOSCHANGED: return "WM_WINDOWPOSCHANGED";
        case WM_SIZE: return "WM_SIZE";
        case WM_SYSCOMMAND: return "WM_SYSCOMMAND";
        case WM_ENTERMENULOOP: return "WM_ENTERMENULOOP";
        case WM_EXITMENULOOP: return "WM_EXITMENULOOP";
        case WM_ENTERSIZEMOVE: return "WM_ENTERSIZEMOVE";
        case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
        case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
        case WM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
        case WM_LBUTTONUP: return "WM_LBUTTONUP";
        case WM_RBUTTONUP: return "WM_RBUTTONUP";
        case WM_NCLBUTTONDOWN: return "WM_NCLBUTTONDOWN";
        case WM_NCRBUTTONDOWN: return "WM_NCRBUTTONDOWN";
        case WM_MOUSEWHEEL: return "WM_MOUSEWHEEL";
        case WM_MOUSEHWHEEL: return "WM_MOUSEHWHEEL";
        case WM_KEYDOWN: return "WM_KEYDOWN";
        case WM_SYSKEYDOWN: return "WM_SYSKEYDOWN";
    }
    return nullptr;
}

static void AddNote(std::string text) {
    ProbeSignal note;
    note.role = "probe";
    note.text = std::move(text);
    note.time = GetTickCount() - g_probe.start;
    g_probe.signals.push_back(std::move(note));
}

// Called from the thread's message hooks: never throws into system code.
static void Record(HWND window, UINT message, WPARAM wParam, LPARAM lParam, POINT point) {
    if (!g_probe.recording) return;
    const char* name = MessageName(message);
    if (!name) return;
    try {
        ProbeSignal signal;
        signal.role = Role(window);
        if (signal.role.starts_with("own:")) return;  // IME and COM helper windows.
        signal.message = message;
        signal.time = GetTickCount() - g_probe.start;
        std::string detail;
        switch (message) {
            case WM_ACTIVATE:
                signal.flag = LOWORD(wParam) != WA_INACTIVE;
                detail = std::string(LOWORD(wParam) == WA_INACTIVE ? "inactive" :
                                     LOWORD(wParam) == WA_CLICKACTIVE ? "click-active" : "active") +
                         ", other=" + Role(HWND(lParam));
                break;
            case WM_NCACTIVATE:
            case WM_ACTIVATEAPP:
            case WM_ENABLE:
                signal.flag = wParam != 0;
                detail = wParam ? "1" : "0";
                break;
            case WM_MOUSEACTIVATE:
                detail = "top=" + Role(HWND(wParam));
                break;
            case WM_CAPTURECHANGED:
                detail = "new=" + Role(HWND(lParam));
                break;
            case WM_SETFOCUS:
                detail = "old=" + Role(HWND(wParam));
                break;
            case WM_KILLFOCUS:
                detail = "new=" + Role(HWND(wParam));
                break;
            case WM_SHOWWINDOW:
                signal.flag = wParam != 0;
                detail = std::string(wParam ? "show" : "hide") +
                         (lParam == SW_PARENTCLOSING ? ", parent closing" :
                          lParam == SW_PARENTOPENING ? ", parent opening" : "");
                break;
            case WM_WINDOWPOSCHANGED: {
                UINT flags = reinterpret_cast<const WINDOWPOS*>(lParam)->flags;
                if (!(flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW))) return;
                signal.flag = (flags & SWP_SHOWWINDOW) != 0;
                detail = signal.flag ? "show" : "hide";
                break;
            }
            case WM_SIZE:
                if (wParam != SIZE_MINIMIZED) return;
                detail = "minimized";
                break;
            case WM_SYSCOMMAND: {
                char code[16];
                std::snprintf(code, sizeof(code), "0x%04X", unsigned(wParam & 0xFFF0));
                detail = code;
                break;
            }
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
                if (window == g_probe.popup) {
                    RECT rect{};
                    GetWindowRect(window, &rect);
                    signal.flag = !PtInRect(&rect, point);
                    detail = signal.flag ? "outside, " : "inside, ";
                }
                detail += "over " + RootRoleAt(point);
                break;
            case WM_NCLBUTTONDOWN:
            case WM_NCRBUTTONDOWN:
                detail = "hit=" + std::to_string(wParam) + ", over " + RootRoleAt(point);
                break;
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                detail = "over " + RootRoleAt(point);
                break;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if (lParam & 0x40000000) return;  // Auto-repeat
                char key[16];
                std::snprintf(key, sizeof(key), "vk=0x%02X", unsigned(wParam));
                detail = key;
                break;
            }
        }
        signal.text = signal.role + ":" + name + (detail.empty() ? "" : "(" + detail + ")");
        g_probe.signals.push_back(std::move(signal));
    } catch (...) {
    }
}

static LRESULT CALLBACK ProbeSentHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* sent = reinterpret_cast<const CWPSTRUCT*>(lParam);
        Record(sent->hwnd, sent->message, sent->wParam, sent->lParam, {});
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

static LRESULT CALLBACK ProbePostedHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == PM_REMOVE) {
        const auto* posted = reinterpret_cast<const MSG*>(lParam);
        Record(posted->hwnd, posted->message, posted->wParam, posted->lParam, posted->pt);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

static ProbeSnapshot Snap() {
    ProbeSnapshot s;
    s.capture = Role(GetCapture());
    s.active = Role(GetActiveWindow());
    s.focus = Role(GetFocus());
    s.foreground = Role(GetForegroundWindow());
    s.popupVisible = g_probe.popup && IsWindowVisible(g_probe.popup);
    if (HWND main = g_probe.main) {
        // The host's activate handler: the last active popup when enabled and visible, else main.
        HWND last = GetLastActivePopup(main);
        HWND target = last != main && !(IsWindowEnabled(last) && IsWindowVisible(last)) ? main : last;
        s.lastActive = Role(last);
        s.target = Role(target);
        s.iconic = IsIconic(main);
        s.enabled = IsWindowEnabled(main);
    }
    return s;
}

static std::string Describe(const ProbeSnapshot& s, bool foreground) {
    std::string text = "capture=" + s.capture + " active=" + s.active + " focus=" + s.focus +
                       " last_active_popup=" + s.lastActive + " activate_target=" + s.target +
                       " popup_visible=" + std::to_string(s.popupVisible) +
                       " owner_minimized=" + std::to_string(s.iconic) +
                       " owner_enabled=" + std::to_string(s.enabled);
    if (foreground) text += " foreground=" + s.foreground;
    return text;
}

static std::string Join(const std::vector<ProbeSignal>& signals) {
    std::string text;
    for (const ProbeSignal& signal : signals) text += (text.empty() ? "" : ", ") + signal.text;
    return text;
}

static bool Has(const std::vector<ProbeSignal>& signals, const char* role, UINT message,
                std::optional<bool> flag = std::nullopt) {
    return std::any_of(signals.begin(), signals.end(), [&](const ProbeSignal& signal) {
        return signal.role == role && signal.message == message && (!flag || signal.flag == *flag);
    });
}

// The popup-close candidates that fired, by the variant's capture holder.
static std::string Fired(const std::vector<ProbeSignal>& signals, ProbeCapture capture) {
    std::vector<std::string> fired;
    auto rolesOf = [&](auto match) {
        std::string roles;
        for (const ProbeSignal& signal : signals) {
            if (match(signal) && roles.find(signal.role) == std::string::npos) {
                roles += (roles.empty() ? "" : "+") + signal.role;
            }
        }
        return roles;
    };
    const char* holder = capture == ProbeCapture::Popup ? "popup" : "viewport";
    if (capture != ProbeCapture::None && Has(signals, holder, WM_CAPTURECHANGED)) {
        fired.push_back("capture_lost");
    }
    if (std::string roles = rolesOf([](const ProbeSignal& s) { return s.message == WM_CANCELMODE; });
        !roles.empty()) {
        fired.push_back("cancelmode(" + roles + ")");
    }
    if (Has(signals, "main", WM_ACTIVATE, false)) fired.push_back("owner_deactivated");
    if (Has(signals, "main", WM_NCACTIVATE, false)) fired.push_back("owner_caption_inactive");
    if (std::string roles = rolesOf([](const ProbeSignal& s) {
            return s.message == WM_ACTIVATEAPP && !s.flag;
        });
        !roles.empty()) {
        fired.push_back("activateapp_off(" + roles + ")");
    }
    auto button = [](UINT message) {
        return message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
               message == WM_NCLBUTTONDOWN || message == WM_NCRBUTTONDOWN;
    };
    if (std::any_of(signals.begin(), signals.end(), [&](const ProbeSignal& s) {
            return s.role == "popup" && button(s.message) && s.flag;
        })) {
        fired.push_back("popup_outside_click");
    }
    if (std::any_of(signals.begin(), signals.end(), [&](const ProbeSignal& s) {
            return s.role == "popup" && button(s.message) && !s.flag;
        })) {
        fired.push_back("popup_inside_click");
    }
    if (std::string roles = rolesOf([&](const ProbeSignal& s) {
            return s.role != "popup" && s.role != "probe" && button(s.message);
        });
        !roles.empty()) {
        fired.push_back("click_to(" + roles + ")");
    }
    if (Has(signals, "main", WM_KILLFOCUS) || Has(signals, "viewport", WM_KILLFOCUS)) {
        fired.push_back("owner_focus_lost");
    }
    if (Has(signals, "main", WM_SIZE)) fired.push_back("owner_minimized");
    if (Has(signals, "popup", WM_SHOWWINDOW, false) || Has(signals, "popup", WM_WINDOWPOSCHANGED, false)) {
        fired.push_back("popup_hidden");
    }
    if (Has(signals, "popup", WM_SHOWWINDOW, true)) fired.push_back("popup_reshown");
    if (std::string roles = rolesOf([](const ProbeSignal& s) { return s.message == WM_ENTERMENULOOP; });
        !roles.empty()) {
        fired.push_back("menu_loop(" + roles + ")");
    }
    if (std::string roles = rolesOf([](const ProbeSignal& s) {
            return s.message == WM_MOUSEWHEEL || s.message == WM_MOUSEHWHEEL;
        });
        !roles.empty()) {
        fired.push_back("wheel_to(" + roles + ")");
    }
    if (std::string roles = rolesOf([](const ProbeSignal& s) {
            return s.message == WM_KEYDOWN || s.message == WM_SYSKEYDOWN;
        });
        !roles.empty()) {
        fired.push_back("keys_to(" + roles + ")");
    }
    std::string text;
    for (const std::string& item : fired) text += (text.empty() ? "" : " ") + item;
    return text.empty() ? "nothing" : text;
}

// Dispatches what is queued (the script runs inside one host message).
static void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            PostQuitMessage(int(message.wParam));
            return;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static void ClosePopup() {
    if (g_probe.popup) DestroyWindow(g_probe.popup);  // WM_NCDESTROY clears g_probe.popup.
}

// Below the dropdown card, owned by main; shown without activation unless asked to.
static void OpenPopup(ProbeCapture capture, DWORD exStyle, bool showActivating) {
    ClosePopup();
    UiHost& host = *g_probe.host;
    float scale = UiHostScale(host);
    POINT at{LONG(std::lround(g_probe.dropdown.left * scale)),
             LONG(std::lround((g_probe.dropdown.bottom - host.scroll + 4) * scale))};
    ClientToScreen(g_probe.viewport, &at);
    g_probe.popup = CreateWindowExW(exStyle, kProbePopupClass, L"Probe popup", WS_POPUP, at.x, at.y,
                                    int(std::lround(kPopupWidth * scale)),
                                    int(std::lround(kPopupHeight * scale)), g_probe.main, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
    if (!g_probe.popup) throw std::runtime_error("popup creation failed");
    g_probe.popupSurface.window = g_probe.popup;
    if (showActivating) {
        ShowWindow(g_probe.popup, SW_SHOW);
    } else {
        SetWindowPos(g_probe.popup, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (capture == ProbeCapture::Popup) SetCapture(g_probe.popup);
    else if (capture == ProbeCapture::Owner) SetCapture(g_probe.viewport);
}

static LRESULT CALLBACK ProbePopupProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            try {
                UiHost& host = *g_probe.host;
                if (ID2D1HwndRenderTarget* target =
                        UiSurfaceTarget(g_probe.popupSurface, host.graphics->d2d.get(), host.dpi)) {
                    target->BeginDraw();
                    D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
                    target->SetTransform(&identity);
                    target->Clear(host.palette.background);
                    UiPaintItems(target, *host.graphics, g_probe.popupSurface.cache, host.palette,
                                 g_probe.popupItems, {}, {0, 0}, UiHostScale(host), -1e9f, 1e9f);
                    UiSurfaceDrawResult(g_probe.popupSurface, target->EndDraw());
                }
            } catch (...) {
                Wh_Log(L"Popup paint failed");
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_NCDESTROY:
            UiDiscardSurface(g_probe.popupSurface);
            g_probe.popupSurface.window = nullptr;
            if (g_probe.popup == window) g_probe.popup = nullptr;
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

#include "popup-automated.inc"
#include "popup-interactive.inc"

// Host client ---------------------------------------------------------------------------------

struct PopupProbeClient final : UiHostClient {
    void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) override {
        UiItem nav;
        nav.id = 1;
        nav.kind = UiItemKind::Nav;
        nav.rect = {8, 6, 192, 44};
        nav.text = L"Probe";
        nav.icon = L"\uE946";
        nav.selected = true;
        nav.action = {UiActionKind::Navigate, 0};
        frame.main.push_back(nav);
        float right = std::max(client.width - 24, 260.0f);
        UiItem title;
        title.id = 2;
        title.kind = UiItemKind::Text;
        title.font = UiFont::Subtitle;
        title.rect = {216, 10, right, 40};
        title.runs = {{L"Popup capture probe"}};
        frame.main.push_back(title);
        UiItem instruction;
        instruction.id = 3;
        instruction.kind = UiItemKind::Text;
        instruction.rect = {216, 46, right, 126};
        if (!g_probe.interactive) {
            instruction.runs = {{L"Automated run on a hidden desktop."}};
        } else if (g_probe.step < kStepCount) {
            const ProbeStep& step = kSteps[g_probe.step];
            wchar_t heading[96];
            swprintf(heading, ARRAYSIZE(heading), L"Step %u of %u, capture on %hs: ",
                     unsigned(g_probe.step + 1), unsigned(kStepCount), CaptureName(step.capture));
            std::string action = step.action;
            instruction.runs = {{heading, UiRunStyle::Strong},
                                {std::wstring(action.begin(), action.end())},
                                {L" Open the dropdown first. F9 ends the step, F8 skips it.",
                                 UiRunStyle::Normal, UiInk::Secondary}};
        }
        frame.main.push_back(instruction);
        frame.viewport = {200, 132, std::max(client.width, 200.0f), std::max(client.height, 132.0f)};

        uint32_t id = kOpenDropdown;
        float width = std::clamp(client.width - 240, 200.0f, 520.0f);
        UiCardSpec dropdown;
        dropdown.glyph = L"\uE8A4";
        dropdown.title = L"Open dropdown";
        dropdown.description = {{L"Opens the probe popup with the current step's capture"}};
        dropdown.trailingGlyph = L"\uE70D";
        dropdown.action = {UiActionKind::Navigate, kOpenDropdown};
        g_probe.dropdown = UiAddCard(*host.graphics, frame.content, id, 20, 0, width, dropdown).card;
        float y = g_probe.dropdown.bottom + 3;
        for (int i = 0; i < 14; i++) {
            UiCardSpec filler;
            filler.title = L"Filler " + std::to_wstring(i + 1);
            filler.description = {{L"Scrollable content"}};
            y = UiAddCard(*host.graphics, frame.content, id, 20, y, width, filler).card.bottom + 3;
        }
        frame.contentHeight = y + 28;
    }

    void Invoke(UiHost&, UiAction action) override {
        if (!g_probe.interactive || action.kind != UiActionKind::Navigate ||
            action.value != kOpenDropdown || g_probe.step >= kStepCount) {
            return;
        }
        if (g_probe.opened++) {
            AddNote("popup reopened");
        } else {
            g_probe.start = GetTickCount();
        }
        g_probe.recording = true;
        OpenPopup(kSteps[g_probe.step].capture, kPopupExStyle, false);
    }

    bool Message(UiHost&, UINT message, WPARAM, LPARAM, LRESULT& result) override {
        result = 0;
        if (message == kProbeRefresh) return true;
        if (message != kProbeStart) return false;
        if (g_probe.interactive) {
            StartInteractive();
        } else {
            RunAutomated();
            PostMessageW(g_probe.main, WM_CLOSE, 0, 0);
        }
        return true;
    }

    bool Attach(UiHost& host) override {
        g_probe.host = &host;
        g_probe.main = host.main;
        g_probe.viewport = host.viewport;
        HINSTANCE instance = GetModuleHandleW(nullptr);
        float scale = UiHostScale(host);
        g_probe.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"text box",
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                       int(std::lround(16 * scale)), int(std::lround(56 * scale)),
                                       int(std::lround(168 * scale)), int(std::lround(28 * scale)),
                                       host.main, nullptr, instance, nullptr);
        if (g_probe.interactive) {
            g_probe.control = CreateWindowExW(0, kProbeControlClass, nullptr, 0, 0, 0, 0, 0,
                                              HWND_MESSAGE, nullptr, instance, nullptr);
        } else {
            g_probe.other = CreateWindowExW(0, kProbeOtherClass, L"Other probe window",
                                            WS_OVERLAPPEDWINDOW, 1100, 100, 300, 200, nullptr,
                                            nullptr, instance, nullptr);
            if (g_probe.other) ShowWindow(g_probe.other, SW_SHOWNOACTIVATE);
        }
        g_probe.sentHook = SetWindowsHookExW(WH_CALLWNDPROC, ProbeSentHook, nullptr,
                                             GetCurrentThreadId());
        g_probe.postedHook = SetWindowsHookExW(WH_GETMESSAGE, ProbePostedHook, nullptr,
                                               GetCurrentThreadId());
        if (!g_probe.edit || !(g_probe.interactive ? g_probe.control : g_probe.other) ||
            !g_probe.sentHook || !g_probe.postedHook) {
            std::printf("FAIL probe setup: %lu\n", GetLastError());
            return false;
        }
        UiItem frame;
        frame.id = 1;
        frame.kind = UiItemKind::Card;
        frame.rect = {0, 0, kPopupWidth, kPopupHeight};
        g_probe.popupItems.push_back(frame);
        for (int i = 0; i < 5; i++) {
            UiItem option;
            option.id = uint32_t(2 + i);
            option.kind = UiItemKind::Nav;
            option.rect = {4, 4 + 36.8f * float(i), kPopupWidth - 4, 4 + 36.8f * float(i) + 34};
            option.text = L"Option " + std::to_wstring(i + 1);
            option.selected = i == 0;
            g_probe.popupItems.push_back(option);
        }
        PostMessageW(host.main, kProbeStart, 0, 0);
        return true;
    }

    bool CancelModal() override { return false; }
    bool Unloading() override { return false; }
    bool PrepareClose(UiHost&) override { return true; }

    void Destroying() override {
        g_probe.recording = false;
        ClosePopup();
        if (g_probe.control) {
            for (int id : {kHotkeyDone, kHotkeySkip, kHotkeyActivate}) UnregisterHotKey(g_probe.control, id);
            DestroyWindow(g_probe.control);
            g_probe.control = nullptr;
        }
        if (g_probe.other) {
            DestroyWindow(g_probe.other);
            g_probe.other = nullptr;
        }
        if (g_probe.sentHook) UnhookWindowsHookEx(g_probe.sentHook);
        if (g_probe.postedHook) UnhookWindowsHookEx(g_probe.postedHook);
        g_probe.sentHook = g_probe.postedHook = nullptr;
        if (g_probe.interactive && g_probe.log) {
            FinishInteractive(("Window closed at step " + std::to_string(g_probe.step + 1) + " of " +
                               std::to_string(kStepCount) + ".").c_str());
        }
    }

    void Destroyed() override {
        g_probe.main = g_probe.viewport = g_probe.edit = nullptr;
        g_probe.host = nullptr;
    }
};

static DWORD WINAPI ProbeThread(LPVOID desktop) {
    // A thread joins another desktop only before it owns windows, hooks or a COM apartment.
    if (desktop && !SetThreadDesktop(HDESK(desktop))) {
        std::printf("FAIL cannot join the hidden desktop: %lu\n", GetLastError());
        ++g_probe.failed;
        return 1;
    }
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    std::pair<PCWSTR, WNDPROC> classes[] = {{kProbePopupClass, ProbePopupProc},
                                            {kProbeOtherClass, DefWindowProcW},
                                            {kProbeControlClass, ProbeControlProc}};
    for (auto [name, proc] : classes) {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = proc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = name;
        RegisterClassExW(&windowClass);
    }
    bool created = false;
    {
        PopupProbeClient client;
        UiHostConfig config;
        config.mainClass = kPopupProbeClass;
        config.viewportClass = kPopupProbeViewportClass;
        config.title = L"Popup capture probe";
        config.activateMessage = g_probe.activateMessage =
            RegisterWindowMessageW(L"AiQuotaPopupProbeActivate");
        config.size = {900, 640};
        config.activate = g_probe.interactive;
        if (!g_probe.interactive) config.position = POINT{100, 100};
        created = UiHostRun(config, client);
    }
    for (auto [name, proc] : classes) UnregisterClassW(name, instance);
    winrt::uninit_apartment();
    return created ? 0 : 1;
}

int wmain(int argc, wchar_t** argv) {
    std::wstring logPath;
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--interactive") == 0) {
            g_probe.interactive = true;
        } else if (wcscmp(argv[i], L"--log") == 0 && i + 1 < argc) {
            logPath = argv[++i];
        } else {
            std::fprintf(stderr, "Usage: popup-probe.exe [--interactive [--log PATH]]\n");
            return 2;
        }
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // As in the mod: keep the MTA alive so a worker apartment's teardown can't strand
    // C++/WinRT's cached activation factories.
    CO_MTA_USAGE_COOKIE mta{};
    CoIncrementMTAUsage(&mta);
    HDESK desktop = nullptr;
    if (g_probe.interactive) {
        if (logPath.empty()) logPath = L"popup-interactive.log";
        g_probe.log = _wfopen(logPath.c_str(), L"wb");
        if (!g_probe.log) {
            std::fprintf(stderr, "Cannot write the log: %ls\n", logPath.c_str());
            return 2;
        }
        std::printf("Log: %ls\n", logPath.c_str());
    } else {
        // Never switched to: the user's input desktop, foreground and pointer stay untouched.
        wchar_t name[64];
        swprintf(name, ARRAYSIZE(name), L"AiQuotaPopupProbe-%lu", GetCurrentProcessId());
        desktop = CreateDesktopW(name, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        if (!desktop) {
            std::printf("FAIL cannot create a hidden desktop: %lu\nRESULT 0 passed, 1 failed; 0 assertions\n",
                        GetLastError());
            return 1;
        }
    }
    HANDLE thread = CreateThread(nullptr, 0, ProbeThread, desktop, 0, nullptr);
    bool finished = thread &&
        WaitForSingleObject(thread, g_probe.interactive ? INFINITE : 90000) == WAIT_OBJECT_0;
    DWORD code = 1;
    if (finished) GetExitCodeThread(thread, &code);
    if (!finished) std::printf("FAIL the probe thread did not finish\n");
    else if (code) std::printf("FAIL the probe window could not run\n");
    if (!finished || code) ++g_probe.failed;
    if (!g_probe.interactive) {
        std::printf("RESULT %d passed, %d failed; %d assertions\n", g_probe.passed, g_probe.failed,
                    g_probe.assertions);
    }
    std::fflush(stdout);
    // A stuck thread may still use the desktop and apartment: exit without cleanup.
    if (!finished) return 1;
    CloseHandle(thread);
    if (desktop) CloseDesktop(desktop);
    CoDecrementMTAUsage(mta);
    return g_probe.failed ? 1 : 0;
}
