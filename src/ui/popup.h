// Dropdown popup (native): an owned window listing a dropdown's options. It never activates
// (WS_EX_NOACTIVATE, shown without activation, no taskbar button) and holds mouse capture while
// open; keyboard focus stays on the dropdown field, whose keys drive it. The host creates it and
// routes its messages and every close trigger (ui/host.h).

struct UiPopup {
    HWND window = nullptr;
    UiSurface surface;
    UINT dpi = 96;
    uint32_t field = 0;        // The dropdown item that opened it
    std::vector<UiItem> rows;  // Popup DIPs; ids 1..n
    int highlight = -1;        // Row under the pointer or chosen with keys
    int pressed = -1;          // Row pressed with the left button
    bool open = false;
    // An outside press closed it: the window is hidden but keeps capture until this button-up,
    // so the release doesn't reach whatever is under the pointer.
    UINT swallowUp = 0;
};

static constexpr DWORD kUiPopupExStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

// The one close routine: marks the popup closed first, releases capture only while the popup
// still holds it, destroys the window and never restores focus or the foreground. The capture
// change it causes re-enters here and finds nothing left to do.
static void UiPopupClose(UiPopup& popup) {
    popup.open = false;
    popup.swallowUp = 0;
    popup.highlight = popup.pressed = -1;
    HWND window = std::exchange(popup.window, nullptr);
    if (!window) return;
    if (GetCapture() == window) ReleaseCapture();
    DestroyWindow(window);
}

// The row under a popup client point in pixels, or -1.
static int UiPopupRowAt(const UiPopup& popup, POINT point) {
    float scale = float(popup.dpi) / 96;
    D2D1_POINT_2F at{float(point.x) / scale, float(point.y) / scale};
    for (size_t i = 0; i < popup.rows.size(); i++) {
        if (UiContains(popup.rows[i].rect, at)) return int(i);
    }
    return -1;
}

// Paints the rows on the popup surface; false when it can't draw.
static bool UiPaintPopup(UiPopup& popup, UiGraphics& g, const UiPalette& palette) {
    ID2D1HwndRenderTarget* target = UiSurfaceTarget(popup.surface, g.d2d.get(), popup.dpi);
    if (!target) return false;
    float scale = float(popup.dpi) / 96;
    UiPaintState state;
    if (popup.highlight >= 0) state.hot = {uint32_t(popup.highlight + 1), -1};
    if (popup.pressed >= 0) state.pressed = {uint32_t(popup.pressed + 1), -1};
    target->BeginDraw();
    D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
    target->SetTransform(&identity);
    target->Clear(palette.popup);
    UiPaintItems(target, g, popup.surface.cache, palette, popup.rows, state, {0, 0}, scale, -1e9f,
                 1e9f);
    // Windows draws the rounded border; high contrast gets its own in the text color.
    if (palette.kind == UiThemeKind::HighContrast) {
        D2D1_SIZE_F size = target->GetSize();
        UiStrokeRect(target, popup.surface.cache, {0, 0, size.width, size.height}, 0,
                     palette.cardBorder, 1, scale);
    }
    return UiSurfaceDrawResult(popup.surface, target->EndDraw());
}
