// Settings > About: app card, version info to copy, links, credits and license (approved mockup
// about.png). Pure builder.

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
static float SettingsUiBuildAbout(UiGraphics& g, const SettingsView& view,
                                  std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                  float width) {
    const I18nLocale& locale = view.locale;

    // App card (mockup .aboutcard): icon, name, version line, summary.
    {
        constexpr float kPadX = 20, kPadY = 18, kIcon = 52, kGap = 18;
        float textLeft = x + 1 + kPadX + kIcon + kGap;
        float textWidth = std::max(x + width - 1 - kPadX - textLeft, 40.0f);
        std::wstring versionLine = I18nText(locale, Tr(L"Version {version}  \u00B7  by Cleroth  \u00B7  Windhawk mod"),
                                            {{L"version", view.version}});
        std::wstring summary = I18nText(locale, Tr(L"AI subscription quotas for Anthropic, OpenAI, Google Antigravity, "
                                                   L"GitHub Copilot and OpenCode Go on the Windows 11 taskbar."));
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
        addLine(L"Taskbar AI Quota", UiFont::Subtitle, UiInk::Primary, 26);  // i18n-en: the product name
        lineTop += 2;
        addLine(versionLine, UiFont::Caption, UiInk::Muted, versionHeight);
        addLine(summary, UiFont::Caption, UiInk::Muted, summaryHeight);
        y += height;
    }

    // Version info for bug reports; the button says for a moment whether it copied, as wide as its
    // widest state.
    {
        UiCardSpec spec;
        spec.glyph = L"\uE8C8";
        spec.title = I18nText(locale, Tr(L"Copy version info"));
        spec.description = {{I18nText(locale, Tr(L"The mod\u2019s name and version and the Windows build, for bug "
                                                 L"reports."))}};
        spec.minHeight = 62;
        const std::vector<std::wstring> states = {I18nText(locale, TrCtx(L"button", L"Copy")),
                                                  I18nText(locale, TrCtx(L"button", L"Copied")),
                                                  I18nText(locale, TrCtx(L"button", L"Copy failed"))};
        y = SettingsAddButtonCard(g, items, id, x, y + 3, width, spec, SettingsField::CopyVersionInfo,
                                  states[!view.versionCopied ? 0 : *view.versionCopied ? 1 : 2], {}, states);
    }

    // Links: whole cards open the page in the browser.
    y = UiAddSection(items, id, x, y + 20, width, I18nText(locale, TrCtx(L"settings-section", L"Links")));
    struct LinkCard {
        PCWSTR glyph;
        std::wstring title, description, url;
    };
    const LinkCard links[] = {
        {L"\uE774", I18nText(locale, Tr(L"Project page")), L"github.com/Cleroth/windhawk-taskbar-ai-quota",
         kSettingsProjectUrl},
        {L"\uEBE8", I18nText(locale, Tr(L"Report an issue")),
         I18nText(locale, Tr(L"Suggestions and bug reports on GitHub")),
         std::wstring(kSettingsProjectUrl) + L"/issues/new"},
        {L"\uE81C", I18nText(locale, Tr(L"Usage history guide")),
         I18nText(locale, Tr(L"Recording rules, storage, export and verification")),
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
    y = UiAddSection(items, id, x, y - 3 + 20, width,
                     I18nText(locale, TrCtx(L"settings-section", L"Credits and license")));
    {
        // Alphabetical, ignoring case (README > Contributors), joined by the language's list
        // patterns; each name links to its GitHub profile by its login, its link ID.
        constexpr std::pair<PCWSTR, PCWSTR> kContributors[] = {
            {L"amirf147", L"amirf147"}, {L"Hugh Macdonald", L"HughMacdonald"},  // i18n-en
            {L"incconutwo", L"incconutwo"}, {L"smnio", L"smnio"},
            {L"Sof2245", L"Sof2245"}, {L"yalov", L"yalov"},  // i18n-en
        };
        std::vector<std::vector<I18nRun>> names;
        for (auto [name, login] : kContributors) names.push_back({{name, false, login}});
        std::vector<I18nRun> sentence =
            I18nRuns(locale, Tr(L"Thanks to {names}."), {{L"names", I18nList(locale, names)}});
        UiCardSpec spec;
        spec.glyph = L"\uE716";
        spec.title = I18nText(locale, Tr(L"Contributors"));
        for (const I18nRun& run : sentence) {
            spec.description.push_back(
                run.link.empty() ? UiRun{run.text} :
                                   UiRun{run.text, UiRunStyle::Link, UiInk::Inherit,
                                         {UiActionKind::OpenUrl, 0, L"https://github.com/" + run.link}});
        }
        y = UiAddCard(g, items, id, x, y, width, spec).card.bottom + 3;
    }
    {
        constexpr float kLogo = 18, kLogoGap = 14;
        UiCardSpec spec;
        spec.glyph = L"\uE8B9";
        spec.title = I18nText(locale, Tr(L"Provider logos"));
        spec.description = I18nUiRuns(
            I18nRuns(locale, Tr(L"From <link id=\"simple_icons\">Simple Icons</link> (CC0 1.0); the GitHub Copilot "
                                L"mark from <link id=\"octicons\">Primer Octicons</link> (MIT License, Copyright (c) "
                                L"2026 GitHub Inc.). Google Antigravity uses the Gemini mark.\nClaude, Anthropic, "
                                L"OpenAI, ChatGPT, Codex, Google, Gemini, Antigravity, GitHub, GitHub Copilot and "
                                L"OpenCode are trademarks of their respective owners. Logos only identify which "
                                L"account a tile shows.")),
            {{L"simple_icons", {UiActionKind::OpenUrl, 0, L"https://simpleicons.org"}},
             {L"octicons", {UiActionKind::OpenUrl, 0, L"https://github.com/primer/octicons"}}});
        spec.trailing = {5 * kLogo + 4 * kLogoGap, kLogo};
        UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
        float logoLeft = card.trailing.left;
        for (PCWSTR path :
             {kClaudeLogoPath, kOpenAiLogoPath, kGeminiLogoPath, kCopilotLogoPath, kOpenCodeLogoPath}) {
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
        std::wstring label = I18nText(locale, Tr(L"MIT License"));
        float labelWidth =
            UiLayoutSize(UiLayout(g, label, UiTextStyleFor(UiFont::Body), 1000, false)).width;
        UiCardSpec spec;
        spec.glyph = L"\uE8A5";
        spec.title = I18nText(locale, Tr(L"License"));
        spec.description = {{I18nText(locale, Tr(L"Free to use, modify and share."))}};
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
