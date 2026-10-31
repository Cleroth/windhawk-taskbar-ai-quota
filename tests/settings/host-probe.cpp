// Native-host probe driver (hand-written, not production code). Loads the probe DLL built from
// the extracted toolkit host, opens its window on the DLL's worker thread, closes it from this
// thread, joins and unloads, repeatedly; then failed creation, a conflicting class, render-target
// loss and an unload close during a message box. Windows are off-screen and never activated;
// the message box shows briefly.
// Usage: host-probe.exe <ui-host.dll>
#include <windows.h>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

using OpenFn = BOOL (*)(BOOL);
using WindowFn = HWND (*)();
using CountFn = int (*)(int);
using ClassFn = PCWSTR (*)();
using UnloadFn = void (*)();
using JoinFn = int (*)(DWORD);

// Mirrors the DLL's counters and messages.
enum ProbeCounter { kBuilds, kAttaches, kDestroying, kDestroyed, kCancels, kLossResult };
static constexpr UINT kProbePing = WM_APP + 70;
static constexpr UINT kProbeLoseTarget = WM_APP + 71;
static constexpr UINT kProbeModal = WM_APP + 72;
static constexpr LRESULT kProbeMagic = 0x51A7E;
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

    // Opens the window and waits until it answers on its own thread.
    HWND Open() {
        CHECK(open(FALSE));
        HWND hwnd = nullptr;
        for (DWORD waited = 0; !(hwnd = window()) && waited < kTimeout; waited += 10) Sleep(10);
        CHECK(hwnd);
        DWORD_PTR answer = 0;
        CHECK(SendMessageTimeoutW(hwnd, kProbePing, 0, 0, SMTO_BLOCK, kTimeout, &answer));
        CHECK(LRESULT(answer) == kProbeMagic);
        return hwnd;
    }

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
        CHECK(probe.open(TRUE));
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

    std::printf("RESULT %d passed, %d failed; %d assertions\n", passed, failed, assertions);
    CoDecrementMTAUsage(mta);
    return failed ? 1 : 0;
}
