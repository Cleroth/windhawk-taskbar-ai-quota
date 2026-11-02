// Native-host probe driver (hand-written, not production code). Loads the probe DLL built from
// the extracted toolkit host, opens its window on the DLL's worker thread, closes it from this
// thread, joins and unloads, repeatedly; then failed creation, a conflicting class, render-target
// loss, window icons across DPI changes and theme notifications (handle counts read on the
// window thread once it settles) and an unload close during a message box. Windows are
// off-screen and never activated; the message box shows briefly. The dropdown popup, text box,
// segmented control, key forwarding, activation request, navigation pane, chart and tooltip
// cases run on a hidden desktop this probe creates and never switches to, so their focus,
// capture, foreground, tooltips and message box stay off the user's screen.
// Usage: host-probe.exe <ui-host.dll>
//        host-probe.exe <ui-host.dll> --tooltip-interactive --log <file>  (by hand, on the user's
//        desktop: tooltip pointer routing; F9 ends a step, F8 skips it)
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using OpenFn = BOOL (*)(BOOL, HDESK);
using WindowFn = HWND (*)();
using CountFn = int (*)(int);
using ClassFn = PCWSTR (*)();
using UnloadFn = void (*)();
using JoinFn = int (*)(DWORD);

// Mirrors the DLL's counters and messages.
enum ProbeCounter { kBuilds, kAttaches, kDestroying, kDestroyed, kCancels, kLossResult, kColorResult,
                    kCloses, kClosesDragging, kAutoScrolls, kChartHovers, kChartLeaves, kChartRepaints,
                    kTipShows, kTipHides, kTipMouse, kTipActivate, kWindowPresses, kTipAlive, kThemeMessages,
                    kSetIcons, kGdiAtDestroyed, kUserAtDestroyed };
static constexpr UINT kProbePing = WM_APP + 70;
static constexpr UINT kProbeLoseTarget = WM_APP + 71;
static constexpr UINT kProbeModal = WM_APP + 72;
static constexpr UINT kProbeCommand = WM_APP + 73;
static constexpr UINT kProbeColor = WM_APP + 74;
static constexpr LRESULT kProbeMagic = 0x51A7E;
enum ProbeCommand { kOpenPopup, kPopupState, kFieldKey, kPopupMouse, kSendMain, kAction, kFocus,
                    kEditState, kEditWindow, kEditChar, kEditKey, kEditText, kRebuild, kScroll,
                    kFocusedItem, kDropFocus, kSliderMouse, kPreviewMouse, kPreviewWheel, kDropCapture,
                    kHandleMouse, kSysKey, kTextBoxLimit, kTextBoxChar, kTextBoxKey, kItemTop, kTextBoxTop,
                    kPaintNow, kSliderSweep, kSliderValue, kLiveMain, kPaintMain, kDeclineClose, kViewHeight,
                    kScrollTop, kStopAutoScroll, kHistoryMode, kNavScroll, kNavRange, kNavVisible, kWheel,
                    kNavMouse, kNavBar, kSelectNav, kHot, kSegmentMouse, kClientKeys, kTipMode, kChartMouse,
                    kTipButton, kTipState, kTipRect, kTipKey, kTipNcHit, kTipFromPoint, kTipUnregister,
                    kTipRegister, kEnableMain, kTipLoseTarget, kMoveMain, kSendView, kCursorWidth, kHandles };
enum ProbeTip { kTipWindow = 1, kTipVisible = 2, kTipOwned = 4, kTipNoActivate = 8, kTipNotActive = 16,
                kTipNoCapture = 32, kTipClear = 64, kTipReadout = 128, kTipText = 256, kTipPending = 512 };
static constexpr int kTipShown = kTipWindow | kTipVisible | kTipOwned | kTipNoActivate | kTipNotActive |
                                 kTipNoCapture | kTipClear;
static constexpr int kChart = 1100, kTipButtonId = 1101;
static constexpr PCWSTR kTipClass = L"AiQuotaProbeUiTip";

// kChartMouse's argument: kind (0 move, 1 left down, 2 up) at (x, y) DIPs in the chart.
static int ChartAt(int kind, int x, int y) {
    return kind << 24 | x << 12 | y;
}
enum ProbeUpdate { kMainUpdate = 1, kViewportUpdate = 2 };
enum ProbeState { kWindow = 1, kVisible = 2, kCaptured = 4, kNoActivate = 8, kOwned = 16,
                  kNotActive = 32, kOpen = 64, kAnyCapture = 128 };
enum ProbeEdit { kEditExists = 1, kEditFocused = 2, kEditEnabled = 4 };
static constexpr int kDropdown = 500, kNumberBox = 600, kSlider = 700, kCard = 900, kHandle = 901,
                     kTextBox = 910, kMenu = 920, kSegments = 1000, kSegmentsOff = 1001, kNav = 2000;
static constexpr int kOpenState = kWindow | kVisible | kCaptured | kNoActivate | kOwned | kNotActive |
                                  kOpen | kAnyCapture;
// UiActionKind values the probe client records.
enum ActionKind { kNone, kNavigate, kOpenUrl, kCommand, kStep, kTextChange, kTextCommit, kTextBlur,
                  kTextRevert, kSlide, kSlideEnd, kPan, kDrag, kDrop, kDragCancel };
static constexpr DWORD kTimeout = 15000;

static int passed = 0, failed = 0, assertions = 0;
static std::wstring g_dll;

#define CHECK(condition) do { ++assertions; if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

static void Run(const char* name, const std::function<void()>& body) {
    try {
        body();
        ++passed;
        std::printf("PASS %s\n", name);
    } catch (const std::exception& e) {
        ++failed;
        std::printf("FAIL %s: %s\n", name, e.what());
    }
    std::fflush(stdout);
}

struct Probe {
    HMODULE module = nullptr;
    OpenFn open;
    WindowFn window, modal;
    CountFn count;
    ClassFn className;
    UnloadFn unload;
    JoinFn join;

    Probe() {
        module = LoadLibraryW(g_dll.c_str());
        if (!module) throw std::runtime_error("LoadLibrary failed: " + std::to_string(GetLastError()));
        auto get = [&](const char* name) {
            FARPROC proc = GetProcAddress(module, name);
            if (!proc) throw std::runtime_error(std::string("missing export ") + name);
            return proc;
        };
        open = reinterpret_cast<OpenFn>(get("ProbeOpen"));
        window = reinterpret_cast<WindowFn>(get("ProbeWindow"));
        modal = reinterpret_cast<WindowFn>(get("ProbeModalWindow"));
        count = reinterpret_cast<CountFn>(get("ProbeCount"));
        className = reinterpret_cast<ClassFn>(get("ProbeClassName"));
        unload = reinterpret_cast<UnloadFn>(get("ProbeUnload"));
        join = reinterpret_cast<JoinFn>(get("ProbeJoin"));
    }
    // After a failed check the worker may still run: close and join it before unloading, and
    // leak the module rather than unload code that still runs.
    ~Probe() {
        if (!module) return;
        if (join(0) == -1) {
            unload();
            if (join(kTimeout) == -1) return;
        }
        FreeLibrary(module);
    }

    // Opens the window (on `desktop`, if any) and waits until it answers on its own thread.
    HWND Open(HDESK desktop = nullptr) {
        CHECK(open(FALSE, desktop));
        HWND hwnd = nullptr;
        for (DWORD waited = 0; !(hwnd = window()) && waited < kTimeout; waited += 10) Sleep(10);
        CHECK(hwnd);
        DWORD_PTR answer = 0;
        CHECK(SendMessageTimeoutW(hwnd, kProbePing, 0, 0, SMTO_BLOCK, kTimeout, &answer));
        CHECK(LRESULT(answer) == kProbeMagic);
        hwnd_ = hwnd;
        return hwnd;
    }

    // A command run on the window's thread (fixtures/host-dll.inc).
    int Call(int command, int argument = 0) {
        DWORD_PTR answer = 0;
        CHECK(SendMessageTimeoutW(hwnd_, kProbeCommand, command, argument, SMTO_BLOCK, kTimeout,
                                  &answer));
        return int(answer);
    }
    HWND hwnd_ = nullptr;

    void Closed(int expectedExit = 0) {
        CHECK(join(kTimeout) == expectedExit);
        CHECK(count(kAttaches) == 1 && count(kDestroying) == 1 && count(kDestroyed) == 1);
        CHECK(!window());
    }
};

static bool ClassRegistered(PCWSTR name) {
    WNDCLASSEXW info{sizeof(info)};
    return GetClassInfoExW(GetModuleHandleW(nullptr), name, &info) != FALSE;
}

