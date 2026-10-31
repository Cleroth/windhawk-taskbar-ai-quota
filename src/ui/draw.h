// Direct2D/DirectWrite primitives and resource caches of the custom-drawn windows. Pure: callers
// pass the factories and the target; nothing here touches windows or globals. Coordinates are
// DIPs; targets carry the window DPI.

// Device-independent resources shared by every target of one UI thread: font collections and
// resolved families, text formats, effect-free layouts (measuring and plain drawing) and path
// geometries. Owned by that thread's graphics scope, released before the apartment is.
struct UiGraphics {
    winrt::com_ptr<ID2D1Factory> d2d;
    winrt::com_ptr<IDWriteFactory> dwrite;
    winrt::com_ptr<ID2D1StrokeStyle> roundCaps;
    winrt::com_ptr<IDWriteFontCollection> fonts, typographicFonts;
    std::array<std::wstring, std::size(kUiFamilyNames)> families;
    std::map<std::wstring, winrt::com_ptr<IDWriteTextFormat>> formats;
    std::map<std::wstring, winrt::com_ptr<IDWriteTextLayout>> layouts;
    std::map<PCWSTR, winrt::com_ptr<ID2D1PathGeometry>> paths;
};

// Resources bound to one render target: a recolored brush for primitives, drawing-effect
// brushes by color, gradient brushes by color pair, and layouts that carry those effects.
// Discarded with the target.
struct UiTargetCache {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    std::map<uint32_t, winrt::com_ptr<ID2D1SolidColorBrush>> effects;
    std::map<uint64_t, winrt::com_ptr<ID2D1LinearGradientBrush>> gradients;
    std::map<std::wstring, winrt::com_ptr<IDWriteTextLayout>> layouts;

