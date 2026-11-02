// Usage history window shell: the scrolling navigation pane, the pinned header (title, status or
// an export's progress, pill, icon buttons, view and range segments, Compare) and the content,
// built from the view snapshot (history/view.h; approved mockups s-light, s-min, s-menu, s-states,
// q13b-scroll). Pure.

static constexpr float kHistoryUiNavWidth = 260;

static void HistoryUiBuildFrame(UiGraphics& g, const HistoryUiView& view, D2D1_SIZE_F client, UiFrame& frame) {
    uint32_t id = kHistoryUiFirstRunning;
    const bool light = view.theme == UiThemeKind::Light;
    // While an export writes, every control but the legend's chips is disabled (s-states).
    const bool active = !view.exporting;
    const HistoryUiViewAccount* selected =
        view.selected >= 0 && size_t(view.selected) < view.accounts.size() ? &view.accounts[size_t(view.selected)] :
                                                                              nullptr;

    // Navigation pane (mockup .nav, .nitem): 38 DIP entries on a 41 DIP pitch, scrolling on its
    // own. All accounts; configured accounts with their logo, label and weekly value (as their
    // flyout's weekly row: used or remaining, in the usage color, muted while stale, a dash while
    // unknown); then the Removed group (mockup .ngroup: a 36 DIP header), muted and without values.
    frame.navPane = {0, 0, kHistoryUiNavWidth, std::max(client.height, 0.0f)};
    float y = 6;
    UiItem all;
    all.id = kHistoryUiNavAll;
    all.kind = UiItemKind::Nav;
    all.rect = {8, y, kHistoryUiNavWidth - 8, y + 38};
    all.text = L"All accounts";
    all.icon = L"\uE716";
    all.selected = !selected;
    all.enabled = active;
    all.action = HistoryUiAction(HistoryUiCommand::Select);
    frame.nav.push_back(std::move(all));
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
            header.runs = {{L"Removed", UiRunStyle::Strong}};
            frame.nav.push_back(std::move(header));
            y += 36;
        }
        UiItem entry;
        entry.id = kHistoryUiNavAccount + account.slot;
        entry.kind = UiItemKind::Nav;
        entry.rect = {8, y, kHistoryUiNavWidth - 8, y + 38};
        entry.text = account.label;
        if (PCWSTR logo = HistoryUiLogo(account.provider)) entry.parts = {{logo, {}, UiInk::Primary}};
        entry.ink = account.removed ? UiInk::Muted : UiInk::Primary;
        entry.selected = selected == &account;
        entry.enabled = active;
        entry.action = HistoryUiAction(HistoryUiCommand::Select, 0, account.historyId);
        if (!account.removed) {
            UiRun value{L"\u2014", UiRunStyle::Normal, UiInk::Muted};
            if (account.weeklyPct >= 0) {
                double shown = view.remaining ? std::clamp(100 - account.weeklyPct, 0.0, 100.0) : account.weeklyPct;
                value.text = HistoryUiNumber(shown, 0) + L"%";
                if (!account.stale) {
                    // Light theme: darker variants of the usage colors, readable on its surface.
                    static constexpr uint32_t kLightInks[][2] = {
                        {0xE53935, 0xC42B1C}, {0xFB8C00, 0xB35C00}, {0xFDD835, 0x876A00}, {0x43A047, 0x1D7A35},
                        {0xD55E00, 0xB75100}, {0xE69F00, 0x946700}, {0x56B4E9, 0x387597},
                    };
                    uint32_t rgb = UsageColor(account.weeklyPct, false, view.yellowThreshold, view.orangeThreshold,
                                              view.redThreshold, view.colorblind) & 0xFFFFFF;
                    for (const auto& ink : kLightInks) {
                        if (light && ink[0] == rgb) rgb = ink[1];
                    }
                    value.ink = UiInk::Inherit;
                    value.color = UiRgb(rgb);
                }
            }
            entry.runs.push_back(std::move(value));
        }
        frame.nav.push_back(std::move(entry));
        y += 41;
    }
    frame.navHeight = y - 3 + 6;

    // Header (mockup .hhead): 12 DIP from the pane, 24 DIP from the right edge.
    float left = kHistoryUiNavWidth + 12, right = std::max(client.width - 24, left + 1), width = right - left;

    // Icon buttons (mockup .ibtn: 32 x 30 DIP, 4 apart) at the title row's right: Refresh,
    // Export (once loaded) and the More menu, named by their tooltips (s-tooltips, q7a-icons;
    // Export's by its scope, as the classic button, over the classic description).
    struct IconButton {
        uint32_t id;
        PCWSTR glyph;
        HistoryUiCommand command;
        bool enabled;
        PCWSTR name, detail;
    };
    const IconButton buttons[] = {
        {kHistoryUiRefresh, L"\uE72C", HistoryUiCommand::Refresh, active, L"Refresh (F5)", nullptr},
        {kHistoryUiExport, L"\uEDE1", HistoryUiCommand::Export, active && view.canExport,
         selected ? L"Export CSV\u2026" : L"Export all CSV\u2026",
         L"Export all raw meters for the selected account, or every account under All accounts. Uses the selected "
         L"date range, plus the preceding 7 days in week comparison. UTC timestamps; text IDs; no labels."},
        {kHistoryUiMore, L"\uE712", HistoryUiCommand::Menu, active, L"More options", nullptr}};
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
        button.tooltip = spec.detail ? std::vector<UiRun>{{spec.name, UiRunStyle::Strong},
                                                          {std::wstring(L"\n") + spec.detail, UiRunStyle::Normal,
                                                           UiInk::Secondary}} :
                                       std::vector<UiRun>{{spec.name}};
        button.action = HistoryUiAction(spec.command, spec.command == HistoryUiCommand::Menu ? -1 : 0);
        if (spec.command == HistoryUiCommand::Menu) {
            button.options = {{L"Settings", int(HistoryUiMenuItem::Settings), {}, {}, L"\uE713"},
                              {L"Open history folder", int(HistoryUiMenuItem::Folder), {}, {}, L"\uE838"},
                              {L"About these charts", int(HistoryUiMenuItem::About), {}, {}, L"\uE946"},
                              {L"Clear all history\u2026", int(HistoryUiMenuItem::Clear), {}, {}, L"\uE74D", true}};
        }
        frame.main.push_back(std::move(button));
        x += 36;
    }

    // Status and pill (mockup .hstat, .pill) 12 DIP before the buttons. The status (or a running
    // export's progress in its place, s-states .upd: a 16 DIP ring and 6 DIP gap before its row
    // count, drawn by the client so the count changes without a rebuild; as wide as the journal's
    // largest export, 250,000 samples of 8 rows) stays on the title row while it holds the whole
    // title too; otherwise it joins the line under the title.
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
    const std::wstring widest = HistoryUiProgressText(2000000);
    struct Pill {
        std::wstring text;
        UiInk ink;
        bool on;
    };
    Pill pill = view.pill == HistoryUiPill::Off       ? Pill{L"Off", UiInk::Secondary, false} :
                view.pill == HistoryUiPill::NotSaving ? Pill{L"Not saving", UiInk::Danger, true} :
                view.pill == HistoryUiPill::Waiting   ? Pill{L"Waiting", UiInk::Caution, true} :
                Pill{L"Recording \u00B7 " + std::to_wstring(view.retentionDays) +
                         (view.retentionDays == 1 ? L" day" : L" days"), UiInk::Success, true};
    PCWSTR title = selected ? selected->label.c_str() : L"All accounts";
    D2D1_SIZE_F pillSize = UiPillSize(g, pill.text);
    float titleWidth = std::ceil(UiLayoutSize(UiLayout(g, title, UiTextStyleFor(UiFont::Title), 100000, false)).width);
    float statusWidth = view.exporting     ? 22 + measure(widest, UiFont::Caption) :
                        view.status.empty() ? 0 : measure(view.status, UiFont::Caption);
    bool narrow = statusWidth > 0 && titleWidth + 12 + statusWidth + 12 + pillSize.width + 12 + 104 > width;
    float pillRight = right - 104 - 12, titleRight = pillRight - pillSize.width - 12;
    UiAddPill(frame.main, id++, {titleRight + 12, 14, pillRight, 34}, pill.text, pill.ink, pill.on);
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
    // didn't fit the title row; an export's progress 12 DIP after that line, or starting it.
    std::wstring subtitle;
    if (selected) {
        subtitle = std::wstring(HistoryUiProviderName(selected->provider)) + L" \u00B7 " +
                   (selected->removed                                    ? L"Removed" :
                    !view.recordingEnabled || selected->recordingOff ? L"Recording off" :
                    selected->paused                                     ? L"Paused" : L"Recording");
    }
    if (narrow && !view.exporting) subtitle += (subtitle.empty() ? L"" : L" \u00B7 ") + view.status;
    float top = 56;
    if (!subtitle.empty() || narrow) {
        float lineRight = right;
        if (narrow && view.exporting) {
            float progressWidth = 22 + measure(widest, UiFont::Note);
            float x = subtitle.empty() ? left :
                      std::max(std::min(left + measure(subtitle, UiFont::Note) + 12, right - progressWidth), left);
            progress({x, 42, x + progressWidth, 61}, UiFont::Note, DWRITE_TEXT_ALIGNMENT_LEADING);
            lineRight = x - 12;
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
        top = 70;
    }

    // Command bar (mockup .cmdbar): View and Range segments and the Compare switch (4 DIP more
    // before it), 10 DIP apart, wrapping into rows 10 DIP apart. Comparing shows Range as 7 days,
    // disabled.
    std::vector<UiOption> views, ranges;
    for (int i = 0; i < int(std::size(kHistoryViewNames)); i++) views.push_back({kHistoryViewNames[i], i});
    for (int i = 0; i < int(std::size(kHistoryUiRangeNames)); i++) ranges.push_back({kHistoryUiRangeNames[i], i});
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
    PCWSTR compare = L"Compare previous week";
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
