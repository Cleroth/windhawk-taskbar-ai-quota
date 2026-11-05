// Settings window shell: navigation pane, pinned page header (with the Layout and Display
// preview, or Accounts' note and Add account menu) and page content, built from an explicit view
// snapshot (settings/view.h; approved mockups: nav and header of layout-v2.png, accounts-add.png).
// Pure.

static constexpr float kSettingsNavWidth = 260;
static constexpr float kSettingsHeaderHeight = 56;
static constexpr float kSettingsContentMaxWidth = 1000;

static void SettingsUiBuildFrame(UiGraphics& g, const SettingsView& view, D2D1_SIZE_F client,
                                 UiFrame& frame) {
    static constexpr std::pair<I18nKey, PCWSTR> kPages[] = {
        {TrCtx(L"settings-page", L"Accounts"), L"\uE716"}, {TrCtx(L"settings-page", L"Layout"), L"\uE8A9"},
        {TrCtx(L"settings-page", L"Display"), L"\uE790"},  {TrCtx(L"settings-page", L"Behavior"), L"\uE9E9"},
        {TrCtx(L"settings-page", L"History"), L"\uE81C"},  {TrCtx(L"settings-page", L"About"), L"\uE946"},
    };
    int page = std::clamp(int(view.page), 0, int(SettingsUiPage::Count) - 1);

    // Navigation pane (mockup .nav, .nitem): 38 DIP items, 3 DIP apart.
    for (int i = 0; i < int(SettingsUiPage::Count); i++) {
        UiItem nav;
        nav.id = uint32_t(1 + i);
        nav.kind = UiItemKind::Nav;
        nav.rect = {8, 6 + 41.0f * float(i), kSettingsNavWidth - 8, 44 + 41.0f * float(i)};
        nav.text = I18nText(view.locale, kPages[i].first);
        nav.icon = kPages[i].second;
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
    title.runs.push_back({I18nText(view.locale, kPages[page].first)});
    frame.main.push_back(std::move(title));

    // Running ids are unique per page; interactive controls take fixed ones (settings/view.h).
    // On Layout and Display the preview card is pinned under the title, 12 DIP above the
    // viewport, unless the viewport would get too short (UiUnpinHeader): then it scrolls at the
    // content's top.
    uint32_t id = uint32_t(page + 1) << 12;
    float header = kSettingsHeaderHeight, contentTop = 0;
    // Accounts: Add account (mockup .btn.accent: 14 DIP padding, 12 DIP glyph) at the title row's
    // right opens the provider menu; while accounts exist, a note under the title.
    if (SettingsUiPage(page) == SettingsUiPage::Accounts) {
        std::wstring label = I18nText(view.locale, TrCtx(L"button", L"Add account"));
        float labelWidth = UiLayoutSize(UiLayout(g, label, UiTextStyleFor(UiFont::Body), 1000, false)).width;
        float right = kSettingsNavWidth + left + std::max(width, 1.0f);
        UiItem add;
        add.id = SettingsControlId(SettingsField::AccountAdd);
        add.kind = UiItemKind::Button;
        add.button = UiButtonStyle::Accent;
        add.rect = {right - std::ceil(labelWidth) - 28 - 12 - 8, 14, right, 46};
        add.text = label;
        add.icon = L"\uE710";
        add.iconSize = 12;
        add.action = SettingsCommand(SettingsField::AccountAdd, -1);
        add.options = {
            {L"Anthropic (Claude)", 0, L"Sign in with your browser", {{kClaudeLogoPath, {}, UiInk::Primary}}},
            {L"OpenAI (ChatGPT/Codex)", 1, L"Sign in with your browser", {{kOpenAiLogoPath, {}, UiInk::Primary}}},
            {L"Google Antigravity", 2, L"Uses the signed-in Antigravity app or CLI",
             {{kGeminiLogoPath, {}, UiInk::Primary}}},
            {L"GitHub Copilot", 3, L"Sign in with a code on github.com", {{kCopilotLogoPath, {}, UiInk::Primary}}},
            {L"OpenCode Go", 4, L"Sign in with an API key from the OpenCode console",
             {{kOpenCodeLogoPath, {}, UiInk::Primary}}},
        };
        frame.main.push_back(std::move(add));
        if (!view.settings.accounts.empty()) {
            UiItem note;
            note.id = 101;
            note.kind = UiItemKind::Text;
            note.font = UiFont::Note;
            note.ink = UiInk::Muted;
            note.rect = {kSettingsNavWidth + left, 50, right, 69};
            note.runs.push_back({view.settings.skipHiddenPolling ?
                                     L"Drag to reorder. Toggle hides an account from the taskbar; hidden "
                                     L"accounts are not polled." :
                                     L"Drag to reorder. Toggle hides an account from the taskbar."});
            frame.main.push_back(std::move(note));
            header = 69;
        }
    }
    if (SettingsUiPage(page) == SettingsUiPage::Layout || SettingsUiPage(page) == SettingsUiPage::Display) {
        std::vector<UiItem> preview;
        float height = SettingsAddPreviewCard(g, view, preview, id, 0, 0, std::max(width, 1.0f));
        bool unpinned = UiUnpinHeader(client.height, kSettingsHeaderHeight, height + 12);
        float dx = unpinned ? left : kSettingsNavWidth + left, dy = unpinned ? 0 : kSettingsHeaderHeight;
        for (UiItem& item : preview) {
            item.rect = {item.rect.left + dx, item.rect.top + dy, item.rect.right + dx, item.rect.bottom + dy};
            (unpinned ? frame.content : frame.main).push_back(std::move(item));
        }
        (unpinned ? contentTop : header) += height + 12;
    }
    frame.viewport = {kSettingsNavWidth, header, std::max(client.width, kSettingsNavWidth),
                      std::max(client.height, header)};

    // Content, in viewport coordinates.
    float bottom = 0;
    switch (SettingsUiPage(page)) {
        case SettingsUiPage::Accounts:
            bottom = SettingsUiBuildAccounts(g, view, frame.content, id, left, 0, width);
            break;
        case SettingsUiPage::About:
            bottom = SettingsUiBuildAbout(g, view, frame.content, id, left, 2, width);
            break;
        case SettingsUiPage::Behavior:
            bottom = SettingsUiBuildBehavior(g, view, frame.content, id, left, 0, width);
            break;
        case SettingsUiPage::History:
            bottom = SettingsUiBuildHistory(g, view, frame.content, id, left, 0, width);
            break;
        case SettingsUiPage::Layout:
            bottom = SettingsUiBuildLayout(g, view, frame.content, id, left, contentTop, width);
            break;
        case SettingsUiPage::Display:
            bottom = SettingsUiBuildDisplay(g, view, frame.content, id, left, contentTop, width);
            break;
        case SettingsUiPage::Count:
            break;
    }
    frame.contentHeight = bottom + 28;
}
