// The journal's memory-only parts (history/journal.h), on synthetic in-memory state: the history
// window's peek (PeekHistorySnapshot), the status notifier (SetHistoryStatus) and an account
// deletion's memory step (EraseHistorySamplesLocked) with what its readers then see. Nothing that
// loads or writes storage is extracted, so the peek compiles here only while it never loads.
// Then an observation of a refresh's cost at the 250,000-sample cap: the peek's copy, the sort,
// account collection and series analysis, and reduction, each timed (OBSERVE lines, no limits).
    {
        auto at = [](int i) { return baseTime + ULONGLONG(i) * hour; };
        // A loaded journal: account 7's readings at hours 0-4, epoch 3, revision 5, 7 appended.
        auto journal = [&] {
            g_historySamples.clear();
            for (int i = 0; i < 5; ++i) {
                HistorySample sample{};
                sample.accountId = 7; sample.provider = 1; sample.timeMs = at(i); sample.intervalMs = hour;
                sample.meters[1].value = 10.0 * i;
                g_historySamples.push_back(sample);
            }
            g_historyLoaded = true;
            g_historyEpoch = 3; g_historyRevision = 5; g_historyAppendCount = 7;
            g_historyStatus.clear(); g_historyCapped = false;
            return HistoryJournalStamp{true, 3, 5, 7};
        };
        auto times = [](const std::vector<HistorySample>& samples) {
            std::vector<ULONGLONG> result;
            for (const auto& sample : samples) result.push_back(sample.timeMs);
            return result;
        };

        Run("journal/peek: the held stamp from the same or a later start replaces nothing; another stamp or an "
            "earlier start replaces with every sample from the start", [&] {
            const HistoryJournalStamp held = journal();
            CHECK(PeekHistorySnapshot(at(2), true, held, at(2)).stamp == held);
            CHECK(!PeekHistorySnapshot(at(2), true, held, at(2)).samples);
            CHECK(!PeekHistorySnapshot(at(3), true, held, at(2)).samples);
            auto earlier = PeekHistorySnapshot(at(1), true, held, at(2));
            CHECK(earlier.samples && (times(*earlier.samples) == std::vector<ULONGLONG>{at(1), at(2), at(3), at(4)}));
            for (int field = 0; field < 4; ++field) {
                HistoryJournalStamp other = held;
                if (field == 0) other.loaded = false;
                if (field == 1) other.epoch = 2;
                if (field == 2) other.revision = 4;
                if (field == 3) other.appended = 6;
                auto peek = PeekHistorySnapshot(at(3), true, other, at(3));
                CHECK(peek.stamp == held && peek.samples &&
                      (times(*peek.samples) == std::vector<ULONGLONG>{at(3), at(4)}));
            }
            // Status and stamp only: never samples.
            CHECK(!PeekHistorySnapshot(at(0), false, {}, ULLONG_MAX).samples);
        });
        Run("journal/peek: an empty replacement differs from none; unloaded storage replaces nothing but "
            "reports its stamp, status and cap", [&] {
            HistoryJournalStamp held = journal();
            held.appended = 6;
            auto none = PeekHistorySnapshot(at(9), true, held, at(9));
            CHECK(none.samples && none.samples->empty());
            // Clear: another epoch, no samples.
            const HistoryJournalStamp before = journal();
            g_historySamples.clear();
            g_historyEpoch = 4;
            auto cleared = PeekHistorySnapshot(at(0), true, before, at(0));
            CHECK(cleared.samples && cleared.samples->empty() && cleared.stamp.epoch == 4);
            // Unloaded (ownership lost): no replacement whatever was held.
            journal();
            g_historyLoaded = false;
            g_historyStatus = L"History storage is in use by another Explorer session, or is inaccessible.";
            g_historyCapped = true;
            auto unloaded = PeekHistorySnapshot(at(0), true, {}, ULLONG_MAX);
            CHECK(!unloaded.samples && !unloaded.stamp.loaded && unloaded.stamp.revision == 5 &&
                  unloaded.status == g_historyStatus && unloaded.capped);
        });
        Run("journal/peek: memory only, under the history lock, changing nothing and notifying nothing", [&] {
            const HistoryJournalStamp held = journal();
            g_historyStatus = L"Damaged history record. Export readable samples or clear history.";
            settingsNotifications = 0;
            std::atomic<bool> done{false};
            bool waited = false;
            std::thread reader;
            {
                std::lock_guard lock(g_historyMutex);
                reader = std::thread([&] {
                    auto peek = PeekHistorySnapshot(at(0), true, {}, ULLONG_MAX);
                    done = peek.samples && peek.samples->size() == 5;
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                waited = !done;  // Waiting for the lock this thread holds
            }
            reader.join();
            CHECK(waited && done);
            CHECK(g_historyMutex.try_lock());
            g_historyMutex.unlock();
            auto peek = PeekHistorySnapshot(at(0), true, {}, ULLONG_MAX);
            CHECK(peek.stamp == held && peek.status == g_historyStatus && g_historyLoaded && g_historyRevision == 5 &&
                  g_historySamples.size() == 5 && settingsNotifications == 0);
        });
        Run("journal/status: every change of the text notifies once, an unchanged text doesn't; the storage "
            "error flag follows", [&] {
            g_historyStatus.clear();
            g_historyStorageError = false;
            settingsNotifications = 0;
            SetHistoryStatus({});
            CHECK(settingsNotifications == 0 && !g_historyStorageError);
            SetHistoryStatus(L"History write failed; the previous journal was kept.");
            CHECK(settingsNotifications == 1 && g_historyStorageError);
            SetHistoryStatus(L"History write failed; the previous journal was kept.");
            CHECK(settingsNotifications == 1);
            SetHistoryStatus(L"History is not being saved to disk. Check disk space and permissions.");
            CHECK(settingsNotifications == 2 && g_historyStorageError);
            SetHistoryStatus({});
            CHECK(settingsNotifications == 3 && !g_historyStorageError && g_historyStatus.empty());
            SetHistoryStatus({});
            CHECK(settingsNotifications == 3);
        });
        Run("journal/account deletion: only that account's samples go, the rest in order; the revision changes, so "
            "the window's peek replaces its snapshot and the fetch thread's tails refill; an absent account changes "
            "nothing", [&] {
            // Account 7 at hours 0-4, account 9 half an hour after each.
            const HistoryJournalStamp held = journal();
            std::deque<HistorySample> mixed;
            for (const auto& sample : g_historySamples) {
                mixed.push_back(sample);
                HistorySample other = sample;
                other.accountId = 9;
                other.timeMs += hour / 2;
                mixed.push_back(other);
            }
            g_historySamples = mixed;
            BurnUpSampleCache cache;
            UpdateBurnUpTails(cache, g_historySamples, 3, 5, 7, {{7, 1, at(0)}, {9, 1, at(0)}}, false);
            CHECK(cache.accounts.size() == 2 && cache.accounts[1].samples.size() == 5);
            settingsNotifications = 0;
            {
                std::lock_guard lock(g_historyMutex);
                EraseHistorySamplesLocked(9);
            }
            const std::vector<ULONGLONG> sevens{at(0), at(1), at(2), at(3), at(4)};
            std::vector<HistorySample> kept(g_historySamples.begin(), g_historySamples.end());
            CHECK(times(kept) == sevens && std::all_of(kept.begin(), kept.end(), [](const HistorySample& sample) {
                return sample.accountId == 7;
            }));
            CHECK(g_historyRevision == 6 && g_historyEpoch == 3 && g_historyAppendCount == 7 && g_historyLoaded);
            auto peek = PeekHistorySnapshot(at(0), true, held, at(0));
            CHECK(peek.samples && times(*peek.samples) == sevens && peek.stamp.revision == 6);
            // Account 9 is no longer configured: only 7's tail is requested, and refilled whole.
            UpdateBurnUpTails(cache, g_historySamples, 3, g_historyRevision, g_historyAppendCount, {{7, 1, at(0)}}, false);
            CHECK(cache.accounts.size() == 1 && cache.accounts[0].historyId == 7 && cache.accounts[0].samples.size() == 5);
            {
                std::lock_guard lock(g_historyMutex);
                EraseHistorySamplesLocked(42);
            }
            CHECK(g_historyRevision == 6 && g_historySamples.size() == 5);
            CHECK(!PeekHistorySnapshot(at(0), true, peek.stamp, at(0)).samples);
            CHECK(settingsNotifications == 0);  // The deletion notifies the windows after the lock
        });

        Run("journal/refresh cost at the sample cap (observation)", [&] {
            // Five accounts (Anthropic, OpenAI, Antigravity, Anthropic, OpenAI) polled in turn over 90
            // days, every meter of their provider recorded.
            constexpr size_t count = 250000;
            constexpr ULONGLONG span = 90 * kHistoryDayMs;
            g_historySamples.clear();
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
                g_historySamples.push_back(sample);
            }
            g_historyLoaded = true;
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
            HistoryPeek peek = PeekHistorySnapshot(baseTime, true, {}, ULLONG_MAX);
            auto t1 = Clock::now();
            std::vector<HistorySample>& samples = *peek.samples;
            auto earlier = [](const HistorySample& a, const HistorySample& b) { return a.timeMs < b.timeMs; };
            bool sorted = std::is_sorted(samples.begin(), samples.end(), earlier);
            auto t2 = Clock::now();
            std::stable_sort(samples.begin(), samples.end(), earlier);
            auto t3 = Clock::now();
            std::vector<HistoryUiAccount> accounts = HistoryCollectAccounts(configured, samples, baseTime,
                                                                            baseTime + span, Never);
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
            g_historySamples.clear();
            g_historySamples.shrink_to_fit();
        });
    }
