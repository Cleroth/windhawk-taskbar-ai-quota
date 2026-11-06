// The history window's frame (history/shell.h, content.h) per state, and the toolkit pieces it
// added (one-line menu rows, icon-only buttons). Synthetic views modeled on the approved mockups;
// hand-derived expectations.

// The mockups' accounts (OAI, CC 1, CC 2, Go; slots 10-13 apart from their order), fresh weekly
// values, recording, updated Sat 31 Oct 14:32; loaded (Export can start), no cards.
static HistoryUiView TestHistoryView(UiThemeKind theme = UiThemeKind::Dark) {
    HistoryUiView view;
    view.theme = theme;
    view.loading = false;
    view.canExport = true;
    view.status = L"Updated Sat 31 Oct 14:32";
    struct Entry {
        uint64_t id;
        uint32_t slot, provider;
        PCWSTR label;
        double pct;
    };
    for (const Entry& e : {Entry{0x0A, 12, 2, L"OAI", 62}, Entry{0xC1, 10, 1, L"CC 1", 47},
                           Entry{0xC2, 13, 1, L"CC 2", 93}, Entry{0x60, 11, 3, L"Go", 30}}) {
        HistoryUiViewAccount account;
        account.historyId = e.id;
        account.slot = e.slot;
        account.provider = e.provider;
        account.label = e.label;
        account.weeklyPct = e.pct;
        account.lastPollMs = kWeekTo;
        view.accounts.push_back(std::move(account));
    }
    return view;
}

static HistoryUiViewAccount TestRemoved(uint64_t id, uint32_t slot, uint32_t provider) {
    HistoryUiViewAccount account;
    account.historyId = id;
    account.slot = slot;
    account.provider = provider;
    wchar_t label[32];
    swprintf(label, ARRAYSIZE(label), L"account_%016llx", static_cast<unsigned long long>(id));
    account.label = label;
    account.removed = true;
    return account;
}

// A card in the series palette (`hue` indexes kHistoryColors; tinted as a scoped quota).
static HistoryUiCard TestCard(std::wstring title, size_t hue, std::wstring value, std::wstring when,
                              std::vector<std::wstring> lines, bool tinted = false) {
    HistoryUiCard card;
    card.title = std::move(title);
    COLORREF c = kHistoryColors[hue];
    card.color = tinted ? RGB((GetRValue(c) * 3 + 255) / 4, (GetGValue(c) * 3 + 255) / 4, (GetBValue(c) * 3 + 255) / 4) : c;
    card.value = std::move(value);
    card.when = std::move(when);
    card.lines = std::move(lines);
    return card;
}

static std::vector<std::wstring> TestLines(PCWSTR peak, PCWSTR coverage, PCWSTR estimate) {
    return {std::wstring(L"Peak ") + peak, std::wstring(L"Coverage ") + coverage,
            std::wstring(L"Est. \u226590%: ") + estimate};
}

// The approved s-light cards: All accounts, Weekly.
static std::vector<HistoryUiCard> TestWeeklyCards() {
    return {TestCard(L"OAI \u00B7 Weekly", 5, L"62%", L"now",
                     TestLines(L"77% \u00B7 Mon 26 Oct 11:31", L"166 h (98.8%)", L"0 h")),
            TestCard(L"CC 1 \u00B7 Weekly", 1, L"47%", L"now",
                     TestLines(L"63% \u00B7 Mon 26 Oct 21:32", L"166 h (98.8%)", L"0 h")),
            TestCard(L"CC 2 \u00B7 Weekly", 4, L"93%", L"now",
                     TestLines(L"93% \u00B7 Sat 31 Oct 14:31", L"166 h (98.8%)", L"0.4 h")),
            TestCard(L"CC 2 \u00B7 Fable weekly", 4, L"58%", L"now",
                     TestLines(L"58% \u00B7 Sat 31 Oct 13:32", L"166 h (98.8%)", L"0 h"), true),
            TestCard(L"Go \u00B7 Gemini weekly", 0, L"30%", L"now",
                     TestLines(L"61% \u00B7 Thu 29 Oct 20:31", L"166 h (98.8%)", L"0 h"))};
}

// Their chart lines: the same series, chips by the accounts' slots (OAI 12, CC 1 10, CC 2 13,
// Go 11) and meters.
static std::vector<HistoryUiLine> TestWeeklyLines() {
    struct Entry {
        uint64_t id;
        uint32_t slot;
        int meter;
        PCWSTR name, title;
    };
    const Entry entries[] = {
        {0x0A, 12, 1, L"OAI \u00B7 Weekly", L"OAI \u00B7 Weekly used (%)"},
        {0xC1, 10, 1, L"CC 1 \u00B7 Weekly", L"CC 1 \u00B7 Weekly used (%)"},
        {0xC2, 13, 1, L"CC 2 \u00B7 Weekly", L"CC 2 \u00B7 Weekly used (%)"},
        {0xC2, 13, 2, L"CC 2 \u00B7 Fable weekly", L"CC 2 \u00B7 Fable weekly used (%)"},
        {0x60, 11, 1, L"Go \u00B7 Gemini weekly", L"Go \u00B7 Gemini weekly used (%)"}};
    std::vector<HistoryUiCard> cards = TestWeeklyCards();
    std::vector<HistoryUiLine> lines;
    for (size_t i = 0; i < std::size(entries); i++) {
        HistoryUiLine line;
        line.chip = kHistoryUiChip + entries[i].slot * 8 + uint32_t(entries[i].meter);
        line.historyId = entries[i].id;
        line.meter = entries[i].meter;
        line.series = i;
        line.name = entries[i].name;
        line.title = entries[i].title;
        line.color = cards[i].color;
        line.stroke = entries[i].meter == 2 ? 2 : 3;
        lines.push_back(std::move(line));
    }
    return lines;
}

static HistoryUiView TestWeeklyView(UiThemeKind theme = UiThemeKind::Dark) {
    HistoryUiView view = TestHistoryView(theme);
    view.cards = TestWeeklyCards();
    view.lines = TestWeeklyLines();
    return view;
}

static std::wstring TestItemText(const UiItem& item) {
    return item.kind == UiItemKind::Text ? UiRunsText(item.runs) : item.text;
}

static const UiItem* TestFind(const std::vector<UiItem>& items, std::wstring_view text) {
    auto at = std::find_if(items.begin(), items.end(), [&](const UiItem& item) { return TestItemText(item) == text; });
    return at != items.end() ? &*at : nullptr;
}

static UiFrame TestHistoryFrame(const HistoryUiView& view, D2D1_SIZE_F client = {1100, 790}) {
    UiFrame frame;
    HistoryUiBuildFrame(TestGraphics(), view, client, frame);
    return frame;
}

static bool TestSameRect(D2D1_RECT_F a, D2D1_RECT_F b) {
    return std::abs(a.left - b.left) < 1e-3f && std::abs(a.top - b.top) < 1e-3f &&
           std::abs(a.right - b.right) < 1e-3f && std::abs(a.bottom - b.bottom) < 1e-3f;
}

// The content's panels (cards, info bars) in order.
static std::vector<const UiItem*> TestPanels(const UiFrame& frame) {
    std::vector<const UiItem*> panels;
    for (const UiItem& item : frame.content) {
        if (item.kind == UiItemKind::Card) panels.push_back(&item);
    }
    return panels;
}

