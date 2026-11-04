// System theme reads and window chrome of the custom-drawn windows (native): app and Windows
// (taskbar) light/dark mode, high contrast with its system colors, the accent palette,
// transparency effects, the standard DWM caption recolored to the page background, and the DWM
// backdrop.

// UISettings is created on first use and released with the window thread's graphics scope,
// before the apartment. Accent changes are read on theme messages, never subscribed to.
struct UiThemeReader {
    std::optional<winrt::Windows::UI::ViewManagement::UISettings> settings;
};

static UiThemeInput UiReadTheme(UiThemeReader& reader) {
    UiThemeInput theme;
    DWORD systemLight = 0;
    DWORD systemSize = sizeof(systemLight);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &systemLight, &systemSize);
    theme.systemLight = systemLight != 0;
    DWORD transparency = 1;
    DWORD transparencySize = sizeof(transparency);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &transparencySize);
    theme.transparency = transparency != 0;
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        auto system = [](int index) {
            COLORREF color = GetSysColor(index);
            return uint32_t(GetRValue(color)) << 16 | uint32_t(GetGValue(color)) << 8 |
                   GetBValue(color);
        };
        theme.kind = UiThemeKind::HighContrast;
        theme.system = {system(COLOR_WINDOW),    system(COLOR_WINDOWTEXT),
                        system(COLOR_HIGHLIGHT), system(COLOR_HIGHLIGHTTEXT),
                        system(COLOR_GRAYTEXT),  system(COLOR_HOTLIGHT),
                        system(COLOR_BTNFACE),   system(COLOR_BTNTEXT)};
        return theme;
    }

    DWORD light = 1;
    DWORD size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    theme.kind = light ? UiThemeKind::Light : UiThemeKind::Dark;
    try {
        using winrt::Windows::UI::ViewManagement::UIColorType;
        if (!reader.settings) reader.settings.emplace();
        auto accent = [&](UIColorType type) {
            winrt::Windows::UI::Color color = reader.settings->GetColorValue(type);
            return uint32_t(color.R) << 16 | uint32_t(color.G) << 8 | color.B;
        };
        theme.accent = {accent(UIColorType::Accent),      accent(UIColorType::AccentLight1),
                        accent(UIColorType::AccentLight2), accent(UIColorType::AccentLight3),
                        accent(UIColorType::AccentDark1),  accent(UIColorType::AccentDark2),
                        accent(UIColorType::AccentDark3)};
    } catch (...) {
        Wh_Log(L"Accent colors unavailable; using the default accent");
    }
    return theme;
}

// Standard caption: dark mode follows the palette, caption and title colors are the page
// background and text; high contrast keeps the system's caption. With a backdrop (Windows 11
// 22H2 and later, transparency effects on, not in high contrast) DWM draws it under the whole
// window, the frame extended over the client area, and the caption keeps the system's colors over
// it. Returns whether the backdrop is on. Attributes older Windows versions lack are ignored.
static bool UiApplyChrome(HWND window, const UiPalette& palette, UiBackdrop backdrop, bool transparency) {
    auto opaque = [&](D2D1_COLOR_F color) {
        auto channel = [&](float value, float under) {
            return BYTE(std::lround(255 * (value * color.a + under * (1 - color.a))));
        };
        return RGB(channel(color.r, palette.background.r), channel(color.g, palette.background.g),
                   channel(color.b, palette.background.b));
    };
    bool hc = palette.kind == UiThemeKind::HighContrast;
    BOOL dark = palette.kind == UiThemeKind::Dark;
    DWM_SYSTEMBACKDROP_TYPE type = backdrop == UiBackdrop::Mica    ? DWMSBT_MAINWINDOW :
                                   backdrop == UiBackdrop::MicaAlt ? DWMSBT_TABBEDWINDOW :
                                   backdrop == UiBackdrop::Acrylic ? DWMSBT_TRANSIENTWINDOW : DWMSBT_AUTO;
    bool on = !hc && transparency && type != DWMSBT_AUTO &&
              SUCCEEDED(DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type)));
    if (!on) {
        type = DWMSBT_AUTO;
        DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
    }
    MARGINS frame{on ? -1 : 0, on ? -1 : 0, on ? -1 : 0, on ? -1 : 0};
    DwmExtendFrameIntoClientArea(window, &frame);
    COLORREF caption = hc || on ? DWMWA_COLOR_DEFAULT : opaque(palette.background);
    COLORREF text = hc || on ? DWMWA_COLOR_DEFAULT : opaque(palette.text);
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &text, sizeof(text));
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    return on;
}
