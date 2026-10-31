// Direct2D/DirectWrite primitives and resource caches of the custom-drawn windows. Pure: callers
// pass the factories and the target; nothing here touches windows or globals. Coordinates are
// DIPs; targets carry the window DPI.

// Device-independent resources shared by every target of one UI thread: resolved font families,
// text formats, effect-free layouts (measuring and plain drawing) and path geometries. Owned by
// that thread's graphics scope, released before the apartment is.
struct UiGraphics {
    winrt::com_ptr<ID2D1Factory> d2d;
    winrt::com_ptr<IDWriteFactory> dwrite;
    winrt::com_ptr<ID2D1StrokeStyle> roundCaps;
    std::array<std::wstring, 3> families;
    std::map<std::wstring, winrt::com_ptr<IDWriteTextFormat>> formats;
    std::map<std::wstring, winrt::com_ptr<IDWriteTextLayout>> layouts;
    std::map<PCWSTR, winrt::com_ptr<ID2D1PathGeometry>> paths;
};

// Resources bound to one render target: a recolored brush for primitives, drawing-effect
// brushes by color, and layouts that carry those effects. Discarded with the target.
struct UiTargetCache {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    std::map<uint32_t, winrt::com_ptr<ID2D1SolidColorBrush>> effects;
    std::map<std::wstring, winrt::com_ptr<IDWriteTextLayout>> layouts;

    void Clear() {
        layouts.clear();
        effects.clear();
        brush = nullptr;
    }
};

static void UiCreateGraphics(UiGraphics& g) {
    winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                           __uuidof(ID2D1Factory), nullptr, g.d2d.put_void()));
    winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                             reinterpret_cast<IUnknown**>(g.dwrite.put())));
    D2D1_STROKE_STYLE_PROPERTIES round = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND);
    winrt::check_hresult(g.d2d->CreateStrokeStyle(&round, nullptr, 0, g.roundCaps.put()));
}

// Effect-free layouts accumulate while text changes; trimming between frames bounds them. No
// layout pointer from UiLayout may be held across this call.
static void UiTrimCaches(UiGraphics& g) {
    if (g.layouts.size() > 512) g.layouts.clear();
}

static IDWriteTextFormat* UiFormat(UiGraphics& g, const UiTextStyle& style) {
    std::wstring key = std::to_wstring(int(style.family)) + L'|' + std::to_wstring(style.size) +
                       L'|' + std::to_wstring(style.lineHeight) + L'|' +
                       std::to_wstring(int(style.weight));
    auto& format = g.formats[key];
    if (format) return format.get();

    winrt::com_ptr<IDWriteFontCollection> fonts;
    winrt::check_hresult(g.dwrite->GetSystemFontCollection(fonts.put(), FALSE));
    std::wstring& family = g.families[size_t(style.family)];
    if (family.empty()) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        PCWSTR preferred = kUiFamilyNames[size_t(style.family)][0];
        family = SUCCEEDED(fonts->FindFamilyName(preferred, &index, &exists)) && exists ?
                     preferred : kUiFamilyNames[size_t(style.family)][1];
    }
    winrt::check_hresult(g.dwrite->CreateTextFormat(
        family.c_str(), nullptr, style.weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, style.size, L"en-us", format.put()));

    // Uniform line height with the baseline where CSS puts it: half the leading above the
    // ascent. Fonts without metrics keep DirectWrite's default spacing.
    UINT32 index = 0;
    BOOL exists = FALSE;
    winrt::com_ptr<IDWriteFontFamily> fontFamily;
    winrt::com_ptr<IDWriteFont> font;
    if (SUCCEEDED(fonts->FindFamilyName(family.c_str(), &index, &exists)) && exists &&
        SUCCEEDED(fonts->GetFontFamily(index, fontFamily.put())) &&
        SUCCEEDED(fontFamily->GetFirstMatchingFont(style.weight, DWRITE_FONT_STRETCH_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL, font.put()))) {
        DWRITE_FONT_METRICS metrics{};
        font->GetMetrics(&metrics);
        if (metrics.designUnitsPerEm) {
            float unit = style.size / float(metrics.designUnitsPerEm);
            float ascent = float(metrics.ascent) * unit;
            float descent = float(metrics.descent) * unit;
            format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, style.lineHeight,
                                   (style.lineHeight - ascent - descent) / 2 + ascent);
        }
    }
    return format.get();
}

