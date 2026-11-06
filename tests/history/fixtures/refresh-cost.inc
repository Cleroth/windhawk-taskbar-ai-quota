// An observation of the history window's refresh at the 250,000-sample cap: the peek's copy of the
// journal's memory from the range's start, the sort check and sort, account collection, series
// analysis and reduction, each timed (one OBSERVE line, no limits). The journal's peek itself is
// tested on synthetic files by the journal target (journal-window.inc).
    Run("history/refresh cost at the sample cap (observation)", [&] {
        // Five accounts (Anthropic, OpenAI, Antigravity, Anthropic, OpenAI) polled in turn over 90
        // days, every meter of their provider recorded.
        constexpr size_t count = 250000;
        constexpr ULONGLONG span = 90 * kHistoryDayMs;
        std::deque<HistorySample> memory;
        for (size_t i = 0; i < count; ++i) {
            HistorySample sample{};
            sample.accountId = 1 + i % 5;
            sample.provider = uint32_t(1 + (i % 5) % 3);
            sample.timeMs = baseTime + ULONGLONG(i) * span / count;
            sample.intervalMs = 600000;
            sample.session = 1;
            for (int meter = 0; meter < int(kHistoryMeterCount); ++meter) {
                sample.meters[meter].value = double((i / 5 + size_t(meter) * 37) % 1000) / 10;
            }
            memory.push_back(sample);
        }
        std::vector<AccountConfig> configured;
        for (uint64_t id = 1; id <= 5; ++id) {
            AccountConfig account;
            account.provider = (id - 1) % 3 == 0 ? L"anthropic" : (id - 1) % 3 == 1 ? L"openai" : L"antigravity";
            account.label = L"account " + std::to_wstring(id);
            account.historyId = id;
            configured.push_back(account);
        }
        using Clock = std::chrono::steady_clock;
        auto ms = [](Clock::time_point a, Clock::time_point b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        auto t0 = Clock::now();
        std::vector<HistorySample> samples;
        for (const auto& sample : memory) {
            if (sample.timeMs >= baseTime) samples.push_back(sample);
        }
        auto t1 = Clock::now();
        auto earlier = [](const HistorySample& a, const HistorySample& b) { return a.timeMs < b.timeMs; };
        bool sorted = std::is_sorted(samples.begin(), samples.end(), earlier);
        auto t2 = Clock::now();
        std::stable_sort(samples.begin(), samples.end(), earlier);
        auto t3 = Clock::now();
        std::vector<HistoryUiAccount> accounts = HistoryCollectAccounts(configured, samples, baseTime, baseTime + span,
                                                                        Never);
        auto t4 = Clock::now();
        HistorySeriesQuery all{-1, HistoryView::AllMeters, baseTime, baseTime + span, baseTime, false, 90};
        HistorySeriesSet series = HistoryBuildSeries(samples, accounts, all, Never);
        auto t5 = Clock::now();
        HistorySeriesQuery weekly = all;
        weekly.view = HistoryView::Weekly;
        HistorySeriesSet weeklySeries = HistoryBuildSeries(samples, accounts, weekly, Never);
        auto t6 = Clock::now();
        size_t points = 0;
        for (const HistorySeries& line : series.series) {
            points += HistoryReduceRows(samples, accounts[line.account].rows, line.meter, baseTime, baseTime + span,
                                        1600, Never).points.size();
        }
        auto t7 = Clock::now();
        std::cout << "OBSERVE refresh-cost samples=" << samples.size() << " copy_ms=" << ms(t0, t1)
                  << " sorted_check_ms=" << ms(t1, t2) << " stable_sort_ms=" << ms(t2, t3)
                  << " collect_ms=" << ms(t3, t4) << " series_all_meters_ms=" << ms(t4, t5) << " ("
                  << series.series.size() << " series) series_weekly_ms=" << ms(t5, t6) << " ("
                  << weeklySeries.series.size() << " series) reduce_ms=" << ms(t6, t7) << " (" << points
                  << " points at 1600 px)\n";
        CHECK(sorted && samples.size() == count && accounts.size() == 5 && !series.series.empty() && points > 0);
    });
