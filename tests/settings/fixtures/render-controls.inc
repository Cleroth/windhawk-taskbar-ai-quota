// Offscreen sheet of the toolkit controls the history window builds on (history mockups
// s-light, s-hc, s-states, s-menu, s-compare, q13b-scroll): two scrolled navigation panes
// painted as the host paints its main surface (UiPaintNavPane), then rows of controls, each
// with its own interaction state, and an open one-line menu. PNG evidence for review, no pixel
// goldens.

static void RenderControlsSheet(UiGraphics& g, IWICImagingFactory* wic, const std::wstring& path,
                                const UiThemeInput& theme, UINT dpi) {
    constexpr float kWidth = 1290, kHeight = 760, kLeft = 560;
    float scale = float(dpi) / 96;
    winrt::com_ptr<IWICBitmap> bitmap = RenderBitmap(wic, {kWidth, kHeight}, dpi);
    winrt::com_ptr<ID2D1RenderTarget> target = RenderTarget(g, bitmap.get(), dpi);
    UiTargetCache cache;
    UiPalette palette = UiPaletteFor(theme);
    bool light = theme.kind == UiThemeKind::Light;
    target->BeginDraw();
    target->Clear(palette.background);
    auto caption = [&](PCWSTR text, float x, float y) {
        UiDrawLayout(target.get(), cache, UiLayout(g, text, UiTextStyleFor(UiFont::Caption), 700, false), {x, y},
                     palette.textMuted);
    };

    // Navigation entries as the history sidebar shows them: All accounts, accounts with logos and
    // weekly values in usage colors (a long label, an unknown stale value, a selected and a
    // disabled entry; brand-colored logos on CC 1, Go, the selected CC 6 and the disabled Go 3,
    // which keeps its ink), the Removed header and removed accounts in muted ink.
    std::vector<UiItem> nav;
    float navHeight = 0;
    {
        struct Entry {
            PCWSTR label, logo, value;
            uint32_t color;
            bool removed = false;
        };
        const uint32_t yellow = light ? 0x8A6D00 : 0xFDD835, green = light ? 0x2E7D32 : 0x43A047,
                       red = light ? 0xC62828 : 0xE53935, orange = light ? 0xC25E00 : 0xFB8C00;
        const Entry entries[] = {
            {L"OAI", kOpenAiLogoPath, L"62%", yellow},
            {L"CC 1", kClaudeLogoPath, L"47%", green},
            {L"CC 2", kClaudeLogoPath, L"93%", red},
            {L"Go", kGeminiLogoPath, L"30%", green},
            {L"OAI 2", kOpenAiLogoPath, L"18%", green},
            {L"CC 3", kClaudeLogoPath, L"71%", yellow},
            {L"Research \u2014 shared Max seat", kClaudeLogoPath, L"55%", yellow},
            {L"CC 4", kClaudeLogoPath, L"8%", green},
            {L"Codex CI", kOpenAiLogoPath, L"84%", orange},
            {L"Go 2", kGeminiLogoPath, L"12%", green},
            {L"CC 5", kClaudeLogoPath, L"39%", green},
            {L"OAI Team", kOpenAiLogoPath, L"66%", yellow},
            {L"CC 6", kClaudeLogoPath, L"91%", red},
            {L"Claude personal", kClaudeLogoPath, L"\u2014", 0},
            {L"Go 3", kGeminiLogoPath, L"24%", green},
            {L"account_3f9c1a2b4d5e6f70", kClaudeLogoPath, nullptr, 0, true},
            {L"account_a01b77c2e9d40c15", kOpenAiLogoPath, nullptr, 0, true},
        };
        float y = 6;
        UiItem all;
        all.id = 1;
        all.kind = UiItemKind::Nav;
        all.rect = {8, y, 252, y + 38};
        all.text = L"All accounts";
        all.icon = L"\uE716";
        all.action = {UiActionKind::Command, 0, {}, 1};
        nav.push_back(all);
        y += 41;
        uint32_t id = 2;
        bool removed = false;
        for (const Entry& entry : entries) {
            if (entry.removed && !std::exchange(removed, true)) {
                // Removed group header (mockup .ngroup): 12 DIP semibold muted caption in 36 DIP.
                UiItem header;
                header.id = 90;
                header.kind = UiItemKind::Text;
                header.font = UiFont::Caption;
                header.ink = UiInk::Muted;
                header.rect = {22, y - 3 + 12, 252, y - 3 + 28};
                header.runs = {{L"Removed", UiRunStyle::Strong}};
                nav.push_back(std::move(header));
                y += 36 - 3;
            }
            UiItem item;
            item.id = id++;
            item.kind = UiItemKind::Nav;
            item.rect = {8, y, 252, y + 38};
            item.text = entry.label;
            item.parts = {{entry.logo, {}, UiInk::Primary}};
            for (PCWSTR branded : {L"CC 1", L"Go", L"CC 6", L"Go 3"}) {
                if (std::wstring_view(entry.label) != branded) continue;
                const LogoBrand& b = entry.logo == kClaudeLogoPath ? kClaudeLogoBrand : kGeminiLogoBrand;
                item.parts[0].fill = UiPartFill{UiRgb(b.from & 0xFFFFFF), UiRgb(b.to & 0xFFFFFF), {b.x0, b.y0}, {b.x1, b.y1}};
            }
            item.ink = entry.removed ? UiInk::Muted : UiInk::Primary;
            if (entry.value) {
                UiRun value{entry.value};
                if (entry.color) value.color = UiRgb(entry.color);
                else value.ink = UiInk::Muted;
                item.runs.push_back(std::move(value));
            }
            item.selected = std::wstring_view(entry.label) == L"CC 6";
            item.enabled = std::wstring_view(entry.label) != L"Go 3";
            item.action = {UiActionKind::Command, int(item.id), {}, 1};
            nav.push_back(std::move(item));
            y += 41;
        }
        navHeight = y - 3 + 6;
    }

    // Two panes, 600 DIP tall: scrolled to the end with keyboard focus on the last removed
    // account, the pointer on Codex CI and the thin scrollbar; scrolled part way with the
    // expanded scrollbar under the pointer.
    for (int pane = 0; pane < 2; pane++) {
        UiFrame frame;
        float x = pane * 270.0f;
        frame.navPane = {x, 0, x + 260, 600};
        frame.nav = nav;
        for (UiItem& item : frame.nav) {
            item.rect = {item.rect.left + x, item.rect.top, item.rect.right + x, item.rect.bottom};
        }
        frame.navHeight = navHeight;
        float range = frame.navHeight - 600;
        UiPaintState state;
        if (pane == 0) {
            state.focus = {frame.nav.back().id};
            state.focusVisible = true;
            state.hot = {10};
        }
        float offset = std::round((pane == 0 ? range : range / 3) * scale) / scale;  // Pixel-aligned, as the host
        UiPaintNavPane(target.get(), g, cache, palette, frame, state, offset, pane == 1, scale);
        CHECK(range > 0);
    }

    // Rows of controls, each with its own state.
    UiAction command{UiActionKind::Command, 0, {}, 2};
    std::vector<UiOption> views = {{L"Short-term", 0}, {L"Weekly", 1}, {L"Extra usage", 2}, {L"All meters", 3}};
    std::vector<UiOption> ranges = {{L"24h", 1}, {L"7d", 7}, {L"30d", 30}, {L"90d", 90}, {L"All", 0}};
    auto row = [&](const std::vector<UiItem>& items, UiPaintState state) {
        UiPaintItems(target.get(), g, cache, palette, items, state, {0, 0}, scale, -1e9f, 1e9f);
    };
    auto segmented = [&](std::vector<UiItem>& items, uint32_t id, float x, float y,
                         const std::vector<UiOption>& options, int value, bool enabled) {
        D2D1_SIZE_F size = UiSegmentedSize(g, options);
        UiAction action = command;
        action.value = value;
        UiAddSegmented(items, id, {x, y, x + size.width, y + size.height}, options, enabled, action);
        return x + size.width + 10;
    };

    float y = 12;
    caption(L"Segments: the pointer on Weekly; keyboard focus on 7d", kLeft, y);
    std::vector<UiItem> items;
    float x = segmented(items, 20, kLeft, y + 20, views, 3, true);
    segmented(items, 21, x, y + 20, ranges, 7, true);
    UiPaintState state;
    state.hot = {20, 1};
    state.focus = {21};
    state.focusVisible = true;
    row(items, state);

    y += 68;
    caption(L"A disabled group (compare forces 7d); Extra usage pressed", kLeft, y);
    items.clear();
    x = segmented(items, 22, kLeft, y + 20, ranges, 7, false);
    segmented(items, 23, x, y + 20, views, 1, true);
    state = {};
    state.hot = state.pressed = {23, 2};
    row(items, state);

    y += 68;
    caption(L"Leading switch: off under the pointer, on, on disabled", kLeft, y);
    items.clear();
    x = kLeft;
    for (int i = 0; i < 3; i++) {
        D2D1_SIZE_F size = UiToggleSize(g, L"Compare previous week");
        UiAddToggle(items, 30 + uint32_t(i), {x, y + 20, x + size.width, y + 20 + size.height}, i > 0, i < 2,
                    command, L"Compare previous week");
        x += size.width + 24;
    }
    state = {};
    state.hot = {30};
    row(items, state);

    y += 68;
    caption(L"Pills: recording, waiting, not saving, off; a label cut at 150 DIP", kLeft, y);
    items.clear();
    x = kLeft;
    struct Pill {
        PCWSTR text;
        UiInk ink;
        bool on;
    };
    const Pill pills[] = {{L"Recording \u00B7 90 days", UiInk::Success, true}, {L"Waiting", UiInk::Caution, true},
                          {L"Not saving", UiInk::Danger, true}, {L"Off", UiInk::Secondary, false}};
    for (uint32_t i = 0; i < std::size(pills); i++) {
        D2D1_SIZE_F size = UiPillSize(g, pills[i].text);
        UiAddPill(items, 40 + i, {x, y + 26, x + size.width, y + 26 + size.height}, pills[i].text, pills[i].ink,
                  pills[i].on);
        x += size.width + 12;
    }
    UiItem label;
    label.id = 45;
    label.kind = UiItemKind::Text;
    label.trim = true;
    label.rect = {x + 12, y + 26, x + 162, y + 46};
    label.runs = {{L"Research \u2014 shared Max seat \u00B7 Fable weekly"}};
    items.push_back(std::move(label));
    row(items, {});

    y += 68;
    caption(L"Info bars: caution with its button under the pointer, error, info", kLeft, y);
    items.clear();
    uint32_t id = 100;
    float bottom = UiAddInfoBar(g, items, id, 50, kLeft, y + 20, 700,
                                {{L"History recording is off. Saved history stays; nothing new is recorded."}},
                                UiSeverity::Caution, UiInfoButton{51, L"Enable recording", command});
    bottom = UiAddInfoBar(g, items, id, 52, kLeft, bottom + 8, 700,
                          {{L"History storage is in use by another Explorer session, or is inaccessible."}},
                          UiSeverity::Error);
    bottom = UiAddInfoBar(g, items, id, 53, kLeft, bottom + 8, 700,
                          {{L"Exported 12,345 meter rows (all accounts) to C:\\Users\\clero\\Documents\\"
                            L"usage-history.csv. Labels omitted; times UTC."}});
    bottom = UiAddInfoBar(g, items, id, 54, kLeft, bottom + 8, 700, {{L"Could not save recording consent."}},
                          UiSeverity::Error, UiInfoButton{55, L"Retry", command, false});
    state = {};
    state.hot = {51};
    row(items, state);

    // An icon-only menu button (its glyph centered) holding its one-line menu open, right-aligned
    // 4 DIP under it as the host places it: glyph rows, a separator before the last one, the
    // pointer on the second.
    y = bottom + 16;
    caption(L"Menu button open: one-line rows with glyphs and a separator; the pointer on the second row", kLeft, y);
    items.clear();
    UiItem more;
    more.id = 60;
    more.kind = UiItemKind::Button;
    more.button = UiButtonStyle::Subtle;
    more.rect = {kLeft + 380, y + 20, kLeft + 412, y + 50};
    more.icon = L"\uE712";
    more.iconSize = 15;
    more.action = command;
    more.action.value = -1;  // No row is the current one
    more.options = {{L"Settings", 0, {}, {}, L"\uE713"},
                    {L"Open history folder", 1, {}, {}, L"\uE838"},
                    {L"About these charts", 2, {}, {}, L"\uE946"},
                    {L"Clear all history\u2026", 3, {}, {}, L"\uE74D", true}};
    items.push_back(more);
    state = {};
    state.open = 60;
    row(items, state);
    std::vector<UiItem> rows;
    D2D1_SIZE_F size = UiLayoutOptions(g, more.options, more.action, 32, rows);
    float menuLeft = std::round((more.rect.right - size.width) * scale) / scale;
    D2D1_RECT_F box{menuLeft, more.rect.bottom + 4, menuLeft + size.width, more.rect.bottom + 4 + size.height};
    UiFillRect(target.get(), cache, box, 8, palette.popup, scale);
    UiStrokeRect(target.get(), cache, box, 8,
                 palette.kind == UiThemeKind::HighContrast ? palette.cardBorder : palette.controlBorder, 1, scale);
    UiPaintState hover;
    hover.hot = {2};
    UiPaintItems(target.get(), g, cache, palette, rows, hover, {box.left, box.top}, scale, -1e9f, 1e9f);
    CHECK(box.bottom <= kHeight);

    winrt::check_hresult(target->EndDraw());
    cache.Clear();
    SaveRender(wic, bitmap.get(), path);
    std::string narrow;
    for (wchar_t c : path) narrow += c < 0x80 ? char(c) : '?';
    std::cout << "RENDER " << narrow << '\n';
}

static void RunControlRenders(const std::wstring& directory) {
    winrt::check_hresult(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    {
        UiGraphics g;
        UiCreateGraphics(g);
        winrt::com_ptr<IWICImagingFactory> wic;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                              __uuidof(IWICImagingFactory), wic.put_void()));
        for (const RenderTheme& theme : RenderThemes()) {
            for (UINT dpi : {96u, 144u, 192u}) {
                std::string name = std::string("render-controls-history-") + theme.name + "-" +
                                   std::to_string(dpi) + ".png";
                Run(name, [&] {
                    RenderControlsSheet(g, wic.get(), directory + L"\\" + std::wstring(name.begin(), name.end()),
                                        theme.input, dpi);
                });
            }
        }
    }
    CoUninitialize();
}
