// Settings > About: app card, links, credits and license (approved mockup about.png). Pure
// builder.

// App icon (16 DIP view box): green, yellow and red quota bars on tracks, drawn like the
// taskbar's stacked bars: corners a quarter of the 4-unit thickness, fills cut straight and shaded
// from their usage highlight (UsageHighlight). Bars and 2-unit gaps land on whole pixels at 16,
// 24, 32 and 48 px.
static constexpr PCWSTR kSettingsAppIconTracks[3] = {
    L"M2,0H14A1,1 0 0 1 15,1V3A1,1 0 0 1 14,4H2A1,1 0 0 1 1,3V1A1,1 0 0 1 2,0Z",
    L"M2,6H14A1,1 0 0 1 15,7V9A1,1 0 0 1 14,10H2A1,1 0 0 1 1,9V7A1,1 0 0 1 2,6Z",
    L"M2,12H14A1,1 0 0 1 15,13V15A1,1 0 0 1 14,16H2A1,1 0 0 1 1,15V13A1,1 0 0 1 2,12Z",
};
static constexpr PCWSTR kSettingsAppIconFills[3] = {
    L"M2,0H6V4H2A1,1 0 0 1 1,3V1A1,1 0 0 1 2,0Z",
    L"M2,6H10V10H2A1,1 0 0 1 1,9V7A1,1 0 0 1 2,6Z",
    L"M2,12H13V16H2A1,1 0 0 1 1,15V13A1,1 0 0 1 2,12Z",
};
static constexpr uint32_t kSettingsAppIconColors[3] = {0x43A047, 0xFDD835, 0xE53935};
static constexpr uint32_t kSettingsAppIconHighlights[3] = {0x57B359, 0xFFEEAE, 0xFB5048};

// The app icon over `rect`: the About page's, and the settings and usage history windows'
// (ui/window-icon.h).
static UiItem SettingsAppIcon(D2D1_RECT_F rect) {
    UiItem icon;
    icon.kind = UiItemKind::Shape;
    icon.viewBox = 16;
    icon.rect = rect;
    for (int i = 0; i < 3; i++) {
        icon.parts.push_back({kSettingsAppIconTracks[i], {}, UiInk::Primary, 0.2f});
        icon.parts.push_back({kSettingsAppIconFills[i], UiRgb(kSettingsAppIconColors[i]), UiInk::Inherit, 1, 0,
                              UiRgb(kSettingsAppIconHighlights[i])});
    }
    return icon;
}

