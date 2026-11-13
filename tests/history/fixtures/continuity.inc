// Synthetic deadline transitions: independent expectations, actual HistoryConnection.
// Table mutations describe observations, not a second implementation of the rule.
    {
        HistorySample idle{};
        idle.accountId = 1; idle.provider = 2; idle.timeMs = baseTime;
        idle.intervalMs = 300000; idle.session = 1;
        idle.meters[1] = {0, baseTime + kHistoryWeekMs, kHistoryWeekMs};
        auto later = idle;
        later.timeMs += 300000; later.meters[1].resetMs += 300000;
        struct Case {
            const char* name;
            HistoryBreak expected;
            std::function<void(HistorySample&, HistorySample&)> observe;
        };
        const Case cases[] = {
            {"idle rolling weekly deadline", HistoryBreak::None, [](auto&, auto& b) { ++b.timeMs; }},
            {"rolling fractional jitter", HistoryBreak::None, [](auto&, auto& b) { ++b.timeMs; b.meters[1].resetMs -= 999; }},
            {"exception is OpenAI only", HistoryBreak::Reset, [](auto& a, auto& b) { a.provider = b.provider = 1; }},
            {"used quota is not idle", HistoryBreak::Reset, [](auto& a, auto&) { a.meters[1].value = 1; }},
            {"first usage anchors rolling deadline", HistoryBreak::None, [](auto&, auto& b) { b.meters[1].value = 1; }},
            {"anchor early in poll", HistoryBreak::None, [](auto& a, auto& b) { b.meters[1].value = 1; b.meters[1].resetMs = a.meters[1].resetMs + 60000; }},
            {"anchor midway in poll", HistoryBreak::None, [](auto& a, auto& b) { b.meters[1].value = 1; b.meters[1].resetMs = a.meters[1].resetMs + 150000; }},
            {"anchor late in poll", HistoryBreak::None, [](auto& a, auto& b) { b.meters[1].value = 1; b.meters[1].resetMs = a.meters[1].resetMs + 240000; }},
            {"arbitrary forward deadline jump", HistoryBreak::Reset, [](auto&, auto& b) { b.meters[1].resetMs += kHistoryDayMs; }},
            {"backward deadline jump", HistoryBreak::Reset, [](auto& a, auto& b) { b.meters[1].resetMs = a.meters[1].resetMs - 300000; }},
            {"rolling partial window not guessed", HistoryBreak::Reset, [](auto& a, auto& b) { a.meters[1].resetMs -= kHistoryDayMs; b.meters[1].resetMs -= kHistoryDayMs; }},
            {"anchored zero cannot discard old window", HistoryBreak::Reset, [](auto& a, auto&) { a.meters[1].resetMs -= 600000; }},
            {"session break", HistoryBreak::Gap, [](auto&, auto& b) { b.session = 2; }},
            {"provider change", HistoryBreak::Gap, [](auto&, auto& b) { b.provider = 3; }},
            {"account change", HistoryBreak::Gap, [](auto&, auto& b) { ++b.accountId; }},
            {"large gap", HistoryBreak::Gap, [](auto&, auto& b) { b.timeMs += 900000; b.meters[1].resetMs += 900000; }},
            {"clock rollback", HistoryBreak::Gap, [](auto& a, auto& b) { b.timeMs = a.timeMs - 1; }},
            {"failed reading", HistoryBreak::Unavailable, [](auto&, auto& b) { b.failed = 1; }},
            {"missing reading", HistoryBreak::Unavailable, [](auto&, auto& b) { b.meters[1].value = -1; }},
            {"duration change", HistoryBreak::Reset, [](auto&, auto& b) { b.meters[1].durationMs = kHistoryDayMs; }},
            {"reset presence change", HistoryBreak::Reset, [](auto&, auto& b) { b.meters[1].resetMs = 0; }},
            {"real zero-to-zero cycle boundary", HistoryBreak::Reset, [](auto& a, auto&) { a.meters[1].resetMs = a.timeMs + 180000; }},
            {"real exhausted-to-zero reset", HistoryBreak::Reset, [](auto& a, auto&) { a.meters[1].resetMs = a.timeMs + 180000; a.meters[1].value = 100; }},
            {"normal deadline jitter", HistoryBreak::None, [](auto& a, auto& b) { a.meters[1].value = 50; b.meters[1].value = 51; b.meters[1].resetMs = a.meters[1].resetMs + 1000; }},
        };
        for (const auto& test : cases) Run(std::string("continuity/") + test.name, [&] {
            auto a = idle, b = later;
            test.observe(a, b);
            CHECK(HistoryConnection(a, b, 1) == test.expected);
        });
    }
