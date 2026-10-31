// Settings > About: app card, links, credits and license (approved mockup about.png). Pure
// builder.

// App icon (52 DIP view box): four quota arcs on tracks with pace dots, as a segmented ring tile.
static constexpr PCWSTR kSettingsAboutTracks[4] = {
    L"M3.103,21.963 A23.25,23.25 0 0 1 21.963,3.103",
    L"M30.037,3.103 A23.25,23.25 0 0 1 48.897,21.963",
    L"M48.897,30.037 A23.25,23.25 0 0 1 30.037,48.897",
    L"M21.963,48.897 A23.25,23.25 0 0 1 3.103,30.037",
};
static constexpr PCWSTR kSettingsAboutFills[4] = {
    L"M3.103,21.963 A23.25,23.25 0 0 1 3.939,18.661",
    L"M30.037,3.103 A23.25,23.25 0 0 1 48.468,20.022",
    L"M48.897,30.037 A23.25,23.25 0 0 1 40.757,43.966",
    L"M21.963,48.897 A23.25,23.25 0 0 1 16.543,47.24",
};
static constexpr uint32_t kSettingsAboutFillColors[4] = {0x43A047, 0xE53935, 0xFDD835, 0x43A047};
static constexpr PCWSTR kSettingsAboutDots[4] = {
    L"M4.371,14.025a1.7,1.7 0 1 0 3.4,0a1.7,1.7 0 1 0 -3.4,0Z",
    L"M44.374,14.27a1.7,1.7 0 1 0 3.4,0a1.7,1.7 0 1 0 -3.4,0Z",
    L"M36.03,46.074a1.7,1.7 0 1 0 3.4,0a1.7,1.7 0 1 0 -3.4,0Z",
    L"M1.567,30.874a1.7,1.7 0 1 0 3.4,0a1.7,1.7 0 1 0 -3.4,0Z",
};

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
        PCWSTR summary = L"AI subscription quotas for Anthropic, OpenAI and Google Antigravity on "
                         L"the Windows 11 taskbar.";
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

        UiItem icon;
        icon.id = id++;
        icon.kind = UiItemKind::Shape;
        icon.viewBox = 52;
        icon.rect = {x + 1 + kPadX, center - kIcon / 2, x + 1 + kPadX + kIcon, center + kIcon / 2};
        for (int i = 0; i < 4; i++) {
            icon.parts.push_back({kSettingsAboutTracks[i], {}, UiInk::Primary, 0.17f, 4.5f});
            icon.parts.push_back({kSettingsAboutFills[i], UiRgb(kSettingsAboutFillColors[i]),
                                  UiInk::Inherit, 1, 4.5f});
        }
        for (PCWSTR dot : kSettingsAboutDots) {
            icon.parts.push_back({dot, UiRgb(0xFFFFFF)});
            icon.parts.push_back({dot, UiRgb(0x000000, 0.55f), UiInk::Inherit, 1, 0.8f});
        }
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
            {L" (CC0 1.0). Google Antigravity uses the Gemini mark.\n"
             L"Claude, Anthropic, OpenAI, ChatGPT, Codex, Google, Gemini and Antigravity are "
             L"trademarks of their respective owners. Logos only identify which account a tile "
             L"shows."},
        };
        spec.trailing = {3 * kLogo + 2 * kLogoGap, kLogo};
        UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
        float logoLeft = card.trailing.left;
        for (PCWSTR path : {kClaudeLogoPath, kOpenAiLogoPath, kGeminiLogoPath}) {
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
