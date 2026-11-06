// Localization in the usage history window: the compact dates and the chart's ticks per language
// (each language's order and names, always 24-hour; Russian months in a date's form and alone),
// the status pill's plural, the readout's hint, and the controller's language: each composition
// takes the current one, a modal keeps the one it started in, and a switch re-labels what was
// built in the old one (the status, the series, the ticks, the readout and its key, an operation's
// notice). An export's notice and progress in Russian with their plurals, Delete history's dialog
// and notices in Russian with the journal's reason, a dialog keeping the language it opened in,
// the readout's rows in Chinese, and abbreviated credits' own template in Russian. Regional
// number formats (support.inc's regions): values, hours, axis labels and cards in English and
// German with de-DE's decimal comma, never grouped;
// export counts in each region's digit groups inside their plurals; the large-export warning's
// ungrouped estimate; a regional format change re-labeling the composed window and the readout.
// Expected text is written out by hand from the production catalogs.

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
            {L"de", L"Sa\u00A031.\u00A0Okt\u00A014:31", L"25.\u00A0Mai\u00A02026\u00A023:59"},
            {L"es", L"s\u00E1b\u00A031\u00A0oct 14:31", L"25\u00A0may\u00A02026 23:59"},
            {L"fr", L"sam.\u00A031\u00A0oct. 14:31", L"25\u00A0mai\u00A02026 23:59"},
            {L"ja", L"10\u670831\u65E5(\u571F) 14:31", L"2026\u5E745\u670825\u65E5 23:59"},
            {L"pt-BR", L"s\u00E1b\u00A031\u00A0out 14:31", L"25\u00A0mai\u00A02026 23:59"},
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
            return TestLabels(HistoryChartTicks(TestGraphics(), TestLocale(tag), from, to, 704, zone));
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
        CHECK((labels(L"fr", may - 30 * kHistoryDayMs, may).front() == L"4\u00A0mai"));
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
        CHECK(c.view.locale == de && c.view.status == L"Aktualisiert Sa\u00A031.\u00A0Okt\u00A014:32" && c.seriesKey.locale == de);
        CHECK(std::any_of(c.view.cards.begin(), c.view.cards.end(), [](const HistoryUiCard& card) {
            return std::any_of(card.lines.begin(), card.lines.end(),
                               [](const std::wstring& line) { return line.find(L".\u00A0Okt\u00A0") != std::wstring::npos; });
        }));
        // The same plot: the ticks are labeled again, the readout too, under another key.
        TestLaid(*w);
        CHECK((TestLabels(chart.ticks).front() == L"So 25.") && chart.tickLocale == de);
        UiTooltipContent german;
        c.Hover(TestAt(chart, 0, 0, 0), german, palette);
        CHECK(german.key && german.key != english.key && UiRunsText(german.rows[0].detail).ends_with(L"Sa\u00A024.\u00A0Okt\u00A014:33") &&
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

    Run("history export in Russian: the save dialog titled in its language, the notice's rows in each plural form "
        "(1, 3, 5, 12, 21, 100,003), the selected account's while comparing with its second sentence, re-labeled "
        "by a switch to English; the progress line's plural", [] {
        TestHistoryOps ops = TestControllerOps();
        ops.locale = TestLocale(L"ru");
        HistoryController c(ops);
        TestLoaded(c, ops);
        // choose Экспорт всех аккаунтов — все показатели, выбранный период (при сравнении — обе недели)
        const std::wstring title =
            L"choose \u042D\u043A\u0441\u043F\u043E\u0440\u0442 \u0432\u0441\u0435\u0445 \u0430\u043A\u043A\u0430"
            L"\u0443\u043D\u0442\u043E\u0432 \u2014 \u0432\u0441\u0435 \u043F\u043E\u043A\u0430\u0437\u0430\u0442"
            L"\u0435\u043B\u0438, \u0432\u044B\u0431\u0440\u0430\u043D\u043D\u044B\u0439 \u043F\u0435\u0440\u0438"
            L"\u043E\u0434 (\u043F\u0440\u0438 \u0441\u0440\u0430\u0432\u043D\u0435\u043D\u0438\u0438 \u2014 \u043E"
            L"\u0431\u0435 \u043D\u0435\u0434\u0435\u043B\u0438)";
        // ... показателей (все аккаунты) в C:\Exports\usage.csv. Метки не включены; время в UTC.
        const std::wstring all =
            L" \u043F\u043E\u043A\u0430\u0437\u0430\u0442\u0435\u043B\u0435\u0439 (\u0432\u0441\u0435 \u0430\u043A"
            L"\u043A\u0430\u0443\u043D\u0442\u044B) \u0432 C:\\Exports\\usage.csv. \u041C\u0435\u0442\u043A\u0438 "
            L"\u043D\u0435 \u0432\u043A\u043B\u044E\u0447\u0435\u043D\u044B; \u0432\u0440\u0435\u043C\u044F \u0432 "
            L"UTC.";
        // Экспортирована 1 строка (one), Экспортированы 3 строки (few), Экспортировано 5 строк (many) ...
        const std::pair<size_t, std::wstring> cases[] = {
            {1, L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442\u0438\u0440\u043E\u0432\u0430\u043D\u0430 1 \u0441\u0442"
                L"\u0440\u043E\u043A\u0430"},
            {3, L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442\u0438\u0440\u043E\u0432\u0430\u043D\u044B 3 \u0441\u0442"
                L"\u0440\u043E\u043A\u0438"},
            {5, L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442\u0438\u0440\u043E\u0432\u0430\u043D\u043E 5 \u0441\u0442"
                L"\u0440\u043E\u043A"},
            {12, L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442\u0438\u0440\u043E\u0432\u0430\u043D\u043E 12 \u0441"
                 L"\u0442\u0440\u043E\u043A"},
            {21, L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442\u0438\u0440\u043E\u0432\u0430\u043D\u0430 21 \u0441"
                 L"\u0442\u0440\u043E\u043A\u0430"},
            {100003, L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442\u0438\u0440\u043E\u0432\u0430\u043D\u044B "
                     L"100,003 \u0441\u0442\u0440\u043E\u043A\u0438"}};
        for (const auto& [rows, head] : cases) {
            ops.writeResult = {rows, 0, true};
            ops.log.clear();
            c.Command(HistoryUiCommand::Export);
            CHECK(ops.log.at(0) == title);
            CHECK(c.WriteTick());
            CHECK(c.view.notice.text == head + all && c.view.notice.severity == UiSeverity::Info);
        }
        c.Command(HistoryUiCommand::Select, 0, 0xC1);
        c.Command(HistoryUiCommand::Compare, 1);
        ops.writeResult = {5, 0, true};
        c.Command(HistoryUiCommand::Export);
        CHECK(c.WriteTick());
        // Экспортировано 5 строк показателей (выбранный аккаунт) в C:\Exports\usage.csv. Метки не включены;
        // время в UTC. Включает оба прошедших 7-дневных периода.
        CHECK(c.view.notice.text ==
              L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442\u0438\u0440\u043E\u0432\u0430\u043D\u043E 5 \u0441\u0442"
              L"\u0440\u043E\u043A \u043F\u043E\u043A\u0430\u0437\u0430\u0442\u0435\u043B\u0435\u0439 (\u0432\u044B"
              L"\u0431\u0440\u0430\u043D\u043D\u044B\u0439 \u0430\u043A\u043A\u0430\u0443\u043D\u0442) \u0432 C:\\Exp"
              L"orts\\usage.csv. \u041C\u0435\u0442\u043A\u0438 \u043D\u0435 \u0432\u043A\u043B\u044E\u0447\u0435"
              L"\u043D\u044B; \u0432\u0440\u0435\u043C\u044F \u0432 UTC. \u0412\u043A\u043B\u044E\u0447\u0430\u0435"
              L"\u0442 \u043E\u0431\u0430 \u043F\u0440\u043E\u0448\u0435\u0434\u0448\u0438\u0445 7-\u0434\u043D\u0435"
              L"\u0432\u043D\u044B\u0445 \u043F\u0435\u0440\u0438\u043E\u0434\u0430.");
        // The notice stays a code: the next composition shows it in the new language.
        ops.locale = {};
        c.Notify();
        CHECK(c.ProcessRefresh(false, false));
        CHECK(c.view.notice.text == L"Exported 5 meter rows (selected account) to C:\\Exports\\usage.csv. Labels "
                                    L"omitted; times UTC. Includes both elapsed 7-day periods.");
        // Экспорт: 1 строка… / 3 строки… / 5 строк… / 2,000,000 строк…
        I18nLocale ru = TestLocale(L"ru");
        CHECK(HistoryUiProgressText(ru, 1) ==
              L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 1 \u0441\u0442\u0440\u043E\u043A\u0430\u2026");
        CHECK(HistoryUiProgressText(ru, 3) ==
              L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 3 \u0441\u0442\u0440\u043E\u043A\u0438\u2026");
        CHECK(HistoryUiProgressText(ru, 5) ==
              L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 5 \u0441\u0442\u0440\u043E\u043A\u2026");
        CHECK(HistoryUiProgressText(ru, 2000000) ==
              L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 2,000,000 \u0441\u0442\u0440\u043E\u043A\u2026");
        CHECK(HistoryUiProgressText({}, 1) == L"Exporting 1 rows\u2026");
    });

    Run("history Delete history in Russian: the confirmation and its caption, a failure's notice with the "
        "journal's reason, then the success naming the account", [] {
        TestHistoryOps ops = TestControllerOps();
        ops.locale = TestLocale(L"ru");
        ops.deleteResult = false;
        HistoryController c(ops);
        TestLoaded(c, ops, 0xDEAD);
        c.Command(HistoryUiCommand::DeleteHistory, 0, 0xDEAD);
        // ask Удаление истории аккаунта
        CHECK(ops.log.at(0) == L"ask \u0423\u0434\u0430\u043B\u0435\u043D\u0438\u0435 \u0438\u0441\u0442\u043E\u0440"
                               L"\u0438\u0438 \u0430\u043A\u043A\u0430\u0443\u043D\u0442\u0430");
        // Безвозвратно удалить всю историю использования удалённого аккаунта Anthropic account_000000000000dead?
        CHECK(ops.asked.at(0).first.starts_with(
            L"\u0411\u0435\u0437\u0432\u043E\u0437\u0432\u0440\u0430\u0442\u043D\u043E \u0443\u0434\u0430\u043B"
            L"\u0438\u0442\u044C \u0432\u0441\u044E \u0438\u0441\u0442\u043E\u0440\u0438\u044E \u0438\u0441\u043F"
            L"\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u043D\u0438\u044F \u0443\u0434\u0430\u043B\u0451\u043D"
            L"\u043D\u043E\u0433\u043E \u0430\u043A\u043A\u0430\u0443\u043D\u0442\u0430 Anthropic account_000000000"
            L"000dead?\r\n\r\n"));
        // Не удалось удалить историю этого аккаунта. Хранилище истории используется другим сеансом Проводника
        // или недоступно.
        CHECK(c.view.notice.text ==
              L"\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u0443\u0434\u0430\u043B\u0438\u0442\u044C "
              L"\u0438\u0441\u0442\u043E\u0440\u0438\u044E \u044D\u0442\u043E\u0433\u043E \u0430\u043A\u043A\u0430"
              L"\u0443\u043D\u0442\u0430. \u0425\u0440\u0430\u043D\u0438\u043B\u0438\u0449\u0435 \u0438\u0441\u0442"
              L"\u043E\u0440\u0438\u0438 \u0438\u0441\u043F\u043E\u043B\u044C\u0437\u0443\u0435\u0442\u0441\u044F "
              L"\u0434\u0440\u0443\u0433\u0438\u043C \u0441\u0435\u0430\u043D\u0441\u043E\u043C \u041F\u0440\u043E"
              L"\u0432\u043E\u0434\u043D\u0438\u043A\u0430 \u0438\u043B\u0438 \u043D\u0435\u0434\u043E\u0441\u0442"
              L"\u0443\u043F\u043D\u043E.");
        CHECK(c.view.notice.severity == UiSeverity::Error);
        ops.deleteResult = true;
        c.Command(HistoryUiCommand::DeleteHistory, 0, 0xDEAD);
        // История удалённого аккаунта account_000000000000dead удалена.
        CHECK(c.view.notice.text ==
              L"\u0418\u0441\u0442\u043E\u0440\u0438\u044F \u0443\u0434\u0430\u043B\u0451\u043D\u043D\u043E\u0433"
              L"\u043E \u0430\u043A\u043A\u0430\u0443\u043D\u0442\u0430 account_000000000000dead \u0443\u0434\u0430"
              L"\u043B\u0435\u043D\u0430.");
        CHECK(c.view.notice.severity == UiSeverity::Info);
    });

    Run("history dialogs keep the language they opened in: Clear's confirmation in Japanese while a switch to "
        "German arrives, whose language its notice then shows", [] {
        TestHistoryOps ops = TestControllerOps();
        ops.locale = TestLocale(L"ja");
        HistoryController c(ops);
        TestLoaded(c, ops);
        ops.asking = [&] {
            ops.locale = TestLocale(L"de");
            c.Notify();
        };
        c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Clear));
        // ask すべての使用履歴の消去; この Windows ユーザーのローカルの使用履歴を、削除したアカウントの分も含めて
        // すべて完全に消去しますか？
        CHECK(ops.log.at(0) == L"ask \u3059\u3079\u3066\u306E\u4F7F\u7528\u5C65\u6B74\u306E\u6D88\u53BB");
        CHECK(ops.asked.at(0).first.starts_with(
            L"\u3053\u306E Windows \u30E6\u30FC\u30B6\u30FC\u306E\u30ED\u30FC\u30AB\u30EB\u306E\u4F7F\u7528\u5C65"
            L"\u6B74\u3092\u3001\u524A\u9664\u3057\u305F\u30A2\u30AB\u30A6\u30F3\u30C8\u306E\u5206\u3082\u542B"
            L"\u3081\u3066\u3059\u3079\u3066\u5B8C\u5168\u306B\u6D88\u53BB\u3057\u307E\u3059\u304B\uFF1F\r\n\r\n"));
        // Der gesamte lokale Verlauf wurde gelöscht. Künftige Abfragen können wieder mit der Aufzeichnung beginnen.
        CHECK(c.view.locale == TestLocale(L"de") &&
              c.view.notice.text == L"Der gesamte lokale Verlauf wurde gel\u00F6scht. K\u00FCnftige Abfragen k\u00F6nnen "
                                    L"wieder mit der Aufzeichnung beginnen.");
    });

    Run("history readout rows in Chinese: the period tags, each unit and the time, in the row's inks", [] {
        I18nLocale zh = TestLocale(L"zh-Hans");
        auto row = [&](int meter, int period, double value, WORD day, WORD hour, WORD minute) {
            return HistoryChartRow(zh, L"", {}, 2, meter, period, value, TestLocalMs(2026, 10, day, hour, minute), kWeekTo,
                                   TestZone()).detail;
        };
        std::vector<UiRun> previous = row(1, 1, 37.6, 20, 12, 32);
        // [前一周] 37.6% · 10月20日 周二 12:32
        CHECK(UiRunsText(previous) == L"[\u524D\u4E00\u5468] 37.6% \u00B7 10\u670820\u65E5 \u5468\u4E8C 12:32");
        CHECK(previous.size() == 3 && previous[0].text == L"[\u524D\u4E00\u5468]" && previous[0].ink == UiInk::Secondary &&
              previous[1].ink == UiInk::Inherit && previous[2].ink == UiInk::Secondary);
        // [当前] 38.0% · 10月27日 周二 12:31
        CHECK(UiRunsText(row(1, 0, 38.04, 27, 12, 31)) ==
              L"[\u5F53\u524D] 38.0% \u00B7 10\u670827\u65E5 \u5468\u4E8C 12:31");
        // [前一周] 9999.94 积分 · 10月20日 周二 12:32
        CHECK(UiRunsText(row(7, 1, 9999.94, 20, 12, 32)) ==
              L"[\u524D\u4E00\u5468] 9999.94 \u79EF\u5206 \u00B7 10\u670820\u65E5 \u5468\u4E8C 12:32");
        // [当前] USD 11.00 · 10月27日 周二 12:31: dollars keep their code.
        CHECK(UiRunsText(row(6, 0, 11, 27, 12, 31)) ==
              L"[\u5F53\u524D] USD 11.00 \u00B7 10\u670827\u65E5 \u5468\u4E8C 12:31");
    });

    Run("history credits: an abbreviated amount takes its own template (Russian: кредитов after 25.0K, кредита "
        "after 125.50); English alike either way", [] {
        const I18nLocale ru = TestLocale(L"ru");
        CHECK(HistoryUiValue({}, 7, 125.5, 0) == L"125.50 credits" && HistoryUiValue({}, 7, 25000, 0) == L"25.0K credits");
        // 125.50 кредита, 9999.99 кредита; 25.0K кредитов, 1.2M кредитов (a no-break space before the unit).
        CHECK(HistoryUiValue(ru, 7, 125.5, 0) == L"125.50\u00A0\u043A\u0440\u0435\u0434\u0438\u0442\u0430");
        CHECK(HistoryUiValue(ru, 7, 9999.99, 0) == L"9999.99\u00A0\u043A\u0440\u0435\u0434\u0438\u0442\u0430");
        CHECK(HistoryUiValue(ru, 7, 25000, 0) == L"25.0K\u00A0\u043A\u0440\u0435\u0434\u0438\u0442\u043E\u0432");
        CHECK(HistoryUiValue(ru, 7, 1234567, 0) == L"1.2M\u00A0\u043A\u0440\u0435\u0434\u0438\u0442\u043E\u0432");
        // Rounding that carries into the next magnitude abbreviates: 9999.996 shows 10.0K.
        CHECK(HistoryUiValue(ru, 7, 9999.996, 0) == L"10.0K\u00A0\u043A\u0440\u0435\u0434\u0438\u0442\u043E\u0432");
    });

    Run("history numbers in a regional format: values, hours, axis labels and the cards take its decimal "
        "separator, never grouped, in English and German; dates stay the language's compact form", [] {
        const I18nLocale en = TestRegional(L"en", L"de-DE"), de = TestRegional(L"de", L"de-DE"),
                         hi = TestRegional(L"en", L"hi-IN");
        CHECK(HistoryUiNumber(en, 9999.99, 2) == L"9999,99");
        CHECK(HistoryUiNumber(en, 9999.996, 2) == L"10,0K");
        CHECK(HistoryUiNumber(en, 12345, 2) == L"12,3K" && HistoryUiNumber(en, -12345, 2) == L"-12,3K");
        CHECK(HistoryUiNumber(en, 999950, 2) == L"1,0M" && HistoryUiNumber(en, 62.4, 0) == L"62");
        CHECK(HistoryUiNumber(hi, 9999.99, 2) == L"9999.99");
        CHECK(HistoryUiValue(en, 1, 93.04, 0) == L"93%" && HistoryUiValue(en, 5, 38.04, 1) == L"38,0%");
        CHECK(HistoryUiValue(en, 6, 10, 0) == L"USD 10,00" && HistoryUiValue(de, 6, 10, 0) == L"USD 10,00");
        CHECK(HistoryUiValue(en, 7, 1240, 0) == L"1240,00 credits" && HistoryUiValue(de, 7, 1240, 0) == L"1240,00\u00A0Credits");
        CHECK(HistoryUiHours(en, 23.2 * double(kHourMs)) == L"23,2 h" && HistoryUiHours(de, 23.2 * double(kHourMs)) == L"23,2\u00A0Std.");
        CHECK(HistoryUiHours(en, 166 * double(kHourMs)) == L"166 h" && HistoryUiHours(en, 87600 * double(kHourMs)) == L"87600 h");
        // Axis labels: trailing decimal zeros dropped before the region's separator.
        CHECK(HistoryChartAxisLabel(en, 1, 0.5) == L"0,5 USD" && HistoryChartAxisLabel(en, 1, 0.75) == L"0,75 USD");
        CHECK(HistoryChartAxisLabel(en, 1, 1250) == L"1250 USD" && HistoryChartAxisLabel(en, 0, -25) == L"-25%");
        CHECK(HistoryChartAxisLabel(en, 2, 12500) == L"12,5K cr" && HistoryChartAxisLabel(en, 2, 10000) == L"10K cr");
        CHECK(HistoryChartAxisLabel(de, 2, 12500) == L"12,5K cr" && HistoryChartAxisLabel(hi, 1, 0.75) == L"0.75 USD");
        // One account's cards (Weekly 40, 95 and 93%): coverage and the time at the threshold.
        ULONGLONG recorded = kWeekFrom + 3 * kHourMs;
        auto cards = [&](const I18nLocale& locale) {
            return TestCards(TestOneAccount(), [&](HistoryUiView& view) {
                TestPolled(recorded - 5)(view);
                view.locale = locale;
            }).cards;
        };
        std::vector<HistoryUiCard> english = cards(en);
        CHECK(english.size() == 2 && english[0].title == L"Weekly" && english[0].value == L"93%" &&
              english[0].when == L"now");
        CHECK((english[0].lines == std::vector<std::wstring>{L"Peak 95% \u00B7 Sat 24 Oct 16:32", L"Coverage 2 h (1,2%)",
                                                              L"Est. \u226590%: 1,1 h"}));
        // Wöchentlich 93% jetzt; Höchstwert 95% · Sa 24. Okt 16:32; Abdeckung 2 Std. (1,2%); Geschätzt ≥90%: 1,1 Std.,
        // no-break spaces inside the date and before each unit.
        std::vector<HistoryUiCard> german = cards(de);
        CHECK(german.size() == 2 && german[0].title == L"W\u00F6chentlich" && german[0].value == L"93%" &&
              german[0].when == L"jetzt");
        CHECK((german[0].lines == std::vector<std::wstring>{L"H\u00F6chstwert 95% \u00B7 Sa\u00A024.\u00A0Okt\u00A016:32",
                                                             L"Abdeckung 2\u00A0Std. (1,2%)",
                                                             L"Gesch\u00E4tzt \u226590%: 1,1\u00A0Std."}));
    });

    Run("history export counts in a region's digit groups inside their plurals: progress and the exported notice "
        "(English, German, Russian, Spanish, hi-IN); the large-export warning's estimate stays ungrouped", [] {
        const I18nLocale de = TestRegional(L"de", L"de-DE"), hi = TestRegional(L"en", L"hi-IN");
        CHECK(HistoryUiProgressText(TestRegional(L"en", L"en-US"), 12345) == L"Exporting 12,345 rows\u2026");
        CHECK(HistoryUiProgressText(TestRegional(L"en", L"fr-FR"), 12345) == L"Exporting 12\u202F345 rows\u2026");
        CHECK(HistoryUiProgressText(hi, 1234567) == L"Exporting 12,34,567 rows\u2026");
        CHECK(HistoryUiProgressText(TestRegional(L"en", L"no-grouping"), 12345) == L"Exporting 12345 rows\u2026");
        CHECK(HistoryUiProgressText(de, 1) == L"1 Zeile wird exportiert\u2026");
        CHECK(HistoryUiProgressText(de, 12345) == L"12.345 Zeilen werden exportiert\u2026");
        // Экспорт: 21 строка… / 1 234 строки… / 12 345 строк…, ru-RU's no-break space between groups.
        const I18nLocale ru = TestRegional(L"ru", L"ru-RU");
        CHECK(HistoryUiProgressText(ru, 21) ==
              L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 21 \u0441\u0442\u0440\u043E\u043A\u0430\u2026");
        CHECK(HistoryUiProgressText(ru, 1234) ==
              L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 1\u00A0234 \u0441\u0442\u0440\u043E\u043A\u0438\u2026");
        CHECK(HistoryUiProgressText(ru, 12345) ==
              L"\u042D\u043A\u0441\u043F\u043E\u0440\u0442: 12\u00A0345 \u0441\u0442\u0440\u043E\u043A\u2026");
        // Spanish's many for a million: Exportando 1.000.000 de filas…
        CHECK(HistoryUiProgressText(TestRegional(L"es", L"de-DE"), 1000000) == L"Exportando 1.000.000 de filas\u2026");
        HistoryNotice exported{.code = HistoryNoticeCode::Exported, .rows = 1234567, .all = true, .text = L"C:\\x.csv"};
        CHECK(HistoryNoticeText(de, exported).text ==
              L"1.234.567 Messwertzeilen (alle Konten) nach C:\\x.csv exportiert. Ohne Bezeichnungen; Zeiten in UTC.");
        exported.rows = 1;
        CHECK(HistoryNoticeText(de, exported).text ==
              L"1 Messwertzeile (alle Konten) nach C:\\x.csv exportiert. Ohne Bezeichnungen; Zeiten in UTC.");
        exported.rows = 1234567;
        exported.all = false;
        CHECK(HistoryNoticeText(hi, exported).text ==
              L"Exported 12,34,567 meter rows (selected account) to C:\\x.csv. Labels omitted; times UTC.");
        // OC (OpenCode Go, 0x0C): 166,667 readings of three rows, about 500,001: the warning's estimate as
        // before, ungrouped, under de-DE.
        TestHistoryOps ops = TestControllerOps();
        ops.locale = TestRegional(L"en", L"de-DE");
        ops.settings.settings.accounts.push_back(TestHistoryAccount(L"opencode", L"OC", 0x0C));
        for (ULONGLONG i = 0; i < 166667; i++) ops.journal.push_back(TestReading(0x0C, 5, kWeekFrom + i * 3000, {}));
        ops.stamp.appended = ops.journal.size();
        ops.answers = {IDCANCEL};
        HistoryController c(ops);
        TestLoaded(c, ops, 0x0C);
        c.Command(HistoryUiCommand::Export);
        TestSettled(c, ops);
        CHECK(ops.asked.size() == 1 && ops.asked[0].first.starts_with(L"This export contains about 500001 rows. "));
    });

    Run("history controller: a regional format change composes the window again in the same language: the cards' "
        "and the readout's decimals (its key moves), the ticks' labels as they were", [] {
        UiPalette palette = TestPalette(UiThemeKind::Dark);
        auto w = TestChartOf(TestMockupWeek());
        HistoryController& c = w->controller;
        HistoryChart& chart = TestLaid(*w);
        UiTooltipContent before;
        c.Hover(TestAt(chart, 0, 0, 0), before, palette);
        CHECK(before.key && before.rows.at(0).detail.at(1).text.find(L'.') != std::wstring::npos);
        const I18nLocale regional = TestRegional(L"en", L"de-DE");
        w->ops.locale = regional;
        c.Notify();
        CHECK(c.ProcessRefresh(false, false));
        CHECK(c.view.locale == regional && c.seriesKey.locale == regional &&
              c.view.status == L"Updated Sat 31 Oct 14:32");
        CHECK(std::any_of(c.view.cards.begin(), c.view.cards.end(), [](const HistoryUiCard& card) {
            return std::any_of(card.lines.begin(), card.lines.end(), [](const std::wstring& line) {
                return line.starts_with(L"Coverage ") && line.find(L',') != std::wstring::npos &&
                       line.find(L'.') == std::wstring::npos;
            });
        }));
        TestLaid(*w);
        CHECK((TestLabels(chart.ticks).front() == L"Sun 25") && chart.tickLocale == regional);
        UiTooltipContent after;
        c.Hover(TestAt(chart, 0, 0, 0), after, palette);
        const std::wstring& value = after.rows.at(0).detail.at(1).text;
        CHECK(after.key && after.key != before.key && value.find(L',') != std::wstring::npos &&
              value.find(L'.') == std::wstring::npos);
    });
}
