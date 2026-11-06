// The usage history window in every language and the pseudo-locale at its default and minimum
// client size: each state as its controller composes it in that language over the mockups'
// synthetic journals (history-chart.inc), audited (layout-audit.inc) for text that doesn't fit,
// text cut without its full text reachable and items over each other or out of their card; the
// header's items inside its column, legend chips' names, the chart's time labels apart and inside
// its card (at 96 and 144 DPI) and its message inside the plot. Then the measured layout: an
// export's progress reserve over plural forms, the header's pill under a title that needs the
// room, the summary cards' wrapping, the ticks' measured spacing and cut navigation labels'
// tooltips. Also the review renders' cases (history-render.inc).

// A case: a controller over a synthetic journal composed in a language, then view states the
// test ops don't reach (an export's write, the waiting pill).
struct HistoryLayoutCase {
    std::string name;
    std::function<std::shared_ptr<TestChartWindow>()> make;
    std::function<void(HistoryUiView&)> adjust{};
};

// The history window's smallest client area: its minimum outer size (history/window.h, 800 x 590)
// less the standard frame (8 DIP borders, a 23 DIP caption) at 96 DPI; its default (1100 x 830)
// the same way.
static constexpr D2D1_SIZE_F kHistoryLayoutMinClient{800 - 16, 590 - 39};
static constexpr D2D1_SIZE_F kHistoryLayoutClient{1100 - 16, 830 - 39};