    void Clear() {
        layouts.clear();
        gradients.clear();
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

// A family's font collection, its name resolved (preferred, else fallback) on first use. The
// taskbar's family is the typographic one: the variable font has no WWS family of that name.
static IDWriteFontCollection* UiFontCollection(UiGraphics& g, UiFamily family) {
    bool typographic = family == UiFamily::Taskbar;
    winrt::com_ptr<IDWriteFontCollection>& fonts = typographic ? g.typographicFonts : g.fonts;
    if (!fonts && typographic) {
        if (auto factory = g.dwrite.try_as<IDWriteFactory6>()) {
            winrt::com_ptr<IDWriteFontCollection2> collection;
            if (SUCCEEDED(factory->GetSystemFontCollection(
                    FALSE, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, collection.put()))) {
                fonts = collection;
            }
        }
    }
    if (!fonts) winrt::check_hresult(g.dwrite->GetSystemFontCollection(fonts.put(), FALSE));
    std::wstring& name = g.families[size_t(family)];
    if (name.empty()) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        PCWSTR preferred = kUiFamilyNames[size_t(family)][0];
        name = SUCCEEDED(fonts->FindFamilyName(preferred, &index, &exists)) && exists ?
                   preferred : kUiFamilyNames[size_t(family)][1];
    }
    return fonts.get();
}

// A style's font metrics in DIPs; zero when its family has no matching font.
struct UiFontMetrics {
    float ascent = 0, descent = 0, lineGap = 0, capHeight = 0;
};

static UiFontMetrics UiFontMetricsFor(UiGraphics& g, const UiTextStyle& style) {
    IDWriteFontCollection* fonts = UiFontCollection(g, style.family);
    UINT32 index = 0;
    BOOL exists = FALSE;
    winrt::com_ptr<IDWriteFontFamily> family;
    winrt::com_ptr<IDWriteFont> font;
    DWRITE_FONT_METRICS metrics{};
    if (SUCCEEDED(fonts->FindFamilyName(g.families[size_t(style.family)].c_str(), &index, &exists)) &&
        exists && SUCCEEDED(fonts->GetFontFamily(index, family.put())) &&
        SUCCEEDED(family->GetFirstMatchingFont(style.weight, DWRITE_FONT_STRETCH_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, font.put()))) {
        font->GetMetrics(&metrics);
    }
    if (!metrics.designUnitsPerEm) return {};
    float unit = style.size / float(metrics.designUnitsPerEm);
    return {float(metrics.ascent) * unit, float(metrics.descent) * unit,
            float(metrics.lineGap) * unit, float(metrics.capHeight) * unit};
}

static IDWriteTextFormat* UiFormat(UiGraphics& g, const UiTextStyle& style) {
    std::wstring key = std::to_wstring(int(style.family)) + L'|' + std::to_wstring(style.size) +
                       L'|' + std::to_wstring(style.lineHeight) + L'|' +
                       std::to_wstring(int(style.weight));
    auto& format = g.formats[key];
    if (format) return format.get();

    IDWriteFontCollection* fonts = UiFontCollection(g, style.family);
    winrt::check_hresult(g.dwrite->CreateTextFormat(
        g.families[size_t(style.family)].c_str(), fonts, style.weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, style.size, L"en-us", format.put()));
    // The taskbar's XAML text: optical size follows the font size.
    if (style.family == UiFamily::Taskbar) {
        if (auto format3 = format.try_as<IDWriteTextFormat3>()) {
            format3->SetAutomaticFontAxes(DWRITE_AUTOMATIC_FONT_AXES_OPTICAL_SIZE);
        }
    }

    // Uniform line height with the baseline where CSS puts it: half the leading above the
    // ascent. Fonts without metrics keep DirectWrite's default spacing.
    UiFontMetrics metrics = style.lineHeight > 0 ? UiFontMetricsFor(g, style) : UiFontMetrics{};
    if (metrics.ascent + metrics.descent > 0) {
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, style.lineHeight,
                               (style.lineHeight - metrics.ascent - metrics.descent) / 2 +
                                   metrics.ascent);
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

// A color as 0xAARRGGBB: cache keys.
static uint32_t UiColorKey(D2D1_COLOR_F color) {
    auto channel = [](float v) { return uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return channel(color.a) << 24 | channel(color.r) << 16 | channel(color.g) << 8 | channel(color.b);
}

// A brush that stays this color: drawing effects keep a reference to it.
static ID2D1SolidColorBrush* UiEffectBrush(ID2D1RenderTarget* target, UiTargetCache& cache,
                                           D2D1_COLOR_F color) {
    auto& brush = cache.effects[UiColorKey(color)];
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

// A linear gradient across the rectangle, left to right or top to bottom.
static void UiFillGradient(ID2D1RenderTarget* target, UiTargetCache& cache, D2D1_RECT_F rect,
                           float radius, D2D1_COLOR_F from, D2D1_COLOR_F to, bool vertical) {
    auto& brush = cache.gradients[uint64_t(UiColorKey(from)) << 32 | UiColorKey(to)];
    if (!brush) {
        const D2D1_GRADIENT_STOP stops[] = {{0, from}, {1, to}};
        winrt::com_ptr<ID2D1GradientStopCollection> collection;
        winrt::check_hresult(target->CreateGradientStopCollection(
            stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, collection.put()));
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES line{{0, 0}, {1, 0}};
        D2D1_BRUSH_PROPERTIES properties{1, D2D1::Matrix3x2F::Identity()};
        winrt::check_hresult(target->CreateLinearGradientBrush(&line, &properties, collection.get(),
                                                               brush.put()));
    }
    brush->SetStartPoint({rect.left, rect.top});
    brush->SetEndPoint(vertical ? D2D1_POINT_2F{rect.left, rect.bottom} :
                                  D2D1_POINT_2F{rect.right, rect.top});
    D2D1_ROUNDED_RECT rounded{rect, radius, radius};
    target->FillRoundedRectangle(&rounded, brush.get());
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
