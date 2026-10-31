// Headers the extracted toolkit and its native host expect from the mod's entry file, plus the
// suite's only stub: Wh_Log prints to the probe's console.
#include "harness.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <atomic>
#include <cstdarg>

static void Wh_Log(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    std::fputws(L"LOG ", stdout);
    std::vfwprintf(stdout, format, args);
    std::fputwc(L'\n', stdout);
    std::fflush(stdout);
    va_end(args);
}