static std::vector<HistoryLayoutCase> HistoryLayoutCases() {
    using Setup = std::function<void(HistoryController&)>;
    auto chart = [](std::vector<HistorySample> journal, Setup setup = {},
                    std::vector<AccountConfig> accounts = TestMockupAccounts()) {
        return [=] { return TestChartOf(journal, setup, accounts); };
    };
    auto select = [](uint64_t id, HistoryView view, bool compare = false) -> Setup {
        return [=](HistoryController& c) {
            c.Command(HistoryUiCommand::Select, 0, id);
            c.Command(HistoryUiCommand::View, int(view));
            if (compare) c.Command(HistoryUiCommand::Compare, 1);
        };
    };
    // A window whose test ops change before it loads again.
    auto reloaded = [](std::function<void(TestChartWindow&)> change, std::vector<HistorySample> journal = TestMockupWeek(),
                       Setup setup = {}) {
        return [=] {
            auto w = TestChartOf(journal, setup);
            change(*w);
            w->controller.Load();
            return w;
        };
    };
    std::vector<HistoryLayoutCase> cases;
    cases.push_back({"default", chart(TestMockupWeek())});
    cases.push_back({"short-term", chart(TestMockupShort(), [](HistoryController& c) {
                         c.Command(HistoryUiCommand::View, int(HistoryView::ShortTerm));
                         c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day));
                     })});
    cases.push_back({"extra-usage", chart(TestMockupWeek(), [](HistoryController& c) {
                         c.Command(HistoryUiCommand::View, int(HistoryView::ExtraUsage));
                     })});
    cases.push_back({"all-meters", chart(TestMockupWeek(), [](HistoryController& c) {
                         c.Command(HistoryUiCommand::View, int(HistoryView::AllMeters));
                     })});
    // One account: OAI's credits with its recording off; CC 1 paused (hidden); CC 2 in All
    // meters; CC 2 removed with Delete history.
    cases.push_back({"account-oai", reloaded([](TestChartWindow& w) { w.ops.settings.settings.accounts[0].recordHistory = false; },
                                             TestMockupWeek(), select(0x0A, HistoryView::ExtraUsage))});
    cases.push_back({"account-paused", reloaded([](TestChartWindow& w) { w.ops.settings.settings.accounts[1].hidden = true; },
                                                TestMockupWeek(), select(0xC1, HistoryView::ExtraUsage))});
    cases.push_back({"account-all-meters", chart(TestMockupWeek(), select(0xC2, HistoryView::AllMeters))});
    {
        std::vector<AccountConfig> kept = TestMockupAccounts();
        kept.erase(kept.begin() + 2);
        cases.push_back({"account-removed", chart(TestMockupWeek(), select(0xC2, HistoryView::AllMeters), kept)});
    }
    cases.push_back({"compare", chart(TestMockupCompare(), select(0xC2, HistoryView::Weekly, true))});
    cases.push_back({"compare-all", chart(TestMockupWeek(), [](HistoryController& c) { c.Command(HistoryUiCommand::Compare, 1); })});
    cases.push_back({"band", chart(TestMockupBand())});
    cases.push_back({"remaining", reloaded([](TestChartWindow& w) { w.ops.settings.settings.barMode = BarMode::Remaining; },
                                           TestMockupBand())});
    // States: recording off (its bar with Enable recording, and Enable recording's failure),
    // storage failing and capped, waiting, an export's progress.
    auto off = [](TestChartWindow& w) { w.ops.settings.settings.historyEnabled = false; };
    cases.push_back({"recording-off", reloaded(off)});
    cases.push_back({"consent-failed", [off] {
                         auto w = TestChartOf(TestMockupWeek());
                         off(*w);
                         w->controller.Load();
                         w->controller.notice = {HistoryNoticeCode::ConsentFailed};
                         return w;
                     }});
    cases.push_back({"storage", reloaded([](TestChartWindow& w) {
                         w.ops.status = HistoryJournalError::InUse;
                         w.ops.capped = true;
                     })});
    cases.push_back({"waiting", chart(TestMockupWeek()), [](HistoryUiView& v) { v.pill = HistoryUiPill::Waiting; }});
    cases.push_back({"exporting", chart(TestMockupWeek()), [](HistoryUiView& v) { v.exporting = true; }});
    cases.push_back({"exporting-account", chart(TestMockupWeek(), select(0xC2, HistoryView::Weekly)),
                     [](HistoryUiView& v) { v.exporting = true; }});
    // Each operation's notice; an export's names a long destination.
    const std::pair<const char*, HistoryNotice> notices[] = {
        {"notice-nothing-recorded", {HistoryNoticeCode::NothingRecorded}},
        {"notice-untracked", {HistoryNoticeCode::Untracked}},
        {"notice-save-dialog", {.code = HistoryNoticeCode::SaveDialogFailed, .error = 0x3002}},
        {"notice-not-started", {HistoryNoticeCode::ExportNotStarted}},
        {"notice-temporary", {.code = HistoryNoticeCode::TemporaryFileFailed, .error = 5}},
        {"notice-export-failed", {.code = HistoryNoticeCode::ExportFailed, .error = 112}},
        {"notice-exported", {.code = HistoryNoticeCode::Exported, .rows = 1234567, .all = true, .compare = true,
                             .text = L"C:\\Users\\clero\\Documents\\Exports\\usage-history-2026-10-31.csv"}},
        {"notice-exported-one", {.code = HistoryNoticeCode::Exported, .rows = 1, .text = L"C:\\Exports\\usage.csv"}},
        {"notice-clear-failed", {.code = HistoryNoticeCode::ClearFailed, .reason = HistoryJournalError::InUse}},
        {"notice-cleared", {HistoryNoticeCode::Cleared}},
        {"notice-delete-failed", {.code = HistoryNoticeCode::DeleteFailed, .reason = HistoryJournalError::CannotWrite}},
        {"notice-deleted", {.code = HistoryNoticeCode::Deleted, .text = L"account_3f9c1a2b4d5e6f70"}},
    };
    for (const auto& [name, notice] : notices) {
        cases.push_back({name, [notice] {
                             auto w = TestChartOf(TestMockupWeek());
                             w->controller.notice = notice;
                             return w;
                         }});
    }
    // Empty states: the first load, no accounts, a view no account supports (one account, All
    // accounts), no readings in range with recording off, dollars only, the No readings card
    // beside plotted accounts.
    cases.push_back({"loading", [] {
                         auto w = std::make_shared<TestChartWindow>();
                         w->ops.settings.settings.accounts = TestMockupAccounts();
                         w->controller.Open();
                         return w;
                     }});
    cases.push_back({"no-accounts", chart({}, {}, {})});
    cases.push_back({"unsupported-account", chart(TestMockupWeek(), select(0x60, HistoryView::ExtraUsage))});
    cases.push_back({"unsupported-all",
                     chart(TestMockupWeek(), [](HistoryController& c) { c.Command(HistoryUiCommand::View, int(HistoryView::ExtraUsage)); },
                           {TestHistoryAccount(L"antigravity", L"Go", 0x60)})});
    cases.push_back({"no-readings", reloaded(off, {}, [](HistoryController& c) {
                         c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day));
                     })});
    cases.push_back({"dollars", chart(TestMockupWeek(), select(0xC2, HistoryView::ExtraUsage))});
    {
        std::vector<HistorySample> some = TestMockupWeek();
        std::erase_if(some, [](const HistorySample& s) { return s.accountId == 0xC1 || s.accountId == 0x60; });
        cases.push_back({"partial", reloaded([](TestChartWindow& w) {
                             w.ops.settings.settings.accounts[1].recordHistory = false;
                             w.ops.settings.settings.accounts[3].hidden = true;
                         }, some)});
    }
    // Many accounts with long labels, two of them removed: the navigation pane and the legend.
    cases.push_back({"many", [] {
                         std::vector<AccountConfig> accounts = TestMockupAccounts();
                         for (auto [provider, label, id] :
                              {std::tuple{L"openai", L"OAI 2", 0x0B}, {L"anthropic", L"Research \u2014 shared Max seat", 0xC3},
                               {L"openai", L"Codex CI", 0x0C}, {L"anthropic", L"Claude personal", 0xC4}}) {
                             accounts.push_back(TestHistoryAccount(provider, label, uint64_t(id)));
                         }
                         std::vector<HistorySample> journal = TestMockupWeek();
                         for (uint64_t id : {0x0BULL, 0xC3ULL, 0x0CULL, 0xC4ULL, 0xD1ULL, 0xD2ULL}) {
                             journal.push_back(TestReading(id, id == 0x0B || id == 0x0C ? 2 : 1, kWeekFrom + 2 * kHourMs,
                                                           {{1, 40}}));
                         }
                         return TestChartOf(journal, {}, accounts);
                     }});
    // Each range's ticks over two weekly lines kept 900 days.
    for (auto [range, name] : {std::pair{HistoryUiRange::Day, "ticks-24h"}, std::pair{HistoryUiRange::Week, "ticks-7d"},
                               std::pair{HistoryUiRange::Month, "ticks-30d"}, std::pair{HistoryUiRange::Quarter, "ticks-90d"},
                               std::pair{HistoryUiRange::All, "ticks-all"}}) {
        int value = int(range);
        cases.push_back({name, [value] {
                             auto w = TestChartOf(TestMockupLong(900),
                                                  [value](HistoryController& c) { c.Command(HistoryUiCommand::Range, value); });
                             w->ops.settings.settings.historyRetentionDays = 900;
                             w->controller.Load();
                             return w;
                         }});
    }
    return cases;
}