// A shared, effect-free layout of plain text. maxWidth bounds wrapping (or alignment without it).
static IDWriteTextLayout* UiLayout(UiGraphics& g, std::wstring_view text, const UiTextStyle& style,
                                   float maxWidth, bool wrap = true,
                                   DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    std::wstring key = std::to_wstring(int(style.family)) + L'|' + std::to_wstring(style.size) +
                       L'|' + std::to_wstring(style.lineHeight) + L'|' +
                       std::to_wstring(int(style.weight)) + L'|' + std::to_wstring(maxWidth) +
                       L'|' + (wrap ? L'w' : L'n') + std::to_wstring(int(align)) + L'|';
    key += text;
    auto& layout = g.layouts[key];
    if (layout) return layout.get();
    winrt::check_hresult(g.dwrite->CreateTextLayout(text.data(), UINT32(text.size()),
                                                    UiFormat(g, style), std::max(maxWidth, 0.0f),
                                                    100000.0f, layout.put()));
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(align);
    return layout.get();
}

// Layout box size: the widest line and the line-box height.
static D2D1_SIZE_F UiLayoutSize(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS metrics{};
    winrt::check_hresult(layout->GetMetrics(&metrics));
    return {metrics.widthIncludingTrailingWhitespace, metrics.height};
}

static ID2D1SolidColorBrush* UiBrush(ID2D1RenderTarget* target, UiTargetCache& cache,
                                     D2D1_COLOR_F color) {
    if (!cache.brush) {
        winrt::check_hresult(target->CreateSolidColorBrush(color, cache.brush.put()));
    } else {
        cache.brush->SetColor(&color);
    }
    return cache.brush.get();
}

// A brush that stays this color: drawing effects keep a reference to it.
static ID2D1SolidColorBrush* UiEffectBrush(ID2D1RenderTarget* target, UiTargetCache& cache,
                                           D2D1_COLOR_F color) {
    auto channel = [](float v) { return uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    uint32_t key = channel(color.a) << 24 | channel(color.r) << 16 | channel(color.g) << 8 |
                   channel(color.b);
    auto& brush = cache.effects[key];
    if (!brush) winrt::check_hresult(target->CreateSolidColorBrush(color, brush.put()));
    return brush.get();
}

// Device-pixel snapping: crisp fills and strokes at any DPI. scale = DPI / 96.
static float UiSnap(float value, float scale) {
    return std::round(value * scale) / scale;
}

static D2D1_RECT_F UiSnapRect(D2D1_RECT_F rect, float scale) {
    return {UiSnap(rect.left, scale), UiSnap(rect.top, scale), UiSnap(rect.right, scale),
            UiSnap(rect.bottom, scale)};
}

static D2D1_RECT_F UiInflate(D2D1_RECT_F rect, float dx, float dy) {
    return {rect.left - dx, rect.top - dy, rect.right + dx, rect.bottom + dy};
}

static bool UiContains(D2D1_RECT_F rect, D2D1_POINT_2F point) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top &&
           point.y < rect.bottom;
}

static void UiFillRect(ID2D1RenderTarget* target, UiTargetCache& cache, D2D1_RECT_F rect,
                       float radius, D2D1_COLOR_F color, float scale) {
    if (color.a <= 0) return;
    rect = UiSnapRect(rect, scale);
    if (radius > 0) {
        D2D1_ROUNDED_RECT rounded{rect, radius, radius};
        target->FillRoundedRectangle(&rounded, UiBrush(target, cache, color));
    } else {
        target->FillRectangle(&rect, UiBrush(target, cache, color));
    }
}

// A border of `pixels` device pixels drawn inside the snapped rectangle.
static void UiStrokeRect(ID2D1RenderTarget* target, UiTargetCache& cache, D2D1_RECT_F rect,
                         float radius, D2D1_COLOR_F color, float pixels, float scale) {
    if (color.a <= 0) return;
    float width = pixels / scale;
    rect = UiInflate(UiSnapRect(rect, scale), -width / 2, -width / 2);
    D2D1_ROUNDED_RECT rounded{rect, std::max(radius - width / 2, 0.0f),
                              std::max(radius - width / 2, 0.0f)};
    target->DrawRoundedRectangle(&rounded, UiBrush(target, cache, color), width);
}

static void UiDrawLayout(ID2D1RenderTarget* target, UiTargetCache& cache, IDWriteTextLayout* layout,
                         D2D1_POINT_2F origin, D2D1_COLOR_F color) {
    target->DrawTextLayout(origin, layout, UiBrush(target, cache, color),
                           D2D1_DRAW_TEXT_OPTIONS_NONE);
}