static void RunHistoryFrameTests() {
    Run("history frame: the navigation pane lists All accounts, accounts with logos and weekly "
        "values, then the Removed group; entry ids follow history IDs", [] {
        HistoryUiView view = TestHistoryView();
        view.accounts.push_back(TestRemoved(0x3F9C1A2B4D5E6F70, 20, 1));
        view.accounts.push_back(TestRemoved(0xA01B77C2E9D40C15, 21, 2));
        UiFrame frame = TestHistoryFrame(view);
        CHECK(TestSameRect(frame.navPane, {0, 0, 260, 790}));
        const UiItem& all = frame.nav[0];
        CHECK(all.id == kHistoryUiNavAll && all.kind == UiItemKind::Nav && all.text == L"All accounts" &&
              all.icon == L"\uE716" && all.selected && TestSameRect(all.rect, {8, 6, 252, 44}));
        CHECK(all.action.kind == UiActionKind::Command && all.action.target == uint32_t(HistoryUiCommand::Select) &&
              all.action.key == 0);
        PCWSTR logos[] = {kOpenAiLogoPath, kClaudeLogoPath, kClaudeLogoPath, kGeminiLogoPath};
        PCWSTR values[] = {L"62%", L"47%", L"93%", L"30%"};
        uint32_t colors[] = {0xFDD835, 0x43A047, 0xE53935, 0x43A047};
        for (size_t i = 0; i < 4; i++) {
            const HistoryUiViewAccount& account = view.accounts[i];
            const UiItem* entry = UiFindItem(frame, kHistoryUiNavAccount + account.slot);
            CHECK(entry && entry == &frame.nav[1 + i] && entry->kind == UiItemKind::Nav && entry->text == account.label);
            CHECK(entry->parts.size() == 1 && entry->parts[0].path == logos[i] && entry->ink == UiInk::Primary);
            CHECK_NEAR(entry->rect.top, 47 + 41 * double(i));
            CHECK(entry->action.target == uint32_t(HistoryUiCommand::Select) && entry->action.key == account.historyId);
            CHECK(entry->runs.size() == 1 && entry->runs[0].text == values[i] && entry->runs[0].color &&
                  UiColorKey(*entry->runs[0].color) == (0xFF000000 | colors[i]) && !entry->selected);
        }
        // The Removed header 3 DIP under the last account, 36 DIP tall; its accounts muted, no values.
        const UiItem* header = TestFind(frame.nav, L"Removed");
        CHECK(header && header == &frame.nav[5] && header->kind == UiItemKind::Text && header->ink == UiInk::Muted);
        CHECK_NEAR(header->rect.top, 47 + 41 * 3 + 38 + 3 + 12);
        const UiItem& removed = frame.nav[6];
        CHECK(removed.id == kHistoryUiNavAccount + 20 && removed.text == L"account_3f9c1a2b4d5e6f70" &&
              removed.ink == UiInk::Muted && removed.runs.empty() && removed.parts[0].path == kClaudeLogoPath);
        CHECK_NEAR(removed.rect.top, 47 + 41 * 3 + 38 + 3 + 36);
        CHECK(frame.nav.back().text == L"account_a01b77c2e9d40c15" && frame.nav.back().action.key == 0xA01B77C2E9D40C15);
        CHECK_NEAR(frame.navHeight, frame.nav.back().rect.bottom + 6);
        // Without removed accounts, no group.
        CHECK(!TestFind(TestHistoryFrame(TestHistoryView()).nav, L"Removed"));
        // Logos in the primary ink; with brand colors chosen, the configured Claude and Gemini
        // logos fill with them (orange; purple to blue), OpenAI's and removed accounts' don't.
        for (size_t i = 1; i < frame.nav.size(); i++) CHECK(frame.nav[i].parts.empty() || !frame.nav[i].parts[0].fill);
        view.logoBrands = {true, true};
        frame = TestHistoryFrame(view);
        for (size_t i = 0; i < 4; i++) {
            const UiShapePart& logo = UiFindItem(frame, kHistoryUiNavAccount + view.accounts[i].slot)->parts[0];
            CHECK(logo.ink == UiInk::Primary && bool(logo.fill) == (i > 0));
            if (!logo.fill) continue;
            uint32_t from = i == 3 ? 0xFF9168C0 : 0xFFD97757, to = i == 3 ? 0xFF1BA1E3 : 0xFFD97757;
            CHECK(UiColorKey(logo.fill->from) == from && UiColorKey(logo.fill->to) == to);
            CHECK(i < 3 || (logo.fill->start.x < logo.fill->end.x && logo.fill->start.y < logo.fill->end.y));
        }
        CHECK(!frame.nav[6].parts[0].fill && !frame.nav.back().parts[0].fill);
        view.logoBrands = {};

        // Reordered and with one more account, each entry keeps its id; the selected one is marked.
        std::rotate(view.accounts.begin(), view.accounts.begin() + 2, view.accounts.begin() + 4);
        view.accounts.insert(view.accounts.begin(), TestHistoryView().accounts[0]);
        view.accounts.front().slot = 30;
        view.accounts.front().historyId = 0x0B;
        view.selected = 1;
        frame = TestHistoryFrame(view);
        CHECK(UiFindItem(frame, kHistoryUiNavAccount + 13)->text == L"CC 2");
        CHECK(UiFindItem(frame, kHistoryUiNavAccount + 13)->selected && !frame.nav[0].selected);
        CHECK(UiFindItem(frame, kHistoryUiNavAccount + 12)->text == L"OAI");
        CHECK(UiFindItem(frame, kHistoryUiNavAccount + 30)->action.key == 0x0B);
        // Focus order: the pane first, from All accounts down.
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops[0].ref.id == kHistoryUiNavAll && stops[1].ref.id == kHistoryUiNavAccount + 30 && stops[1].nav);
    });

    Run("history frame: navigation values as the flyout's weekly row: remaining mode, above 100%, "
        "stale muted, unknown a dash; light ink variants", [] {
        HistoryUiView view = TestHistoryView(UiThemeKind::Light);
        view.accounts[0].stale = true;
        view.accounts[1].weeklyPct = -1;
        view.accounts[2].weeklyPct = 112;
        UiFrame frame = TestHistoryFrame(view);
        const UiRun& stale = frame.nav[1].runs[0];
        CHECK(stale.text == L"62%" && !stale.color && stale.ink == UiInk::Muted);
        const UiRun& unknown = frame.nav[2].runs[0];
        CHECK(unknown.text == L"\u2014" && !unknown.color && unknown.ink == UiInk::Muted);
        CHECK(frame.nav[3].runs[0].text == L"112%" && UiColorKey(*frame.nav[3].runs[0].color) == 0xFFC42B1C);
        CHECK(UiColorKey(*frame.nav[4].runs[0].color) == 0xFF1D7A35);
        view.remaining = true;
        view.usageColors = UsagePaletteColors(UsagePalette::Colorblind, kDefaultUsageColors);
        frame = TestHistoryFrame(view);
        CHECK(frame.nav[3].runs[0].text == L"0%" && UiColorKey(*frame.nav[3].runs[0].color) == 0xFFB75100);
        CHECK(frame.nav[4].runs[0].text == L"70%" && UiColorKey(*frame.nav[4].runs[0].color) == 0xFF0072B2);
        view.theme = UiThemeKind::Dark;
        CHECK(UiColorKey(*TestHistoryFrame(view).nav[3].runs[0].color) == 0xFFD55E00);

        // Custom: the default colors keep their light variants; other colors show as picked in the
        // dark theme and, in the light one, darken (same hue) until they read at 4.5:1 on white,
        // unless they already do.
        view.remaining = false;
        view.usageColors = UsagePaletteColors(UsagePalette::Custom, kDefaultUsageColors);
        view.theme = UiThemeKind::Light;
        CHECK(UiColorKey(*TestHistoryFrame(view).nav[3].runs[0].color) == 0xFFC42B1C);
        CHECK(UiColorKey(*TestHistoryFrame(view).nav[4].runs[0].color) == 0xFF1D7A35);
        const uint32_t pale = 0xFFFFD0E0, deep = 0xFF203060;  // Pink above red; navy below yellow
        view.usageColors = {deep, 0xFF000000, 0xFF000000, pale};
        frame = TestHistoryFrame(view);
        uint32_t pink = UiColorKey(*frame.nav[3].runs[0].color);
        CHECK(pink != pale && 1.05 / (RelativeLuminance(pink) + 0.05) >= 4.5);
        CHECK(1.05 / (RelativeLuminance(pink) + 0.05) < 5.5);  // Darkened only as far as needed
        CHECK((pink >> 16 & 0xFF) > (pink & 0xFF) && (pink >> 16 & 0xFF) > (pink >> 8 & 0xFF));  // Still reddish
        CHECK(UiColorKey(*frame.nav[4].runs[0].color) == deep);
        view.theme = UiThemeKind::Dark;
        frame = TestHistoryFrame(view);
        CHECK(UiColorKey(*frame.nav[3].runs[0].color) == pale && UiColorKey(*frame.nav[4].runs[0].color) == deep);
    });

    Run("history frame: header title, status and pill; icon buttons; the narrow rule moves the "
        "status under the title", [] {
        HistoryUiView view = TestHistoryView();
        UiFrame frame = TestHistoryFrame(view);
        const UiItem* title = TestFind(frame.main, L"All accounts");
        CHECK(title && title->font == UiFont::Title && title->trim && TestSameRect(title->rect, {272, 6, title->rect.right, 42}));
        const UiItem* pill = &*std::find_if(frame.main.begin(), frame.main.end(),
                                            [](const UiItem& item) { return item.kind == UiItemKind::Pill; });
        CHECK(pill->text == L"Recording \u00B7 90 days" && pill->ink == UiInk::Success && pill->selected);
        CHECK_NEAR(pill->rect.right, 1076 - 104 - 12);
        CHECK_NEAR(pill->rect.top, 14);
        const UiItem* status = TestFind(frame.main, L"Updated Sat 31 Oct 14:32");
        CHECK(status && status->font == UiFont::Caption && status->ink == UiInk::Muted);
        CHECK_NEAR(status->rect.right, pill->rect.left - 12);
        CHECK_NEAR(title->rect.right, status->rect.left - 12);
        // Buttons: 32 x 30 DIP, 4 apart, icon only; Export once loaded; More holds the menu
        // (s-menu: Settings, Open history folder, About these charts, then Clear all history after
        // a separator).
        uint32_t ids[] = {kHistoryUiRefresh, kHistoryUiExport, kHistoryUiMore};
        HistoryUiCommand commands[] = {HistoryUiCommand::Refresh, HistoryUiCommand::Export, HistoryUiCommand::Menu};
        for (int i = 0; i < 3; i++) {
            const UiItem* button = UiFindItem(frame, ids[i]);
            CHECK(button && button->kind == UiItemKind::Button && button->button == UiButtonStyle::Subtle &&
                  button->text.empty() && !button->icon.empty());
            CHECK(TestSameRect(button->rect, {972 + 36.0f * float(i), 8, 1004 + 36.0f * float(i), 38}));
            CHECK(button->action.target == uint32_t(commands[i]) && button->enabled);
        }
        HistoryUiView unloaded = view;
        unloaded.canExport = false;
        CHECK(!UiFindItem(TestHistoryFrame(unloaded), kHistoryUiExport)->enabled);
        const UiItem* more = UiFindItem(frame, kHistoryUiMore);
        CHECK(more->options.size() == 4 && more->action.value == -1);
        const std::pair<PCWSTR, PCWSTR> menu[] = {{L"Settings", L"\uE713"}, {L"Open history folder", L"\uE838"},
                                                  {L"About these charts", L"\uE946"},
                                                  {L"Clear all history\u2026", L"\uE74D"}};
        const HistoryUiMenuItem values[] = {HistoryUiMenuItem::Settings, HistoryUiMenuItem::Folder,
                                            HistoryUiMenuItem::About, HistoryUiMenuItem::Clear};
        for (size_t i = 0; i < 4; i++) {
            const UiOption& option = more->options[i];
            CHECK(option.text == menu[i].first && option.icon == menu[i].second && option.value == int(values[i]) &&
                  option.separator == (i == 3));
        }
        // Their tooltips (s-tooltips, q7a-icons): names; Export's in strong text by its scope, over
        // its description.
        CHECK(UiRunsText(UiFindItem(frame, kHistoryUiRefresh)->tooltip) == L"Refresh (F5)");
        CHECK(UiRunsText(more->tooltip) == L"More options" && more->tooltip[0].style == UiRunStyle::Normal);
        const std::vector<UiRun>& exportTip = UiFindItem(frame, kHistoryUiExport)->tooltip;
        CHECK(exportTip.size() == 2 && exportTip[0].text == L"Export all CSV\u2026" &&
              exportTip[0].style == UiRunStyle::Strong && exportTip[1].ink == UiInk::Secondary &&
              exportTip[1].text == L"\nExport all raw meters for the selected account, or every account under All "
                                   L"accounts. Uses the selected date range, plus the preceding 7 days in week "
                                   L"comparison. UTC timestamps; text IDs; no labels.");
        HistoryUiView one = view;
        one.selected = 1;
        CHECK(UiFindItem(TestHistoryFrame(one), kHistoryUiExport)->tooltip[0].text == L"Export CSV\u2026");
        CHECK_NEAR(frame.viewport.top, 98);
        CHECK(TestSameRect(frame.viewport, {260, 98, 1100, 790}));

        // Pill states.
        struct PillCase {
            HistoryUiPill pill;
            int days;
            PCWSTR text;
            UiInk ink;
            bool on;
        };
        for (const PillCase& c : {PillCase{HistoryUiPill::Off, 90, L"Off", UiInk::Secondary, false},
                                  PillCase{HistoryUiPill::NotSaving, 90, L"Not saving", UiInk::Danger, true},
                                  PillCase{HistoryUiPill::Waiting, 90, L"Waiting", UiInk::Caution, true},
                                  PillCase{HistoryUiPill::Recording, 1, L"Recording \u00B7 1 day", UiInk::Success, true},
                                  PillCase{HistoryUiPill::Recording, 0, L"Recording \u00B7 forever", UiInk::Success, true}}) {
            view.pill = c.pill;
            view.retentionDays = c.days;
            frame = TestHistoryFrame(view);
            const UiItem* p = TestFind(frame.main, c.text);
            CHECK(p && p->kind == UiItemKind::Pill && p->ink == c.ink && p->selected == c.on);
        }

        // One account: its provider and recording under the title.
        view = TestHistoryView();
        view.selected = 2;
        frame = TestHistoryFrame(view);
        CHECK(TestFind(frame.main, L"CC 2") && TestFind(frame.main, L"Anthropic \u00B7 Recording"));
        CHECK_NEAR(UiFindItem(frame, kHistoryUiViews)->rect.top, 70);
        CHECK_NEAR(frame.viewport.top, 112);
        struct SubtitleCase {
            std::function<void(HistoryUiView&)> edit;
            PCWSTR text;
        };
        for (const SubtitleCase& c : {
                 SubtitleCase{[](HistoryUiView& v) { v.accounts[2].recordingOff = true; }, L"Anthropic \u00B7 Recording off"},
                 SubtitleCase{[](HistoryUiView& v) { v.accounts[2].paused = true; }, L"Anthropic \u00B7 Paused"},
                 SubtitleCase{[](HistoryUiView& v) { v.recordingEnabled = false; }, L"Anthropic \u00B7 Recording off"},
                 SubtitleCase{[](HistoryUiView& v) { v.accounts[2].removed = true; }, L"Anthropic \u00B7 Removed"},
                 SubtitleCase{[](HistoryUiView& v) { v.selected = 3; }, L"Google Antigravity \u00B7 Recording"}}) {
            HistoryUiView edited = TestHistoryView();
            edited.selected = 2;
            c.edit(edited);
            CHECK(TestFind(TestHistoryFrame(edited).main, c.text));
        }

        // The minimum window: the status joins the line under the title; the range segments and
        // Compare wrap to a second row (mockup s-min: a 154 DIP header).
        view = TestHistoryView();
        frame = TestHistoryFrame(view, {800, 550});
        const UiItem* under = TestFind(frame.main, L"Updated Sat 31 Oct 14:32");
        CHECK(under && under->font == UiFont::Note && under->rect.top == 42);
        const UiItem* ranges = UiFindItem(frame, kHistoryUiRanges);
        const UiItem* compare = UiFindItem(frame, kHistoryUiCompare);
        CHECK_NEAR(UiFindItem(frame, kHistoryUiViews)->rect.top, 70);
        CHECK(ranges->rect.left == 272 && ranges->rect.top == 112);
        CHECK_NEAR(compare->rect.left, ranges->rect.right + 10 + 4);
        CHECK(compare->rect.top == 112 && frame.viewport.top == 154);
        // It measures: a short title keeps the status on its row, a longer one joins it to the
        // account's line.
        view.selected = 2;
        frame = TestHistoryFrame(view, {800, 550});
        CHECK(TestFind(frame.main, L"Updated Sat 31 Oct 14:32")->font == UiFont::Caption);
        view.accounts[2].label = L"Claude Max personal";
        CHECK(TestFind(TestHistoryFrame(view, {800, 550}).main,
                       L"Anthropic \u00B7 Recording \u00B7 Updated Sat 31 Oct 14:32"));
        // Before the first load (no status) nothing moves; a title too long for the row moves it,
        // and one too long even beside the pill alone moves the pill too: after the account's
        // line, the title up to the buttons.
        view = TestHistoryView();
        view.status.clear();
        frame = TestHistoryFrame(view, {800, 550});
        CHECK(UiFindItem(frame, kHistoryUiViews)->rect.top == 56 && frame.viewport.top == 140);
        view = TestHistoryView();
        view.accounts[0].label = L"Research \u2014 shared Max seat for the whole engineering team";
        view.selected = 0;
        frame = TestHistoryFrame(view);
        const UiItem* accountLine = TestFind(frame.main, L"OpenAI \u00B7 Recording \u00B7 Updated Sat 31 Oct 14:32");
        const UiItem* longTitle = TestFind(frame.main, view.accounts[0].label);
        const UiItem* movedPill = TestFind(frame.main, L"Recording \u00B7 90 days");
        float accountWidth = TestTextWidth(L"OpenAI \u00B7 Recording \u00B7 Updated Sat 31 Oct 14:32", UiFont::Note);
        CHECK(accountLine && longTitle->trim && longTitle->rect.right == 1100 - 24 - 104 - 12);
        CHECK(TestSameRect(movedPill->rect, {272 + accountWidth + 12, 41.5f, movedPill->rect.right, 61.5f}));
        CHECK(accountLine->rect.right == movedPill->rect.left - 12 && UiFindItem(frame, kHistoryUiViews)->rect.top == 70);
    });

    Run("history frame: view and range segments, the Compare switch; comparing shows 7d disabled", [] {
        HistoryUiView view = TestHistoryView();
        view.view = HistoryView::AllMeters;
        view.range = HistoryUiRange::Month;
        UiFrame frame = TestHistoryFrame(view);
        const UiItem* views = UiFindItem(frame, kHistoryUiViews);
        const UiItem* ranges = UiFindItem(frame, kHistoryUiRanges);
        const UiItem* compare = UiFindItem(frame, kHistoryUiCompare);
        CHECK(views->kind == UiItemKind::Segmented && views->enabled && views->options.size() == 4);
        CHECK(views->options[0].text == L"Short-term" && views->options[3].text == L"All meters");
        CHECK(views->action.target == uint32_t(HistoryUiCommand::View) && UiSelectedSegment(*views) == 3);
        CHECK(ranges->kind == UiItemKind::Segmented && ranges->enabled && ranges->options.size() == 5);
        CHECK(ranges->options[0].text == L"24h" && ranges->options[4].text == L"All");
        CHECK(ranges->action.target == uint32_t(HistoryUiCommand::Range) && UiSelectedSegment(*ranges) == 2);
        CHECK(TestSameRect(views->rect, {272, 56, views->rect.right, 88}));
        CHECK_NEAR(ranges->rect.left, views->rect.right + 10);
        CHECK(compare->kind == UiItemKind::Toggle && compare->align == DWRITE_TEXT_ALIGNMENT_TRAILING &&
              compare->text == L"Compare previous week" && !compare->selected && compare->action.value == 1 &&
              compare->action.target == uint32_t(HistoryUiCommand::Compare));
        CHECK_NEAR(compare->rect.left, ranges->rect.right + 14);
        // Focus order: the pane, then Refresh, Export, More, the segments and Compare.
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        std::vector<uint32_t> main;
        for (const UiFocusStop& stop : stops) {
            if (stop.surface == UiSurfaceId::Main) main.push_back(stop.ref.id);
        }
        CHECK((main == std::vector<uint32_t>{kHistoryUiRefresh, kHistoryUiExport, kHistoryUiMore, kHistoryUiViews,
                                             kHistoryUiRanges, kHistoryUiCompare}));
        CHECK(stops.size() == 5 + main.size());
        view.compare = true;
        frame = TestHistoryFrame(view);
        ranges = UiFindItem(frame, kHistoryUiRanges);
        compare = UiFindItem(frame, kHistoryUiCompare);
        CHECK(!ranges->enabled && UiSelectedSegment(*ranges) == 1 && compare->selected && compare->action.value == 0);
        CHECK(UiSelectedSegment(*UiFindItem(frame, kHistoryUiViews)) == 3);
    });

    Run("history frame: info bars by state, in order, 10 DIP apart", [] {
        HistoryUiView view = TestWeeklyView();
        CHECK(TestPanels(TestHistoryFrame(view))[0]->rect.top == 0);  // The chart's card first
        view.recordingEnabled = false;
        view.storageError = L"History storage is in use by another Explorer session, or is inaccessible.";
        view.storageCapped = true;
        UiFrame frame = TestHistoryFrame(view);
        std::vector<const UiItem*> panels = TestPanels(frame);
        CHECK(panels[0]->ink == UiInk::Caution && panels[1]->ink == UiInk::Danger && panels[2]->ink == UiInk::Caution);
        CHECK(panels[0]->rect.top == 0 && panels[1]->rect.top == panels[0]->rect.bottom + 10 &&
              panels[2]->rect.top == panels[1]->rect.bottom + 10 && panels[3]->rect.top == panels[2]->rect.bottom + 10);
        const UiItem* banner = TestFind(frame.content, L"History recording is off. Saved history stays; nothing new is recorded.");
        CHECK(banner && banner->rect.top > panels[0]->rect.top && banner->rect.bottom < panels[0]->rect.bottom);
        const UiItem* enable = UiFindItem(frame, kHistoryUiEnableRecording);
        CHECK(enable && enable->kind == UiItemKind::Button && enable->text == L"Enable recording" && !enable->enabled &&
              enable->action.target == uint32_t(HistoryUiCommand::EnableRecording));
        CHECK(TestFind(frame.content, view.storageError) && TestFind(frame.content, L"Storage cap reached; oldest samples were pruned."));
        auto stop = [](const UiFrame& f, uint32_t id) {
            std::vector<UiFocusStop> stops = UiFocusStops(f);
            return std::find_if(stops.begin(), stops.end(), [&](const UiFocusStop& s) { return s.ref.id == id; }) !=
                   stops.end();
        };
        CHECK(!stop(frame, kHistoryUiEnableRecording));
        // Enabled only while it can run; the first content stop, before the legend and chart.
        view.canEnableRecording = true;
        frame = TestHistoryFrame(view);
        CHECK(UiFindItem(frame, kHistoryUiEnableRecording)->enabled);
        CHECK(stop(frame, kHistoryUiEnableRecording));
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        auto content = std::find_if(stops.begin(), stops.end(),
                                    [](const UiFocusStop& s) { return s.surface == UiSurfaceId::Content; });
        CHECK(content != stops.end() && content->ref.id == kHistoryUiEnableRecording);

        // One account's recording off or pause: an info bar while this user records.
        PCWSTR off = L"Recording is off for this account. Its saved history stays; turn on Record history in "
                     L"Settings > Accounts to record new polls.";
        PCWSTR paused = L"Recording is paused for this account: it is hidden from the taskbar, and hidden accounts "
                        L"are not polled.";
        view = TestHistoryView();
        view.selected = 0;
        view.accounts[0].recordingOff = true;
        frame = TestHistoryFrame(view);
        CHECK(TestPanels(frame)[0]->ink == UiInk::Primary && TestFind(frame.content, off));
        view.accounts[0].recordingOff = false;
        view.accounts[0].paused = true;
        CHECK(TestFind(TestHistoryFrame(view).content, paused));
        view.recordingEnabled = false;
        frame = TestHistoryFrame(view);
        CHECK(!TestFind(frame.content, paused) && TestPanels(frame)[0]->ink == UiInk::Caution);
        view.recordingEnabled = true;
        view.accounts[0].removed = true;
        CHECK(!TestFind(TestHistoryFrame(view).content, paused));
        view.selected = -1;
        view.accounts[0].removed = false;
        CHECK(!TestFind(TestHistoryFrame(view).content, paused));

        // A removed account: its bar with Delete history for its history ID, whatever this user's
        // recording; disabled while an export writes.
        PCWSTR removed = L"This account was removed. Its saved history stays until it expires or is deleted.";
        view = TestHistoryView();
        view.accounts.push_back(TestRemoved(0x3F9C1A2B4D5E6F70, 20, 1));
        view.selected = 4;
        frame = TestHistoryFrame(view);
        const UiItem* remove = UiFindItem(frame, kHistoryUiDeleteHistory);
        CHECK(TestPanels(frame)[0]->ink == UiInk::Primary && TestFind(frame.content, removed));
        CHECK(remove && remove->kind == UiItemKind::Button && remove->text == L"Delete history\u2026" &&
              remove->enabled && remove->action.target == uint32_t(HistoryUiCommand::DeleteHistory) &&
              remove->action.key == 0x3F9C1A2B4D5E6F70 && stop(frame, kHistoryUiDeleteHistory));
        view.recordingEnabled = false;
        frame = TestHistoryFrame(view);
        CHECK(TestPanels(frame)[0]->ink == UiInk::Caution && TestPanels(frame)[1]->ink == UiInk::Primary &&
              TestFind(frame.content, removed));
        view.exporting = true;
        CHECK(!UiFindItem(TestHistoryFrame(view), kHistoryUiDeleteHistory)->enabled);
        view.exporting = false;
        view.selected = 0;
        CHECK(!UiFindItem(TestHistoryFrame(view), kHistoryUiDeleteHistory));
    });

    Run("history frame: the chart's card: legend chips, the key line and the chart, by the mockups' "
        "geometry", [] {
        HistoryUiView view = TestWeeklyView();
        UiFrame frame = TestHistoryFrame(view);
        // As tall as the mockup's graph card (368 DIP with a 300 DIP chart), shorter in short windows.
        const UiItem* card = TestPanels(frame)[0];
        CHECK(TestSameRect(card->rect, {12, 0, 816, 368}));
        CHECK_NEAR(TestPanels(TestHistoryFrame(view, {1100, 550}))[0]->rect.bottom, 452 - 212 + 68);
        CHECK_NEAR(TestPanels(TestHistoryFrame(view, {800, 450}))[0]->rect.bottom, 200 + 68 + 30);  // Two chip rows
        // The chips: Custom items by line, 24 DIP tall, 6 DIP apart from 15 DIP inside the card;
        // each 47 DIP wider than its 12.5 DIP name; showing their lines, toggling them.
        UiTextStyle name{UiFamily::Text, 12.5f, 16, DWRITE_FONT_WEIGHT_NORMAL};
        float x = 27;
        for (const HistoryUiLine& line : view.lines) {
            const UiItem* chip = UiFindItem(frame, line.chip);
            float width = std::ceil(UiLayoutSize(UiLayout(TestGraphics(), line.name, name, 100000, false)).width) + 47;
            CHECK(chip && chip->kind == UiItemKind::Custom && chip->text == line.name && chip->selected);
            CHECK(TestSameRect(chip->rect, {x, 11, x + width, 35}));
            CHECK(chip->action.kind == UiActionKind::Command && chip->action.target == uint32_t(HistoryUiCommand::Line) &&
                  chip->action.key == line.historyId && chip->action.value == line.meter);
            CHECK(chip->tooltip.size() == 2 && chip->tooltip[0].text == line.title &&
                  chip->tooltip[0].style == UiRunStyle::Strong && chip->tooltip[1].ink == UiInk::Secondary &&
                  chip->tooltip[1].text == L"\nSolid / filled: current range. Click to hide or show this line.");
            x += width + 6;
        }
        CHECK(UiFindItem(frame, kHistoryUiChip + 13 * 8 + 2)->text == L"CC 2 \u00B7 Fable weekly");
        const UiItem* key = TestFind(frame.content, L"Solid: current");
        CHECK(key && key->font == UiFont::Caption && key->ink == UiInk::Muted && TestSameRect(key->rect, {29, 39, 801, 55}));
        const UiItem* chart = UiFindItem(frame, kHistoryUiChart);
        CHECK(chart && chart->kind == UiItemKind::Chart && TestSameRect(chart->rect, {27, 59, 801, 359}));
        // Focus: the chips, then the chart; a press on a chip hits it, on the chart the chart.
        std::vector<uint32_t> content;
        for (const UiFocusStop& stop : UiFocusStops(frame)) {
            if (stop.surface == UiSurfaceId::Content) content.push_back(stop.ref.id);
        }
        CHECK(content.size() == 6 && content[0] == view.lines[0].chip && content[5] == kHistoryUiChart);
        CHECK(UiHitTest(TestGraphics(), frame.content, {40, 20}).id == view.lines[0].chip);
        CHECK(UiHitTest(TestGraphics(), frame.content, {400, 200}).id == kHistoryUiChart);
        CHECK(UiHitTest(TestGraphics(), frame.content, {20, 20}).id == 0);  // The card itself is no target

        // Comparing: the key names both periods and so do the chips' tooltips; a hidden line's chip
        // shows it off and stays a stop.
        view.compare = true;
        view.lines[1].hidden = true;
        frame = TestHistoryFrame(view);
        CHECK(TestFind(frame.content, L"Solid: current \u00B7 Dashed: previous"));
        CHECK(UiFindItem(frame, view.lines[0].chip)->tooltip[1].text ==
              L"\nSolid / filled: current week. Dashed / hollow: previous week. Click to hide or show this line.");
        CHECK(!UiFindItem(frame, view.lines[1].chip)->selected && UiFocusable(*UiFindItem(frame, view.lines[1].chip)));

        // At the minimum width the chips wrap into rows 6 DIP apart and the card grows with them;
        // a name wider than the row is cut to it.
        frame = TestHistoryFrame(view, {800, 550});
        std::vector<float> rows;
        for (const HistoryUiLine& line : view.lines) {
            const UiItem* chip = UiFindItem(frame, line.chip);
            CHECK(chip->rect.left >= 27 && chip->rect.right <= 12 + 504 - 15 + 0.01f);
            if (std::find(rows.begin(), rows.end(), chip->rect.top) == rows.end()) rows.push_back(chip->rect.top);
        }
        CHECK(rows.size() == 2 && rows[1] == rows[0] + 30);
        CHECK_NEAR(TestPanels(frame)[0]->rect.bottom, 200 + 68 + 30);  // The viewport 396 DIP tall: a 200 DIP chart
        view.lines[0].name =
            L"Research \u2014 shared Max seat for the whole engineering team and its contractors \u00B7 Fable weekly";
        frame = TestHistoryFrame(view, {800, 550});
        CHECK_NEAR(UiFindItem(frame, view.lines[0].chip)->rect.right - UiFindItem(frame, view.lines[0].chip)->rect.left,
                   504 - 30);
    });

    Run("history frame: the chart's card without lines, and the empty states", [] {
        struct Empty {
            std::function<void(HistoryUiView&)> edit;
            PCWSTR message;
            float height;
        };
        // Without a chart: the message centered in a shorter card.
        const Empty empties[] = {
            {[](HistoryUiView& v) { v.loading = true; v.status.clear(); }, L"Loading local history\u2026", 340},
            {[](HistoryUiView& v) { v.accounts.clear(); }, L"No accounts to display.", 340},
            {[](HistoryUiView&) {}, L"No accounts support this view.", 340},
            {[](HistoryUiView& v) { v.selected = 3; }, L"This account has no meters in this view.", 340},
        };
        for (const Empty& empty : empties) {
            HistoryUiView edited = TestHistoryView();
            empty.edit(edited);
            UiFrame frame = TestHistoryFrame(edited);
            const UiItem* card = TestPanels(frame)[0];
            const UiItem* message = TestFind(frame.content, empty.message);
            CHECK(message && message->font == UiFont::Note && message->ink == UiInk::Secondary);
            CHECK_NEAR(card->rect.bottom - card->rect.top, empty.height);
            CHECK_NEAR((message->rect.top + message->rect.bottom) / 2, (card->rect.top + card->rect.bottom) / 2);
            CHECK(!UiFindItem(frame, kHistoryUiChart));
            bool ring = std::any_of(frame.content.begin(), frame.content.end(), [&](const UiItem& item) {
                return item.kind == UiItemKind::Shape && item.rect.bottom <= card->rect.bottom &&
                       item.rect.right <= message->rect.left;
            });
            CHECK(ring == edited.loading);
        }
        // No readings in range, or dollars only: the chart without legend or key (its message is
        // the chart's, history-chart.inc), in a card of the chart and its padding.
        for (bool dollars : {false, true}) {
            HistoryUiView edited = TestHistoryView();
            if (dollars) {
                edited.cards = {TestCard(L"Extra spending (USD) \u2014 details only", 6, L"USD 10.00", L"now", {})};
                edited.cards[0].detailsOnly = true;
            } else {
                edited.noReadings = {{2, L"OAI", L"OpenAI"}};
            }
            UiFrame frame = TestHistoryFrame(edited);
            const UiItem* chart = UiFindItem(frame, kHistoryUiChart);
            CHECK(chart && TestSameRect(chart->rect, {27, 11, 801, 311}));
            CHECK_NEAR(TestPanels(frame)[0]->rect.bottom, 320);
            CHECK(!TestFind(frame.content, L"Solid: current") && !TestFind(frame.content, L"No readings in this range."));
            CHECK(std::none_of(frame.content.begin(), frame.content.end(),
                               [](const UiItem& item) { return item.kind == UiItemKind::Custom; }));
        }
        UiFrame none = TestHistoryFrame([] { HistoryUiView v = TestHistoryView(); v.accounts.clear(); return v; }());
        CHECK(none.nav.size() == 1);
    });

    Run("history frame: summary cards in up to three columns (two comparing), rows as tall as their "
        "tallest card; the No readings card last", [] {
        HistoryUiView view = TestWeeklyView();
        UiFrame frame = TestHistoryFrame(view);
        std::vector<const UiItem*> panels = TestPanels(frame);
        CHECK(panels.size() == 6);
        float width = (804 - 16) / 3.0f;
        for (size_t i = 1; i < 6; i++) {
            float x = 12 + (width + 8) * float((i - 1) % 3), y = 368 + 10 + (122 + 8) * float((i - 1) / 3);
            CHECK(TestSameRect(panels[i]->rect, {x, y, x + width, y + 122}));
        }
        CHECK_NEAR(frame.contentHeight, panels[5]->rect.bottom + 20);
        // A card: its line, title, headline, then caption lines (the chip of its name is no text).
        const UiItem* title = &*std::find_if(frame.content.begin(), frame.content.end(), [](const UiItem& item) {
            return item.kind == UiItemKind::Text && UiRunsText(item.runs) == L"OAI \u00B7 Weekly";
        });
        CHECK(title && title->font == UiFont::Caption && title->trim && title->rect.left == 12 + 13 + 24 &&
              title->rect.top == panels[1]->rect.top + 11);
        const UiItem* line = &*std::find_if(frame.content.begin(), frame.content.end(), [&](const UiItem& item) {
            return item.kind == UiItemKind::Shape && item.rect.top == panels[1]->rect.top + 15;
        });
        CHECK(line->rect.left == 25 && line->parts.size() == 1 && line->parts[0].stroke == 3);
        CHECK(UiColorKey(line->parts[0].color) == 0xFF33C3A6);
        const UiItem* value = TestFind(frame.content, L"62%");
        CHECK(value->font == UiFont::Subtitle && value->rect.top == panels[1]->rect.top + 33);
        const UiItem* when = TestFind(frame.content, L"now");
        CHECK(when->font == UiFont::Caption && when->ink == UiInk::Muted && when->rect.left == value->rect.right + 5);
        CHECK(when->rect.top > value->rect.top && when->rect.bottom < value->rect.bottom + 4);
        const UiItem* peak = TestFind(frame.content, L"Peak 77% \u00B7 Mon 26 Oct 11:31");
        CHECK(peak->ink == UiInk::Muted && peak->rect.top == panels[1]->rect.top + 63);
        CHECK(TestFind(frame.content, L"Est. \u226590%: 0 h")->rect.top == panels[1]->rect.top + 63 + 32);

        // Line colors per theme: the scoped tint kept; high contrast alternates text and link.
        auto lineOf = [](const UiFrame& f, size_t card) {
            std::vector<const UiItem*> shapes;
            for (const UiItem& item : f.content) {
                if (item.kind == UiItemKind::Shape) shapes.push_back(&item);
            }
            return shapes[card]->parts[0];
        };
        CHECK(UiColorKey(lineOf(frame, 3).color) == UiColorKey(UiOver(UiRgb(0xFFFFFF, 0.25f), UiRgb(0xA184FF))));
        CHECK(lineOf(frame, 3).stroke == 3);  // Whatever the series' width
        view.theme = UiThemeKind::Light;
        frame = TestHistoryFrame(view);
        CHECK(UiColorKey(lineOf(frame, 0).color) == 0xFF0E8F78);
        CHECK(UiColorKey(lineOf(frame, 3).color) == UiColorKey(UiOver(UiRgb(0, 0.25f), UiRgb(0x6F4FE6))));
        view.theme = UiThemeKind::HighContrast;
        frame = TestHistoryFrame(view);
        CHECK(lineOf(frame, 0).ink == UiInk::Primary && lineOf(frame, 1).ink == UiInk::Link &&
              lineOf(frame, 2).ink == UiInk::Primary);

        // Two columns at the minimum width and while comparing; a row takes its tallest card.
        view = TestWeeklyView();
        frame = TestHistoryFrame(view, {800, 550});
        panels = TestPanels(frame);
        CHECK_NEAR(panels[1]->rect.right - panels[1]->rect.left, (504 - 8) / 2.0);
        CHECK_NEAR(panels[3]->rect.left, 12);
        view.compare = true;
        view.cards.resize(2);
        view.cards[0].lines.insert(view.cards[0].lines.end(), {L"Previous: Peak \u2014"});
        frame = TestHistoryFrame(view);
        panels = TestPanels(frame);
        CHECK_NEAR(panels[1]->rect.right - panels[1]->rect.left, (804 - 8) / 2.0);
        CHECK_NEAR(panels[1]->rect.bottom - panels[1]->rect.top, 122 + 16);
        CHECK_NEAR(panels[2]->rect.bottom - panels[2]->rect.top, 122 + 16);

        // A status after its account's first title; details-only dollars without a line; the No
        // readings card last: its logo, label and provider with status.
        view = TestHistoryView();
        view.cards = {TestCard(L"CC 1 \u00B7 Weekly", 1, L"47%", L"Fri 30 Oct 20:32", {L"Peak 63%"}),
                      TestCard(L"CC 1 \u00B7 Extra spending (USD) \u2014 details only", 6, L"USD 11.00", L"now",
                               {L"Peak USD 11.00 \u00B7 Fri 30 Oct 20:32"})};
        view.cards[0].status = L"recording off";
        view.cards[1].detailsOnly = true;
        view.noReadings = {{1, L"CC 2", L"Anthropic \u00B7 recording off"}, {3, L"Go", L"Google Antigravity \u00B7 paused"}};
        frame = TestHistoryFrame(view);
        panels = TestPanels(frame);
        CHECK(panels.size() == 4);
        const UiItem* status = TestFind(frame.content, L"CC 1 \u00B7 Weekly \u00B7 recording off");
        CHECK(status && status->runs.size() == 2 && status->runs[1].ink == UiInk::Muted);
        CHECK(TestFind(frame.content, L"CC 1 \u00B7 Extra spending (USD) \u2014 details only")->rect.left ==
              panels[2]->rect.left + 13);
        const UiItem* heading = TestFind(frame.content, L"No readings in range");
        CHECK(heading && heading->rect.top == panels[3]->rect.top + 11 && panels[3]->rect.top == panels[1]->rect.top);
        CHECK_NEAR(panels[3]->rect.bottom - panels[3]->rect.top, 11 + 16 + 4 + 48 + 11);
        const UiItem* go = TestFind(frame.content, L"Go");
        const UiItem* goDetail = TestFind(frame.content, L"Google Antigravity \u00B7 paused");
        CHECK(go->font == UiFont::Note && go->rect.left == panels[3]->rect.left + 13 + 24 && go->rect.top == heading->rect.top + 20 + 24 + 2.5f);
        CHECK(goDetail->ink == UiInk::Muted && goDetail->rect.left == go->rect.right + 8);
        CHECK(std::any_of(frame.content.begin(), frame.content.end(), [&](const UiItem& item) {
            return item.kind == UiItemKind::Shape && item.parts[0].path == kGeminiLogoPath && item.rect.top == go->rect.top - 2.5f + 4 &&
                   !item.parts[0].fill;
        }));
        // Its logos take the brand colors too.
        view.logoBrands = {false, true};
        frame = TestHistoryFrame(view);
        auto logoOf = [&](PCWSTR path) {
            return std::find_if(frame.content.begin(), frame.content.end(), [&](const UiItem& item) {
                return item.kind == UiItemKind::Shape && item.parts[0].path == path;
            });
        };
        CHECK(logoOf(kGeminiLogoPath) != frame.content.end() && logoOf(kGeminiLogoPath)->parts[0].fill &&
              UiColorKey(logoOf(kGeminiLogoPath)->parts[0].fill->from) == 0xFF9168C0);
        CHECK(logoOf(kClaudeLogoPath) != frame.content.end() && !logoOf(kClaudeLogoPath)->parts[0].fill);
    });

    Run("menus: one-line rows with glyphs; a separator adds 5 DIP above its row and is no row", [] {
        UiGraphics& g = TestGraphics();
        std::vector<UiOption> options = {{L"Settings", 0, {}, {}, L"\uE713"},
                                         {L"Open history folder", 1, {}, {}, L"\uE838"},
                                         {L"About these charts", 2, {}, {}, L"\uE946"},
                                         {L"Clear all history\u2026", 3, {}, {}, L"\uE74D", true}};
        std::vector<UiItem> rows;
        D2D1_SIZE_F size = UiLayoutOptions(g, options, {UiActionKind::Command, -1, {}, 7}, 32, rows);
        float widest = 0;
        for (const UiOption& option : options) widest = std::max(widest, TestTextWidth(option.text.c_str(), UiFont::Body));
        CHECK_NEAR(size.width, widest + 5 + 12 + 16 + 12 + 28 + 5);
        CHECK_NEAR(size.height, 4 + 36 * 4 + 5);
        CHECK(rows.size() == 4);
        float tops[] = {4, 40, 76, 117};
        for (size_t i = 0; i < 4; i++) {
            CHECK(TestSameRect(rows[i].rect, {5, tops[i], size.width - 5, tops[i] + 32}));
            CHECK(rows[i].icon == options[i].icon && rows[i].runs.empty() && rows[i].parts.empty());
            CHECK(rows[i].separator == (i == 3) && !rows[i].selected && rows[i].action.value == int(i));
        }
        CHECK(std::none_of(rows.begin(), rows.end(), [](const UiItem& row) { return UiContains(row.rect, {50, 114}); }));
        // No separator above the first row or among a dropdown's rows.
        options[0].separator = true;
        for (UiOption& option : options) option.icon.clear();
        size = UiLayoutOptions(g, options, {UiActionKind::Command, 2, {}, 7}, 32, rows);
        CHECK_NEAR(size.height, 4 + 36 * 4);
        CHECK(rows[3].rect.top == 112 && !rows[3].separator && !rows[0].separator && rows[2].selected);
        CHECK_NEAR(size.width, widest + 5 + 12 + 12 + 5);
        // The history window's More menu: these four rows, as above.
        UiFrame frame = TestHistoryFrame(TestHistoryView());
        const UiItem* more = UiFindItem(frame, kHistoryUiMore);
        size = UiLayoutOptions(g, more->options, more->action, 32, rows);
        CHECK(rows.size() == 4 && rows[3].separator && size.height == 4 + 36 * 4 + 5);
        CHECK_NEAR(size.width, widest + 78);
        for (size_t i = 0; i < 4; i++) {
            CHECK(rows[i].text == options[i].text && rows[i].icon == more->options[i].icon && !rows[i].icon.empty());
        }
    });

    Run("history frame: an export's write: its progress in the status slot (the title row, else under the title "
        "after the account's line), every control but the legend's chips disabled", [] {
        float widest = TestTextWidth(L"Exporting 2,000,000 rows\u2026", UiFont::Caption);
        HistoryUiView view = TestWeeklyView();
        view.exporting = true;
        UiFrame frame = TestHistoryFrame(view);
        const UiItem* progress = UiFindItem(frame, kHistoryUiProgress);
        const UiItem* pill = &*std::find_if(frame.main.begin(), frame.main.end(),
                                            [](const UiItem& item) { return item.kind == UiItemKind::Pill; });
        CHECK(progress && progress->kind == UiItemKind::Custom && progress->action.kind == UiActionKind::None);
        CHECK(progress->font == UiFont::Caption && progress->align == DWRITE_TEXT_ALIGNMENT_TRAILING);
        CHECK(TestSameRect(progress->rect, {pill->rect.left - 12 - 22 - widest, 14, pill->rect.left - 12, 34}));
        CHECK(!TestFind(frame.main, L"Updated Sat 31 Oct 14:32"));
        CHECK_NEAR(TestFind(frame.main, L"All accounts")->rect.right, progress->rect.left - 12);
        for (uint32_t id : {uint32_t(kHistoryUiNavAll), kHistoryUiNavAccount + 10, uint32_t(kHistoryUiRefresh),
                            uint32_t(kHistoryUiExport), uint32_t(kHistoryUiMore), uint32_t(kHistoryUiViews),
                            uint32_t(kHistoryUiRanges), uint32_t(kHistoryUiCompare)}) {
            CHECK(!UiFindItem(frame, id)->enabled);
        }
        for (const HistoryUiLine& line : view.lines) CHECK(UiFindItem(frame, line.chip)->enabled);
        CHECK(UiFindItem(frame, kHistoryUiChart)->enabled);
        // The focus stops left: the chips and the chart.
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == view.lines.size() + 1 && stops.back().ref.id == kHistoryUiChart);
        // Recording off: the banner's button too.
        view.recordingEnabled = false;
        view.canEnableRecording = true;
        CHECK(!UiFindItem(TestHistoryFrame(view), kHistoryUiEnableRecording)->enabled);

        // The minimum window: under the title, starting the line (All accounts), or 12 DIP after the
        // account's line, in note type.
        view = TestHistoryView();
        view.exporting = true;
        frame = TestHistoryFrame(view, {800, 550});
        progress = UiFindItem(frame, kHistoryUiProgress);
        float note = TestTextWidth(L"Exporting 2,000,000 rows\u2026", UiFont::Note);
        CHECK(progress && progress->font == UiFont::Note && progress->align == DWRITE_TEXT_ALIGNMENT_LEADING);
        CHECK(TestSameRect(progress->rect, {272, 42, 272 + 22 + note, 61}));
        CHECK(UiFindItem(frame, kHistoryUiViews)->rect.top == 70);
        view.selected = 2;
        view.accounts[2].label = L"Claude Max personal";
        frame = TestHistoryFrame(view, {800, 550});
        progress = UiFindItem(frame, kHistoryUiProgress);
        const UiItem* line = TestFind(frame.main, L"Anthropic \u00B7 Recording");
        float lineWidth = TestTextWidth(L"Anthropic \u00B7 Recording", UiFont::Note);
        CHECK(line && progress && progress->rect.left == 272 + lineWidth + 12 && line->rect.right == progress->rect.left - 12);
    });

    Run("buttons: an icon-only button centers its glyph", [] {
        UiGraphics& g = TestGraphics();
        winrt::com_ptr<IWICImagingFactory> wic;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                              __uuidof(IWICImagingFactory), wic.put_void()));
        for (UINT dpi : {96u, 144u}) {
            float scale = float(dpi) / 96;
            winrt::com_ptr<IWICBitmap> bitmap;
            winrt::check_hresult(wic->CreateBitmap(UINT(80 * scale), UINT(40 * scale), GUID_WICPixelFormat32bppPBGRA,
                                                   WICBitmapCacheOnLoad, bitmap.put()));
            D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), float(dpi), float(dpi));
            winrt::com_ptr<ID2D1RenderTarget> target;
            winrt::check_hresult(g.d2d->CreateWicBitmapRenderTarget(bitmap.get(), &properties, target.put()));
            UiThemeInput theme;
            theme.kind = UiThemeKind::Light;
            UiPalette palette = UiPaletteFor(theme);
            UiTargetCache cache;
            UiItem button;
            button.id = 1;
            button.kind = UiItemKind::Button;
            button.rect = {24, 5, 56, 35};
            button.action = HistoryUiAction(HistoryUiCommand::Menu);
            button.button = UiButtonStyle::Subtle;
            button.icon = L"\uE712";  // Three dots: symmetric
            button.iconSize = 15;
            target->BeginDraw();
            target->Clear(D2D1::ColorF(1, 1, 1, 1));
            UiPaintItems(target.get(), g, cache, palette, {button}, {}, {0, 0}, scale, -1e9f, 1e9f);
            winrt::check_hresult(target->EndDraw());
            cache.Clear();
            target = nullptr;
            UINT width = 0, height = 0;
            winrt::check_hresult(bitmap->GetSize(&width, &height));
            std::vector<BYTE> pixels(size_t(width) * height * 4);
            WICRect all{0, 0, INT(width), INT(height)};
            winrt::check_hresult(bitmap->CopyPixels(&all, width * 4, UINT(pixels.size()), pixels.data()));
            int left = INT_MAX, right = -1;
            for (UINT y = 0; y < height; y++) {
                for (UINT x = 0; x < width; x++) {
                    if (pixels[(size_t(y) * width + x) * 4 + 1] < 160) {
                        left = std::min(left, int(x));
                        right = std::max(right, int(x));
                    }
                }
            }
            CHECK(right >= left);
            CHECK(std::abs((left + right + 1) / 2.0 - 40 * scale) <= 1);
        }
    });
}
