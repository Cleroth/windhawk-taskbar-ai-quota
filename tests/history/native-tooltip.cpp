// Native tooltip contract probe, with explicitly opt-in unsafe preactivation diagnostics.
// No production code or storage is loaded.
// The owner stays hidden; a tooltip-only subclass prevents any on-screen positioning.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <stdexcept>
#include <string>

static int assertions = 0;
#define CHECK(condition) do { ++assertions; if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

struct NativeScope {
    HANDLE context = INVALID_HANDLE_VALUE;
    ULONG_PTR cookie = 0;
    HWND owner = nullptr, child = nullptr, tip = nullptr;
    HFONT font = nullptr;
    std::wstring text;
    int clampedMoves = 0, showNotifications = 0, hideNotifications = 0, hidePositions = 0;
    ~NativeScope() {
        if (tip) DestroyWindow(tip); // Text and subclass state stay alive through destruction.
        if (owner) DestroyWindow(owner);
        if (font) DeleteObject(font);
        if (cookie) DeactivateActCtx(0, cookie);
        if (context != INVALID_HANDLE_VALUE) ReleaseActCtx(context);
    }
};

static LRESULT CALLBACK OffscreenOnly(HWND window, UINT message, WPARAM wParam,
                                      LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto& native = *reinterpret_cast<NativeScope*>(data);
    LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_WINDOWPOSCHANGING) {
        auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
        if (position->flags & SWP_HIDEWINDOW) ++native.hidePositions;
        if (!(position->flags & SWP_NOMOVE)) {
            if (position->x != -32000 || position->y != -32000) ++native.clampedMoves;
            position->x = position->y = -32000;
        }
        position->flags |= SWP_NOACTIVATE;
    }
    if (message == WM_SHOWWINDOW) {
        if (wParam) ++native.showNotifications;
        else ++native.hideNotifications;
    }
    return result;
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::cout << std::unitbuf;
    bool preactivation = argc > 1 && std::wstring(argv[argc-1]) == L"--preactivation";
    if (preactivation)
        std::cout << "NATIVE_DIAGNOSTIC observation_only=1 abnormal_exit_possible=1 unsafe_call_usable=not_assumed\n";
    try {
        NativeScope native;
        int positional = argc - int(preactivation);
        CHECK(positional == 1 || positional == 2);
        if (positional == 2) {
            ACTCTXW context{}; context.cbSize = sizeof(context); context.lpSource = argv[1];
            native.context = CreateActCtxW(&context); CHECK(native.context != INVALID_HANDLE_VALUE);
            CHECK(ActivateActCtx(native.context, &native.cookie));
        }
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_WIN95_CLASSES}; CHECK(InitCommonControlsEx(&init));
        HINSTANCE module = GetModuleHandleW(nullptr);
        native.owner = CreateWindowExW(0, L"STATIC", L"Hidden offscreen tooltip probe", WS_OVERLAPPED,
            -32000, -32000, 640, 480, nullptr, nullptr, module, nullptr);
        CHECK(native.owner);
        native.child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 640, 480,
            native.owner, nullptr, module, nullptr);
        CHECK(native.child);
        native.tip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, -32000, -32000,
            0, 0, native.child, nullptr, module, nullptr);
        CHECK(native.tip);
        CHECK(SetWindowSubclass(native.tip, OffscreenOnly, 1, DWORD_PTR(&native)));
        CHECK(SetWindowPos(native.tip, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
        struct Version { DWORD size, major, minor, build, platform; } version{sizeof(Version), 0, 0, 0, 0};
        auto versionProc = reinterpret_cast<HRESULT (CALLBACK*)(Version*)>(GetProcAddress(
            reinterpret_cast<HMODULE>(GetClassLongPtrW(native.tip, GCLP_HMODULE)), "DllGetVersion"));
        CHECK(versionProc && SUCCEEDED(versionProc(&version)));
        std::cout << "NATIVE_CONTROL context=" << (positional == 2 ? "v6" : "default")
            << " version=" << version.major << '.' << version.minor << " build=" << version.build
            << " full_size=" << sizeof(TOOLINFOW) << " v2_size=" << TTTOOLINFOW_V2_SIZE << '\n';
        native.font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        CHECK(native.font); SendMessageW(native.tip, WM_SETFONT, WPARAM(native.font), FALSE);
        TOOLINFOW tool{}; tool.cbSize = TTTOOLINFOW_V2_SIZE;
        tool.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
        tool.hwnd = native.child; tool.uId = 1; tool.lpszText = native.text.data();
        CHECK(SendMessageW(native.tip, TTM_ADDTOOLW, 0, LPARAM(&tool)));
        std::cout << "NATIVE_V2_ADD accepted=1 cbSize=" << tool.cbSize << '\n';
        tool.uFlags = 0;
        SendMessageW(native.tip, TTM_SETMAXTIPWIDTH, 0, 320);

        // All structs use V2. Bubble queries are optional diagnostics only.
        SIZE dimensions[3]{};
        const wchar_t* texts[] = {L"Short tip", L"A much longer recorded sample title\r\nCurrent: 57%\r\nPrevious week: 42%", L"Short tip"};
        int showsAfterActivation = 0, hidesAfterActivation = 0, hidePositionsAfterActivation = 0, bubbleQueries = 0;
        for (int i = 0; i < 3; ++i) {
            std::wstring replacement = texts[i]; native.text.swap(replacement);
            tool.lpszText = native.text.data();
            std::cout << "NATIVE_STAGE text=" << i << " before_update\n";
            SendMessageW(native.tip, TTM_UPDATETIPTEXTW, 0, LPARAM(&tool));
            std::cout << "NATIVE_STAGE text=" << i << " after_update\n";
            if (i == 0) {
                // Known negative control: v5 can fault here. Only run in an opt-in child process.
                if (preactivation) {
                    std::cout << "NATIVE_STAGE before_preactivation_getbubble\n";
                    ++bubbleQueries;
                    LRESULT before = SendMessageW(native.tip, TTM_GETBUBBLESIZE, 0, LPARAM(&tool));
                    std::cout << "NATIVE_PREACTIVATION width=" << LOWORD(before) << " height=" << HIWORD(before)
                        << " usable_for_sizing=not_assumed\n";
                }
                SendMessageW(native.tip, TTM_TRACKPOSITION, 0, MAKELPARAM(-32000, -32000));
                SendMessageW(native.tip, TTM_TRACKACTIVATE, TRUE, LPARAM(&tool));
                showsAfterActivation = native.showNotifications; hidesAfterActivation = native.hideNotifications;
                hidePositionsAfterActivation = native.hidePositions;
            }
            // No pumping, sleeping, extra activation, or repositioning between active text updates.
            RECT rect{}; CHECK(GetWindowRect(native.tip, &rect));
            dimensions[i] = {rect.right-rect.left, rect.bottom-rect.top};
            LRESULT bubble = 0;
            if (preactivation) { ++bubbleQueries; bubble = SendMessageW(native.tip, TTM_GETBUBBLESIZE, 0, LPARAM(&tool)); }
            std::cout << "NATIVE_ACTIVE text=" << i << " width=" << dimensions[i].cx << " height=" << dimensions[i].cy
                << " x=" << rect.left << " y=" << rect.top;
            if (preactivation) std::cout << " bubble=" << LOWORD(bubble) << 'x' << HIWORD(bubble);
            else std::cout << " bubble=not_queried";
            std::cout << " show_notifications=" << native.showNotifications << " hide_notifications=" << native.hideNotifications << '\n';
            CHECK(dimensions[i].cx > 20 && dimensions[i].cx <= 360);
            CHECK(dimensions[i].cy >= 12 && dimensions[i].cy < 300);
            CHECK(rect.left == -32000 && rect.top == -32000);
            CHECK(!IsWindowVisible(native.owner) && !IsWindowVisible(native.child));
            CHECK(IsWindowVisible(native.tip)); // WS_VISIBLE offscreen, never on the user's display.
            RECT stable{}; CHECK(GetWindowRect(native.tip, &stable)); CHECK(EqualRect(&stable, &rect));
            if (preactivation) CHECK(LOWORD(bubble) > 20 && LOWORD(bubble) <= 360 && HIWORD(bubble) >= 12 && HIWORD(bubble) < 300);
            CHECK(native.showNotifications == showsAfterActivation && native.hideNotifications == hidesAfterActivation);
            CHECK(native.hidePositions == hidePositionsAfterActivation);
            wchar_t stored[1024]{};
            TOOLINFOW query{}; query.cbSize = TTTOOLINFOW_V2_SIZE; query.hwnd = native.child; query.uId = 1; query.lpszText = stored;
            SendMessageW(native.tip, TTM_GETTEXTW, std::size(stored), LPARAM(&query));
            CHECK(std::wstring(stored) == native.text);
            TOOLINFOW current{}; current.cbSize = TTTOOLINFOW_V2_SIZE;
            CHECK(SendMessageW(native.tip, TTM_GETCURRENTTOOLW, 0, LPARAM(&current)));
            CHECK(current.hwnd == native.child && current.uId == 1);
        }
        CHECK(dimensions[1].cx > dimensions[0].cx); CHECK(dimensions[1].cy > dimensions[0].cy);
        CHECK(dimensions[2].cx == dimensions[0].cx && dimensions[2].cy == dimensions[0].cy);
        SendMessageW(native.tip, TTM_TRACKACTIVATE, FALSE, LPARAM(&tool));
        CHECK(!IsWindowVisible(native.tip));
        if (!preactivation) CHECK(bubbleQueries == 0);
        std::cout << "NATIVE_V2_DEACTIVATE hidden=1 cbSize=" << tool.cbSize
            << " activation_calls=1 deactivation_calls=1 offscreen_safety_corrections=" << native.clampedMoves
            << " getbubblesize_calls=" << bubbleQueries << '\n';
        if (preactivation) std::cout << "NATIVE_DIAGNOSTIC completed=1 observation_only=1 assertions=" << assertions << '\n';
        else std::cout << "ACTIVE_RESULT 1 passed, 0 failed; " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) {
        if (preactivation)
            std::cout << "NATIVE_DIAGNOSTIC completed=0 observation_only=1 error=" << error.what()
                << " winerror=" << GetLastError() << '\n';
        else {
            std::cout << "ACTIVE_FAIL " << error.what() << " winerror=" << GetLastError() << '\n';
            std::cout << "ACTIVE_RESULT 0 passed, 1 failed; " << assertions << " assertions\n";
        }
        return 1;
    }
}