// OpenCode Go (provider 5): an idle 0% 5-hour reading whose deadline rolls a full window ahead
// between polls, on its 5-hour (0), weekly (1) and monthly (3) lanes, and on other providers' lanes.
    {
        auto rolling = [](uint32_t provider, int lane) {
            ULONGLONG duration = lane == 0 ? 5 * hour : lane == 1 ? kHistoryWeekMs : 30 * kHistoryDayMs;
            HistorySample a{};
            a.accountId = 1; a.provider = provider; a.timeMs = baseTime;
            a.intervalMs = 300000; a.session = 1;
            a.meters[lane] = {0, baseTime + duration, duration};
            auto b = a;
            b.timeMs += 300000; b.meters[lane].resetMs += 300000;
            return std::pair{a, b};
        };
        struct Case {
            const char* name;
            HistoryBreak expected;
            uint32_t provider;
            int lane;
            std::function<void(HistorySample&, HistorySample&)> observe;
        };
        const Case cases[] = {
            {"OpenCode Go idle rolling 5-hour deadline", HistoryBreak::None, 5, 0, [](auto&, auto& b) { ++b.timeMs; }},
            {"OpenCode Go rolling fractional jitter", HistoryBreak::None, 5, 0, [](auto&, auto& b) { ++b.timeMs; b.meters[0].resetMs -= 999; }},
            {"OpenCode Go first usage anchors rolling deadline", HistoryBreak::None, 5, 0, [](auto&, auto& b) { b.meters[0].value = 1; }},
            {"OpenCode Go anchor midway in poll", HistoryBreak::None, 5, 0, [](auto& a, auto& b) { b.meters[0].value = 1; b.meters[0].resetMs = a.meters[0].resetMs + 150000; }},
            {"OpenCode Go anchor late in poll", HistoryBreak::None, 5, 0, [](auto& a, auto& b) { b.meters[0].value = 1; b.meters[0].resetMs = a.meters[0].resetMs + 240000; }},
            {"OpenCode Go used 5-hour quota is not idle", HistoryBreak::Reset, 5, 0, [](auto& a, auto&) { a.meters[0].value = 1; }},
            {"OpenCode Go arbitrary forward deadline jump", HistoryBreak::Reset, 5, 0, [](auto&, auto& b) { b.meters[0].resetMs += hour; }},
            {"OpenCode Go backward deadline jump", HistoryBreak::Reset, 5, 0, [](auto& a, auto& b) { b.meters[0].resetMs = a.meters[0].resetMs - 300000; }},
            {"OpenCode Go rolling partial window not guessed", HistoryBreak::Reset, 5, 0, [](auto& a, auto& b) { a.meters[0].resetMs -= hour; b.meters[0].resetMs -= hour; }},
            {"OpenCode Go anchored zero cannot discard old window", HistoryBreak::Reset, 5, 0, [](auto& a, auto&) { a.meters[0].resetMs -= 600000; }},
            {"OpenCode Go real zero-to-zero cycle boundary", HistoryBreak::Reset, 5, 0, [](auto& a, auto&) { a.meters[0].resetMs = a.timeMs + 180000; }},
            {"OpenCode Go real exhausted-to-zero reset", HistoryBreak::Reset, 5, 0, [](auto& a, auto&) { a.meters[0].resetMs = a.timeMs + 180000; a.meters[0].value = 100; }},
            {"OpenCode Go session break", HistoryBreak::Gap, 5, 0, [](auto&, auto& b) { b.session = 2; }},
            {"OpenCode Go large gap", HistoryBreak::Gap, 5, 0, [](auto&, auto& b) { b.timeMs += 900000; b.meters[0].resetMs += 900000; }},
            {"OpenCode Go clock rollback", HistoryBreak::Gap, 5, 0, [](auto& a, auto& b) { b.timeMs = a.timeMs - 1; }},
            {"OpenCode Go weekly lane does not roll", HistoryBreak::Reset, 5, 1, [](auto&, auto& b) { ++b.timeMs; }},
            {"OpenCode Go weekly deadline jitter", HistoryBreak::None, 5, 1, [](auto& a, auto& b) { a.meters[1].value = 50; b.meters[1].value = 51; b.meters[1].resetMs = a.meters[1].resetMs + 1000; }},
            {"OpenCode Go monthly lane does not roll", HistoryBreak::Reset, 5, 3, [](auto&, auto& b) { ++b.timeMs; }},
            {"OpenCode Go monthly deadline jitter", HistoryBreak::None, 5, 3, [](auto& a, auto& b) { a.meters[3].value = 50; b.meters[3].value = 51; b.meters[3].resetMs = a.meters[3].resetMs + 1000; }},
            {"OpenAI's 5-hour lane rolls too", HistoryBreak::None, 2, 0, [](auto&, auto& b) { ++b.timeMs; }},
            {"Anthropic's 5-hour lane does not roll", HistoryBreak::Reset, 1, 0, [](auto&, auto& b) { ++b.timeMs; }},
            {"GitHub Copilot's monthly lane does not roll", HistoryBreak::Reset, 4, 3, [](auto&, auto& b) { ++b.timeMs; }},
        };
        for (const auto& test : cases) Run(std::string("continuity/") + test.name, [&] {
            auto [a, b] = rolling(test.provider, test.lane);
            test.observe(a, b);
            CHECK(HistoryConnection(a, b, test.lane) == test.expected);
        });
    }