static constexpr PCWSTR kSettingsProjectUrl = L"https://github.com/Cleroth/windhawk-taskbar-ai-quota";

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildAbout(UiGraphics& g, std::wstring_view version,
                                  std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                  float width) {
    // App card (mockup .aboutcard): icon, name, version line, summary.
    {
        constexpr float kPadX = 20, kPadY = 18, kIcon = 52, kGap = 18;
        float textLeft = x + 1 + kPadX + kIcon + kGap;
        float textWidth = std::max(x + width - 1 - kPadX - textLeft, 40.0f);
        std::wstring versionLine =
            L"Version " + std::wstring(version) + L"  \u00B7  by Cleroth  \u00B7  Windhawk mod";
        PCWSTR summary = L"AI subscription quotas for Anthropic, OpenAI, Google Antigravity and GitHub "
                         L"Copilot on the Windows 11 taskbar.";
        UiTextStyle caption = UiTextStyleFor(UiFont::Caption);
        float versionHeight = UiLayoutSize(UiLayout(g, versionLine, caption, textWidth)).height;
        float summaryHeight = UiLayoutSize(UiLayout(g, summary, caption, textWidth)).height;
        float textHeight = 26 + 2 + versionHeight + summaryHeight;
        float height = 2 * (1 + kPadY) + std::max(kIcon, textHeight);
        float center = y + height / 2, top = center - textHeight / 2;

        UiItem card;
        card.id = id++;
        card.kind = UiItemKind::Card;
        card.rect = {x, y, x + width, y + height};
        items.push_back(std::move(card));

        UiItem icon =
            SettingsAppIcon({x + 1 + kPadX, center - kIcon / 2, x + 1 + kPadX + kIcon, center + kIcon / 2});
        icon.id = id++;
        items.push_back(std::move(icon));

        float lineTop = top;
        auto addLine = [&](std::wstring text, UiFont font, UiInk ink, float height) {
            UiItem line;
            line.id = id++;
            line.kind = UiItemKind::Text;
            line.font = font;
            line.ink = ink;
            line.rect = {textLeft, lineTop, textLeft + textWidth, lineTop + height};
            line.runs.push_back({std::move(text)});
            items.push_back(std::move(line));
            lineTop += height;
        };
        addLine(L"Taskbar AI Quota Bars", UiFont::Subtitle, UiInk::Primary, 26);
        lineTop += 2;
        addLine(versionLine, UiFont::Caption, UiInk::Muted, versionHeight);
        addLine(summary, UiFont::Caption, UiInk::Muted, summaryHeight);
        y += height;
    }

    // Links: whole cards open the page in the browser.
    y = UiAddSection(items, id, x, y + 20, width, L"Links");
    struct LinkCard {
        PCWSTR glyph, title, description;
        std::wstring url;
    };
    const LinkCard links[] = {
        {L"\uE774", L"Project page", L"github.com/Cleroth/windhawk-taskbar-ai-quota",
         kSettingsProjectUrl},
        {L"\uEBE8", L"Report an issue", L"Suggestions and bug reports on GitHub",
         std::wstring(kSettingsProjectUrl) + L"/issues/new"},
        {L"\uE81C", L"Usage history guide", L"Recording rules, storage, export and verification",
         std::wstring(kSettingsProjectUrl) + L"#usage-history"},
    };
    for (const LinkCard& link : links) {
        UiCardSpec spec;
        spec.glyph = link.glyph;
        spec.title = link.title;
        spec.description = {{link.description}};
        spec.trailingGlyph = L"\uE8A7";
        spec.action = {UiActionKind::OpenUrl, 0, link.url};
        spec.minHeight = 62;
        y = UiAddCard(g, items, id, x, y, width, spec).card.bottom + 3;
    }

    // Credits and license.
    y = UiAddSection(items, id, x, y - 3 + 20, width, L"Credits and license");
    {
        constexpr float kLogo = 18, kLogoGap = 14;
        UiCardSpec spec;
        spec.glyph = L"\uE8B9";
        spec.title = L"Provider logos";
        spec.description = {
            {L"From "},
            {L"Simple Icons", UiRunStyle::Link, UiInk::Inherit,
             {UiActionKind::OpenUrl, 0, L"https://simpleicons.org"}},
            {L" (CC0 1.0); the GitHub Copilot mark from "},
            {L"Primer Octicons", UiRunStyle::Link, UiInk::Inherit,
             {UiActionKind::OpenUrl, 0, L"https://github.com/primer/octicons"}},
            {L" (MIT License, Copyright (c) 2026 GitHub Inc.). Google Antigravity uses the Gemini mark.\n"
             L"Claude, Anthropic, OpenAI, ChatGPT, Codex, Google, Gemini, Antigravity, GitHub and "
             L"GitHub Copilot are trademarks of their respective owners. Logos only identify which "
             L"account a tile shows."},
        };
        spec.trailing = {4 * kLogo + 3 * kLogoGap, kLogo};
        UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
        float logoLeft = card.trailing.left;
        for (PCWSTR path : {kClaudeLogoPath, kOpenAiLogoPath, kGeminiLogoPath, kCopilotLogoPath}) {
            UiItem logo;
            logo.id = id++;
            logo.kind = UiItemKind::Shape;
            logo.rect = {logoLeft, card.trailing.top, logoLeft + kLogo, card.trailing.top + kLogo};
            logo.opacity = 0.85f;
            logo.parts.push_back({path, {}, UiInk::Primary});
            items.push_back(std::move(logo));
            logoLeft += kLogo + kLogoGap;
        }
        y = card.card.bottom + 3;
    }
    {
        PCWSTR label = L"MIT License";
        float labelWidth =
            UiLayoutSize(UiLayout(g, label, UiTextStyleFor(UiFont::Body), 1000, false)).width;
        UiCardSpec spec;
        spec.glyph = L"\uE8A5";
        spec.title = L"License";
        spec.description = {{L"Free to use, modify and share."}};
        spec.trailing = {std::ceil(labelWidth), 20};
        UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
        // Hyperlink button: its padding reaches into the card's, so the text aligns right.
        UiItem link;
        link.id = id++;
        link.kind = UiItemKind::Link;
        link.text = label;
        link.rect = UiInflate(card.trailing, 11, 6);
        link.action = {UiActionKind::OpenUrl, 0, L"https://opensource.org/license/mit"};
        items.push_back(std::move(link));
        y = card.card.bottom;
    }
    return y;
}