// A visible dialog (#32770) of the window's thread, or nullptr.
static HWND ThreadDialog(HWND window) {
    HWND found = nullptr;
    EnumThreadWindows(GetWindowThreadProcessId(window, nullptr), [](HWND candidate, LPARAM param) -> BOOL {
        wchar_t name[16] = {};
        if (GetClassNameW(candidate, name, 16) && wcscmp(name, L"#32770") == 0 && IsWindowVisible(candidate)) {
            *reinterpret_cast<HWND*>(param) = candidate;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

// Interactive tooltip routing (--tooltip-interactive): the probe window on the user's desktop,
// activated, in tooltip mode (a chart, an icon button with a tooltip). The user does each step
// with real input and ends it with F9 (F8 skips). Each step logs how often the readout and the
// tooltip showed and hid, the chart's hover reports, mouse and activation messages that reached
// the tooltip window (it hides on any mouse message; activations should never come), presses
// that reached the probe window, and the foreground window at its end.
static int RunTooltipInteractive(const std::wstring& logPath) {
    struct Step {
        const char* id;
        const char* action;
    };
    static constexpr Step kSteps[] = {
        {"hover", "Move the pointer slowly over the chart (the empty panel under the dropdown and the slider). The "
                  "readout follows it and never covers the pointer."},
        {"onto-readout", "Move the pointer quickly from the chart onto the readout, then around inside it."},
        {"click-beside", "With the readout showing, click the chart right beside it, then where it was."},
        {"icon-tooltip", "Rest the pointer on the icon button at the top until its tooltip shows (about half a "
                         "second), then click the button."},
        {"click-other-app", "With the readout showing, click another app's window (for example this console)."},
        {"alt-tab", "With the readout showing, press Alt+Tab to another window, then Alt+Tab back."},
        {"wheel", "With the readout showing, turn the mouse wheel over the chart."},
        {"screen-corner", "Drag the probe window to a screen corner and hover the chart next to it: the readout "
                          "takes another side, still clear of the pointer."},
        {"taskbar", "Move the probe window so the chart is just above the taskbar and hover the chart's bottom "
                    "edge: the readout stays off the taskbar."},
        {"second-monitor", "With a second monitor at another scale (F8 skips), move the window there and hover the "
                           "chart: the readout is crisp and sized like the window's text."},
        {"win-key", "With the readout showing, press the Windows key, then Esc."},
    };
    FILE* log = _wfopen(logPath.c_str(), L"w");
    auto out = [&](const std::string& text) {
        std::printf("%s", text.c_str());
        std::fflush(stdout);
        if (log) {
            std::fputs(text.c_str(), log);
            std::fflush(log);
        }
    };
    struct Hotkey {
        int id;
        UINT vk;
        const char* name;
    };
    std::string keys[3];
    for (Hotkey key : {Hotkey{1, VK_F9, "F9"}, Hotkey{2, VK_F8, "F8"}}) {
        keys[key.id] = key.name;
        if (!RegisterHotKey(nullptr, key.id, MOD_NOREPEAT, key.vk)) {
            keys[key.id] = std::string("Ctrl+Alt+") + key.name;
            if (!RegisterHotKey(nullptr, key.id, MOD_NOREPEAT | MOD_CONTROL | MOD_ALT, key.vk)) {
                std::fprintf(stderr, "Cannot register hotkey %s\n", keys[key.id].c_str());
                return 2;
            }
        }
    }
    int result = 0;
    try {
        Probe probe;
        reinterpret_cast<void (*)(BOOL)>(GetProcAddress(probe.module, "ProbeSetInteractive"))(TRUE);
        HWND hwnd = probe.Open();
        OSVERSIONINFOW version{sizeof(version)};
        using RtlGetVersion_t = LONG(WINAPI*)(OSVERSIONINFOW*);
        if (auto get = reinterpret_cast<RtlGetVersion_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) {
            get(&version);
        }
        std::time_t now = std::time(nullptr);
        char stamp[32] = {};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", std::localtime(&now));
        char header[512];
        std::snprintf(header, sizeof(header),
                      "Tooltip routing probe, interactive. %s. Windows build %lu, window DPI %u.\nStandalone process "
                      "(not Explorer). Keys: %s ends a step, %s skips it.\n",
                      stamp, version.dwBuildNumber, GetDpiForWindow(hwnd), keys[1].c_str(), keys[2].c_str());
        out(header);
        static constexpr ProbeCounter kLogged[] = {kChartHovers, kChartLeaves, kTipShows, kTipHides, kTipMouse,
                                                   kTipActivate, kWindowPresses};
        static constexpr const char* kNames[] = {"chart hover reports", "chart hovers ended", "tooltip shows",
                                                 "tooltip hides", "mouse messages that reached the tooltip",
                                                 "activation or focus messages to the tooltip",
                                                 "presses that reached the probe window"};
        for (size_t i = 0; i < std::size(kSteps) && IsWindow(hwnd); i++) {
            char line[640];
            std::snprintf(line, sizeof(line), "\nStep %zu of %zu: %s\n  Then press %s (%s skips).\n", i + 1,
                          std::size(kSteps), kSteps[i].action, keys[1].c_str(), keys[2].c_str());
            std::printf("%s", line);
            std::fflush(stdout);
            int before[std::size(kLogged)];
            for (size_t c = 0; c < std::size(kLogged); c++) before[c] = probe.count(kLogged[c]);
            int key = 0;
            for (MSG msg{}; !key && IsWindow(hwnd);) {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_HOTKEY) key = int(msg.wParam);
                }
                if (!key) MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
            }
            std::string block = "\n[step " + std::to_string(i + 1) + "] " + kSteps[i].id + ": " +
                                (key == 1 ? "done" : key == 2 ? "SKIPPED" : "window closed") + "\naction: " +
                                kSteps[i].action + "\n";
            for (size_t c = 0; c < std::size(kLogged); c++) {
                block += "  " + std::string(kNames[c]) + ": " + std::to_string(probe.count(kLogged[c]) - before[c]) + "\n";
            }
            if (IsWindow(hwnd)) {
                int state = probe.Call(kTipState);
                HWND foreground = GetForegroundWindow();
                block += std::string("end: tooltip ") + (state & kTipVisible ? "showing" : "hidden") +
                         (state & kTipWindow && !(state & kTipNotActive) ? ", ACTIVE" : "") +
                         "; foreground " + (foreground == hwnd ? "the probe window" : "another window") + "\n";
            }
            out(block);
        }
        if (IsWindow(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
        out(std::string("\nAll steps done; tooltip window ") +
            (probe.count(kTipAlive) ? "outlived the main window" : "gone before the main window") +
            ". Send the log to the agent.\n");
    } catch (const std::exception& e) {
        out(std::string("\nProbe failed: ") + e.what() + "\n");
        result = 1;
    }
    UnregisterHotKey(nullptr, 1);
    UnregisterHotKey(nullptr, 2);
    if (log) std::fclose(log);
    return result;
}

int wmain(int argc, wchar_t** argv) {
    bool interactive = argc == 5 && std::wstring(argv[2]) == L"--tooltip-interactive" &&
                       std::wstring(argv[3]) == L"--log";
    if (argc != 2 && !interactive) {
        std::fprintf(stderr, "Usage: host-probe.exe <ui-host.dll> [--tooltip-interactive --log <file>]\n");
        return 2;
    }
    g_dll = argv[1];
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // C++/WinRT caches activation factories process-wide; the mod keeps the MTA alive the same
    // way so a worker apartment's teardown can't strand one.
    CO_MTA_USAGE_COOKIE mta{};
    CoIncrementMTAUsage(&mta);
    if (interactive) {
        int code = RunTooltipInteractive(argv[4]);
        CoDecrementMTAUsage(mta);
        return code;
    }
    std::wstring className = L"AiQuotaProbeUi";
    HANDLE process = GetCurrentProcess();
    DWORD gdiWarm = 0, userWarm = 0, gdiEnd = 0, userEnd = 0;

    Run("load, open on a worker, close, join, unload: repeated", [&] {
        for (int cycle = 0; cycle < 6; cycle++) {
            {
                Probe probe;
                HWND hwnd = probe.Open();
                // Alternate a user close and unload's close (cancellation flag set first).
                if (cycle % 2) probe.unload();
                else PostMessageW(hwnd, WM_CLOSE, 0, 0);
                probe.Closed();
                CHECK(!IsWindow(hwnd));
            }
            CHECK(!GetModuleHandleW(g_dll.c_str()));
            CHECK(!ClassRegistered(className.c_str()));
            CHECK(!FindWindowW(className.c_str(), nullptr));
            if (cycle == 1) {
                gdiWarm = GetGuiResources(process, GR_GDIOBJECTS);
                userWarm = GetGuiResources(process, GR_USEROBJECTS);
            }
        }
        gdiEnd = GetGuiResources(process, GR_GDIOBJECTS);
        userEnd = GetGuiResources(process, GR_USEROBJECTS);
        std::printf("HANDLES gdi_after_warmup=%lu gdi_end=%lu user_after_warmup=%lu user_end=%lu\n",
                    gdiWarm, gdiEnd, userWarm, userEnd);
        CHECK(gdiWarm == gdiEnd && userWarm == userEnd);
    });

    Run("repeated open and close within one load", [&] {
        Probe probe;
        for (int i = 0; i < 3; i++) {
            HWND hwnd = probe.Open();
            CHECK(probe.count(kBuilds) >= 1);
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            probe.Closed();
            CHECK(!ClassRegistered(className.c_str()));
        }
    });

    Run("client creation failure leaves no window or class", [&] {
        Probe probe;
        CHECK(probe.open(TRUE, nullptr));
        CHECK(probe.join(kTimeout) == 1);
        CHECK(probe.count(kAttaches) == 1 && probe.count(kDestroying) == 1 &&
              probe.count(kDestroyed) == 1 && probe.count(kBuilds) == 0);
        CHECK(!FindWindowW(className.c_str(), nullptr));
        CHECK(!ClassRegistered(className.c_str()));
    });

    Run("a conflicting registration of the class is replaced", [&] {
        WNDCLASSEXW conflicting{sizeof(conflicting)};
        conflicting.lpfnWndProc = DefWindowProcW;
        conflicting.hInstance = GetModuleHandleW(nullptr);
        conflicting.lpszClassName = className.c_str();
        CHECK(RegisterClassExW(&conflicting));
        {
            Probe probe;
            HWND hwnd = probe.Open();  // Answers the ping: the host's procedure is in charge.
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            probe.Closed();
        }
        CHECK(!ClassRegistered(className.c_str()));
    });

    Run("render-target loss: discard and recreate through the production path", [&] {
        Probe probe;
        HWND hwnd = probe.Open();
        DWORD_PTR recreated = 0;
        CHECK(SendMessageTimeoutW(hwnd, kProbeLoseTarget, 0, 0, SMTO_BLOCK, kTimeout, &recreated));
        std::printf("LOSS result=%d\n", probe.count(kLossResult));
        CHECK(recreated == 1);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("window icons: both sizes at the window's DPI, redrawn on DPI changes and theme notifications without "
        "handle growth (read on the window thread once it settles), destroyed after close", [&] {
        Probe probe;
        HWND hwnd = probe.Open();
        auto iconOf = [&](WPARAM type) {
            DWORD_PTR answer = 0;
            CHECK(SendMessageTimeoutW(hwnd, WM_GETICON, type, 0, SMTO_BLOCK, kTimeout, &answer));
            return reinterpret_cast<HICON>(answer);
        };
        // An icon's width; 0 once it is destroyed.
        auto width = [](HICON icon) {
            ICONINFO info{};
            if (!icon || !GetIconInfo(icon, &info)) return 0;
            BITMAP bitmap{};
            GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
            if (info.hbmColor) DeleteObject(info.hbmColor);
            if (info.hbmMask) DeleteObject(info.hbmMask);
            return int(bitmap.bmWidth);
        };
        UINT dpi = GetDpiForWindow(hwnd);
        HICON last[2] = {iconOf(ICON_SMALL), iconOf(ICON_BIG)};
        CHECK(width(last[0]) == GetSystemMetricsForDpi(SM_CXSMICON, dpi));
        CHECK(width(last[1]) == GetSystemMetricsForDpi(SM_CXICON, dpi));
        // Object counts: USER and GDI read by this thread, and on the window thread once its
        // pending messages and paints are handled (`settled`) or as it is (`now`).
        struct Counts {
            int user, gdi;
            bool operator==(const Counts&) const = default;
        };
        auto here = [&] {
            return Counts{int(GetGuiResources(process, GR_USEROBJECTS)), int(GetGuiResources(process, GR_GDIOBJECTS))};
        };
        auto there = [&](bool settled) {
            int packed = probe.Call(kHandles, settled);
            return Counts{packed >> 16, packed & 0xFFFF};
        };
        auto inventory = [&] {
            auto list = reinterpret_cast<int (*)(wchar_t*, int)>(GetProcAddress(probe.module, "ProbeWindowInventory"));
            wchar_t buffer[2048] = {};
            int windows = list ? list(buffer, 2048) : -1;
            std::printf("WINDOW inventory (%d): %ls\n", windows, buffer);
        };
        Counts warm{};
        int steps = 0;
        auto step = [&](const char* phase, int change, Counts settled, Counts driver, Counts unsettled) {
            std::printf("STEP icons %d %s settled=%d/%d driver=%d/%d unsettled=%d/%d theme=%d seticon=%d\n", change,
                        phase, settled.user, settled.gdi, driver.user, driver.gdi, unsettled.user, unsettled.gdi,
                        probe.count(kThemeMessages), probe.count(kSetIcons));
            if (steps++ && warm.user && !(settled == warm)) {
                std::printf("ANOMALY icons %d %s settled %+d USER %+d GDI\n", change, phase, settled.user - warm.user,
                            settled.gdi - warm.gdi);
                inventory();
            }
        };
        // DPI changes as the system reports them (made-up DPIs, the window's rectangle kept): new
        // icons at the new sizes replace the old, which are destroyed; repeated, no handles stay.
        for (int change = 0; change < 6; change++) {
            UINT next = change % 2 ? 192 : 144;
            step("before", change, there(true), here(), there(false));
            RECT rect{};
            GetWindowRect(hwnd, &rect);
            DWORD_PTR answer = 0;
            CHECK(SendMessageTimeoutW(hwnd, WM_DPICHANGED, MAKEWPARAM(next, next), LPARAM(&rect), SMTO_BLOCK,
                                      kTimeout, &answer));
            Counts driver = here(), unsettled = there(false);
            step("dpi", change, there(true), driver, unsettled);
            probe.Call(kPaintMain);
            HICON now[2] = {iconOf(ICON_SMALL), iconOf(ICON_BIG)};
            driver = here();
            unsettled = there(false);
            Counts settled = there(true);
            step("icons", change, settled, driver, unsettled);
            CHECK(width(now[0]) == GetSystemMetricsForDpi(SM_CXSMICON, next));
            CHECK(width(now[1]) == GetSystemMetricsForDpi(SM_CXICON, next));
            CHECK(!width(last[0]) && !width(last[1]));
            last[0] = now[0];
            last[1] = now[1];
            if (change == 1) warm = settled;
        }
        Counts end = there(true);
        // Theme notifications to this window only (posted, or the color-set change sent from a
        // helper thread: it carries a string), read meanwhile on this thread: what a read off
        // the window thread can catch mid-redraw, and what stays once it is handled.
        Counts widest = end;
        for (int i = 0; i < 24; i++) {
            HANDLE sender = nullptr;
            if (i % 3 == 2) {
                sender = CreateThread(nullptr, 0, [](LPVOID window) -> DWORD {
                    DWORD_PTR answer = 0;
                    SendMessageTimeoutW(HWND(window), WM_SETTINGCHANGE, 0, LPARAM(L"ImmersiveColorSet"), SMTO_BLOCK,
                                        kTimeout, &answer);
                    return 0;
                }, hwnd, 0, nullptr);
                CHECK(sender);
            } else {
                PostMessageW(hwnd, i % 3 ? WM_THEMECHANGED : WM_SYSCOLORCHANGE, 0, 0);
            }
            for (DWORD start = GetTickCount(); GetTickCount() - start < 30;) {
                Counts c = here();
                widest = {std::max(widest.user, c.user), std::max(widest.gdi, c.gdi)};
            }
            if (sender) {
                CHECK(WaitForSingleObject(sender, kTimeout) == WAIT_OBJECT_0);
                CloseHandle(sender);
            }
        }
        Counts themed = there(true);
        std::printf("NOTE theme notifications handled by the window: %d of 24 sent (more: the system's)\n",
                    probe.count(kThemeMessages));
        std::printf("HANDLES icons settled user %d -> %d -> %d after themes; gdi %d -> %d -> %d after themes; "
                    "during themes this thread read up to %+d USER %+d GDI\n",
                    warm.user, end.user, themed.user, warm.gdi, end.gdi, themed.gdi, widest.user - end.user,
                    widest.gdi - end.gdi);
        CHECK(warm == end && end == themed);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
        std::printf("HANDLES icons at the main window's NCDESTROY user %d gdi %d; after join user %d gdi %d\n",
                    probe.count(kUserAtDestroyed), probe.count(kGdiAtDestroyed), here().user, here().gdi);
        CHECK(!width(last[0]) && !width(last[1]));
    });

    Run("cursor rectangle: the real cursor's image around its hotspot (read on this thread's desktop); its "
        "bitmaps freed: repeated without GDI handle growth", [&] {
        Probe probe;
        auto cursorRect = reinterpret_cast<RECT (*)(POINT*)>(GetProcAddress(probe.module, "ProbeCursorRect"));
        CHECK(cursorRect);
        POINT at{};
        RECT rect = cursorRect(&at);
        CURSORINFO info{sizeof(info)};
        bool showing = GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING);
        std::printf("OBSERVE cursor rectangle %ld x %ld px, hotspot +%ld,+%ld (%s)\n", rect.right - rect.left,
                    rect.bottom - rect.top, at.x - rect.left, at.y - rect.top,
                    showing ? "cursor image" : "cursor hidden: the SM_CXCURSOR fallback");
        CHECK(rect.left <= at.x && at.x < rect.right && rect.top <= at.y && at.y < rect.bottom);
        DWORD gdi = GetGuiResources(process, GR_GDIOBJECTS);
        for (int i = 0; i < 200; i++) cursorRect(&at);
        CHECK(GetGuiResources(process, GR_GDIOBJECTS) == gdi);
    });

    Run("unload close from another thread during a message box", [&] {
        Probe probe;
        HWND hwnd = probe.Open();
        PostMessageW(hwnd, kProbeModal, 0, 0);
        for (DWORD waited = 0; !probe.modal() && waited < kTimeout; waited += 10) Sleep(10);
        CHECK(probe.modal());
        probe.unload();
        probe.Closed();
        CHECK(probe.count(kCancels) >= 1);
        CHECK(!probe.modal());
    });

    // Dropdown popup and text boxes, on a hidden desktop.
    wchar_t desktopName[64];
    swprintf(desktopName, 64, L"AiQuotaHostProbe-%lu", GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(desktopName, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    std::printf("CONTEXT popup and text box cases on a hidden desktop: %s\n", desktop ? "yes" : "no");

    Run("popup: owned, never active, holds capture; WM_CANCELMODE to main closes it", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        CHECK(probe.Call(kFocus, kDropdown) == 1);
        CHECK(probe.Call(kOpenPopup) == 1);
        CHECK(probe.Call(kPopupState) == kOpenState);
        probe.Call(kSendMain, WM_CANCELMODE);
        CHECK(probe.Call(kPopupState) == 0);
        CHECK(probe.Call(kAction, 0) == 0);
        CHECK(probe.Call(kFocusedItem) == kDropdown);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
        CHECK(!ClassRegistered(L"AiQuotaProbeUiPopup"));
    });

    Run("popup: the field's keys move and choose; Esc, Alt, deactivation, a system command and "
        "wheels elsewhere close it without a choice", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        CHECK(probe.Call(kFocus, kDropdown) == 1);
        probe.Call(kFieldKey, VK_RETURN);  // Opens
        CHECK(probe.Call(kPopupState) == kOpenState);
        probe.Call(kFieldKey, VK_DOWN);
        probe.Call(kFieldKey, VK_RETURN);  // Chooses the second option
        CHECK(probe.Call(kPopupState) == 0);
        CHECK(probe.Call(kAction, 0) == 1 && probe.Call(kAction, 1) == kCommand &&
              probe.Call(kAction, 2) == 1);
        // Wheels with the pointer at (0, 0), away from the off-screen popup.
        for (int close : {MAKELONG(WM_KEYDOWN, VK_ESCAPE), MAKELONG(WM_SYSKEYDOWN, VK_MENU),
                          MAKELONG(WM_ACTIVATE, WA_INACTIVE), MAKELONG(WM_SYSCOMMAND, SC_RESTORE),
                          MAKELONG(WM_MOUSEWHEEL, 0), MAKELONG(WM_MOUSEHWHEEL, 0)}) {
            CHECK(probe.Call(kFocus, kDropdown) == 1);
            CHECK(probe.Call(kOpenPopup) == 1);
            if (LOWORD(close) == WM_KEYDOWN) probe.Call(kFieldKey, HIWORD(close));
            else probe.Call(kSendMain, close);
            CHECK(probe.Call(kPopupState) == 0);
        }
        CHECK(probe.Call(kOpenPopup) == 1);
        probe.Call(kScroll, 60);  // Scrolling closes it too
        CHECK(probe.Call(kPopupState) == 0);
        CHECK(probe.Call(kAction, 0) == 1);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("popup: an outside press hides it and keeps capture until its release; an inside click chooses", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        CHECK(probe.Call(kFocus, kDropdown) == 1);
        CHECK(probe.Call(kOpenPopup) == 1);
        probe.Call(kPopupMouse, 2);  // Outside press
        CHECK(probe.Call(kPopupState) == (kWindow | kCaptured | kNoActivate | kOwned | kNotActive |
                                          kAnyCapture));
        probe.Call(kPopupMouse, 3);  // Its release
        CHECK(probe.Call(kPopupState) == 0);
        CHECK(probe.Call(kAction, 0) == 0);
        CHECK(probe.Call(kOpenPopup) == 1);
        probe.Call(kPopupMouse, 0);
        probe.Call(kPopupMouse, 1);
        CHECK(probe.Call(kPopupState) == 0);
        CHECK(probe.Call(kAction, 0) == 1 && probe.Call(kAction, 2) == 2);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("text box: kept across rebuilds; typing, Esc, Enter, arrows and Tab reach the client; "
        "focus leaving the window reports nothing", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        CHECK(probe.Call(kEditState) == (kEditExists | kEditEnabled));
        int window = probe.Call(kEditWindow);
        CHECK(probe.Call(kFocus, kNumberBox) == 1);
        CHECK(probe.Call(kEditState) == (kEditExists | kEditFocused | kEditEnabled));
        probe.Call(kEditKey, VK_END);
        probe.Call(kEditChar, L'7');
        CHECK(probe.Call(kAction, 1) == kTextChange && probe.Call(kAction, 3) == 107);
        probe.Call(kRebuild);
        CHECK(probe.Call(kEditWindow) == window && probe.Call(kEditText) == 107);
        probe.Call(kEditKey, VK_ESCAPE);
        CHECK(probe.Call(kAction, 1) == kTextRevert && probe.Call(kEditText) == 10);
        probe.Call(kEditKey, VK_RETURN);
        CHECK(probe.Call(kAction, 1) == kTextCommit);
        probe.Call(kEditKey, VK_UP);
        CHECK(probe.Call(kAction, 1) == kStep && probe.Call(kAction, 2) == 1);
        // Focus leaving the window (another app activating) keeps the draft: no focus loss event.
        int actions = probe.Call(kAction, 0);
        probe.Call(kDropFocus);
        CHECK(probe.Call(kEditState) == (kEditExists | kEditEnabled) && probe.Call(kAction, 0) == actions);
        CHECK(probe.Call(kFocus, kNumberBox) == 1);
        // Focus loss reports a blur; the rebuild it requests keeps the text box.
        probe.Call(kEditKey, VK_TAB);
        CHECK(probe.Call(kFocusedItem) == kSlider);  // The next stop after the box
        CHECK(probe.Call(kAction, 1) == kTextBlur);
        CHECK(probe.Call(kEditState) == (kEditExists | kEditEnabled));
        CHECK(probe.Call(kEditWindow) == window);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("unload with the popup open and capture held, then with a focused text box", [&] {
        CHECK(desktop);
        for (bool popup : {true, false}) {
            Probe probe;
            probe.Open(desktop);
            if (popup) {
                CHECK(probe.Call(kFocus, kDropdown) == 1);
                CHECK(probe.Call(kOpenPopup) == 1);
                CHECK(probe.Call(kPopupState) == kOpenState);
            } else {
                CHECK(probe.Call(kFocus, kNumberBox) == 1);
            }
            probe.unload();
            probe.Closed();
            CHECK(!ClassRegistered(L"AiQuotaProbeUiPopup"));
        }
    });

    Run("slider: a press jumps and focuses, a drag reports values, the release ends it; a thumb grab "
        "doesn't jump, also at the track's end; losing capture or WM_CANCELMODE ends a drag", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        probe.Call(kSliderMouse, 0 * 1000 + 25);  // Press at 25%: jumps there
        CHECK(probe.Call(kAction, 1) == kSlide && probe.Call(kAction, 2) == 25);
        CHECK(probe.Call(kFocusedItem) == kSlider);
        probe.Call(kSliderMouse, 1 * 1000 + 75);
        CHECK(probe.Call(kAction, 1) == kSlide && probe.Call(kAction, 2) == 75);
        probe.Call(kSliderMouse, 2 * 1000 + 75);
        CHECK(probe.Call(kAction, 1) == kSlideEnd);
        int actions = probe.Call(kAction, 0);
        probe.Call(kSliderMouse, 0 * 1000 + 75);  // On the thumb: no jump
        CHECK(probe.Call(kAction, 0) == actions);
        probe.Call(kSliderMouse, 1 * 1000 + 90);
        CHECK(probe.Call(kAction, 1) == kSlide && probe.Call(kAction, 2) == 90);
        probe.Call(kDropCapture);
        CHECK(probe.Call(kAction, 1) == kSlideEnd);
        actions = probe.Call(kAction, 0);
        probe.Call(kSliderMouse, 1 * 1000 + 10);  // No drag any more
        CHECK(probe.Call(kAction, 0) == actions);
        // WM_CANCELMODE to the main window ends a drag on the viewport.
        probe.Call(kSliderMouse, 0 * 1000 + 50);
        CHECK(probe.Call(kAction, 1) == kSlide && probe.Call(kAction, 2) == 50);
        probe.Call(kSendMain, WM_CANCELMODE);
        CHECK(probe.Call(kAction, 1) == kSlideEnd && probe.Call(kPopupState) == 0);
        actions = probe.Call(kAction, 0);
        probe.Call(kSliderMouse, 1 * 1000 + 20);
        CHECK(probe.Call(kAction, 0) == actions);
        // The thumb at the maximum, pressed at its center (the track's end), grabs it.
        probe.Call(kSliderMouse, 0 * 1000 + 100);
        probe.Call(kSliderMouse, 2 * 1000 + 100);
        CHECK(probe.Call(kAction, 1) == kSlideEnd);
        actions = probe.Call(kAction, 0);
        probe.Call(kSliderMouse, 0 * 1000 + 100);
        CHECK(probe.Call(kAction, 0) == actions);
        probe.Call(kSliderMouse, 1 * 1000 + 75);
        CHECK(probe.Call(kAction, 1) == kSlide && probe.Call(kAction, 2) == 75);
        probe.Call(kSliderMouse, 2 * 1000 + 75);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("slider keys step; a preview pans by drag and sideways wheel without taking focus", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        CHECK(probe.Call(kFocus, kSlider) == 1);
        for (auto [key, step] : {std::pair{VK_RIGHT, 1}, std::pair{VK_DOWN, -1}, std::pair{VK_PRIOR, 10},
                                 std::pair{VK_END, 100}, std::pair{VK_HOME, -100}}) {
            probe.Call(kFieldKey, key);
            CHECK(probe.Call(kAction, 1) == kStep && probe.Call(kAction, 2) == step);
        }
        CHECK(probe.Call(kFocusedItem) == kSlider);
        probe.Call(kPreviewMouse, 0);
        CHECK(probe.Call(kFocusedItem) == kSlider);
        probe.Call(kPreviewMouse, 1 * 1000 + 60);
        CHECK(probe.Call(kAction, 1) == kPan && probe.Call(kAction, 2) > 0);
        int actions = probe.Call(kAction, 0);
        probe.Call(kPreviewMouse, 2 * 1000);
        CHECK(probe.Call(kAction, 0) == actions);  // The release activates nothing
        probe.Call(kPreviewWheel);
        CHECK(probe.Call(kAction, 1) == kPan && probe.Call(kAction, 2) == -48);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("drag handle: a press focuses its owner; past 4 DIP the move reports drags, the release a drop; "
        "a click does nothing; Esc and capture loss cancel; Alt+Up/Down on the owner step it", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        probe.Call(kHandleMouse, 0);
        CHECK(probe.Call(kFocusedItem) == kCard);
        int actions = probe.Call(kAction, 0);
        probe.Call(kHandleMouse, 1000 + 2);
        CHECK(probe.Call(kAction, 0) == actions);
        probe.Call(kHandleMouse, 1000 + 20);
        CHECK(probe.Call(kAction, 1) == kDrag && probe.Call(kAction, 4) == 12 && probe.Call(kAction, 2) > 0);
        int first = probe.Call(kAction, 2);
        probe.Call(kHandleMouse, 1000 + 40);
        CHECK(probe.Call(kAction, 1) == kDrag && probe.Call(kAction, 2) - first >= 19);  // Content DIPs
        probe.Call(kHandleMouse, 2000);
        CHECK(probe.Call(kAction, 1) == kDrop);
        actions = probe.Call(kAction, 0);
        probe.Call(kHandleMouse, 0);
        probe.Call(kHandleMouse, 2000);
        CHECK(probe.Call(kAction, 0) == actions);
        probe.Call(kHandleMouse, 0);
        probe.Call(kHandleMouse, 1000 + 20);
        probe.Call(kFieldKey, VK_ESCAPE);
        CHECK(probe.Call(kAction, 1) == kDragCancel);
        actions = probe.Call(kAction, 0);
        probe.Call(kHandleMouse, 1000 + 40);
        probe.Call(kHandleMouse, 2000);
        CHECK(probe.Call(kAction, 0) == actions);  // Over: neither drags nor drops
        probe.Call(kHandleMouse, 0);
        probe.Call(kHandleMouse, 1000 + 20);
        probe.Call(kDropCapture);
        CHECK(probe.Call(kAction, 1) == kDragCancel);
        CHECK(probe.Call(kFocus, kCard) == 1);
        probe.Call(kSysKey, VK_DOWN);
        CHECK(probe.Call(kAction, 1) == kStep && probe.Call(kAction, 2) == 1 && probe.Call(kAction, 4) == 12);
        probe.Call(kSysKey, VK_UP);
        CHECK(probe.Call(kAction, 1) == kStep && probe.Call(kAction, 2) == -1);
        CHECK(probe.Call(kFocusedItem) == kCard);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("drag handle: later drags move the built items in place without a rebuild, repainting only the "
        "viewport, whose paint brings the text box along; the drop rebuilds", [&] {
        CHECK(desktop);
        Probe probe;
        HWND hwnd = probe.Open(desktop);
        double scale = GetDpiForWindow(hwnd) / 96.0;
        probe.Call(kHandleMouse, 0);
        probe.Call(kHandleMouse, 1000 + 20);
        CHECK(probe.Call(kAction, 1) == kDrag);
        int builds = probe.count(kBuilds), from = probe.Call(kAction, 2);
        int card = probe.Call(kItemTop, kCard), box = probe.Call(kTextBoxTop);
        CHECK(probe.Call(kPaintNow) == 0);
        CHECK(probe.Call(kHandleMouse, 1000 + 60) == kViewportUpdate);
        int moved = probe.Call(kAction, 2) - from;
        CHECK(probe.Call(kAction, 1) == kDrag && moved >= 39);
        CHECK(probe.count(kBuilds) == builds);
        CHECK(probe.Call(kItemTop, kCard) - card == moved);
        probe.Call(kPaintNow);
        CHECK(std::abs(probe.Call(kTextBoxTop) - box - moved * scale) <= 1);
        probe.Call(kHandleMouse, 2000);
        CHECK(probe.Call(kAction, 1) == kDrop && probe.count(kBuilds) == builds + 1);
        CHECK(probe.Call(kItemTop, kCard) == card);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("drag handle: a close cancels the drag first (capture released, content back), then asks; kept "
        "open, Alt+Down steps the card", [&] {
        CHECK(desktop);
        Probe probe;
        HWND hwnd = probe.Open(desktop);
        probe.Call(kHandleMouse, 0);
        int card = probe.Call(kItemTop, kCard);
        probe.Call(kHandleMouse, 1000 + 20);
        probe.Call(kHandleMouse, 1000 + 60);
        CHECK(probe.Call(kItemTop, kCard) != card);
        probe.Call(kDeclineClose);
        probe.Call(kSendMain, WM_CLOSE);  // Nested in a dispatch, as Alt+F4's close is
        for (DWORD waited = 0; probe.count(kCloses) < 1 && waited < kTimeout; waited += 10) Sleep(10);
        CHECK(probe.count(kCloses) == 1 && probe.count(kClosesDragging) == 0);
        CHECK(probe.Call(kAction, 1) == kDragCancel && !(probe.Call(kPopupState) & kAnyCapture));
        CHECK(probe.Call(kItemTop, kCard) == card && probe.Call(kFocusedItem) == kCard);
        probe.Call(kSysKey, VK_DOWN);
        CHECK(probe.Call(kAction, 1) == kStep && probe.Call(kAction, 2) == 1);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("drag handle: the auto-scroll timer keeps ticking while the pointer moves within the edge zone", [&] {
        CHECK(desktop);
        Probe probe;
        HWND hwnd = probe.Open(desktop);
        probe.Call(kHandleMouse, 0);
        int at = probe.Call(kItemTop, kHandle) + 20 - probe.Call(kScrollTop);  // The press, in the viewport
        probe.Call(kHandleMouse, 1000 + 20);
        int below = probe.Call(kViewHeight) - 24 - at + 6;
        CHECK(below > 20 && below < 990);
        int ticks = probe.count(kAutoScrolls);
        for (int i = 0; i < 30; i++) {
            probe.Call(kHandleMouse, 1000 + below + (i & 1));
            Sleep(8);
        }
        CHECK(probe.count(kAutoScrolls) > ticks);
        probe.Call(kHandleMouse, 2000);
        CHECK(probe.Call(kAction, 1) == kDrop);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("drag handle: with the auto-scroll timer starved, moves within the edge zone still scroll on time", [&] {
        CHECK(desktop);
        Probe probe;
        HWND hwnd = probe.Open(desktop);
        probe.Call(kHandleMouse, 0);
        int above = probe.Call(kItemTop, kHandle) + 20 - probe.Call(kScrollTop) - 5;  // To 5 DIP: the top zone
        CHECK(above > 20 && above < 990);
        probe.Call(kHandleMouse, 3000 + above);
        probe.Call(kStopAutoScroll);
        int scroll = probe.Call(kScrollTop);
        CHECK(scroll > 100);
        for (int i = 0; i < 6; i++) {
            Sleep(40);
            probe.Call(kHandleMouse, 3000 + above + (i & 1));
        }
        CHECK(probe.Call(kScrollTop) < scroll);
        probe.Call(kHandleMouse, 2000);
        CHECK(probe.Call(kAction, 1) == kDrop);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("slider drags rebuild once per frame, at the paint, repainting the main window only when named; "
        "values repeat against the last report; the main window's paint brings the viewport's", [&] {
        CHECK(desktop);
        Probe probe;
        HWND hwnd = probe.Open(desktop);
        probe.Call(kSliderMouse, 0 * 1000 + 25);
        CHECK(probe.Call(kAction, 1) == kSlide && probe.Call(kAction, 2) == 25);
        probe.Call(kPaintNow);
        CHECK(probe.Call(kSliderValue) == 25);
        // To 75 and back to the built 25: both reported; only the viewport waits for its paint,
        // the missed rebuild runs once at the dispatch end and repaints only the viewport.
        int sweep = probe.Call(kSliderSweep, 75 * 1000 + 25);
        CHECK((sweep & 3) == kViewportUpdate && ((sweep >> 2) & 3) == kViewportUpdate && (sweep >> 4) == 1);
        CHECK(probe.Call(kAction, 1) == kSlide && probe.Call(kAction, 2) == 25 && probe.Call(kSliderValue) == 25);
        // Named: both surfaces.
        probe.Call(kLiveMain, 1);
        sweep = probe.Call(kSliderSweep, 90 * 1000 + 80);
        CHECK((sweep & 3) == (kMainUpdate | kViewportUpdate) && (sweep >> 4) == 1);
        CHECK(probe.Call(kSliderValue) == 80);
        CHECK(probe.Call(kPaintMain) == 0);
        probe.Call(kSliderMouse, 2 * 1000 + 80);
        CHECK(probe.Call(kAction, 1) == kSlideEnd);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("free text box: its limit; typing and Esc reach the client, Up/Down stay native; a menu button "
        "opens its menu with nothing chosen, keys choose", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        CHECK(probe.Call(kFocus, kTextBox) == 1);
        CHECK(probe.Call(kTextBoxLimit) == 4);
        probe.Call(kTextBoxChar, L'7');
        CHECK(probe.Call(kAction, 1) == kTextChange && probe.Call(kAction, 4) == 13 && probe.Call(kAction, 3) == 7);
        int actions = probe.Call(kAction, 0);
        probe.Call(kTextBoxKey, VK_UP);
        probe.Call(kTextBoxKey, VK_DOWN);
        CHECK(probe.Call(kAction, 0) == actions);
        probe.Call(kTextBoxKey, VK_ESCAPE);
        CHECK(probe.Call(kAction, 1) == kTextRevert);
        CHECK(probe.Call(kFocus, kMenu) == 1);
        probe.Call(kFieldKey, VK_RETURN);
        CHECK(probe.Call(kPopupState) == kOpenState);
        actions = probe.Call(kAction, 0);
        probe.Call(kFieldKey, VK_DOWN);
        probe.Call(kFieldKey, VK_RETURN);
        CHECK(probe.Call(kPopupState) == 0);
        CHECK(probe.Call(kAction, 0) == actions + 1 && probe.Call(kAction, 1) == kCommand &&
              probe.Call(kAction, 2) == 0 && probe.Call(kAction, 4) == 14);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("color dialog: a user close and unload's close cancel it, then the window closes", [&] {
        CHECK(desktop);
        for (bool unload : {false, true}) {
            Probe probe;
            HWND hwnd = probe.Open(desktop);
            PostMessageW(hwnd, kProbeColor, 0, 0);
            HWND dialog = nullptr;
            for (DWORD waited = 0; !(dialog = ThreadDialog(hwnd)) && waited < kTimeout; waited += 10) Sleep(10);
            CHECK(dialog);
            if (unload) probe.unload();
            else PostMessageW(hwnd, WM_CLOSE, 0, 0);
            probe.Closed();
            CHECK(probe.count(kCancels) >= 1 && probe.count(kColorResult) == 1);
            CHECK(!IsWindow(dialog));
        }
    });

    Run("segmented control: one tab stop; its keys select through the host and stay with it; a click "
        "selects the segment under the pointer and focuses the control; the selected segment, a press "
        "released elsewhere and a disabled control do nothing", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        probe.Call(kHistoryMode, 24);
        CHECK(probe.Call(kFocus, kSegments) == 1);
        int keys = probe.Call(kClientKeys, 0);
        auto key = [&](int vk) { probe.Call(kSendMain, MAKELONG(WM_KEYDOWN, vk)); };
        key(VK_RIGHT);
        CHECK(probe.Call(kAction, 1) == kCommand && probe.Call(kAction, 4) == 15 && probe.Call(kAction, 2) == 1);
        key(VK_END);
        CHECK(probe.Call(kAction, 2) == 3);
        int actions = probe.Call(kAction, 0);
        key(VK_RIGHT);  // At the end: nothing to select
        key(VK_END);
        CHECK(probe.Call(kAction, 0) == actions);
        key(VK_HOME);
        CHECK(probe.Call(kAction, 2) == 0 && probe.Call(kAction, 0) == actions + 1);
        key(VK_LEFT);
        CHECK(probe.Call(kAction, 0) == actions + 1);
        CHECK(probe.Call(kClientKeys, 0) == keys && probe.Call(kFocusedItem) == kSegments);
        CHECK(probe.Call(kSegmentMouse, 20) == kSegments * 16 + 3);  // Over the third segment
        probe.Call(kSegmentMouse, 21);
        CHECK(probe.Call(kFocusedItem) == kSegments);
        probe.Call(kSegmentMouse, 22);
        CHECK(probe.Call(kAction, 2) == 2 && probe.Call(kAction, 4) == 15);
        actions = probe.Call(kAction, 0);
        probe.Call(kSegmentMouse, 21);  // The selected segment
        probe.Call(kSegmentMouse, 22);
        probe.Call(kSegmentMouse, 11);  // Pressed on the second, released on the fourth
        probe.Call(kSegmentMouse, 32);
        CHECK(probe.Call(kAction, 0) == actions);
        CHECK(probe.Call(kSegmentMouse, 100) == 0);  // The disabled control: nothing hot
        probe.Call(kSegmentMouse, 101);
        probe.Call(kSegmentMouse, 102);
        CHECK(probe.Call(kAction, 0) == actions && probe.Call(kFocusedItem) == kSegments);
        key(VK_TAB);  // One stop: Tab leaves it for the content's first
        CHECK(probe.Call(kFocusedItem) == kDropdown);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("keys neither surface uses reach the client from the main window and the viewport; keys the "
        "popup, the focus order or a control uses don't", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        auto keys = [&] { return probe.Call(kClientKeys, 0); };
        int count = keys();
        probe.Call(kSendMain, MAKELONG(WM_KEYDOWN, VK_F5));
        CHECK(keys() == count + 1 && probe.Call(kClientKeys, 1) == VK_F5);
        CHECK(probe.Call(kFocus, kDropdown) == 1);
        probe.Call(kFieldKey, VK_F5);  // The viewport
        CHECK(keys() == count + 2 && probe.Call(kClientKeys, 1) == VK_F5);
        probe.Call(kFieldKey, 'R');
        CHECK(keys() == count + 3 && probe.Call(kClientKeys, 1) == 'R');
        probe.Call(kFieldKey, VK_ESCAPE);
        CHECK(keys() == count + 4 && probe.Call(kClientKeys, 1) == VK_ESCAPE);
        CHECK(probe.Call(kOpenPopup) == 1);
        probe.Call(kFieldKey, VK_DOWN);
        probe.Call(kFieldKey, VK_ESCAPE);  // Closes the popup only
        CHECK(probe.Call(kPopupState) == 0 && keys() == count + 4);
        probe.Call(kFieldKey, VK_TAB);
        probe.Call(kFieldKey, VK_NEXT);
        CHECK(probe.Call(kScrollTop) > 0 && keys() == count + 4);
        CHECK(probe.Call(kFocus, kSlider) == 1);
        probe.Call(kFieldKey, VK_RIGHT);
        CHECK(probe.Call(kAction, 1) == kStep && keys() == count + 4);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("activation request: the popup closes and the window comes back, then the client sees it; "
        "theme changes rebuild", [&] {
        CHECK(desktop);
        Probe probe;
        HWND hwnd = probe.Open(desktop);
        UINT activate = RegisterWindowMessageW(L"AiQuotaProbeUiActivate");
        CHECK(activate);
        CHECK(probe.Call(kFocus, kDropdown) == 1);
        CHECK(probe.Call(kOpenPopup) == 1);
        PostMessageW(hwnd, activate, 0, 0);
        for (DWORD waited = 0; probe.Call(kClientKeys, 2) < 1 && waited < kTimeout; waited += 10) Sleep(10);
        CHECK(probe.Call(kClientKeys, 2) == 1 && probe.Call(kClientKeys, 3) == 0);
        CHECK(probe.Call(kPopupState) == 0);
        for (int message : {WM_THEMECHANGED, WM_SYSCOLORCHANGE, WM_DWMCOLORIZATIONCOLORCHANGED}) {
            int builds = probe.count(kBuilds);
            probe.Call(kSendMain, message);
            CHECK(probe.count(kBuilds) == builds + 1);
        }
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("navigation pane: the wheel scrolls it over it and the content over the page; the pointer hits "
        "entries at its scroll; keyboard focus and a new selection scroll entries into view; its "
        "scrollbar widens under the pointer and its thumb drags it", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        probe.Call(kHistoryMode, 24);
        int range = probe.Call(kNavRange);
        CHECK(range > 100);
        CHECK(probe.Call(kNavScroll) == 0 && probe.Call(kScrollTop) == 0);
        probe.Call(kWheel, 0);
        int nav = probe.Call(kNavScroll);
        CHECK(nav > 0 && probe.Call(kScrollTop) == 0);
        probe.Call(kWheel, 10);
        CHECK(probe.Call(kScrollTop) > 0 && probe.Call(kNavScroll) == nav);
        probe.Call(kWheel, 11);
        CHECK(probe.Call(kScrollTop) == 0);
        // The middle of the first entry wholly below y 60 at the pane's scroll.
        int entry = (60 + nav - 6) / 41 + 1, y = 6 + 41 * entry + 19 - nav;
        CHECK(probe.Call(kNavMouse, y) == (kNav + entry) * 16);
        probe.Call(kNavMouse, 10000 + y);
        probe.Call(kNavMouse, 20000 + y);
        CHECK(probe.Call(kAction, 1) == kNavigate && probe.Call(kAction, 2) == entry);
        CHECK(probe.Call(kFocusedItem) == kNav + entry && probe.Call(kNavScroll) == nav);
        probe.Call(kSendMain, MAKELONG(WM_KEYDOWN, VK_END));
        CHECK(probe.Call(kFocusedItem) == kNav + 23 && probe.Call(kNavVisible, 23));
        CHECK(std::abs(probe.Call(kNavScroll) - range) <= 1);
        int end = probe.Call(kNavScroll);
        probe.Call(kSendMain, MAKELONG(WM_KEYDOWN, VK_UP));
        CHECK(probe.Call(kFocusedItem) == kNav + 22 && probe.Call(kNavScroll) == end);
        probe.Call(kSendMain, MAKELONG(WM_KEYDOWN, VK_HOME));
        CHECK(probe.Call(kFocusedItem) == kNav && probe.Call(kNavScroll) == 0);
        // A selection the client makes scrolls its entry into view once.
        probe.Call(kSelectNav, 20);
        CHECK(probe.Call(kNavVisible, 20) && probe.Call(kNavScroll) > 0);
        for (int i = 0; i < 40 && probe.Call(kNavScroll) > 0; i++) probe.Call(kWheel, 1);
        probe.Call(kRebuild);
        CHECK(probe.Call(kNavScroll) == 0);
        CHECK(probe.Call(kNavBar, 0) == 1);  // Hot, no element under it
        CHECK(probe.Call(kNavBar, 1000) == 3);
        probe.Call(kNavBar, 2000 + 60);
        int dragged = probe.Call(kNavScroll);
        CHECK(dragged > 0);
        CHECK(probe.Call(kNavBar, 3000) == 1 && probe.Call(kNavScroll) == dragged);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    // Chart and tooltip cases: the window moved on screen (of the hidden desktop), so the readout
    // sits on the pointer's monitor beside it.
    auto tipWindow = [&](Probe& probe) {
        probe.Open(desktop);
        probe.Call(kMoveMain, 100);
        probe.Call(kTipMode, 1);
    };
    auto visible = [](Probe& probe) { return (probe.Call(kTipState) & kTipVisible) != 0; };

    Run("chart and readout: hover reaches the client without taking focus; the readout shows at once, owned, "
        "never active, without capture, clear of the pointer, passing the pointer through; the same pick moves "
        "it without a repaint, a new one repaints only the chart's surface; a press ends the hover and focuses "
        "the chart, the release activates nothing; its arrows scroll; target loss recreates", [&] {
        CHECK(desktop);
        Probe probe;
        tipWindow(probe);
        CHECK(probe.Call(kFocus, kDropdown) == 1);
        probe.Call(kChartMouse, ChartAt(0, 10, 50));
        CHECK(probe.Call(kTipState) == (kTipShown | kTipReadout));
        CHECK(probe.Call(kFocusedItem) == kDropdown && probe.count(kChartHovers) == 1 && probe.Call(kTipKey) == 1);
        int left = probe.Call(kTipRect, 0), shows = probe.count(kTipShows);
        CHECK(shows == 1);
        probe.Call(kPaintNow);
        CHECK(probe.Call(kChartMouse, ChartAt(0, 30, 70)) == 0);  // The same column: no repaint
        CHECK(probe.Call(kTipState) == (kTipShown | kTipReadout) && probe.Call(kTipKey) == 1);
        CHECK(probe.Call(kTipRect, 0) != left && probe.count(kTipShows) == shows);
        CHECK(probe.Call(kChartMouse, ChartAt(0, 130, 70)) == kViewportUpdate);  // A new column
        CHECK(probe.Call(kTipKey) == 3 && probe.count(kChartRepaints) == 2);
        int hovers = probe.count(kChartHovers);
        probe.Call(kChartMouse, ChartAt(0, 130, 70));  // A move that didn't move
        CHECK(probe.count(kChartHovers) == hovers);
        CHECK(probe.Call(kTipNcHit) == HTTRANSPARENT);
        static constexpr const char* kUnder[] = {"another window", "the tooltip", "the viewport", "the main window"};
        std::printf("OBSERVE WindowFromPoint at the readout's center (window thread): %s\n",
                    kUnder[std::clamp(probe.Call(kTipFromPoint), 0, 3)]);
        std::printf("OBSERVE cursor rectangle width on the hidden desktop: %d px (fallback: 2 x SM_CXCURSOR)\n",
                    probe.Call(kCursorWidth));
        CHECK(probe.Call(kTipLoseTarget) == 1);
        CHECK(probe.count(kTipMouse) == 0 && probe.count(kTipActivate) == 0);
        int actions = probe.Call(kAction, 0), leaves = probe.count(kChartLeaves);
        probe.Call(kChartMouse, ChartAt(1, 130, 70));
        CHECK(!visible(probe) && probe.count(kChartLeaves) == leaves + 1 && probe.Call(kFocusedItem) == kChart);
        probe.Call(kChartMouse, ChartAt(2, 130, 70));
        CHECK(probe.Call(kAction, 0) == actions);
        probe.Call(kFieldKey, VK_DOWN);
        CHECK(probe.Call(kScrollTop) == 40 && probe.Call(kFocusedItem) == kChart);
        probe.Call(kFieldKey, VK_NEXT);
        CHECK(probe.Call(kScrollTop) > 40);
        probe.Call(kFieldKey, VK_HOME);
        CHECK(probe.Call(kScrollTop) == 0);
        probe.Call(kFieldKey, VK_UP);
        CHECK(probe.Call(kScrollTop) == 0 && probe.Call(kFocusedItem) == kChart);
        probe.Call(kFieldKey, VK_RETURN);
        CHECK(probe.Call(kAction, 0) == actions);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
        CHECK(probe.count(kTipAlive) == 0);
        CHECK(!ClassRegistered(kTipClass));
    });

    Run("readout: every hide trigger hides it (leave, press, key, wheel, scroll, rebuild, popup, cancel mode, "
        "deactivation, a disabled owner, focus loss, move, size, minimize, hide, system command, Alt, the "
        "activation request, DPI, theme, a declined close, a modal); none shows with the owner disabled", [&] {
        CHECK(desktop);
        Probe probe;
        tipWindow(probe);
        int x = 10;
        auto show = [&] {
            x = x == 10 ? 14 : 10;
            probe.Call(kChartMouse, ChartAt(0, x, 60));
            CHECK(probe.Call(kTipState) == (kTipShown | kTipReadout));
        };
        UINT activate = RegisterWindowMessageW(L"AiQuotaProbeUiActivate");
        struct Trigger {
            const char* name;
            std::function<void()> fire, undo;
        };
        std::vector<Trigger> triggers = {
            {"leave", [&] { probe.Call(kSendView, MAKELONG(WM_MOUSELEAVE, 0)); }, nullptr},
            {"press", [&] { probe.Call(kChartMouse, ChartAt(1, x, 60)); },
             [&] { probe.Call(kChartMouse, ChartAt(2, x, 60)); }},
            {"key", [&] { probe.Call(kFieldKey, 'A'); }, nullptr},
            {"wheel", [&] { probe.Call(kSendMain, MAKELONG(WM_MOUSEWHEEL, 0)); }, nullptr},
            {"scroll", [&] { probe.Call(kScroll, 30); }, [&] { probe.Call(kScroll, 0); }},
            {"rebuild", [&] { probe.Call(kRebuild); }, nullptr},
            {"popup", [&] { CHECK(probe.Call(kOpenPopup) == 1); },
             [&] { probe.Call(kSendMain, WM_CANCELMODE); }},
            {"cancel mode", [&] { probe.Call(kSendMain, WM_CANCELMODE); }, nullptr},
            {"deactivation", [&] { probe.Call(kSendMain, MAKELONG(WM_ACTIVATE, WA_INACTIVE)); }, nullptr},
            {"disabled owner", [&] { probe.Call(kEnableMain, 0); }, [&] {
                 probe.Call(kChartMouse, ChartAt(0, 40, 60));  // Not while disabled
                 CHECK(!visible(probe) && probe.count(kChartHovers) > 0);
                 probe.Call(kEnableMain, 1);
             }},
            {"focus loss", [&] { probe.Call(kSendMain, WM_KILLFOCUS); }, nullptr},
            {"move", [&] { probe.Call(kSendMain, WM_MOVE); }, nullptr},
            {"size", [&] { probe.Call(kSendMain, MAKELONG(WM_SIZE, SIZE_RESTORED)); }, nullptr},
            {"minimize", [&] { probe.Call(kSendMain, MAKELONG(WM_SIZE, SIZE_MINIMIZED)); }, nullptr},
            {"hide", [&] { probe.Call(kSendMain, MAKELONG(WM_SHOWWINDOW, 0)); }, nullptr},
            {"system command", [&] { probe.Call(kSendMain, MAKELONG(WM_SYSCOMMAND, SC_RESTORE)); }, nullptr},
            {"alt", [&] { probe.Call(kSysKey, VK_MENU); }, nullptr},
            {"activation request", [&] {
                 int requests = probe.Call(kClientKeys, 2);
                 PostMessageW(probe.hwnd_, activate, 0, 0);
                 for (DWORD waited = 0; probe.Call(kClientKeys, 2) == requests && waited < kTimeout; waited += 10) {
                     Sleep(10);
                 }
             }, nullptr},
            {"dpi", [&] {
                 RECT rect{};
                 GetWindowRect(probe.hwnd_, &rect);
                 UINT dpi = GetDpiForWindow(probe.hwnd_);
                 DWORD_PTR answer = 0;
                 CHECK(SendMessageTimeoutW(probe.hwnd_, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), LPARAM(&rect), SMTO_BLOCK,
                                           kTimeout, &answer));
             }, nullptr},
            {"theme", [&] { probe.Call(kSendMain, WM_THEMECHANGED); }, nullptr},
            {"declined close", [&] {
                 probe.Call(kDeclineClose);
                 int closes = probe.count(kCloses);
                 PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
                 for (DWORD waited = 0; probe.count(kCloses) == closes && waited < kTimeout; waited += 10) Sleep(10);
             }, nullptr},
            {"modal", [&] {
                 PostMessageW(probe.hwnd_, kProbeModal, 0, 0);
                 for (DWORD waited = 0; !probe.modal() && waited < kTimeout; waited += 10) Sleep(10);
                 CHECK(probe.modal());
             }, [&] {
                 probe.Call(kChartMouse, ChartAt(0, 40, 60));  // Not during the modal
                 CHECK(!visible(probe));
                 PostMessageW(probe.modal(), WM_COMMAND, IDCANCEL, 0);
                 for (DWORD waited = 0; probe.modal() && waited < kTimeout; waited += 10) Sleep(10);
             }},
        };
        for (const Trigger& trigger : triggers) {
            show();
            trigger.fire();
            bool hidden = !visible(probe);
            if (!hidden) std::printf("NOTE still showing after: %s\n", trigger.name);
            CHECK(hidden);
            if (trigger.undo) trigger.undo();
        }
        CHECK(probe.count(kTipMouse) == 0 && probe.count(kTipActivate) == 0);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
        CHECK(probe.count(kTipAlive) == 0);
    });

    Run("item tooltip: after the hover delay while the pointer stays on its item in the same frame; a new "
        "element hides it; nothing shows after a rebuild, a press, a move away, or with the popup open", [&] {
        CHECK(desktop);
        Probe probe;
        tipWindow(probe);
        DWORD delay = GetDoubleClickTime() + 300;
        auto arm = [&] {
            probe.Call(kTipButton, 3);
            CHECK(probe.Call(kTipButton, 0) == kTipButtonId * 16);
            CHECK((probe.Call(kTipState) & (kTipPending | kTipVisible)) == kTipPending);
        };
        arm();
        Sleep(delay);
        CHECK(probe.Call(kTipState) == (kTipShown | kTipText));
        probe.Call(kTipButton, 3);
        CHECK(probe.Call(kTipState) == (kTipWindow | kTipOwned | kTipNoActivate | kTipNotActive | kTipNoCapture));
        arm();
        probe.Call(kRebuild);
        Sleep(delay);
        CHECK(!visible(probe));
        arm();
        probe.Call(kTipButton, 1);
        Sleep(delay);
        CHECK(!visible(probe));
        probe.Call(kTipButton, 2);
        arm();
        probe.Call(kTipButton, 3);
        Sleep(delay);
        CHECK(!visible(probe));
        // The delay running out with the popup open (armed after it opened), and the readout then.
        CHECK(probe.Call(kOpenPopup) == 1);
        probe.Call(kTipButton, 3);
        CHECK(probe.Call(kTipButton, 0) == kTipButtonId * 16 && (probe.Call(kTipState) & kTipPending));
        Sleep(delay);
        CHECK(!visible(probe) && (probe.Call(kPopupState) & kOpen));
        probe.Call(kChartMouse, ChartAt(0, 20, 60));
        CHECK(!visible(probe));
        probe.Call(kSendMain, WM_CANCELMODE);
        CHECK(probe.count(kTipMouse) == 0 && probe.count(kTipActivate) == 0);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("tooltip: repeated show and hide without USER or GDI handle growth", [&] {
        CHECK(desktop);
        Probe probe;
        tipWindow(probe);
        // Read on the window thread once its messages and paints are handled (the tooltip paints
        // after it shows): USER << 16 | GDI.
        int warm = 0;
        for (int i = 0; i < 60; i++) {
            probe.Call(kChartMouse, ChartAt(0, 10 + (i % 7) * 50, 60 + i % 2));
            CHECK(visible(probe));
            probe.Call(kSendView, MAKELONG(WM_MOUSELEAVE, 0));
            CHECK(!visible(probe));
            if (i == 9) warm = probe.Call(kHandles, 1);
        }
        int end = probe.Call(kHandles, 1);
        std::printf("HANDLES tooltip gdi_after_warmup=%d gdi_end=%d user_after_warmup=%d user_end=%d shows=%d\n",
                    warm & 0xFFFF, end & 0xFFFF, warm >> 16, end >> 16, probe.count(kTipShows));
        CHECK(warm == end && probe.count(kTipShows) == 60);
        PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
        probe.Closed();
    });

    Run("tooltip: unload with the readout showing; the tooltip is gone before the main window's NCDESTROY", [&] {
        CHECK(desktop);
        Probe probe;
        tipWindow(probe);
        probe.Call(kChartMouse, ChartAt(0, 10, 60));
        CHECK(visible(probe));
        probe.unload();
        probe.Closed();
        CHECK(probe.count(kTipAlive) == 0 && probe.count(kTipHides) >= 1);
        CHECK(!ClassRegistered(kTipClass));
    });

    Run("tooltip: a creation failure leaves the window working without tooltips, hover still reported; the "
        "next show retries; a conflicting class registration is replaced", [&] {
        CHECK(desktop);
        {
            Probe probe;
            tipWindow(probe);
            CHECK(probe.Call(kTipUnregister) == 1);
            probe.Call(kChartMouse, ChartAt(0, 10, 60));
            CHECK(!(probe.Call(kTipState) & kTipWindow) && probe.count(kChartHovers) == 1);
            CHECK(probe.Call(kFocus, kDropdown) == 1);
            CHECK(probe.Call(kTipRegister) == 1);
            probe.Call(kChartMouse, ChartAt(0, 60, 60));
            CHECK(probe.Call(kTipState) == (kTipShown | kTipReadout));
            PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
            probe.Closed();
        }
        WNDCLASSEXW conflicting{sizeof(conflicting)};
        conflicting.lpfnWndProc = DefWindowProcW;
        conflicting.hInstance = GetModuleHandleW(nullptr);
        conflicting.lpszClassName = kTipClass;
        CHECK(RegisterClassExW(&conflicting));
        {
            Probe probe;
            tipWindow(probe);
            probe.Call(kChartMouse, ChartAt(0, 10, 60));
            CHECK(probe.Call(kTipState) == (kTipShown | kTipReadout) && probe.Call(kTipNcHit) == HTTRANSPARENT);
            PostMessageW(probe.hwnd_, WM_CLOSE, 0, 0);
            probe.Closed();
        }
        CHECK(!ClassRegistered(kTipClass));
    });

    Run("unload during a message box opened over the popup", [&] {
        CHECK(desktop);
        Probe probe;
        probe.Open(desktop);
        CHECK(probe.Call(kFocus, kDropdown) == 1);
        CHECK(probe.Call(kOpenPopup) == 1);
        PostMessageW(probe.hwnd_, kProbeModal, 0, 0);
        for (DWORD waited = 0; !probe.modal() && waited < kTimeout; waited += 10) Sleep(10);
        CHECK(probe.modal());
        CHECK(probe.Call(kPopupState) == 0);  // The modal ended its capture: closed
        probe.unload();
        probe.Closed();
        CHECK(probe.count(kCancels) >= 1);
    });
    if (desktop) CloseDesktop(desktop);

    std::printf("RESULT %d passed, %d failed; %d assertions\n", passed, failed, assertions);
    CoDecrementMTAUsage(mta);
    return failed ? 1 : 0;
}
