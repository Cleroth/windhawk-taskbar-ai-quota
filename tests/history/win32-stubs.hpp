// Standalone Win32 UI seams. No real windows, settings, journal, auth or network.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

static std::atomic<bool> g_unloading{false}, g_historyWindowCancelRequested{false};
struct StubControl {
    std::vector<std::wstring> items;
    std::wstring text;
    int selection = -1, resets = 0;
    bool enabled = true;
};
static std::map<HWND, StubControl> controls;
static RECT clientRect{0, 0, 1200, 1800};
static SCROLLINFO lastScroll{};
static HDC paintDc = nullptr;
static bool forceHighContrast = false;
static HWND focusWindow = nullptr;
static int invalidations = 0, measurements = 0, focusRects = 0, callbackErrors = 0;
static int clientQueries = 0, scrollCalls = 0, dcReleases = 0, pointerReplacements = 0;
static std::vector<HWND> invalidationWindows;
static std::vector<UINT> defWindowCalls;
static LRESULT defWindowResult = 0;
static std::function<void(UINT)> tipCallback;
static std::function<void()> tipRectCallback;
static std::function<void()> measurementCallback;
static std::function<void()> pointTestCallback;
static bool failGetDc = false, failTipCreate = false, failTipAdd = false, failTracking = false;
static bool failTipRect = false;
static int tipRectQueries = 0;
static constexpr UINT kTipRectQuery = UINT_MAX;
static std::vector<UINT> tipOperations;
static std::map<HWND, LONG_PTR> windowData;
static std::map<HWND, RECT> windowRects;
static std::vector<HWND> destroyedWindows;
static std::vector<DWORD> trackingCalls;
struct TipCall { UINT message; WPARAM value; LPARAM data; UINT toolSize = 0; };
static std::vector<TipCall> tipCalls;
static std::wstring tipText;
static const wchar_t* tipTextPointer = nullptr;
static POINT tipPosition{};
static RECT monitorWork{0, 0, 1920, 1080};
static SIZE tipSize{440, 220};
static bool tipActive = false;
static int deleteFailures = 0;
static std::vector<DWORD> failedDeleteTypes;
struct TextCall { std::wstring text; RECT rect; UINT flags; COLORREF color; };
struct PenCall { DWORD style, width; COLORREF color; };
struct StrokeCall { std::vector<POINT> points; PenCall pen; };
struct LineCall { POINT a, b; PenCall pen; };
static std::map<HGDIOBJ, PenCall> capturedPens;
static std::vector<StrokeCall> strokes;
static std::vector<LineCall> lines;
static std::vector<RECT> ellipses;
static std::vector<TextCall> textCalls;
static std::vector<PenCall> penCalls;
static int polylineCalls = 0, beginPaintCalls = 0, endPaintCalls = 0;

