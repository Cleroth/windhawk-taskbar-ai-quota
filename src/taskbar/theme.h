// XAML colors for the meter model's ARGB values, logo brand brushes, and the details flyout's
// theme. Every flyout XAML brush is created in this file; flyout-material.h builds the card's
// composition materials from the theme.

static winrt::Windows::UI::Color ArgbColor(uint32_t argb) {
    return {(uint8_t)(argb >> 24), (uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb};
}

static bool IsHighContrast() {
    HIGHCONTRASTW highContrast{sizeof(highContrast)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0) &&
           (highContrast.dwFlags & HCF_HIGHCONTRASTON);
}

// A logo's brand colors (logo-paths.h) as a gradient, also when flat (see FlyoutBrush), relative
// to the logo's bounding box: the gradient logo spans its whole viewBox. Throws on failure.
static Brush LogoBrandBrush(LogoBrand const& brand) {
    LinearGradientBrush brush;
    brush.StartPoint({brand.x0 / 24, brand.y0 / 24});
    brush.EndPoint({brand.x1 / 24, brand.y1 / 24});
    for (auto [offset, color] : {std::pair{0.0, brand.from}, std::pair{1.0, brand.to}}) {
        GradientStop stop;
        stop.Color(ArgbColor(color));
        stop.Offset(offset);
        brush.GradientStops().Append(stop);
    }
    return brush;
}

// The single constructor of solid flyout brushes (backgrounds, text, borders and shapes). A
// single-color gradient on purpose: plain SolidColorBrushes inside this popup in the taskbar's
// XAML can render dimmed and translucent, while gradient brushes render at full strength.
static Brush FlyoutBrush(winrt::Windows::UI::Color color) {
    try {
        LinearGradientBrush brush;
        for (double offset : {0.0, 1.0}) {
            GradientStop stop;
            stop.Color(color);
            stop.Offset(offset);
            brush.GradientStops().Append(stop);
        }
        return brush;
    } catch (...) {
        return SolidColorBrush(color);
    }
}

// FlyoutBrush for chart shapes, mapped in absolute coordinates: a horizontal or vertical line
// has an empty bounding box, which a bounding-box-relative gradient can't paint.
static Brush FlyoutShapeBrush(winrt::Windows::UI::Color color) {
    Brush brush = FlyoutBrush(color);
    try {
        if (auto gradient = brush.try_as<LinearGradientBrush>()) {
            gradient.MappingMode(BrushMappingMode::Absolute);
        }
    } catch (...) {}
    return brush;
}

// Quota fill: the usage color, lightened toward the top edge.
static Brush FlyoutFillBrush(winrt::Windows::UI::Color color) {
    try {
        auto lighten = [](uint8_t channel) { return (uint8_t)(channel + (255 - channel) * 28 / 100); };
        LinearGradientBrush brush;
        brush.StartPoint({0.f, 0.f});
        brush.EndPoint({0.f, 1.f});
        GradientStop top;
        top.Color({color.A, lighten(color.R), lighten(color.G), lighten(color.B)});
        top.Offset(0.0);
        GradientStop body;
        body.Color(color);
        body.Offset(0.6);
        brush.GradientStops().Append(top);
        brush.GradientStops().Append(body);
        return brush;
    } catch (...) {
        return FlyoutBrush(color);
    }
}

// Follows the taskbar's Windows mode (not the app mode), transparency effects, high contrast and
// the material setting.
static FlyoutTheme LoadFlyoutTheme() {
    using Color = winrt::Windows::UI::Color;
    FlyoutTheme theme;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        theme.material = g_settings.material;
    }
    theme.highContrast = IsHighContrast();
    auto personalizeValue = [](PCWSTR name, DWORD fallback) {
        DWORD value = fallback;
        DWORD size = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER,
                         L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                         name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
            value = fallback;
        }
        return value;
    };
    theme.light = personalizeValue(L"SystemUsesLightTheme", 0) != 0;
    if (theme.highContrast || personalizeValue(L"EnableTransparency", 1) == 0) {
        theme.material = Material::Solid;
    }

    struct {
        Color text, secondaryText, mutedText, separator, chip, chipBorder, cardBorder;
        Color cardTint, cardSolid, footer, hatchA, hatchB, trackBorder;
        Color warning, error, accent, paceTick, paceHalo;
        Color hover, hoverBorder, pressed;
        double tintOpacity, luminosityOpacity;
    } p;
    constexpr Color kTransparent{0, 0, 0, 0};
    if (theme.highContrast) {
        auto system = [](int index) {
            COLORREF c = GetSysColor(index);
            return Color{0xFF, GetRValue(c), GetGValue(c), GetBValue(c)};
        };
        Color window = system(COLOR_WINDOW);
        Color windowText = system(COLOR_WINDOWTEXT);
        Color highlight = system(COLOR_HIGHLIGHT);
        Color link = system(COLOR_HOTLIGHT);
        p = {windowText, windowText, windowText, windowText, kTransparent, windowText, windowText,
             window, window, kTransparent, window, window, windowText,
             link, windowText, link, windowText, window,
             kTransparent, highlight, kTransparent, 1.0, 1.0};
    } else if (theme.light) {
        p = {{0xFF, 0x1A, 0x1A, 0x1A}, {0xC7, 0, 0, 0}, {0x94, 0, 0, 0}, {0x12, 0, 0, 0},
             {0x0D, 0, 0, 0}, kTransparent, {0x17, 0, 0, 0},
             {0xFF, 0xF6, 0xF7, 0xFA}, {0xFF, 0xF5, 0xF6, 0xF8}, {0x0A, 0, 0, 0},
             {0x0A, 0, 0, 0}, {0x1A, 0, 0, 0}, kTransparent,
             {0xFF, 0xC2, 0x5E, 0x00}, {0xFF, 0xC4, 0x2B, 0x1C}, {0xFF, 0x00, 0x5F, 0xB8},
             {0xFF, 0x1A, 0x1A, 0x1A}, {0x80, 0xFF, 0xFF, 0xFF},
             {0x0A, 0, 0, 0}, kTransparent, {0x06, 0, 0, 0}, 0.5, 0.9};
    } else {
        p = {{0xFF, 0xFF, 0xFF, 0xFF}, {0xCC, 0xFF, 0xFF, 0xFF}, {0x99, 0xFF, 0xFF, 0xFF},
             {0x13, 0xFF, 0xFF, 0xFF}, {0x12, 0xFF, 0xFF, 0xFF}, kTransparent,
             {0x1C, 0xFF, 0xFF, 0xFF},
             {0xFF, 0x24, 0x26, 0x2C}, {0xFF, 0x26, 0x28, 0x2D}, {0x30, 0, 0, 0},
             {0x0F, 0xFF, 0xFF, 0xFF}, {0x24, 0xFF, 0xFF, 0xFF}, kTransparent,
             {0xFF, 0xFB, 0x8C, 0x00}, {0xFF, 0xFF, 0xB4, 0xA9}, {0xFF, 0x60, 0xCD, 0xFF},
             {0xFF, 0xFF, 0xFF, 0xFF}, {0x59, 0, 0, 0},
             {0x0F, 0xFF, 0xFF, 0xFF}, kTransparent, {0x0B, 0xFF, 0xFF, 0xFF}, 0.6, 0.96};
    }

    theme.text = FlyoutBrush(p.text);
    theme.secondaryText = FlyoutBrush(p.secondaryText);
    theme.mutedText = FlyoutBrush(p.mutedText);
    theme.separator = FlyoutBrush(p.separator);
    theme.chip = FlyoutBrush(p.chip);
    theme.chipBorder = FlyoutBrush(p.chipBorder);
    theme.cardBorder = FlyoutBrush(p.cardBorder);
    theme.footer = FlyoutBrush(p.footer);
    theme.trackBorder = FlyoutBrush(p.trackBorder);
    theme.warning = FlyoutBrush(p.warning);
    theme.error = FlyoutBrush(p.error);
    theme.accent = FlyoutBrush(p.accent);
    theme.paceTick = FlyoutBrush(p.paceTick);
    theme.paceHalo = FlyoutBrush(p.paceHalo);
    theme.markerRing = FlyoutBrush(p.cardSolid);
    theme.chartGuide = FlyoutShapeBrush(p.mutedText);
    theme.chartKey = FlyoutShapeBrush(p.text);

    theme.card = FlyoutBrush(p.cardSolid);
    switch (theme.material) {
    case Material::Acrylic:
        try {
            AcrylicBrush brush;
            brush.BackgroundSource(AcrylicBackgroundSource::Backdrop);
            brush.TintColor(p.cardTint);
            brush.TintOpacity(p.tintOpacity);
            brush.FallbackColor(p.cardSolid);
            try {
                brush.TintLuminosityOpacity(p.luminosityOpacity);
            } catch (...) {}
            theme.card = brush;
        } catch (...) {}
        break;
    case Material::Mica:
    case Material::MicaAlt: {
        // Windows App SDK MicaController's MicaKind::Base and BaseAlt recipes, by [alt][light].
        struct MicaRecipe {
            Color tint;
            float tintOpacity, luminosityOpacity;
            Color fallback;
        };
        static constexpr MicaRecipe kRecipes[2][2] = {
            {{{0xFF, 0x20, 0x20, 0x20}, 0.8f, 1.f, {0xFF, 0x20, 0x20, 0x20}},
             {{0xFF, 0xF3, 0xF3, 0xF3}, 0.5f, 1.f, {0xFF, 0xF3, 0xF3, 0xF3}}},
            {{{0xFF, 0x0A, 0x0A, 0x0A}, 0.0f, 1.f, {0xFF, 0x20, 0x20, 0x20}},
             {{0xFF, 0xDA, 0xDA, 0xDA}, 0.5f, 1.f, {0xFF, 0xE8, 0xE8, 0xE8}}},
        };
        MicaRecipe const& recipe = kRecipes[theme.material == Material::MicaAlt][theme.light];
        theme.materialTint = recipe.tint;
        theme.materialTintOpacity = recipe.tintOpacity;
        theme.materialLuminosityOpacity = recipe.luminosityOpacity;
        theme.card = FlyoutBrush(recipe.fallback);
        break;
    }
    case Material::TintedWallpaper:
        theme.materialTint = p.cardSolid;
        theme.materialTintOpacity = 0.8f;
        break;
    case Material::Solid:
        break;
    }

    // Unfilled track: 45-degree stripes repeating every 6 DIP.
    theme.track = FlyoutBrush(p.hatchB);
    if (p.hatchA != p.hatchB) {
        try {
            LinearGradientBrush hatch;
            hatch.MappingMode(BrushMappingMode::Absolute);
            hatch.SpreadMethod(GradientSpreadMethod::Repeat);
            hatch.StartPoint({0.f, 0.f});
            hatch.EndPoint({4.243f, 4.243f});
            for (auto [offset, color] : {std::pair{0.0, p.hatchA}, std::pair{0.5, p.hatchA},
                                         std::pair{0.5, p.hatchB}, std::pair{1.0, p.hatchB}}) {
                GradientStop stop;
                stop.Offset(offset);
                stop.Color(color);
                hatch.GradientStops().Append(stop);
            }
            theme.track = hatch;
        } catch (...) {}
    }

    // Footer icon buttons: flat, rounded, with theme hover/pressed fills and system focus visuals.
    // Single-color gradients, like FlyoutBrush.
    auto brushSetter = [](PCWSTR target, Color c) {
        wchar_t color[16];
        swprintf(color, ARRAYSIZE(color), L"#%02X%02X%02X%02X", c.A, c.R, c.G, c.B);
        return std::wstring(L"<Setter Target=\"") + target +  // i18n-en: XAML
               L"\"><Setter.Value><LinearGradientBrush><GradientStop Color=\"" + color +  // i18n-en: XAML
               L"\" Offset=\"0\"/><GradientStop Color=\"" + color +  // i18n-en: XAML
               L"\" Offset=\"1\"/></LinearGradientBrush></Setter.Value></Setter>";  // i18n-en: XAML
    };
    std::wstring style =
        L"<Style xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        L"xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\" TargetType=\"Button\">"
        L"<Setter Property=\"UseSystemFocusVisuals\" Value=\"True\"/>"
        L"<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"Button\">"
        L"<Border x:Name=\"Root\" Background=\"{TemplateBinding Background}\" "
        L"BorderBrush=\"Transparent\" BorderThickness=\"1\" CornerRadius=\"4\">"
        L"<VisualStateManager.VisualStateGroups><VisualStateGroup x:Name=\"CommonStates\">"
        L"<VisualState x:Name=\"Normal\"/>"
        L"<VisualState x:Name=\"PointerOver\"><VisualState.Setters>" +
        brushSetter(L"Root.Background", p.hover) +
        brushSetter(L"Root.BorderBrush", p.hoverBorder) +
        L"</VisualState.Setters></VisualState>"  // i18n-en: XAML
        L"<VisualState x:Name=\"Pressed\"><VisualState.Setters>" +
        brushSetter(L"Root.Background", p.pressed) +
        brushSetter(L"Root.BorderBrush", p.hoverBorder) +
        L"</VisualState.Setters></VisualState>"  // i18n-en: XAML
        L"<VisualState x:Name=\"Disabled\"><VisualState.Setters>"
        L"<Setter Target=\"Root.Opacity\" Value=\"0.4\"/>"
        L"</VisualState.Setters></VisualState>"
        L"</VisualStateGroup></VisualStateManager.VisualStateGroups>"
        L"<ContentPresenter Content=\"{TemplateBinding Content}\" "
        L"HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\"/>"
        L"</Border></ControlTemplate></Setter.Value></Setter></Style>";
    try {
        theme.buttonStyle =
            winrt::Windows::UI::Xaml::Markup::XamlReader::Load(winrt::hstring(style)).as<Style>();
    } catch (...) {
        Wh_Log(L"Flyout button style failed to load");
    }
    return theme;
}
