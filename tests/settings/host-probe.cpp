// Native-host probe driver (hand-written, not production code). Loads the probe DLL built from
// the extracted toolkit host, opens its window on the DLL's worker thread, closes it from this
// thread, joins and unloads, repeatedly; then failed creation, a conflicting class, render-target
// loss and an unload close during a message box. Windows are off-screen and never activated;
// the message box shows briefly. The dropdown popup and text box cases run on a hidden desktop
// this probe creates and never switches to, so their focus, capture and message box stay off the
// user's screen.
// Usage: host-probe.exe <ui-host.dll>
#include <windows.h>
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
enum ProbeCounter { kBuilds, kAttaches, kDestroying, kDestroyed, kCancels, kLossResult, kColorResult };
static constexpr UINT kProbePing = WM_APP + 70;
static constexpr UINT kProbeLoseTarget = WM_APP + 71;
static constexpr UINT kProbeModal = WM_APP + 72;
static constexpr UINT kProbeCommand = WM_APP + 73;
static constexpr UINT kProbeColor = WM_APP + 74;
static constexpr LRESULT kProbeMagic = 0x51A7E;
enum ProbeCommand { kOpenPopup, kPopupState, kFieldKey, kPopupMouse, kSendMain, kAction, kFocus,
                    kEditState, kEditWindow, kEditChar, kEditKey, kEditText, kRebuild, kScroll,
                    kFocusedItem, kDropFocus, kSliderMouse, kPreviewMouse, kPreviewWheel, kDropCapture };
enum ProbeState { kWindow = 1, kVisible = 2, kCaptured = 4, kNoActivate = 8, kOwned = 16,
                  kNotActive = 32, kOpen = 64, kAnyCapture = 128 };
enum ProbeEdit { kEditExists = 1, kEditFocused = 2, kEditEnabled = 4 };
static constexpr int kDropdown = 500, kNumberBox = 600, kSlider = 700;
static constexpr int kOpenState = kWindow | kVisible | kCaptured | kNoActivate | kOwned | kNotActive |
                                  kOpen | kAnyCapture;
// UiActionKind values the probe client records.
enum ActionKind { kNone, kNavigate, kOpenUrl, kCommand, kStep, kTextChange, kTextCommit, kTextBlur,
                  kTextRevert, kSlide, kSlideEnd, kPan };
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
