// Settings window shell: navigation pane, pinned page header and page content, built from an
// explicit view snapshot (settings/view.h; approved mockups: nav and header of layout-v2.png).
// Pure. Pages not rebuilt yet show a placeholder card.

static constexpr float kSettingsNavWidth = 260;
static constexpr float kSettingsHeaderHeight = 56;
static constexpr float kSettingsContentMaxWidth = 1000;

static void SettingsUiBuildFrame(UiGraphics& g, const SettingsView& view, D2D1_SIZE_F client,
                                 UiFrame& frame) {
    static constexpr PCWSTR kPages[][2] = {
        {L"Accounts", L"\uE716"}, {L"Layout", L"\uE8A9"},  {L"Display", L"\uE790"},
        {L"Behavior", L"\uE9E9"}, {L"History", L"\uE81C"}, {L"About", L"\uE946"},
    };
    int page = std::clamp(int(view.page), 0, int(SettingsUiPage::Count) - 1);

    // Navigation pane (mockup .nav, .nitem): 38 DIP items, 3 DIP apart.
    for (int i = 0; i < int(SettingsUiPage::Count); i++) {
        UiItem nav;
        nav.id = uint32_t(1 + i);
        nav.kind = UiItemKind::Nav;
        nav.rect = {8, 6 + 41.0f * float(i), kSettingsNavWidth - 8, 44 + 41.0f * float(i)};
        nav.text = kPages[i][0];
        nav.icon = kPages[i][1];
        nav.selected = i == page;
        nav.action = {UiActionKind::Navigate, i};
        frame.main.push_back(std::move(nav));
    }

    // Page column: 20 DIP left and 40 DIP right padding; wider windows center the content at
    // its maximum width. The pinned header's title aligns with the content.
    float columnWidth = std::max(client.width - kSettingsNavWidth, 0.0f);
    float width = std::clamp(columnWidth - 60, 0.0f, kSettingsContentMaxWidth);
    float left = 20 + std::max((columnWidth - 60 - width) / 2, 0.0f);
    UiItem title;
    title.id = 100;
    title.kind = UiItemKind::Text;
    title.font = UiFont::Title;
    title.rect = {kSettingsNavWidth + left, 6, kSettingsNavWidth + left + std::max(width, 1.0f), 42};
    title.runs.push_back({kPages[page][0]});
    frame.main.push_back(std::move(title));
    frame.viewport = {kSettingsNavWidth, kSettingsHeaderHeight,
                      std::max(client.width, kSettingsNavWidth),
                      std::max(client.height, kSettingsHeaderHeight)};

    // Content, in viewport coordinates. Running ids are unique per page; interactive controls
    // take fixed ones (settings/view.h).
    uint32_t id = uint32_t(page + 1) << 12;
    float bottom;
    switch (SettingsUiPage(page)) {
        case SettingsUiPage::About:
            bottom = SettingsUiBuildAbout(g, view.version, frame.content, id, left, 2, width);
            break;
        case SettingsUiPage::Behavior:
            bottom = SettingsUiBuildBehavior(g, view, frame.content, id, left, 0, width);
            break;
        case SettingsUiPage::History:
            bottom = SettingsUiBuildHistory(g, view, frame.content, id, left, 0, width);
            break;
        default: {
            UiCardSpec spec;
            spec.glyph = L"\uE946";
            spec.title = L"Not built yet";
            spec.description = {{L"This page is still in the classic Settings window. Close this "
                                 L"window and open Settings from the taskbar menu to change it."}};
            bottom = UiAddCard(g, frame.content, id, left, 2, width, spec).card.bottom;
            break;
        }
    }
    frame.contentHeight = bottom + 28;
}