static HWND FakeHandle(int value) { return reinterpret_cast<HWND>(INT_PTR(value)); }
static LRESULT StubSendMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == TTM_GETBUBBLESIZE)
        throw std::runtime_error("Production must never send TTM_GETBUBBLESIZE");
    if (message == TTM_ADDTOOLW || message == TTM_UPDATETIPTEXTW || message == TTM_TRACKACTIVATE ||
        message == TTM_TRACKPOSITION || message == TTM_SETMAXTIPWIDTH) {
        tipCalls.push_back({message, wParam, lParam});
        tipOperations.push_back(message);
        if (message == TTM_ADDTOOLW || message == TTM_UPDATETIPTEXTW || message == TTM_TRACKACTIVATE) {
            tipCalls.back().toolSize = reinterpret_cast<TOOLINFOW*>(lParam)->cbSize;
            if (tipCalls.back().toolSize != TTTOOLINFOW_V2_SIZE)
                throw std::runtime_error("Graph tooltip must use TOOLINFOW V2 size at every native call");
        }
        if (message == TTM_ADDTOOLW) return !failTipAdd;
        if (message == TTM_UPDATETIPTEXTW) {
            const wchar_t* old = tipTextPointer;
            std::wstring previous = tipText;
            if (old && std::wstring(old) != previous)
                throw std::runtime_error("Old tooltip text died before synchronous replacement entered");
            tipTextPointer = reinterpret_cast<TOOLINFOW*>(lParam)->lpszText;
            tipText = tipTextPointer;
            if (old) ++pointerReplacements;
            if (tipCallback) tipCallback(message);
            if (old && std::wstring(old) != previous)
                throw std::runtime_error("Old tooltip text died before synchronous replacement returned");
            return 0;
        }
        if (message == TTM_TRACKACTIVATE) {
            if (tipTextPointer && std::wstring(tipTextPointer) != tipText)
                throw std::runtime_error("Tooltip backing text changed before native endpoint replacement/destruction");
            tipActive = wParam != 0;
        }
        if (message == TTM_TRACKPOSITION) tipPosition = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (tipCallback) tipCallback(message);
        return 0;
    }
    auto& c = controls[window];
    switch (message) {
        case CB_RESETCONTENT: c.items.clear(); c.selection = -1; ++c.resets; return 0;
        case CB_ADDSTRING: c.items.emplace_back(reinterpret_cast<PCWSTR>(lParam)); return c.items.size() - 1;
        case CB_SETCURSEL: c.selection = int(wParam); return c.selection;
        case CB_GETCURSEL: return c.selection;
        case WM_SETFONT: return 0;
        default: throw std::runtime_error("Unexpected SendMessage stub call");
    }
}
static BOOL StubGetClientRect(HWND window, RECT* rect) {
    ++clientQueries;
    *rect = windowRects.contains(window) ? windowRects.at(window) : clientRect;
    return TRUE;
}
static BOOL StubSetWindowText(HWND window, PCWSTR text) { controls[window].text = text; return TRUE; }
static BOOL StubEnableWindow(HWND window, BOOL enable) { controls[window].enabled = enable; return TRUE; }
static int StubSetScrollInfo(HWND, int, const SCROLLINFO* info, BOOL) { ++scrollCalls; lastScroll = *info; return info->nPos; }
static BOOL StubInvalidateRect(HWND window, const RECT*, BOOL) { ++invalidations; invalidationWindows.push_back(window); return TRUE; }
static BOOL StubUpdateWindow(HWND) { return TRUE; }
static HDC StubBeginPaint(HWND, PAINTSTRUCT* paint) {
    ++beginPaintCalls;
    if (!paintDc) throw std::runtime_error("Paint requires offscreen GDI surface");
    paint->rcPaint = clientRect;
    paint->hdc = paintDc;
    return paintDc;
}
static BOOL StubEndPaint(HWND, const PAINTSTRUCT*) { ++endPaintCalls; return TRUE; }
static HWND StubGetFocus() { return focusWindow; }
static HWND StubSetFocus(HWND window) { HWND old = focusWindow; focusWindow = window; return old; }
static HDC StubGetDC(HWND) { ++measurements; return failGetDc ? nullptr : CreateCompatibleDC(nullptr); }
static int StubReleaseDC(HWND, HDC dc) { ++dcReleases; return DeleteDC(dc); }
static BOOL StubTrackMouseEvent(TRACKMOUSEEVENT* event) { trackingCalls.push_back(event->dwFlags); return !failTracking; }
static BOOL StubClientToScreen(HWND, POINT* point) { point->x += 80; point->y += 40; return TRUE; }
static HMONITOR StubMonitorFromPoint(POINT, DWORD) { return reinterpret_cast<HMONITOR>(1); }
static BOOL StubGetMonitorInfo(HMONITOR, MONITORINFO* info) { info->rcWork = monitorWork; return TRUE; }
static BOOL StubGetWindowRect(HWND, RECT* rect) {
    ++tipRectQueries; tipOperations.push_back(kTipRectQuery);
    if (!tipActive) throw std::runtime_error("Tooltip rectangle requested before native activation");
    if (tipRectCallback) tipRectCallback();
    if (failTipRect) return FALSE;
    *rect = {tipPosition.x, tipPosition.y, tipPosition.x+tipSize.cx, tipPosition.y+tipSize.cy};
    return TRUE;
}
static LONG_PTR StubGetWindowLongPtr(HWND window, int) { return windowData[window]; }
static LONG_PTR StubSetWindowLongPtr(HWND window, int, LONG_PTR value) { LONG_PTR old = windowData[window]; windowData[window] = value; return old; }
static LRESULT StubDefWindowProc(HWND, UINT message, WPARAM, LPARAM) { defWindowCalls.push_back(message); return defWindowResult; }
static HWND StubCreateWindowEx(DWORD, PCWSTR kind, PCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) {
    if (std::wstring(kind) != TOOLTIPS_CLASSW) throw std::runtime_error("Only fake tooltip creation allowed");
    return failTipCreate ? nullptr : FakeHandle(90);
}
static BOOL StubDestroyWindow(HWND window) {
    if (tipTextPointer && std::wstring(tipTextPointer) != tipText)
        throw std::runtime_error("Tooltip backing text invalid before destruction");
    destroyedWindows.push_back(window); tipActive = false; tipTextPointer = nullptr; return TRUE;
}
static BOOL StubPostMessage(HWND, UINT, WPARAM, LPARAM) { return TRUE; }
static void StubLog(PCWSTR) { ++callbackErrors; }
static BOOL StubGetScrollInfo(HWND, int, SCROLLINFO* info) { *info = lastScroll; return TRUE; }
static BOOL StubEnumChildWindows(HWND, WNDENUMPROC callback, LPARAM value) { callback(FakeHandle(1), value); return TRUE; }
static BOOL StubMoveWindow(HWND window, int, int, int w, int h, BOOL) {
    if (window == FakeHandle(1)) clientRect = {0, 0, w, h};
    return TRUE;
}
static BOOL StubShowWindow(HWND, int) { return TRUE; }
static BOOL StubSetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
static BOOL StubSystemParametersInfo(UINT action, UINT, PVOID data, UINT) {
    if (action == SPI_GETWHEELSCROLLLINES) { *static_cast<UINT*>(data) = 3; return TRUE; }
    if (action != SPI_GETHIGHCONTRAST) throw std::runtime_error("Unexpected system parameter query");
    static_cast<HIGHCONTRASTW*>(data)->dwFlags = forceHighContrast ? HCF_HIGHCONTRASTON : 0;
    return TRUE;
}
static int CaptureDrawText(HDC dc, PCWSTR text, int length, RECT* rect, UINT flags) {
    textCalls.push_back({std::wstring(text, length), *rect, flags, GetTextColor(dc)});
    int result = ::DrawTextW(dc, text, length, rect, flags);
    if ((flags & DT_CALCRECT) && measurementCallback) measurementCallback();
    return result;
}
static BOOL CapturePtInRect(const RECT* rect, POINT point) {
    BOOL result = ::PtInRect(rect, point);
    if (pointTestCallback) pointTestCallback();
    return result;
}
static HPEN CaptureExtCreatePen(DWORD style, DWORD width, const LOGBRUSH* brush, DWORD count, const DWORD* styles) {
    penCalls.push_back({style, width, brush->lbColor});
    HPEN pen = ::ExtCreatePen(style, width, brush, count, styles);
    if (!pen) throw std::runtime_error("ExtCreatePen failed");
    capturedPens[pen] = penCalls.back();
    return pen;
}
static BOOL CapturePolyline(HDC dc, const POINT* points, int count) {
    ++polylineCalls;
    strokes.push_back({std::vector<POINT>(points, points + count), capturedPens.at(GetCurrentObject(dc, OBJ_PEN))});
    BOOL result = ::Polyline(dc, points, count);
    if (!result) throw std::runtime_error("Polyline failed");
    return result;
}
static BOOL CaptureEllipse(HDC dc, int left, int top, int right, int bottom) {
    ellipses.push_back({left, top, right, bottom});
    return ::Ellipse(dc, left, top, right, bottom);
}
static BOOL CaptureLineTo(HDC dc, int x, int y) {
    POINT a{}; GetCurrentPositionEx(dc, &a);
    HGDIOBJ pen = GetCurrentObject(dc, OBJ_PEN);
    PenCall style = capturedPens.contains(pen) ? capturedPens.at(pen) : PenCall{PS_SOLID, 1, GetDCPenColor(dc)};
    lines.push_back({a, {x, y}, style});
    return ::LineTo(dc, x, y);
}
static BOOL CaptureFocusRect(HDC dc, const RECT* rect) { ++focusRects; return ::DrawFocusRect(dc, rect); }
static BOOL CaptureDeleteObject(HGDIOBJ object) {
    DWORD type = GetObjectType(object);
    BOOL result = ::DeleteObject(object);
    if (!result) { ++deleteFailures; failedDeleteTypes.push_back(type); }
    return result;
}
#define SendMessageW StubSendMessage
#define GetClientRect StubGetClientRect
#define SetWindowTextW StubSetWindowText
#define EnableWindow StubEnableWindow
#define SetScrollInfo StubSetScrollInfo
#define InvalidateRect StubInvalidateRect
#define UpdateWindow StubUpdateWindow
#define BeginPaint StubBeginPaint
#define EndPaint StubEndPaint
#define GetFocus StubGetFocus
#define SystemParametersInfoW StubSystemParametersInfo
#define DrawTextW CaptureDrawText
#define ExtCreatePen CaptureExtCreatePen
#define Polyline CapturePolyline
#define Ellipse CaptureEllipse
#define LineTo CaptureLineTo
#define DrawFocusRect CaptureFocusRect
#define GetDC StubGetDC
#define ReleaseDC StubReleaseDC
#define TrackMouseEvent StubTrackMouseEvent
#define ClientToScreen StubClientToScreen
#define MonitorFromPoint StubMonitorFromPoint
#define GetMonitorInfoW StubGetMonitorInfo
#define GetWindowRect StubGetWindowRect
#define GetWindowLongPtrW StubGetWindowLongPtr
#define SetWindowLongPtrW StubSetWindowLongPtr
#define DefWindowProcW StubDefWindowProc
#define CreateWindowExW StubCreateWindowEx
#define DestroyWindow StubDestroyWindow
#define PostMessageW StubPostMessage
#define Wh_Log StubLog
#define GetScrollInfo StubGetScrollInfo
#define SetFocus StubSetFocus
#define EnumChildWindows StubEnumChildWindows
#define MoveWindow StubMoveWindow
#define ShowWindow StubShowWindow
#define SetWindowPos StubSetWindowPos
#define DeleteObject CaptureDeleteObject
#define PtInRect CapturePtInRect
