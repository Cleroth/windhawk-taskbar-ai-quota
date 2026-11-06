// The CSV writer (history/csv.h) with frozen export scopes: hand-derived bytes, lanes per
// provider, the scope's bounds and account, 64 KiB pieces, cancellation and a failing sink.
// Memory sinks only; no file is written.
{
    // Anthropic with every kind of value, a failed OpenAI poll, Antigravity with a zero reading
    // and its Gemini Argon weekly in the Fable weekly lane, GitHub Copilot (its AI credits lane only),
    // OpenCode Go (its 5-hour, weekly and monthly lanes only, the monthly in the extra percentage
    // lane; a poll, then a failed one), an unknown provider (every lane), then rows the scope leaves
    // out: no account, before its start, at its end.
    auto sample = [](uint64_t account, uint32_t provider, ULONGLONG time, ULONGLONG interval, uint64_t session) {
        HistorySample s;
        s.accountId = account;
        s.provider = provider;
        s.timeMs = time;
        s.intervalMs = interval;
        s.session = session;
        return s;
    };
    HistorySample a = sample(0xA1, 1, baseTime + 1000, 60000, 5);
    a.meters[0] = {12.5, baseTime + 5 * hour, 5 * hour};
    a.meters[1] = {40, 0, kHistoryWeekMs};
    a.meters[3].value = 7.25;
    a.meters[6].value = 3.5;
    HistorySample b = sample(0x0B, 2, baseTime + 2000, 300000, 0);
    b.failed = 1;
    b.meters[0].value = 10;
    b.meters[1] = {20, baseTime + 3 * kHistoryDayMs, kHistoryWeekMs};
    b.meters[7].value = 800;
    HistorySample c = sample(0xC3, 3, baseTime + 3000, 120000, 0x2A);
    c.meters[0].value = 0;
    c.meters[2] = {65, baseTime + 2 * kHistoryDayMs, kHistoryWeekMs};
    c.meters[4].value = 99.9;
    HistorySample e = sample(0xE5, 4, baseTime + 3500, 600000, 9);
    e.meters[0].value = 11;  // Not a Copilot lane: never exported.
    e.meters[3] = {33.25, baseTime + 30 * kHistoryDayMs, 30 * kHistoryDayMs};
    HistorySample f = sample(0xF6, 5, baseTime + 3600, 300000, 0x0C);
    f.meters[0] = {0, baseTime + 5 * hour, 5 * hour};
    f.meters[1] = {42.5, baseTime + 4 * kHistoryDayMs, kHistoryWeekMs};
    f.meters[3] = {88, baseTime + 20 * kHistoryDayMs, 30 * kHistoryDayMs};
    // Not OpenCode Go lanes: never exported.
    f.meters[2].value = 7;
    f.meters[4].value = 8;
    f.meters[5].value = 9;
    f.meters[6].value = 1.5;
    f.meters[7].value = 100;
    HistorySample g = sample(0xF6, 5, baseTime + 3700, 300000, 0x0C);
    g.failed = 1;
    g.meters[0].value = 5;
    g.meters[1] = {43, baseTime + 4 * kHistoryDayMs, kHistoryWeekMs};
    g.meters[2].value = 7;
    HistorySample d = sample(0xD4, 7, baseTime + 4000, 0, 1);
    d.meters[6].value = 0.1;
    HistorySample unowned = sample(0, 1, baseTime + 5000, 60000, 5), before = a, atEnd = a;
    before.timeMs = baseTime - 1;
    before.meters[1].value = 51;
    atEnd.timeMs = baseTime + kHistoryWeekMs;
    atEnd.meters[1].value = 52;
    // In time order, as snapshots hold them.
    const std::vector<HistorySample> samples = {before, a, b, c, e, f, g, d, unowned, atEnd};

    const std::string header = "timestamp_utc,timestamp_unix_ms,account_id,provider,provider_id,meter_id,unit,value,"
                               "reset_utc,reset_unix_ms,duration_ms,poll_interval_ms,session_id,failed,missing\r\n";
    const std::string at = "2023-11-14T22:13:21.000Z,1700000001000,account_00000000000000a1,anthropic,1,";
    const std::string bt = "2023-11-14T22:13:22.000Z,1700000002000,account_000000000000000b,openai,2,";
    const std::string ct = "2023-11-14T22:13:23.000Z,1700000003000,account_00000000000000c3,antigravity,3,";
    const std::string et = "2023-11-14T22:13:23.500Z,1700000003500,account_00000000000000e5,copilot,4,";
    const std::string ft = "2023-11-14T22:13:23.600Z,1700000003600,account_00000000000000f6,opencode,5,";
    const std::string gt = "2023-11-14T22:13:23.700Z,1700000003700,account_00000000000000f6,opencode,5,";
    const std::string dt = "2023-11-14T22:13:24.000Z,1700000004000,account_00000000000000d4,unknown,7,";
    const std::string accountA =
        at + "short,percent,12.5,2023-11-15T03:13:20.000Z,1700018000000,18000000,60000,session_0000000000000005,0,0\r\n" +
        at + "weekly,percent,40,,0,604800000,60000,session_0000000000000005,0,0\r\n" +
        at + "fable_weekly,percent,,,0,0,60000,session_0000000000000005,0,1\r\n" +
        at + "extra_percent,percent,7.25,,0,0,60000,session_0000000000000005,0,0\r\n" +
        at + "anthropic_extra_used,USD,3.5,,0,0,60000,session_0000000000000005,0,0\r\n";
    const std::string accountF =
        ft + "short,percent,0,2023-11-15T03:13:20.000Z,1700018000000,18000000,300000,session_000000000000000c,0,0\r\n" +
        ft + "weekly,percent,42.5,2023-11-18T22:13:20.000Z,1700345600000,604800000,300000,session_000000000000000c,0,0\r\n" +
        ft + "monthly,percent,88,2023-12-04T22:13:20.000Z,1701728000000,2592000000,300000,session_000000000000000c,0,0\r\n" +
        gt + "short,percent,,,0,0,300000,session_000000000000000c,1,1\r\n" +
        gt + "weekly,percent,,2023-11-18T22:13:20.000Z,1700345600000,604800000,300000,session_000000000000000c,1,1\r\n" +
        gt + "monthly,percent,,,0,0,300000,session_000000000000000c,1,1\r\n";
    const std::string all = header + accountA +
        bt + "short,percent,,,0,0,300000,session_0000000000000000,1,1\r\n" +
        bt + "weekly,percent,,2023-11-17T22:13:20.000Z,1700259200000,604800000,300000,session_0000000000000000,1,1\r\n" +
        bt + "openai_credit_balance,credits,,,0,0,300000,session_0000000000000000,1,1\r\n" +
        ct + "short,percent,0,,0,0,120000,session_000000000000002a,0,0\r\n" +
        ct + "weekly,percent,,,0,0,120000,session_000000000000002a,0,1\r\n" +
        ct + "argon_weekly,percent,65,2023-11-16T22:13:20.000Z,1700172800000,604800000,120000,session_000000000000002a,0,0\r\n" +
        ct + "third_party_short,percent,99.9,,0,0,120000,session_000000000000002a,0,0\r\n" +
        ct + "third_party_weekly,percent,,,0,0,120000,session_000000000000002a,0,1\r\n" +
        et + "extra_percent,percent,33.25,2023-12-14T22:13:20.000Z,1702592000000,2592000000,600000,session_0000000000000009,0,0\r\n" +
        accountF +
        dt + "short,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "weekly,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "fable_weekly,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "extra_percent,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "third_party_short,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "third_party_weekly,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "anthropic_extra_used,USD,0.1,,0,0,0,session_0000000000000001,0,0\r\n" +
        dt + "openai_credit_balance,credits,,,0,0,0,session_0000000000000001,0,1\r\n";

    auto scope = [&](uint64_t account) {
        HistoryExportScope s;
        s.snapshot = std::make_shared<HistorySnapshot>(HistorySnapshot{samples});
        s.accountId = account;
        s.from = baseTime;
        s.to = baseTime + kHistoryWeekMs;
        return s;
    };
    // The writer's pieces with their `last` flags.
    struct Pieces {
        std::vector<std::string> pieces;
        std::vector<bool> lasts;
        std::string Joined() const {
            std::string joined;
            for (const std::string& piece : pieces) joined += piece;
            return joined;
        }
    };
    auto direct = [](const HistoryExportScope& s, size_t& rows, Pieces& out, const std::function<bool()>& cancelled) {
        return HistoryCsvWrite(s, rows, [&](const std::string& piece, bool last) {
            out.pieces.push_back(piece);
            out.lasts.push_back(last);
            return true;
        }, cancelled);
    };

    Run("csv: hand-derived bytes for every provider's lanes, missing and failed readings, reset times and the "
        "scope's bounds and account", [&] {
        for (uint64_t account : {0x0ULL, 0xA1ULL, 0xF6ULL}) {
            const std::string expected = !account ? all : header + (account == 0xA1 ? accountA : accountF);
            Pieces pieces;
            size_t rows = 0;
            CHECK(direct(scope(account), rows, pieces, Never) == HistoryCsvEnd::Written);
            CHECK(rows == (!account ? 28u : account == 0xA1 ? 5u : 6u) && pieces.pieces.size() == 1 && pieces.lasts[0] &&
                  pieces.Joined() == expected);
        }
        // A scope from its start (inclusive) to its end (exclusive): the earlier and the last
        // sample in once the bounds move to them.
        HistoryExportScope wider = scope(0xA1);
        wider.from = baseTime - 1;
        wider.to = baseTime + kHistoryWeekMs + 1;
        Pieces pieces;
        size_t rows = 0;
        CHECK(direct(wider, rows, pieces, Never) == HistoryCsvEnd::Written && rows == 15);
        CHECK(pieces.Joined().find("1699999999999,account_00000000000000a1") != std::string::npos);
        CHECK(pieces.Joined().find("weekly,percent,52,") != std::string::npos);
        // An empty scope: the header alone, as the last piece.
        HistoryExportScope none = scope(0x999);
        Pieces empty;
        CHECK(direct(none, rows, empty, Never) == HistoryCsvEnd::Written && rows == 0);
        CHECK(empty.pieces == std::vector<std::string>{header} && empty.lasts[0]);
    });

    Run("csv: the history model names meters in its query's language while the CSV keeps its English meter IDs and "
        "units (German: Anthropic's 5-hour, weekly, extra budget and dollars, OpenCode Go's monthly)", [&] {
        std::vector<HistoryUiAccount> accounts(2);
        accounts[0].id = 0xA1;
        accounts[0].provider = 1;
        accounts[0].label = L"CC";
        accounts[1].id = 0xF6;
        accounts[1].provider = 5;
        accounts[1].label = L"Go";
        for (size_t row = 0; row < samples.size(); row++) {
            for (HistoryUiAccount& account : accounts) {
                if (samples[row].accountId == account.id) account.rows.push_back(row);
            }
        }
        HistorySeriesQuery query{-1, HistoryView::AllMeters, baseTime, baseTime + kHistoryWeekMs, baseTime, false, 90,
                                 false, I18nLocale(*I18nLanguageByTag(L"de"))};
        HistorySeriesSet german = HistoryBuildSeries(samples, accounts, query, Never);
        CHECK(Meters(german, 0) == (std::set<int>{0, 1, 3, 6}) && Meters(german, 1) == (std::set<int>{0, 1, 3}));
        CHECK(Series(german, 0, 0).legendTitle == L"CC \u00B7 5\u00A0Std." &&
              Series(german, 0, 0).hoverTitle == L"CC \u00B7 5\u00A0Std. verbraucht (%)");
        CHECK(Series(german, 0, 1).hoverTitle == L"CC \u00B7 W\u00F6chentlich verbraucht (%)");
        CHECK(Series(german, 0, 3).hoverTitle == L"CC \u00B7 Zusatzbudget verbraucht (%)");
        CHECK(Series(german, 0, 6).hoverTitle == L"CC \u00B7 Zusatzausgaben (USD)");
        CHECK(Series(german, 1, 3).hoverTitle == L"Go \u00B7 Monatlich verbraucht (%)");
        query.locale = {};
        CHECK(Series(HistoryBuildSeries(samples, accounts, query, Never), 0, 3).hoverTitle ==
              L"CC \u00B7 Extra budget used (%)");
        // The writer takes no language: the same scopes write the same English bytes.
        for (uint64_t account : {0xA1ULL, 0xF6ULL}) {
            Pieces pieces;
            size_t rows = 0;
            CHECK(direct(scope(account), rows, pieces, Never) == HistoryCsvEnd::Written);
            CHECK(pieces.Joined() == header + (account == 0xA1 ? accountA : accountF));
        }
        CHECK(accountA.find("extra_percent,percent,7.25,") != std::string::npos &&
              accountA.find("anthropic_extra_used,USD,3.5,") != std::string::npos &&
              accountF.find("monthly,percent,88,") != std::string::npos);
    });

    Run("csv: a regional number format changes no byte: the history model built under German with de-DE's "
        "decimal comma beside the same scopes' English bytes, values with their point", [&] {
        std::vector<HistoryUiAccount> accounts(1);
        accounts[0].id = 0xA1;
        accounts[0].provider = 1;
        accounts[0].label = L"CC";
        for (size_t row = 0; row < samples.size(); row++) {
            if (samples[row].accountId == 0xA1) accounts[0].rows.push_back(row);
        }
        HistorySeriesQuery query{-1, HistoryView::AllMeters, baseTime, baseTime + kHistoryWeekMs, baseTime, false, 90,
                                 false, I18nLocale(*I18nLanguageByTag(L"de"), I18nNumberFormatFrom(L",", L".", L"3;0"))};
        HistorySeriesSet regional = HistoryBuildSeries(samples, accounts, query, Never);
        CHECK(Series(regional, 0, 0).legendTitle == L"CC \u00B7 5\u00A0Std.");
        for (uint64_t account : {0xA1ULL, 0xF6ULL}) {
            Pieces pieces;
            size_t rows = 0;
            CHECK(direct(scope(account), rows, pieces, Never) == HistoryCsvEnd::Written);
            CHECK(pieces.Joined() == header + (account == 0xA1 ? accountA : accountF));
        }
        CHECK(accountA.find(",12.5,") != std::string::npos && accountA.find(",7.25,") != std::string::npos &&
              accountF.find(",42.5,") != std::string::npos);
    });

    Run("csv: a large export goes out in pieces of at least 64 KiB, then a shorter last one", [&] {
        std::vector<HistorySample> many;
        for (int i = 0; i < 3000; i++) {
            HistorySample s = a;
            s.timeMs = baseTime + 1000 + ULONGLONG(i) * 60000;
            many.push_back(s);
        }
        HistoryExportScope big = scope(0);
        big.snapshot = std::make_shared<HistorySnapshot>(HistorySnapshot{many});
        Pieces pieces;
        size_t rows = 0;
        CHECK(direct(big, rows, pieces, Never) == HistoryCsvEnd::Written && rows == 15000);
        CHECK(pieces.pieces.size() > 3);
        for (size_t i = 0; i + 1 < pieces.pieces.size(); i++) {
            CHECK(pieces.pieces[i].size() >= 65536 && !pieces.lasts[i]);
        }
        CHECK(pieces.pieces.back().size() < 65536 + 1000 && pieces.lasts.back());
    });

    Run("csv: cancellation stops between samples; a failing sink stops the writer", [&] {
        int checks = 0;
        Pieces pieces;
        size_t rows = 0;
        // Checked before each sample: the one before the scope, A (five rows), B (three), then C stops.
        CHECK(direct(scope(0), rows, pieces, [&] { return ++checks > 3; }) == HistoryCsvEnd::Cancelled);
        CHECK(pieces.pieces.empty() && rows == 8 && checks == 4);
        int calls = 0;
        CHECK(HistoryCsvWrite(scope(0), rows, [&](const std::string&, bool) { return ++calls > 1; }, Never) ==
              HistoryCsvEnd::Failed);
        CHECK(calls == 1);
    });
}
