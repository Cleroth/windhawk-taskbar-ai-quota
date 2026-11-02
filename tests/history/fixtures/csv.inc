// The CSV writer (history/csv.h) through the classic window's adapter (HistoryCsvFile::
// WriteSnapshot) and directly with a frozen export scope: hand-derived bytes, lanes per provider,
// the scope's bounds and account, 64 KiB pieces, cancellation and a failing sink. Memory sinks
// only; no file is written.
{
    // Anthropic with every kind of value, a failed OpenAI poll, Antigravity with a zero reading,
    // an unknown provider (every lane), then rows the scope leaves out: no account, before its
    // start, at its end.
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
    c.meters[4].value = 99.9;
    HistorySample d = sample(0xD4, 7, baseTime + 4000, 0, 1);
    d.meters[6].value = 0.1;
    HistorySample unowned = sample(0, 1, baseTime + 5000, 60000, 5), before = a, atEnd = a;
    before.timeMs = baseTime - 1;
    before.meters[1].value = 51;
    atEnd.timeMs = baseTime + kHistoryWeekMs;
    atEnd.meters[1].value = 52;
    // In time order, as snapshots hold them.
    const std::vector<HistorySample> samples = {before, a, b, c, d, unowned, atEnd};

    const std::string header = "timestamp_utc,timestamp_unix_ms,account_id,provider,provider_id,meter_id,unit,value,"
                               "reset_utc,reset_unix_ms,duration_ms,poll_interval_ms,session_id,failed,missing\r\n";
    const std::string at = "2023-11-14T22:13:21.000Z,1700000001000,account_00000000000000a1,anthropic,1,";
    const std::string bt = "2023-11-14T22:13:22.000Z,1700000002000,account_000000000000000b,openai,2,";
    const std::string ct = "2023-11-14T22:13:23.000Z,1700000003000,account_00000000000000c3,antigravity,3,";
    const std::string dt = "2023-11-14T22:13:24.000Z,1700000004000,account_00000000000000d4,unknown,7,";
    const std::string accountA =
        at + "short,percent,12.5,2023-11-15T03:13:20.000Z,1700018000000,18000000,60000,session_0000000000000005,0,0\r\n" +
        at + "weekly,percent,40,,0,604800000,60000,session_0000000000000005,0,0\r\n" +
        at + "fable_weekly,percent,,,0,0,60000,session_0000000000000005,0,1\r\n" +
        at + "extra_percent,percent,7.25,,0,0,60000,session_0000000000000005,0,0\r\n" +
        at + "anthropic_extra_used,USD,3.5,,0,0,60000,session_0000000000000005,0,0\r\n";
    const std::string all = header + accountA +
        bt + "short,percent,,,0,0,300000,session_0000000000000000,1,1\r\n" +
        bt + "weekly,percent,,2023-11-17T22:13:20.000Z,1700259200000,604800000,300000,session_0000000000000000,1,1\r\n" +
        bt + "openai_credit_balance,credits,,,0,0,300000,session_0000000000000000,1,1\r\n" +
        ct + "short,percent,0,,0,0,120000,session_000000000000002a,0,0\r\n" +
        ct + "weekly,percent,,,0,0,120000,session_000000000000002a,0,1\r\n" +
        ct + "third_party_short,percent,99.9,,0,0,120000,session_000000000000002a,0,0\r\n" +
        ct + "third_party_weekly,percent,,,0,0,120000,session_000000000000002a,0,1\r\n" +
        dt + "short,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "weekly,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "fable_weekly,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "extra_percent,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "third_party_short,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "third_party_weekly,percent,,,0,0,0,session_0000000000000001,0,1\r\n" +
        dt + "anthropic_extra_used,USD,0.1,,0,0,0,session_0000000000000001,0,0\r\n" +
        dt + "openai_credit_balance,credits,,,0,0,0,session_0000000000000001,0,1\r\n";

    // The classic window's state over the same samples, All accounts or the Anthropic account.
    auto classic = [&](int selected) {
        HistoryWindowState state;
        state.readFrom = baseTime;
        state.to = baseTime + kHistoryWeekMs;
        state.snapshot.samples = samples;
        HistoryUiAccount account;
        account.id = 0xA1;
        account.provider = 1;
        state.accounts.push_back(account);
        state.selected = selected;
        return state;
    };
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
        "scope's bounds, through the classic adapter and the frozen-scope writer alike", [&] {
        for (int selected : {-1, 0}) {
            const std::string expected = selected < 0 ? all : header + accountA;
            HistoryWindowState state = classic(selected);
            CapturedCsv csv;
            size_t count = 0;
            CHECK(csv.WriteSnapshot(state, &count));
            CHECK(count == (selected < 0 ? 20u : 5u));
            CHECK(csv.bytes == expected);
            Pieces pieces;
            size_t rows = 0;
            CHECK(direct(scope(selected < 0 ? 0 : 0xA1), rows, pieces, Never) == HistoryCsvEnd::Written);
            CHECK(rows == count && pieces.pieces.size() == 1 && pieces.lasts[0] && pieces.Joined() == expected);
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

    Run("csv: a large export goes out in pieces of at least 64 KiB, then a shorter last one; the classic adapter "
        "writes the same bytes and shows its progress between pieces only", [&] {
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
        HistoryWindowState state = classic(-1);
        state.snapshot.samples = many;
        state.status = FakeHandle(9);
        CapturedCsv csv;
        size_t count = 0;
        CHECK(csv.WriteSnapshot(state, &count) && count == rows && csv.bytes == pieces.Joined());
        CHECK(controls[state.status].text.starts_with(L"Exporting ") && controls[state.status].text.ends_with(L" rows..."));
    });

    Run("csv: cancellation stops between samples (the classic adapter reports ERROR_CANCELLED); a failing sink "
        "stops the writer", [&] {
        int checks = 0;
        Pieces pieces;
        size_t rows = 0;
        // Checked before each sample: the one before the scope, A (five rows), B (three), then C stops.
        CHECK(direct(scope(0), rows, pieces, [&] { return ++checks > 3; }) == HistoryCsvEnd::Cancelled);
        CHECK(pieces.pieces.empty() && rows == 8 && checks == 4);
        g_unloading = true;
        HistoryWindowState state = classic(-1);
        CapturedCsv csv;
        size_t count = 0;
        SetLastError(0);
        CHECK(!csv.WriteSnapshot(state, &count) && GetLastError() == ERROR_CANCELLED && csv.bytes.empty());
        g_unloading = false;
        int calls = 0;
        CHECK(HistoryCsvWrite(scope(0), rows, [&](const std::string&, bool) { return ++calls > 1; }, Never) ==
              HistoryCsvEnd::Failed);
        CHECK(calls == 1);
    });
}