static constexpr std::string_view kHistoryLayoutTags[] = {"en", "de", "es", "fr", "ja", "pt-BR", "ru", "zh-Hans",
                                                          "qps-ploc"};

// The case's controller composed in `tag`'s language ("en": English) and its view with the case's
// states.
static std::shared_ptr<TestChartWindow> HistoryLayoutWindow(const HistoryLayoutCase& entry, std::string_view tag) {
    std::shared_ptr<TestChartWindow> w = entry.make();
    w->ops.locale = I18nLocale(*I18nLanguageByTag(std::wstring(tag.begin(), tag.end())));
    w->controller.Compose();
    if (entry.adjust) entry.adjust(w->controller.view);
    return w;
}

// The frame as the client builds it in the view's language, the chart laid out at `scale`.
static UiFrame HistoryLayoutFrame(UiGraphics& g, TestChartWindow& w, D2D1_SIZE_F client, float scale = 1) {
    g.locale = w.controller.view.locale.LocaleName();
    UiFrame frame;
    HistoryUiBuildFrame(g, w.controller.view, client, frame);
    if (const UiItem* chart = UiFindItem(frame, kHistoryUiChart)) w.controller.LayChart(g, chart->rect, scale);
    return frame;
}

// The window's own rules beyond the audit: header items inside the header's column (12 DIP after
// the pane, 24 before the edge); a legend chip's name whole, or whole in its tooltip; time labels
// apart (4 DIP at least between their ink) and inside the chart's card.
static std::vector<std::string> HistoryLayoutRules(UiGraphics& g, const UiFrame& frame, const HistoryChart& chart,
                                                   D2D1_SIZE_F client) {
    std::vector<std::string> findings;
    float left = kHistoryUiNavWidth + 12, right = client.width - 24;
    for (const UiItem& item : frame.main) {
        if (item.rect.left < left - 0.5f || item.rect.right > right + 0.5f) {
            findings.push_back("main #" + std::to_string(item.id) + " out of the header (" + std::to_string(int(item.rect.left)) +
                               ".." + std::to_string(int(item.rect.right)) + " in " + std::to_string(int(left)) + ".." +
                               std::to_string(int(right)) + ")");
        }
    }
    const UiItem* card = nullptr;
    for (const UiItem& item : frame.content) {
        if (item.kind == UiItemKind::Card && !card) card = &item;
        if (item.kind != UiItemKind::Custom || item.id < kHistoryUiChip) continue;
        UiTextStyle name{UiFamily::Text, 12.5f, 16, DWRITE_FONT_WEIGHT_NORMAL};
        float need = UiLayoutSize(UiLayout(g, item.text, name, 100000, false)).width;
        if (need > item.rect.right - item.rect.left - 47 + 0.5f &&
            UiRunsText(item.tooltip).find(item.text) == std::wstring::npos) {
            findings.push_back("chip #" + std::to_string(item.id) + " cut \"" + I18nLayoutNarrow(item.text) + "\"");
        }
    }
    if (const UiItem* item = UiFindItem(frame, kHistoryUiChart); item && !chart.ticks.empty()) {
        float previous = -1e9f;
        for (const HistoryChartTick& tick : chart.ticks) {
            float at = chart.plot.left +
                       float(double(tick.time - chart.from) / double(chart.to - chart.from)) * (chart.plot.right - chart.plot.left);
            float width = UiLayoutSize(UiLayout(g, tick.label, kHistoryChartAxisStyle, 10000, false)).width;
            float x0 = at - width / 2, x1 = at + width / 2;
            if (x0 < previous + 4) {
                findings.push_back("tick \"" + I18nLayoutNarrow(tick.label) + "\" " + std::to_string(int(x0 - previous)) +
                                   " DIP from the previous label");
            }
            if (card && (x0 < card->rect.left + 1 || x1 > card->rect.right - 1)) {
                findings.push_back("tick \"" + I18nLayoutNarrow(tick.label) + "\" out of the chart's card");
            }
            previous = x1;
        }
    }
    // The chart's message wraps in the plot (chart-paint.h): no word broken, inside its height.
    if (UiFindItem(frame, kHistoryUiChart) && !chart.message.empty()) {
        float width = chart.plot.right - chart.plot.left;
        float height = UiLayoutSize(UiLayout(g, chart.message, UiTextStyleFor(UiFont::Note), width)).height;
        if (I18nLayoutWidestWord(g, chart.message, UiFont::Note, width) > width + 0.5f ||
            height > chart.plot.bottom - chart.plot.top) {
            findings.push_back("chart message \"" + I18nLayoutNarrow(chart.message) + "\" doesn't fit the plot");
        }
    }
    return findings;
}

