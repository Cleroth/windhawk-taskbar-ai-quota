// Usage history window shell: the scrolling navigation pane, the pinned header (title, status or
// an export's progress, pill, icon buttons, view and range segments, Compare) and the content,
// built from the view snapshot (history/view.h; approved mockups s-light, s-min, s-menu, s-states,
// q13b-scroll). Pure.

static constexpr float kHistoryUiNavWidth = 260;

static void HistoryUiBuildFrame(UiGraphics& g, const HistoryUiView& view, D2D1_SIZE_F client, UiFrame& frame) {
    uint32_t id = kHistoryUiFirstRunning;
    const bool light = view.theme == UiThemeKind::Light;
    const I18nLocale& locale = view.locale;
    const std::wstring allAccounts = I18nText(locale, TrCtx(L"history-nav", L"All accounts"));
    // While an export writes, every control but the legend's chips is disabled (s-states).
    const bool active = !view.exporting;
    const HistoryUiViewAccount* selected =
        view.selected >= 0 && size_t(view.selected) < view.accounts.size() ? &view.accounts[size_t(view.selected)] :
                                                                              nullptr;

    // Navigation pane (mockup .nav, .nitem): 38 DIP entries on a 41 DIP pitch, scrolling on its
    // own. All accounts; configured accounts with their logo, label and weekly value (as their
    // flyout's weekly row: used or remaining, in the usage color, muted while stale, a dash while
    // unknown); then the Removed group (mockup .ngroup: a 36 DIP header), muted and without values.
    // A label an entry cuts shows whole in its tooltip.
    frame.navPane = {0, 0, kHistoryUiNavWidth, std::max(client.height, 0.0f)};
    auto addEntry = [&](UiItem entry) {
        if (UiLayoutSize(UiLayout(g, entry.text, UiTextStyleFor(UiFont::Body), 10000, false)).width >
            UiNavLabelWidth(g, entry)) {
            entry.tooltip = {{entry.text}};
        }
        frame.nav.push_back(std::move(entry));
    };
    float y = 6;
    UiItem all;
    all.id = kHistoryUiNavAll;
    all.kind = UiItemKind::Nav;
    all.rect = {8, y, kHistoryUiNavWidth - 8, y + 38};
    all.text = allAccounts;
    all.icon = L"\uE716";
    all.selected = !selected;
    all.enabled = active;
    all.action = HistoryUiAction(HistoryUiCommand::Select);
    addEntry(std::move(all));
    y += 41;
    bool removedHeader = false;
    for (size_t i = 0; i < view.accounts.size(); i++) {
        const HistoryUiViewAccount& account = view.accounts[i];
        if (account.removed && !std::exchange(removedHeader, true)) {
            UiItem header;
            header.id = id++;
            header.kind = UiItemKind::Text;
            header.font = UiFont::Caption;
            header.ink = UiInk::Muted;
            header.rect = {22, y + 12, kHistoryUiNavWidth - 8, y + 28};
            header.runs = {{I18nText(locale, TrCtx(L"history-nav", L"Removed")), UiRunStyle::Strong}};
            frame.nav.push_back(std::move(header));
            y += 36;
        }
        UiItem entry;
        entry.id = kHistoryUiNavAccount + account.slot;
        entry.kind = UiItemKind::Nav;
        entry.rect = {8, y, kHistoryUiNavWidth - 8, y + 38};
        entry.text = account.label;
        entry.parts = HistoryUiLogo(view, account.provider, !account.removed);
        entry.ink = account.removed ? UiInk::Muted : UiInk::Primary;
        entry.selected = selected == &account;
        entry.enabled = active;
        entry.action = HistoryUiAction(HistoryUiCommand::Select, 0, account.historyId);
        if (!account.removed) {
            UiRun value{L"\u2014", UiRunStyle::Normal, UiInk::Muted};
            if (account.weeklyPct >= 0) {
                double shown = view.remaining ? std::clamp(100 - account.weeklyPct, 0.0, 100.0) : account.weeklyPct;
                value.text = HistoryUiNumber(locale, shown, 0) + L"%";
                if (!account.stale) {
                    // Light theme: darker variants of the default and colorblind usage colors,
                    // readable on its surface; another color darkens in OKLCH until it reads at
                    // 4.5:1 on white.
                    static constexpr uint32_t kLightInks[][2] = {
                        {0xE53935, 0xC42B1C}, {0xFB8C00, 0xB35C00}, {0xFDD835, 0x876A00}, {0x43A047, 0x1D7A35},
                        {0xD55E00, 0xB75100}, {0xE69F00, 0x946700}, {0x56B4E9, 0x387597},
                    };
                    uint32_t usage = UsageColor(account.weeklyPct, false, view.yellowThreshold, view.orangeThreshold,
                                                view.redThreshold, view.usageColors);
                    uint32_t rgb = usage & 0xFFFFFF;
                    bool variant = false;
                    for (const auto& ink : kLightInks) {
                        if (light && ink[0] == rgb) {
                            rgb = ink[1];
                            variant = true;
                        }
                    }
                    for (double lift = -0.02; light && !variant && 1.05 / (RelativeLuminance(rgb) + 0.05) < 4.5;
                         lift -= 0.02) {
                        rgb = UsageHighlight(usage, lift) & 0xFFFFFF;
                    }
                    value.ink = UiInk::Inherit;
                    value.color = UiRgb(rgb);
                }
            }
            entry.runs.push_back(std::move(value));
        }
        addEntry(std::move(entry));
        y += 41;
    }
    frame.navHeight = y - 3 + 6;

    // Header (mockup .hhead): 12 DIP from the pane, 24 DIP from the right edge.
    float left = kHistoryUiNavWidth + 12, right = std::max(client.width - 24, left + 1), width = right - left;

    // Icon buttons (mockup .ibtn: 32 x 30 DIP, 4 apart) at the title row's right: Refresh,
    // Export (once loaded) and the More menu, named by their tooltips (s-tooltips, q7a-icons;
    // Export's by its scope, over its description).
    struct IconButton {
        uint32_t id;
        PCWSTR glyph;
        HistoryUiCommand command;
        bool enabled;
        std::wstring name, detail;
    };
    const IconButton buttons[] = {
        {kHistoryUiRefresh, L"\uE72C", HistoryUiCommand::Refresh, active,
         I18nText(locale, TrCtx(L"history-button", L"Refresh (F5)")), {}},
        {kHistoryUiExport, L"\uEDE1", HistoryUiCommand::Export, active && view.canExport,
         I18nText(locale, selected ? TrCtx(L"history-button", L"Export CSV\u2026") :
                                     TrCtx(L"history-button", L"Export all CSV\u2026")),
         I18nText(locale, Tr(L"Export all raw meters for the selected account, or every account under All accounts. "
                             L"Uses the selected date range, plus the preceding 7 days in week comparison. UTC "
                             L"timestamps; text IDs; no labels."))},
        {kHistoryUiMore, L"\uE712", HistoryUiCommand::Menu, active,
         I18nText(locale, TrCtx(L"history-button", L"More options")), {}}};
    float x = right - 3 * 32 - 2 * 4;
    for (const IconButton& spec : buttons) {
        UiItem button;
        button.id = spec.id;
        button.kind = UiItemKind::Button;
        button.button = UiButtonStyle::Subtle;
        button.rect = {x, 8, x + 32, 38};
        button.icon = spec.glyph;
        button.iconSize = 15;
        button.enabled = spec.enabled;
        button.tooltip = !spec.detail.empty() ? std::vector<UiRun>{{spec.name, UiRunStyle::Strong},
                                                                   {L"\n" + spec.detail, UiRunStyle::Normal,
                                                                    UiInk::Secondary}} :
                                                std::vector<UiRun>{{spec.name}};
        button.action = HistoryUiAction(spec.command, spec.command == HistoryUiCommand::Menu ? -1 : 0);
        if (spec.command == HistoryUiCommand::Menu) {
            button.options = {
                {I18nText(locale, TrCtx(L"history-menu", L"Settings")), int(HistoryUiMenuItem::Settings), {}, {},
                 L"\uE713"},
                {I18nText(locale, TrCtx(L"history-menu", L"Open history folder")), int(HistoryUiMenuItem::Folder), {},
                 {}, L"\uE838"},
                {I18nText(locale, TrCtx(L"history-menu", L"About these charts")), int(HistoryUiMenuItem::About), {}, {},
                 L"\uE946"},
                {I18nText(locale, TrCtx(L"history-menu", L"Clear all history\u2026")), int(HistoryUiMenuItem::Clear),
                 {}, {}, L"\uE74D", true}};
        }
        frame.main.push_back(std::move(button));
        x += 36;
    }

    // Status and pill (mockup .hstat, .pill) 12 DIP before the buttons. The status (or a running
    // export's progress in its place, s-states .upd: a 16 DIP ring and 6 DIP gap before its row
    // count, drawn by the client so the count changes without a rebuild; as wide as the widest line
    // the journal's largest export, 250,000 samples of 8 rows, can show in any plural form) stays on
    // the title row while it holds the whole title too; otherwise it joins the line under the
    // title. So does the pill, and the status with it, when the title doesn't fit beside the pill.
    auto measure = [&](const std::wstring& text, UiFont font) {
        return std::ceil(UiLayoutSize(UiLayout(g, text, UiTextStyleFor(font), 10000, false)).width);
    };
    auto progress = [&](D2D1_RECT_F rect, UiFont font, DWRITE_TEXT_ALIGNMENT align) {
        UiItem item;
        item.id = kHistoryUiProgress;
        item.kind = UiItemKind::Custom;
        item.rect = rect;
        item.font = font;
        item.align = align;
        frame.main.push_back(std::move(item));
    };
    // Each plural form at its largest count (digits are tabular): counts below 2,000,000 down to
    // the first of each form, and the small ones only some forms take.
    std::vector<std::wstring> progressLines;
    unsigned forms = 0;
    auto form = [&](size_t rows) {
        unsigned bit = 1u << unsigned(I18nPluralCategory(locale.Language().plural, rows));
        if (!(std::exchange(forms, forms | bit) & bit)) progressLines.push_back(HistoryUiProgressText(locale, rows));
    };
    for (size_t rows = 2000000; rows > 1999900; rows--) form(rows);
    form(1);
    form(0);
    auto progressWidth = [&](UiFont font) {
        float widest = 0;
        for (const std::wstring& line : progressLines) widest = std::max(widest, measure(line, font));
        return 22 + widest;
    };
    struct Pill {
        std::wstring text;
        UiInk ink;
        bool on;
    };
    Pill pill = view.pill == HistoryUiPill::Off ? Pill{I18nText(locale, TrCtx(L"history-pill", L"Off")), UiInk::Secondary,
                                                       false} :
                view.pill == HistoryUiPill::NotSaving ?
                Pill{I18nText(locale, TrCtx(L"history-pill", L"Not saving")), UiInk::Danger, true} :
                view.pill == HistoryUiPill::Waiting ?
                Pill{I18nText(locale, TrCtx(L"history-pill", L"Waiting")), UiInk::Caution, true} :
                view.retentionDays <= 0 ?
                Pill{I18nText(locale, TrCtx(L"history-pill", L"Recording \u00B7 forever")), UiInk::Success, true} :
                Pill{I18nText(locale, TrCtx(L"history-pill", L"Recording \u00B7 {days, plural, one{# day} other{# days}}"),
                              {{L"days", view.retentionDays}}),
                     UiInk::Success, true};
    const std::wstring& title = selected ? selected->label : allAccounts;
    D2D1_SIZE_F pillSize = UiPillSize(g, pill.text);
    float titleWidth = std::ceil(UiLayoutSize(UiLayout(g, title, UiTextStyleFor(UiFont::Title), 100000, false)).width);
    float statusWidth = view.exporting     ? progressWidth(UiFont::Caption) :
                        view.status.empty() ? 0 : measure(view.status, UiFont::Caption);
    bool pillUnder = titleWidth + 12 + pillSize.width + 12 + 104 > width;
    bool narrow = statusWidth > 0 && (pillUnder || titleWidth + 12 + statusWidth + 12 + pillSize.width + 12 + 104 > width);
    float pillRight = right - 104 - 12, titleRight = pillRight;
    if (!pillUnder) {
        titleRight = pillRight - pillSize.width - 12;
        UiAddPill(frame.main, id++, {titleRight + 12, 14, pillRight, 34}, pill.text, pill.ink, pill.on);
    }
    if (statusWidth > 0 && !narrow) {
        if (view.exporting) {
            progress({titleRight - statusWidth, 14, titleRight, 34}, UiFont::Caption, DWRITE_TEXT_ALIGNMENT_TRAILING);
        } else {
            UiItem status;
            status.id = id++;
            status.kind = UiItemKind::Text;
            status.font = UiFont::Caption;
            status.ink = UiInk::Muted;
            status.rect = {titleRight - statusWidth, 16, titleRight, 32};
            status.runs = {{view.status}};
            frame.main.push_back(std::move(status));
        }
        titleRight -= statusWidth + 12;
    }
    UiItem heading;
    heading.id = id++;
    heading.kind = UiItemKind::Text;
    heading.font = UiFont::Title;
    heading.rect = {left, 6, std::max(titleRight, left + 1), 42};
    heading.runs = {{title}};
    heading.trim = true;
    frame.main.push_back(std::move(heading));

    // Under the title (mockup .psub): one account's provider and recording, then a status that
    // didn't fit the title row; after that line (or starting it), 12 DIP apart, an export's
    // progress and the pill that didn't fit beside the title, each starting a line of its own 22
    // DIP lower where it doesn't fit after what precedes it.
    std::wstring subtitle;
    if (selected) {
        I18nKey recording = selected->removed ? TrCtx(L"history-account-state", L"Removed") :
                            !view.recordingEnabled || selected->recordingOff ?
                                                TrCtx(L"history-account-state", L"Recording off") :
                            selected->paused ? TrCtx(L"history-account-state", L"Paused") :
                                               TrCtx(L"history-account-state", L"Recording");
        subtitle = I18nText(locale, TrCtx(L"history-account", L"{provider} \u00B7 {recording}"),
                            {{L"provider", HistoryUiProviderName(locale, selected->provider)},
                             {L"recording", I18nText(locale, recording)}});
    }
    if (narrow && !view.exporting) {
        subtitle = subtitle.empty() ? view.status :
                                      I18nText(locale, TrCtx(L"history-subtitle", L"{account} \u00B7 {updated}"),
                                               {{L"account", subtitle}, {L"updated", view.status}});
    }
    float top = 56;
    if (!subtitle.empty() || narrow || pillUnder) {
        float lineRight = right, x = subtitle.empty() ? left : left + measure(subtitle, UiFont::Note) + 12, lineTop = 42;
        auto flow = [&](float itemWidth) {
            if (x > left && x + itemWidth > right) {
                x = left;
                lineTop += 22;
            }
            if (lineTop == 42) lineRight = std::min(lineRight, x - 12);
            float at = x;
            x += itemWidth + 12;
            return at;
        };
        if (narrow && view.exporting) {
            float progressLine = progressWidth(UiFont::Note), at = flow(progressLine);
            progress({at, lineTop, at + progressLine, lineTop + 19}, UiFont::Note, DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        if (pillUnder) {
            float at = flow(pillSize.width);
            UiAddPill(frame.main, id++, {at, lineTop - 0.5f, at + pillSize.width, lineTop + 19.5f}, pill.text, pill.ink,
                      pill.on);
        }
        if (!subtitle.empty()) {
            UiItem line;
            line.id = id++;
            line.kind = UiItemKind::Text;
            line.font = UiFont::Note;
            line.ink = UiInk::Muted;
            line.rect = {left, 42, std::max(lineRight, left + 1), 61};
            line.runs = {{subtitle}};
            line.trim = true;
            frame.main.push_back(std::move(line));
        }
        top = 70 + (lineTop - 42);
    }

    // Command bar (mockup .cmdbar): View and Range segments and the Compare switch (4 DIP more
    // before it), 10 DIP apart, wrapping into rows 10 DIP apart. Comparing shows Range as 7 days,
    // disabled.
    std::vector<UiOption> views, ranges;
    for (int i = 0; i < int(std::size(kHistoryViewNames)); i++) {
        views.push_back({I18nText(locale, kHistoryViewNames[i]), i});
    }
    for (int i = 0; i < int(std::size(kHistoryUiRangeNames)); i++) {
        ranges.push_back({I18nText(locale, kHistoryUiRangeNames[i]), i});
    }
    float cx = left;
    auto place = [&](D2D1_SIZE_F size) {
        if (cx > left && cx + size.width > right) {
            cx = left;
            top += 32 + 10;
        }
        D2D1_RECT_F rect{cx, top, cx + size.width, top + 32};
        cx += size.width + 10;
        return rect;
    };
    D2D1_SIZE_F viewsSize = UiSegmentedSize(g, views), rangesSize = UiSegmentedSize(g, ranges);
    UiAddSegmented(frame.main, kHistoryUiViews, place(viewsSize), std::move(views), active,
                   HistoryUiAction(HistoryUiCommand::View, int(view.view)));
    UiAddSegmented(frame.main, kHistoryUiRanges, place(rangesSize), std::move(ranges), active && !view.compare,
                   HistoryUiAction(HistoryUiCommand::Range,
                                   int(view.compare ? HistoryUiRange::Week : view.range)));
    std::wstring compare = I18nText(locale, TrCtx(L"history-toggle", L"Compare previous week"));
    D2D1_SIZE_F compareSize = UiToggleSize(g, compare);
    D2D1_RECT_F compareSlot = place({4 + compareSize.width, 32});
    UiAddToggle(frame.main, kHistoryUiCompare, {compareSlot.left + 4, compareSlot.top, compareSlot.right, compareSlot.bottom},
                view.compare, active, HistoryUiAction(HistoryUiCommand::Compare, !view.compare), compare);
    float header = top + 32 + 10;

    // Content (mockup .hcont), in viewport coordinates: the header's column, 20 DIP below the end.
    frame.viewport = {kHistoryUiNavWidth, header, std::max(client.width, kHistoryUiNavWidth),
                      std::max(client.height, header)};
    float bottom = HistoryUiBuildContent(g, view, frame.content, id, 12, 0, width,
                                         frame.viewport.bottom - frame.viewport.top);
    frame.contentHeight = bottom + 20;
}