// Anthropic (provider 1) extra usage, its budget % (lane 3) and dollars (lane 6): the provider
// reports no window, so readings are recorded without one and belong to the calendar month (UTC)
// they were taken in. baseTime is 2023-11-14 22:13:20 UTC; November ends at 1701388800000, 30
// days long.
    {
        static constexpr ULONGLONG november = 1701388800000ULL, january = 1704067200000ULL;
        // `reported`: with a window as a provider would report it.
        auto reading = [](ULONGLONG time, bool reported = false) {
            HistorySample sample{};
            sample.accountId = 1; sample.provider = 1; sample.timeMs = time;
            sample.intervalMs = 300000; sample.session = 1;
            sample.meters[3] = {40, reported ? november : 0, reported ? 30 * kHistoryDayMs : 0};
            sample.meters[6] = {400, reported ? november : 0, 0};
            return sample;
        };
        Run("continuity/calendar months in UTC: their ends and lengths", [&] {
            auto month = [](ULONGLONG time, ULONGLONG end, int days) {
                UtcMonth m = UtcMonthOf(time);
                CHECK(m.endMs == end); CHECK(m.durationMs == days * kHistoryDayMs);
            };
            month(baseTime, november, 30);
            // A month's last millisecond, and the first of the next: December, ending with the year.
            month(november - 1, november, 30);
            month(november, january, 31);
            // 2024-02-10: a leap February, ending 2024-03-01.
            month(1707523200000ULL, 1709251200000ULL, 29);
        });
        Run("continuity/Anthropic extra usage without a window belongs to its calendar month, exactly", [&] {
            for (int lane : {3, 6}) {
                // Within November: one window, up to the month's last millisecond (a calendar month
                // has no reset jitter to allow for).
                CHECK(HistoryConnection(reading(baseTime), reading(baseTime + 300000), lane) == HistoryBreak::None);
                CHECK(HistoryConnection(reading(november - 300000), reading(november - 60000), lane) == HistoryBreak::None);
                CHECK(HistoryConnection(reading(november - 300000), reading(november - 1), lane) == HistoryBreak::None);
                // Across the month's end, or with the later reading at December's first instant: a
                // reset. Within December again: one window, from its first instant on.
                CHECK(HistoryConnection(reading(november - 150000), reading(november + 150000), lane) == HistoryBreak::Reset);
                CHECK(HistoryConnection(reading(november - 300000), reading(november), lane) == HistoryBreak::Reset);
                CHECK(HistoryConnection(reading(november), reading(november + 300000), lane) == HistoryBreak::None);
                CHECK(HistoryConnection(reading(january - 300000), reading(january - 1), lane) == HistoryBreak::None);
                // A window a provider did report keeps the reported windows' rules: beside a reading
                // without one it is another window, and its own end has its two minutes of jitter.
                CHECK(HistoryConnection(reading(baseTime, true), reading(baseTime + 300000), lane) == HistoryBreak::Reset);
                CHECK(HistoryConnection(reading(baseTime), reading(baseTime + 300000, true), lane) == HistoryBreak::Reset);
                CHECK(HistoryConnection(reading(baseTime, true), reading(baseTime + 300000, true), lane) == HistoryBreak::None);
                CHECK(HistoryConnection(reading(november - 300000, true), reading(november - 60000, true), lane) ==
                      HistoryBreak::Reset);
            }
            // Only Anthropic's extra usage: its other lanes and other providers' monthly lanes keep a
            // missing window as none, whatever the month.
            HistorySample weekly = reading(november - 150000), next = reading(november + 150000);
            weekly.meters[1] = next.meters[1] = {40, 0, 0};
            CHECK(HistoryConnection(weekly, next, 1) == HistoryBreak::None);
            HistorySample copilot = reading(november - 150000), later = reading(november + 150000);
            copilot.provider = later.provider = 4;
            CHECK(HistoryConnection(copilot, later, 3) == HistoryBreak::None);
            CHECK(HistoryCalendarMonth(reading(baseTime), 3) && HistoryCalendarMonth(reading(baseTime), 6) &&
                  !HistoryCalendarMonth(reading(baseTime, true), 3) && !HistoryCalendarMonth(weekly, 1) &&
                  !HistoryCalendarMonth(copilot, 3));
        });
    }