static void RunHistoryLayoutTests() {
    Run("history window minimum: 800 x 590 DIP outer, a 784 x 551 DIP client; the header's column 488 DIP wide", [] {
        auto w = TestChartOf(TestMockupWeek());
        UiFrame frame = HistoryLayoutFrame(TestGraphics(), *w, kHistoryLayoutMinClient);
        // 784 - 260 - 12 - 24.
        CHECK_NEAR(UiFindItem(frame, kHistoryUiMore)->rect.right, 784 - 24);
        CHECK_NEAR(UiFindItem(frame, kHistoryUiViews)->rect.left, 260 + 12);
    });

    Run("export progress: as wide as the widest line any plural form shows up to 2,000,000 rows (Russian: "
        "1,999,991 строка and 1,999,992 строки are wider than 2,000,000 строк)", [] {
        UiGraphics& g = TestGraphics();
        HistoryUiView view = TestHistoryView();
        view.locale = TestLocale(L"ru");
        view.exporting = true;
        g.locale = L"ru-RU";
        UiFrame frame;
        HistoryUiBuildFrame(g, view, kHistoryLayoutClient, frame);
        auto caption = [&](PCWSTR text) {
            return std::ceil(UiLayoutSize(UiLayout(g, text, UiTextStyleFor(UiFont::Caption), 10000, false)).width);
        };
        float one = caption(L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 1,999,991 \u0441\u0442\u0440\u043E\u043A\u0430\u2026");
        float few = caption(L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 1,999,992 \u0441\u0442\u0440\u043E\u043A\u0438\u2026");
        float many = caption(L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 2,000,000 \u0441\u0442\u0440\u043E\u043A\u2026");
        const UiItem* progress = UiFindItem(frame, kHistoryUiProgress);
        CHECK(progress && progress->font == UiFont::Caption);
        CHECK_NEAR(progress->rect.right - progress->rect.left, 22 + std::max({one, few, many}));
        CHECK(std::max(one, few) > many);
        g.locale = L"en-US";
    });

    Run("history header: a title that doesn't fit beside the pill keeps the row to the buttons; the pill follows "
        "the status under it (French at the minimum size), or starts a line of its own 22 DIP lower when it "
        "doesn't fit after it", [] {
        UiGraphics& g = TestGraphics();
        auto w = TestChartOf(TestMockupWeek());
        UiFrame english = HistoryLayoutFrame(g, *w, kHistoryLayoutMinClient);
        const UiItem* pill = TestFind(english.main, L"Recording \u00B7 90 days");
        CHECK(pill && pill->rect.top == 14 && TestFind(english.main, L"All accounts")->rect.right < pill->rect.left);
        w->ops.locale = TestLocale(L"fr");
        w->controller.Compose();
        UiFrame french = HistoryLayoutFrame(g, *w, kHistoryLayoutMinClient);
        const UiItem* title = TestFind(french.main, L"Tous les comptes");
        const UiItem* status = TestFind(french.main, L"Mis \u00E0 jour sam. 31 oct. 14:32");
        pill = TestFind(french.main, L"Enregistrement \u00B7 90 jours");
        float statusWidth = std::ceil(UiLayoutSize(UiLayout(g, status->runs[0].text, UiTextStyleFor(UiFont::Note), 10000,
                                                            false)).width);
        CHECK(title && title->rect.right == 784 - 24 - 104 - 12 &&
              UiLayoutSize(UiLayout(g, title->runs[0].text, UiTextStyleFor(UiFont::Title), 10000, false)).width <=
                  title->rect.right - title->rect.left);
        CHECK(status && status->font == UiFont::Note && status->rect.top == 42 && status->rect.right == pill->rect.left - 12);
        CHECK(TestSameRect(pill->rect, {272 + statusWidth + 12, 41.5f, pill->rect.right, 61.5f}));
        CHECK(UiFindItem(french, kHistoryUiViews)->rect.top == 70);
        g.locale = L"en-US";
        // A line too long for the pill after it (a long account title and status): the pill on the
        // next line, the account's line to the column's end, the command bar 22 DIP lower.
        HistoryUiView view = TestHistoryView();
        view.selected = 0;
        view.accounts[0].label = L"Research \u2014 shared Max seat";
        view.status = L"Updated Saturday 31 October 2026 at 14:32 local time";
        UiFrame frame = TestHistoryFrame(view, kHistoryLayoutMinClient);
        const UiItem* line = TestFind(frame.main, L"OpenAI \u00B7 Recording \u00B7 Updated Saturday 31 October 2026 at "
                                                   L"14:32 local time");
        pill = TestFind(frame.main, L"Recording \u00B7 90 days");
        CHECK(line && TestSameRect(line->rect, {272, 42, 784 - 24, 61}));
        CHECK(pill && TestSameRect(pill->rect, {272, 63.5f, pill->rect.right, 83.5f}));
        CHECK(UiFindItem(frame, kHistoryUiViews)->rect.top == 92);
    });

    Run("summary cards: text longer than its line wraps and the card grows; a headline's time that doesn't fit "
        "beside its value takes a line under it; a No readings detail goes under its label", [] {
        HistoryUiView view = TestHistoryView();
        view.cards = {TestCard(L"Credits remaining", 7, L"1240.00 credits", L"Thu 29 Oct 17:30",
                               {L"Peak 2500.00 credits \u00B7 Sat 24 Oct 14:32", L"Coverage 123 h (73.2%)"}),
                      TestCard(L"CC 1 \u00B7 Extra spending (USD) \u2014 details only", 6, L"", L"",
                               {L"Peak USD 11.00 \u00B7 Fri 30 Oct 20:32"})};
        view.cards[1].detailsOnly = true;
        view.noReadings = {{3, L"Go", L"Google Antigravity \u00B7 recording off"}};
        // Two 240 DIP columns at the minimum size: 214 DIP for text.
        UiFrame frame = TestHistoryFrame(view, kHistoryLayoutMinClient);
        std::vector<const UiItem*> panels = TestPanels(frame);
        CHECK(panels.size() == 4);
        const D2D1_RECT_F &credits = panels[1]->rect, &spending = panels[2]->rect, &none = panels[3]->rect;
        CHECK_NEAR(credits.right - credits.left, 240);
        // 1240.00 credits: its time under it, then the two lines; 11 + 16 + 36 + 16 + 2 x 16 + 11.
        const UiItem* when = TestFind(frame.content, L"Thu 29 Oct 17:30");
        CHECK(when && TestSameRect(when->rect, {credits.left + 13, credits.top + 63, credits.right - 13, credits.top + 79}));
        CHECK_NEAR(TestFind(frame.content, L"Coverage 123 h (73.2%)")->rect.top, credits.top + 95);
        CHECK_NEAR(credits.bottom - credits.top, 122);
        // The details-only title on two lines, its line under them; the row as tall as its tallest.
        const UiItem* title = TestFind(frame.content, L"CC 1 \u00B7 Extra spending (USD) \u2014 details only");
        CHECK(title && !title->trim && TestSameRect(title->rect, {spending.left + 13, spending.top + 11,
                                                                  spending.right - 13, spending.top + 43}));
        CHECK_NEAR(TestFind(frame.content, L"Peak USD 11.00 \u00B7 Fri 30 Oct 20:32")->rect.top, spending.top + 47);
        CHECK_NEAR(spending.bottom, credits.bottom);
        // No readings: the detail under Go's label; 11 + 16 + 4 + 24 + 16 + 11.
        const UiItem* detail = TestFind(frame.content, L"Google Antigravity \u00B7 recording off");
        CHECK(detail && TestSameRect(detail->rect, {none.left + 13 + 24, none.top + 31 + 22, none.right - 13,
                                                    none.top + 31 + 38}));
        CHECK_NEAR(none.bottom - none.top, 82);
        // Wide enough, as before: each on its line, the time beside its value, the detail beside the
        // label; the row as tall as the credits card.
        frame = TestHistoryFrame(view, {1600, 790});
        panels = TestPanels(frame);
        CHECK(TestFind(frame.content, L"Thu 29 Oct 17:30")->rect.top < panels[1]->rect.top + 63);
        CHECK_NEAR(panels[1]->rect.bottom - panels[1]->rect.top, 11 + 16 + 36 + 2 * 16 + 11);
        CHECK_NEAR(TestFind(frame.content, L"Google Antigravity \u00B7 recording off")->rect.top, panels[3]->rect.top + 35);
        CHECK_NEAR(panels[3]->rect.bottom, panels[1]->rect.bottom);
    });

    Run("history chart ticks: the step keeps the measured labels 12 DIP apart; the pseudo-locale's 900 days at "
        "704 DIP take every sixth month where English takes every third", [] {
        UiGraphics& g = TestGraphics();
        HistoryUiLocalTime zone = TestZone();
        ULONGLONG from = kWeekTo - 900 * kHistoryDayMs;
        CHECK(HistoryChartTicks(g, {}, from, kWeekTo, 704, zone).size() == 10);
        std::vector<HistoryChartTick> ticks = HistoryChartTicks(g, TestLocale(L"qps-ploc"), from, kWeekTo, 704, zone);
        CHECK(ticks.size() == 5 && ticks[0].time == TestLocalMs(2024, 7, 1, 0, 0) &&
              ticks[1].time == TestLocalMs(2025, 1, 1, 0, 0) && ticks[4].time == TestLocalMs(2026, 7, 1, 0, 0));
        float previous = -1e9f;
        for (const HistoryChartTick& tick : ticks) {
            float at = 704 * float(double(tick.time - from) / double(kWeekTo - from));
            float half = UiLayoutSize(UiLayout(g, tick.label, kHistoryChartAxisStyle, 10000, false)).width / 2;
            CHECK(at - half >= previous + 12);
            previous = at + half;
        }
    });

    Run("history navigation: an entry whose label it cuts holds it whole in its tooltip", [] {
        HistoryUiView view = TestHistoryView();
        view.accounts[0].label = L"Research \u2014 shared Max seat";
        UiFrame frame = TestHistoryFrame(view);
        const UiItem* research = UiFindItem(frame, kHistoryUiNavAccount + 12);
        CHECK(research && research->text == L"Research \u2014 shared Max seat" &&
              UiRunsText(research->tooltip) == research->text);
        CHECK(UiFindItem(frame, kHistoryUiNavAccount + 10)->tooltip.empty() && UiFindItem(frame, kHistoryUiNavAll)->tooltip.empty());
    });

    Run("localized layout: every history state fits in every language at the default and minimum size, "
        "its chart's time labels apart at 96 and 144 DPI", [] {
        UiGraphics& g = TestGraphics();
        std::vector<std::string> findings;
        for (const HistoryLayoutCase& entry : HistoryLayoutCases()) {
            for (std::string_view tag : kHistoryLayoutTags) {
                std::shared_ptr<TestChartWindow> w = HistoryLayoutWindow(entry, tag);
                for (D2D1_SIZE_F client : {kHistoryLayoutClient, kHistoryLayoutMinClient}) {
                    for (float scale : {1.0f, 1.5f}) {
                        UiFrame frame = HistoryLayoutFrame(g, *w, client, scale);
                        if (entry.name == "default" && scale == 1) {
                            const UiItem* chart = UiFindItem(frame, kHistoryUiChart);
                            std::cout << "OBSERVE default " << tag << " " << int(client.width) << "x" << int(client.height)
                                      << ": header " << frame.viewport.top << " DIP, command bar rows "
                                      << 1 + int(UiFindItem(frame, kHistoryUiCompare)->rect.top -
                                                 UiFindItem(frame, kHistoryUiViews)->rect.top) / 42
                                      << ", viewport " << frame.viewport.bottom - frame.viewport.top
                                      << " DIP, chart item bottom " << (chart ? chart->rect.bottom : -1) << " DIP\n";
                        }
                        std::vector<std::string> found = HistoryLayoutRules(g, frame, w->controller.chart, client);
                        if (scale == 1) {
                            std::vector<std::string> audit = I18nLayoutAudit(g, frame, client, kHistoryUiNavWidth);
                            found.insert(found.begin(), audit.begin(), audit.end());
                        }
                        std::string where = entry.name + " " + std::string(tag) + " " + std::to_string(int(client.width)) +
                                            "x" + std::to_string(int(client.height)) + " @" +
                                            std::to_string(int(scale * 96)) + ": ";
                        for (const std::string& finding : found) findings.push_back(where + finding);
                        UiTrimCaches(g);
                    }
                }
                g.locale = L"en-US";
            }
        }
        for (const std::string& finding : findings) std::cout << "FINDING " << finding << '\n';
        CHECK(findings.empty());
    });
}
