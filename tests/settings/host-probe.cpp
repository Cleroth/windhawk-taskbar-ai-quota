// Native-host probe driver (hand-written, not production code). Loads the probe DLL built from
// the extracted toolkit host, opens its window on the DLL's worker thread, closes it from this
// thread, joins and unloads, repeatedly; then failed creation, a conflicting class, render-target
// loss, window icons across DPI changes and an unload close during a message box. Windows are
// off-screen and never activated; the message box shows briefly. The dropdown popup, text box,
// segmented control, key forwarding, activation request and navigation pane cases run on a
// hidden desktop this probe creates and never switches to, so their focus, capture, foreground
// and message box stay off the user's screen.
// Usage: host-probe.exe <ui-host.dll>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

using OpenFn = BOOL (*)(BOOL, HDESK);
using WindowFn = HWND (*)();
using CountFn = int (*)(int);
using ClassFn = PCWSTR (*)();
using UnloadFn = void (*)();
using JoinFn = int (*)(DWORD);

// Mirrors the DLL's counters and messages.
enum ProbeCounter { kBuilds, kAttaches, kDestroying, kDestroyed, kCancels, kLossResult, kColorResult,
                    kCloses, kClosesDragging, kAutoScrolls };
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
                    kNavMouse, kNavBar, kSelectNav, kHot, kSegmentMouse, kClientKeys };
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

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: host-probe.exe <ui-host.dll>\n");
        return 2;
    }
    g_dll = argv[1];
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // C++/WinRT caches activation factories process-wide; the mod keeps the MTA alive the same
    // way so a worker apartment's teardown can't strand one.
    CO_MTA_USAGE_COOKIE mta{};
    CoIncrementMTAUsage(&mta);
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

    Run("window icons: both sizes at the window's DPI, redrawn on DPI changes, destroyed after close", [&] {
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
        // DPI changes as the system reports them (made-up DPIs, the window's rectangle kept): new
        // icons at the new sizes replace the old, which are destroyed; repeated, no handles stay.
        DWORD gdiWarm = 0, userWarm = 0;
        for (int change = 0; change < 6; change++) {
            UINT next = change % 2 ? 192 : 144;
            RECT rect{};
            GetWindowRect(hwnd, &rect);
            DWORD_PTR answer = 0;
            CHECK(SendMessageTimeoutW(hwnd, WM_DPICHANGED, MAKEWPARAM(next, next), LPARAM(&rect), SMTO_BLOCK,
                                      kTimeout, &answer));
            probe.Call(kPaintMain);
            HICON now[2] = {iconOf(ICON_SMALL), iconOf(ICON_BIG)};
            CHECK(width(now[0]) == GetSystemMetricsForDpi(SM_CXSMICON, next));
            CHECK(width(now[1]) == GetSystemMetricsForDpi(SM_CXICON, next));
            CHECK(!width(last[0]) && !width(last[1]));
            last[0] = now[0];
            last[1] = now[1];
            if (change == 1) {
                gdiWarm = GetGuiResources(process, GR_GDIOBJECTS);
                userWarm = GetGuiResources(process, GR_USEROBJECTS);
            }
        }
        DWORD gdiEnd = GetGuiResources(process, GR_GDIOBJECTS), userEnd = GetGuiResources(process, GR_USEROBJECTS);
        std::printf("HANDLES icons gdi_after_warmup=%lu gdi_end=%lu user_after_warmup=%lu user_end=%lu\n",
                    gdiWarm, gdiEnd, userWarm, userEnd);
        CHECK(gdiWarm == gdiEnd && userWarm == userEnd);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        probe.Closed();
        CHECK(!width(last[0]) && !width(last[1]));
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
