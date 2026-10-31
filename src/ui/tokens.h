// Theme palette and type ramp of the custom-drawn windows. Pure: every color comes from explicit
// theme inputs. Light/dark follow WinUI 2 token values (the approved mockups where they specify
// one); high contrast uses only system colors.

enum class UiThemeKind : uint8_t { Light, Dark, HighContrast };

// Accent shades as UISettings reports them (0xRRGGBB). Defaults: the Windows 11 default blue.
struct UiAccentColors {
    uint32_t accent = 0x0078D4;
    uint32_t light1 = 0x0091F8, light2 = 0x4CC2FF, light3 = 0x99EBFF;
    uint32_t dark1 = 0x0067C0, dark2 = 0x003E92, dark3 = 0x001A68;
};

// System colors that high contrast draws with (0xRRGGBB). Defaults: the Aquatic theme.
struct UiSystemColors {
    uint32_t window = 0x202020, windowText = 0xFFFFFF;
    uint32_t highlight = 0x8EE3F0, highlightText = 0x263B50;
    uint32_t grayText = 0xA6A6A6, hotlight = 0x75E9FC;
    uint32_t buttonFace = 0x202020, buttonText = 0xFFFFFF;
};

struct UiThemeInput {
    UiThemeKind kind = UiThemeKind::Dark;
    UiAccentColors accent;
    UiSystemColors system;
};

