// Tooltip window (native): an owned popup showing a chart's hover readout or an item's complete
// text beside the pointer, never under it (ui/tooltip-layout.h). It never activates or takes
// focus or capture (WS_EX_NOACTIVATE, shown without activation, HTTRANSPARENT, MA_NOACTIVATE)
// and draws on its own target at its monitor's DPI. The host creates it on first use, routes
// its messages and every hide trigger, and destroys it with the main window (ui/host.h).

struct UiTooltip {
    HWND window = nullptr;
    UiSurface surface;
    UINT dpi = 96;
    UiTooltipModel model;
    std::vector<UiItem> items;      // Tooltip DIPs
    uint32_t pending = 0;           // The item whose hover delay runs (the main window's timer)
    uint32_t pendingGeneration = 0; // The frame it was hot in
};

static constexpr UINT_PTR kUiTooltipTimer = 0x7E02;
static constexpr DWORD kUiTooltipExStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

// The cursor image's screen rectangle with its hotspot on `point`: the color bitmap's size, or
// the monochrome mask's with half its height (AND and XOR masks stacked); both bitmaps freed.
// Without a visible cursor image: SM_CXCURSOR at `dpi` on each side of the point.
static RECT UiCursorRect(POINT point, UINT dpi) {
    CURSORINFO cursor{sizeof(cursor)};
    ICONINFO icon{};
    if (GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) && cursor.hCursor &&
        GetIconInfo(cursor.hCursor, &icon)) {
        BITMAP bitmap{};
        bool color = icon.hbmColor && GetObjectW(icon.hbmColor, sizeof(bitmap), &bitmap);
        if (!color) {
            if (icon.hbmMask && GetObjectW(icon.hbmMask, sizeof(bitmap), &bitmap)) bitmap.bmHeight /= 2;
            else bitmap = {};
        }
        if (icon.hbmColor) DeleteObject(icon.hbmColor);
        if (icon.hbmMask) DeleteObject(icon.hbmMask);
        if (bitmap.bmWidth > 0 && bitmap.bmHeight > 0) {
            LONG left = point.x - LONG(icon.xHotspot), top = point.y - LONG(icon.yHotspot);
            return {left, top, left + bitmap.bmWidth, top + bitmap.bmHeight};
        }
    }
    // Not declared by the toolchain's default headers (Windows 10 1607 and later).
    using GetSystemMetricsForDpi_t = int(WINAPI*)(int, UINT);
    static const auto metricsForDpi = reinterpret_cast<GetSystemMetricsForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
    LONG size = std::max(metricsForDpi ? metricsForDpi(SM_CXCURSOR, dpi) : MulDiv(32, int(dpi), 96), 1);
    return {point.x - size, point.y - size, point.x + size, point.y + size};
}

// A monitor's effective DPI (shcore's GetDpiForMonitor, when the process has shcore loaded, as
// Explorer does), else `fallback`.
static UINT UiMonitorDpi(HMONITOR monitor, UINT fallback) {
    using GetDpiForMonitor_t = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    auto getDpi = shcore ? reinterpret_cast<GetDpiForMonitor_t>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
    UINT x = 0, y = 0;
    return getDpi && SUCCEEDED(getDpi(monitor, 0 /* MDT_EFFECTIVE_DPI */, &x, &y)) && x ? x : fallback;
}

// The one hide routine: cancels the hover delay, hides the window (no activation, focus or
// foreground change) and forgets what it showed, so the next show starts afresh. Repeated calls
// do nothing.
static void UiTooltipHide(UiTooltip& tip, HWND owner) {
    if (std::exchange(tip.pending, 0) && owner) KillTimer(owner, kUiTooltipTimer);
    tip.model = {};
    tip.items.clear();
    if (tip.window && IsWindowVisible(tip.window)) {
        SetWindowPos(tip.window, nullptr, 0, 0, 0, 0,
                     SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
}

// Paints the items on the tooltip's surface; false when it can't draw.
static bool UiPaintTooltip(UiTooltip& tip, UiGraphics& g, const UiPalette& palette) {
    ID2D1HwndRenderTarget* target = UiSurfaceTarget(tip.surface, g.d2d.get(), tip.dpi);
    if (!target) return false;
    float scale = float(tip.dpi) / 96;
    target->BeginDraw();
    D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
    target->SetTransform(&identity);
    target->Clear(palette.popup);
    UiPaintItems(target, g, tip.surface.cache, palette, tip.items, {}, {0, 0}, scale, -1e9f, 1e9f);
    // Windows draws the rounded border; high contrast gets its own in the text color.
    if (palette.kind == UiThemeKind::HighContrast) {
        D2D1_SIZE_F size = target->GetSize();
        UiStrokeRect(target, tip.surface.cache, {0, 0, size.width, size.height}, 0, palette.cardBorder, 1, scale);
    }
    return UiSurfaceDrawResult(tip.surface, target->EndDraw());
}
