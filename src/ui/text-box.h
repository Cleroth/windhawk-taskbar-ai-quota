// Native text boxes of the custom-drawn windows: one single-line EDIT per number box or text box
// item, keyed by the item's id, a child of the viewport over the item's text area. Native entry
// keeps IME, caret, selection, clipboard and undo; rebuilds keep the window and set its text only
// when the client's text changed. The host creates them and routes their messages (ui/host.h).

struct UiTextBox {
    uint32_t id = 0;
    HWND window = nullptr;
    std::wstring text;       // What the client last set or was told
    std::wstring cue;        // The placeholder (cue banner) last set
    bool composing = false;  // IME composition in progress: native handling only
};

struct UiTextBoxes {
    std::vector<UiTextBox> boxes;
    HFONT font = nullptr;
    UINT fontDpi = 0;
    int fontHeight = 0;  // Pixels
    std::vector<std::pair<COLORREF, HBRUSH>> brushes;
    bool programmatic = false;  // Inside our own text set: its EN_CHANGE isn't an edit
    // Focus is leaving the window (deactivation, a modal), not moving inside it: the box keeps
    // its draft instead of reporting focus loss.
    bool leavingWindow = false;
    // Their items moved in place: they follow at the viewport's next paint.
    bool moved = false;
};

static UiTextBox* UiFindTextBox(UiTextBoxes& boxes, uint32_t id) {
    for (UiTextBox& box : boxes.boxes) {
        if (box.id == id) return &box;
    }
    return nullptr;
}

static UiTextBox* UiFindTextBox(UiTextBoxes& boxes, HWND window) {
    if (!window) return nullptr;
    for (UiTextBox& box : boxes.boxes) {
        if (box.window == window) return &box;
    }
    return nullptr;
}

static std::wstring UiTextBoxText(HWND window) {
    std::wstring text(size_t(std::max(GetWindowTextLengthW(window), 0)) + 1, L'\0');
    text.resize(size_t(std::max(GetWindowTextW(window, text.data(), int(text.size())), 0)));
    return text;
}

static void UiSetTextBoxText(UiTextBoxes& boxes, UiTextBox& box, const std::wstring& text) {
    box.text = text;
    boxes.programmatic = true;
    SetWindowTextW(box.window, text.c_str());
    boxes.programmatic = false;
    if (GetFocus() == box.window) SendMessageW(box.window, EM_SETSEL, text.size(), text.size());
}

// The body font at `dpi` (Segoe UI Variable Text, else Segoe UI), applied to every box. The
// previous font is deleted only after no box uses it.
static HFONT UiTextBoxFont(UiTextBoxes& boxes, UINT dpi) {
    if (boxes.font && boxes.fontDpi == dpi) return boxes.font;
    HFONT font = nullptr;
    HDC dc = GetDC(nullptr);
    for (PCWSTR face : kUiFamilyNames[size_t(UiFamily::Text)]) {
        font = CreateFontW(-MulDiv(14, int(dpi), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
        if (!font || !dc) break;
        // GDI substitutes a missing face; keep the font only when it resolved to this one.
        HGDIOBJ previous = SelectObject(dc, font);
        wchar_t resolved[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, resolved);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, previous);
        boxes.fontHeight = int(metrics.tmHeight);
        if (_wcsicmp(resolved, face) == 0) break;
        DeleteObject(font);
        font = nullptr;
    }
    if (dc) ReleaseDC(nullptr, dc);
    if (!font) {
        font = HFONT(GetStockObject(DEFAULT_GUI_FONT));
        boxes.fontHeight = MulDiv(20, int(dpi), 96);
    }
    for (const UiTextBox& box : boxes.boxes) {
        if (box.window) SendMessageW(box.window, WM_SETFONT, WPARAM(font), TRUE);
    }
    if (boxes.font) DeleteObject(boxes.font);
    boxes.font = font;
    boxes.fontDpi = dpi;
    return font;
}

static COLORREF UiColorRef(D2D1_COLOR_F color) {
    auto channel = [](float value) { return BYTE(std::lround(std::clamp(value, 0.0f, 1.0f) * 255)); };
    return RGB(channel(color.r), channel(color.g), channel(color.b));
}

// Text and fill of a box on a card, matching the drawn frame (paint.h): {text, fill}.
static std::pair<COLORREF, COLORREF> UiTextBoxColors(const UiPalette& p, bool focused, bool enabled) {
    D2D1_COLOR_F fill = UiOver(!enabled ? p.controlDisabled : focused ? p.inputActive : p.control,
                               UiOver(p.card, p.background));
    return {UiColorRef(UiOver(enabled ? p.text : p.textDisabled, fill)), UiColorRef(fill)};
}

// Over the window's backdrop a box draws on its own opaque layer: GDI leaves its pixels' alpha
// empty, which DWM would blend into the backdrop (text vanishing on light themes). Its fill is
// the box's color over the palette's opaque background, close to the backdrop's.
static void UiLayerTextBox(HWND window, bool layered) {
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (((style & WS_EX_LAYERED) != 0) == layered) return;
    SetWindowLongPtrW(window, GWL_EXSTYLE, layered ? style | WS_EX_LAYERED : style & ~LONG_PTR(WS_EX_LAYERED));
    if (layered && !SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA)) {
        Wh_Log(L"Could not layer a text box: %lu", GetLastError());
        SetWindowLongPtrW(window, GWL_EXSTYLE, style);
    }
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
}

static HBRUSH UiTextBoxBrush(UiTextBoxes& boxes, COLORREF color) {
    for (const auto& [key, brush] : boxes.brushes) {
        if (key == color) return brush;
    }
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) boxes.brushes.emplace_back(color, brush);
    return brush;
}

// After the boxes' windows are gone: their font and brushes.
static void UiReleaseTextBoxes(UiTextBoxes& boxes) {
    boxes.boxes.clear();
    for (const auto& [color, brush] : boxes.brushes) DeleteObject(brush);
    boxes.brushes.clear();
    if (boxes.font && boxes.font != HFONT(GetStockObject(DEFAULT_GUI_FONT))) DeleteObject(boxes.font);
    boxes.font = nullptr;
    boxes.fontDpi = 0;
}