static D2D1_COLOR_F UiRgb(uint32_t rgb, float alpha = 1.0f) {
    return {float((rgb >> 16) & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f,
            float(rgb & 0xFF) / 255.0f, alpha};
}

// Semantic text colors; items name these, never concrete colors. Muted is the mockups'
// description gray, between WinUI's secondary and tertiary text.
enum class UiInk : uint8_t {
    Inherit, Primary, Secondary, Muted, Disabled, Link, OnAccent, Danger, Success, Caution, Accent
};

struct UiPalette {
    UiThemeKind kind = UiThemeKind::Dark;

    // Surfaces
    D2D1_COLOR_F background{}, card{}, cardHover{}, cardPressed{}, cardBorder{};

    // Controls: standard fills, subtle (navigation, subtle buttons) and accent fills; the strong
    // stroke of an off toggle, a focused text box and the dropdown popup's surface
    D2D1_COLOR_F control{}, controlHover{}, controlPressed{}, controlDisabled{}, controlBorder{};
    D2D1_COLOR_F subtleHover{}, subtlePressed{};
    D2D1_COLOR_F accent{}, accentHover{}, accentPressed{}, accentDisabled{};
    D2D1_COLOR_F controlStrong{}, inputActive{}, popup{};

    // Text
    D2D1_COLOR_F text{}, textSecondary{}, textMuted{}, textDisabled{}, textOnAccent{};
    D2D1_COLOR_F link{}, linkHover{}, linkPressed{};
    D2D1_COLOR_F danger{}, success{}, caution{};

    // High-contrast selection (accent and text on accent elsewhere)
    D2D1_COLOR_F highlight{}, highlightText{};

    // Focus ring, scrollbar
    D2D1_COLOR_F focusOuter{}, focusInner{};
    D2D1_COLOR_F scrollThumb{}, scrollTrack{};
};

static UiPalette UiPaletteFor(const UiThemeInput& theme) {
    UiPalette p;
    p.kind = theme.kind;
    const UiAccentColors& a = theme.accent;
    if (theme.kind == UiThemeKind::HighContrast) {
        const UiSystemColors& s = theme.system;
        p.background = p.card = p.cardHover = p.cardPressed = UiRgb(s.window);
        p.cardBorder = UiRgb(s.windowText);
        p.control = p.controlDisabled = UiRgb(s.buttonFace);
        p.controlHover = p.controlPressed = UiRgb(s.highlight);
        p.controlBorder = UiRgb(s.buttonText);
        p.subtleHover = p.subtlePressed = UiRgb(s.window);
        p.accent = p.accentHover = p.accentPressed = UiRgb(s.highlight);
        p.accentDisabled = UiRgb(s.grayText);
        p.controlStrong = UiRgb(s.windowText);
        p.inputActive = p.popup = UiRgb(s.window);
        p.text = p.textSecondary = p.textMuted = UiRgb(s.windowText);
        p.textDisabled = UiRgb(s.grayText);
        p.textOnAccent = UiRgb(s.highlightText);
        p.link = p.linkHover = p.linkPressed = UiRgb(s.hotlight);
        p.danger = p.success = p.caution = UiRgb(s.windowText);
        p.highlight = UiRgb(s.highlight);
        p.highlightText = UiRgb(s.highlightText);
        p.focusOuter = UiRgb(s.windowText);
        p.focusInner = UiRgb(s.window);
        p.scrollThumb = UiRgb(s.windowText);
        p.scrollTrack = UiRgb(s.window);
        return p;
    }
    if (theme.kind == UiThemeKind::Dark) {
        p.background = UiRgb(0x202020);
        p.card = UiRgb(0xFFFFFF, 0.05f);
        p.cardHover = UiRgb(0xFFFFFF, 0.082f);
        p.cardPressed = UiRgb(0xFFFFFF, 0.03f);
        p.cardBorder = UiRgb(0x000000, 0.25f);
        p.control = UiRgb(0xFFFFFF, 0.06f);
        p.controlHover = UiRgb(0xFFFFFF, 0.082f);
        p.controlPressed = UiRgb(0xFFFFFF, 0.03f);
        p.controlDisabled = UiRgb(0xFFFFFF, 0.043f);
        p.controlBorder = UiRgb(0xFFFFFF, 0.07f);
        p.subtleHover = UiRgb(0xFFFFFF, 0.06f);
        p.subtlePressed = UiRgb(0xFFFFFF, 0.04f);
        p.accent = UiRgb(a.light2);
        p.accentHover = UiRgb(a.light2, 0.9f);
        p.accentPressed = UiRgb(a.light2, 0.8f);
        p.accentDisabled = UiRgb(0xFFFFFF, 0.16f);
        p.controlStrong = UiRgb(0xFFFFFF, 0.6f);
        p.inputActive = UiRgb(0x1E1E1E, 0.7f);
        p.popup = UiRgb(0x2C2C2C);
        p.text = UiRgb(0xFFFFFF);
        p.textSecondary = UiRgb(0xFFFFFF, 0.786f);
        p.textMuted = UiRgb(0xFFFFFF, 0.6f);
        p.textDisabled = UiRgb(0xFFFFFF, 0.365f);
        p.textOnAccent = UiRgb(0x000000);
        p.link = p.linkHover = UiRgb(a.light3);
        p.linkPressed = UiRgb(a.light2);
        p.danger = UiRgb(0xFF99A4);
        p.success = UiRgb(0x6CCB5F);
        p.caution = UiRgb(0xFCE100);
        p.focusOuter = UiRgb(0xFFFFFF);
        p.focusInner = UiRgb(0x000000, 0.7f);
        p.scrollThumb = UiRgb(0xFFFFFF, 0.545f);
        p.scrollTrack = UiRgb(0xFFFFFF, 0.06f);
    } else {
        p.background = UiRgb(0xF3F3F3);
        p.card = UiRgb(0xFFFFFF, 0.7f);
        p.cardHover = UiRgb(0xF9F9F9, 0.5f);
        p.cardPressed = UiRgb(0xF9F9F9, 0.3f);
        p.cardBorder = UiRgb(0x000000, 0.06f);
        p.control = UiRgb(0xFFFFFF, 0.7f);
        p.controlHover = UiRgb(0xF9F9F9, 0.5f);
        p.controlPressed = UiRgb(0xF9F9F9, 0.3f);
        p.controlDisabled = UiRgb(0xF9F9F9, 0.3f);
        p.controlBorder = UiRgb(0x000000, 0.1f);
        p.subtleHover = UiRgb(0x000000, 0.037f);
        p.subtlePressed = UiRgb(0x000000, 0.024f);
        p.accent = UiRgb(a.dark1);
        p.accentHover = UiRgb(a.dark1, 0.9f);
        p.accentPressed = UiRgb(a.dark1, 0.8f);
        p.accentDisabled = UiRgb(0x000000, 0.216f);
        p.controlStrong = UiRgb(0x000000, 0.446f);
        p.inputActive = UiRgb(0xFFFFFF);
        p.popup = UiRgb(0xF9F9F9);
        p.text = UiRgb(0x000000, 0.894f);
        p.textSecondary = UiRgb(0x000000, 0.62f);
        p.textMuted = UiRgb(0x000000, 0.58f);
        p.textDisabled = UiRgb(0x000000, 0.36f);
        p.textOnAccent = UiRgb(0xFFFFFF);
        p.link = UiRgb(a.dark2);
        p.linkHover = UiRgb(a.dark3);
        p.linkPressed = UiRgb(a.dark1);
        p.danger = UiRgb(0xC42B1C);
        p.success = UiRgb(0x0F7B0F);
        p.caution = UiRgb(0x9D5D00);
        p.focusOuter = UiRgb(0x000000, 0.894f);
        p.focusInner = UiRgb(0xFFFFFF, 0.7f);
        p.scrollThumb = UiRgb(0x000000, 0.447f);
        p.scrollTrack = UiRgb(0x000000, 0.037f);
    }
    p.highlight = p.accent;
    p.highlightText = p.textOnAccent;
    return p;
}

static D2D1_COLOR_F UiInkColor(const UiPalette& p, UiInk ink) {
    switch (ink) {
        case UiInk::Secondary: return p.textSecondary;
        case UiInk::Muted: return p.textMuted;
        case UiInk::Disabled: return p.textDisabled;
        case UiInk::Link: return p.link;
        case UiInk::OnAccent: return p.textOnAccent;
        case UiInk::Danger: return p.danger;
        case UiInk::Success: return p.success;
        case UiInk::Caution: return p.caution;
        case UiInk::Accent: return p.accent;
        default: return p.text;
    }
}

// Type ramp (WinUI 2 sizes, mockup line heights) and icon font. Families fall back to Segoe UI
// and Segoe MDL2 Assets where the Windows 11 fonts are missing.
enum class UiFamily : uint8_t { Text, Display, Icons };
static constexpr PCWSTR kUiFamilyNames[3][2] = {
    {L"Segoe UI Variable Text", L"Segoe UI"},
    {L"Segoe UI Variable Display", L"Segoe UI"},
    {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"},
};

// Note: info bar text (mockup .infobar).
enum class UiFont : uint8_t { Caption, Note, Body, BodyStrong, Subtitle, Title };

struct UiTextStyle {
    UiFamily family = UiFamily::Text;
    float size = 14;
    float lineHeight = 20;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
};

static UiTextStyle UiTextStyleFor(UiFont font) {
    switch (font) {
        case UiFont::Caption: return {UiFamily::Text, 12, 16, DWRITE_FONT_WEIGHT_NORMAL};
        case UiFont::Note: return {UiFamily::Text, 13, 19, DWRITE_FONT_WEIGHT_NORMAL};
        case UiFont::BodyStrong: return {UiFamily::Text, 14, 20, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case UiFont::Subtitle: return {UiFamily::Display, 20, 26, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        case UiFont::Title: return {UiFamily::Display, 28, 36, DWRITE_FONT_WEIGHT_SEMI_BOLD};
        default: return {UiFamily::Text, 14, 20, DWRITE_FONT_WEIGHT_NORMAL};
    }
}

static UiTextStyle UiIconStyle(float size) {
    return {UiFamily::Icons, size, size, DWRITE_FONT_WEIGHT_NORMAL};
}
