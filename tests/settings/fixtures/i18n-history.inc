// Localization in the usage history window: the compact dates and the chart's ticks per language
// (each language's order and names, always 24-hour; Russian months in a date's form and alone),
// the status pill's plural, the readout's hint, and the controller's language: each composition
// takes the current one, a modal keeps the one it started in, and a switch re-labels what was
// built in the old one (the status, the series, the ticks, the readout and its key). Expected
// text is written out by hand from the production catalogs.

static I18nLocale TestLocale(std::wstring_view tag) {
    const I18nLanguage* language = I18nLanguageByTag(tag);
    if (!language) throw std::runtime_error("no language");
    return I18nLocale(*language);
}

static void RunHistoryI18nTests() {
    Run("history formats per language: this year's time with its weekday, another year's with the year; "
        "each language's order and names, 24-hour", [] {
        HistoryUiLocalTime zone = TestZone();
        ULONGLONG now = TestLocalMs(2026, 10, 31, 14, 32), later = TestLocalMs(2027, 1, 15, 12, 0);
        ULONGLONG saturday = TestLocalMs(2026, 10, 31, 14, 31), may = TestLocalMs(2026, 5, 25, 23, 59);
        const std::tuple<PCWSTR, PCWSTR, PCWSTR> cases[] = {
            {L"en", L"Sat 31 Oct 14:31", L"25 May 2026 23:59"},
            {L"de", L"Sa 31. Okt 14:31", L"25. Mai 2026 23:59"},
            {L"es", L"s\u00E1b 31 oct 14:31", L"25 may 2026 23:59"},
            {L"fr", L"sam. 31 oct. 14:31", L"25 mai 2026 23:59"},
            {L"ja", L"10\u670831\u65E5(\u571F) 14:31", L"2026\u5E745\u670825\u65E5 23:59"},
            {L"pt-BR", L"s\u00E1b 31 out 14:31", L"25 mai 2026 23:59"},
            {L"ru", L"\u0441\u0431 31 \u043E\u043A\u0442 14:31", L"25 \u043C\u0430\u044F 2026 23:59"},
            {L"zh-Hans", L"10\u670831\u65E5 \u5468\u516D 14:31", L"2026\u5E745\u670825\u65E5 23:59"},
            // The pseudo-locale: each name and the pattern bracketed and padded.
            {L"qps-ploc", L"[[\u0160\u00E5\u0163 \u013C] 31 [\u00D6\u00E7\u0163 \u013C] 14:31 \u013C]",
             L"[25 [\u1E40\u00E5\u00FD \u013C] 2026 23:59 \u013C]"},
        };
        for (auto [tag, thisYear, another] : cases) {
            I18nLocale locale = TestLocale(tag);
            CHECK(HistoryUiTime(locale, saturday, now, zone) == thisYear);
            CHECK(HistoryUiTime(locale, may, later, zone) == another);
        }
        CHECK(HistoryUiTime(TestLocale(L"ja"), 1, now, [](ULONGLONG) { return SYSTEMTIME{}; }) == L"\u2014");
    });

    Run("history chart ticks per language: weekdays with their day, dates on Mondays, months with their year; "
        "a Russian month in a date takes its form (5 мая), alone the other (май 2026)", [] {
        HistoryUiLocalTime zone = TestZone();
        auto labels = [&](PCWSTR tag, ULONGLONG from, ULONGLONG to) {
            return TestLabels(HistoryChartTicks(TestLocale(tag), from, to, 704, zone));
        };
        ULONGLONG may = TestLocalMs(2026, 5, 31, 12, 0);
        // A week: each local midnight.
        CHECK((labels(L"de", kWeekFrom, kWeekTo) ==
               std::vector<std::wstring>{L"So 25.", L"Mo 26.", L"Di 27.", L"Mi 28.", L"Do 29.", L"Fr 30.", L"Sa 31."}));
        CHECK((labels(L"ja", kWeekFrom, kWeekTo) ==
               std::vector<std::wstring>{L"25\u65E5(\u65E5)", L"26\u65E5(\u6708)", L"27\u65E5(\u706B)", L"28\u65E5(\u6C34)",
                                         L"29\u65E5(\u6728)", L"30\u65E5(\u91D1)", L"31\u65E5(\u571F)"}));
        CHECK((labels(L"zh-Hans", kWeekFrom, kWeekTo).back() == L"31\u65E5 \u5468\u516D"));
        // 30 days in May: Mondays.
        CHECK((labels(L"en", may - 30 * kHistoryDayMs, may) ==
               std::vector<std::wstring>{L"May 4", L"May 11", L"May 18", L"May 25"}));
        CHECK((labels(L"ru", may - 30 * kHistoryDayMs, may) ==
               std::vector<std::wstring>{L"4 \u043C\u0430\u044F", L"11 \u043C\u0430\u044F", L"18 \u043C\u0430\u044F",
                                         L"25 \u043C\u0430\u044F"}));
        CHECK((labels(L"fr", may - 30 * kHistoryDayMs, may).front() == L"4 mai"));
        CHECK((labels(L"ja", may - 30 * kHistoryDayMs, may).front() == L"5\u67084\u65E5"));
        // 200 days: every month from May.
        CHECK((labels(L"en", kWeekTo - 200 * kHistoryDayMs, kWeekTo) ==
               std::vector<std::wstring>{L"May 2026", L"Jun 2026", L"Jul 2026", L"Aug 2026", L"Sep 2026", L"Oct 2026"}));
        CHECK((labels(L"ru", kWeekTo - 200 * kHistoryDayMs, kWeekTo) ==
               std::vector<std::wstring>{L"\u043C\u0430\u0439 2026", L"\u0438\u044E\u043D 2026", L"\u0438\u044E\u043B 2026",
                                         L"\u0430\u0432\u0433 2026", L"\u0441\u0435\u043D 2026", L"\u043E\u043A\u0442 2026"}));
        CHECK((labels(L"zh-Hans", kWeekTo - 200 * kHistoryDayMs, kWeekTo).front() == L"2026\u5E745\u6708"));
        CHECK((labels(L"de", kWeekTo - 200 * kHistoryDayMs, kWeekTo).front() == L"Mai 2026"));
        // Hours stay 24-hour; midnight names its day.
        CHECK((labels(L"ja", kWeekTo - kHistoryDayMs, kWeekTo)[4] == L"31\u65E5(\u571F)" &&
               labels(L"ja", kWeekTo - kHistoryDayMs, kWeekTo)[0] == L"16:00"));
    });

    Run("history pill per language: recording with its retention's plural (English, Russian, Japanese) or "
        "forever", [] {
        auto pill = [](PCWSTR tag, int days) {
            HistoryUiView view = TestHistoryView();
            view.locale = TestLocale(tag);
            view.retentionDays = days;
            UiFrame frame = TestHistoryFrame(view);
            return std::find_if(frame.main.begin(), frame.main.end(),
                                [](const UiItem& item) { return item.kind == UiItemKind::Pill; })->text;
        };
        CHECK(pill(L"en", 1) == L"Recording \u00B7 1 day" && pill(L"en", 2) == L"Recording \u00B7 2 days" &&
              pill(L"en", 0) == L"Recording \u00B7 forever");
        const std::pair<int, PCWSTR> russian[] = {
            {1, L"\u0417\u0430\u043F\u0438\u0441\u044C \u00B7 1 \u0434\u0435\u043D\u044C"},
            {2, L"\u0417\u0430\u043F\u0438\u0441\u044C \u00B7 2 \u0434\u043D\u044F"},
            {5, L"\u0417\u0430\u043F\u0438\u0441\u044C \u00B7 5 \u0434\u043D\u0435\u0439"},
            {21, L"\u0417\u0430\u043F\u0438\u0441\u044C \u00B7 21 \u0434\u0435\u043D\u044C"},
            {112, L"\u0417\u0430\u043F\u0438\u0441\u044C \u00B7 112 \u0434\u043D\u0435\u0439"},
            {0, L"\u0417\u0430\u043F\u0438\u0441\u044C \u00B7 \u0431\u0435\u0441\u0441\u0440\u043E\u0447\u043D\u043E"}};
        for (auto [days, text] : russian) CHECK(pill(L"ru", days) == text);
        CHECK(pill(L"ja", 1) == L"\u8A18\u9332\u4E2D \u00B7 1\u65E5" && pill(L"ja", 90) == L"\u8A18\u9332\u4E2D \u00B7 90\u65E5" &&
              pill(L"ja", 0) == L"\u8A18\u9332\u4E2D \u00B7 \u7121\u671F\u9650");
        HistoryUiView off = TestHistoryView();
        off.locale = TestLocale(L"de");
        off.pill = HistoryUiPill::Off;
        CHECK(TestFind(TestHistoryFrame(off).main, L"Aus"));
    });

    Run("history controller: a language switch re-labels the status, cards, ticks and readout (its key moves); "
        "a modal keeps its language until it ends", [] {
        UiPalette palette = TestPalette(UiThemeKind::Dark);
        auto w = TestChartOf(TestMockupWeek(), [](HistoryController& c) { c.Command(HistoryUiCommand::Compare, 1); });
        HistoryController& c = w->controller;
        HistoryChart& chart = TestLaid(*w);
        CHECK(c.view.locale == I18nLocale() && c.view.status == L"Updated Sat 31 Oct 14:32");
        CHECK((TestLabels(chart.ticks).front() == L"Sun 25"));
        D2D1_POINT_2F at = TestAt(chart, 0, 0, 0);
        UiTooltipContent english;
        c.Hover(at, english, palette);
        CHECK(english.key && UiRunsText(english.rows[0].detail).ends_with(L"Sat 24 Oct 14:33") &&
              english.more(3) == L"+3 more \u2014 hide lines in the legend or select one account");
        // The switch arrives as a notification: the live part composes in the new language.
        I18nLocale de = TestLocale(L"de");
        w->ops.locale = de;
        c.Notify();
        CHECK(c.ProcessRefresh(false, false));
        CHECK(c.view.locale == de && c.view.status == L"Aktualisiert Sa 31. Okt 14:32" && c.seriesKey.locale == de);
        CHECK(std::any_of(c.view.cards.begin(), c.view.cards.end(), [](const HistoryUiCard& card) {
            return std::any_of(card.lines.begin(), card.lines.end(),
                               [](const std::wstring& line) { return line.find(L". Okt ") != std::wstring::npos; });
        }));
        // The same plot: the ticks are labeled again, the readout too, under another key.
        TestLaid(*w);
        CHECK((TestLabels(chart.ticks).front() == L"So 25.") && chart.tickLocale == de);
        UiTooltipContent german;
        c.Hover(TestAt(chart, 0, 0, 0), german, palette);
        CHECK(german.key && german.key != english.key && UiRunsText(german.rows[0].detail).ends_with(L"Sa 24. Okt 14:33") &&
              german.more(3) == L"+3 weitere \u2013 Linien in der Legende ausblenden oder ein Konto ausw\u00E4hlen");
        // A modal keeps its language; the next composition after it takes the current one.
        w->ops.locale = TestLocale(L"ja");
        c.ModalBegan();
        c.Compose();
        CHECK(c.view.locale == de);
        c.ModalEnded();
        c.Notify();
        CHECK(c.ProcessRefresh(false, false) && c.view.locale == TestLocale(L"ja"));
    });
}
