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
