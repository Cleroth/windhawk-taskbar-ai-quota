// Synthetic flyout burn-up curves: independent expectations, actual BuildBurnUpCurve,
// FindBurnUpCurve and PublishBurnUpCurves. Meter 1 is a weekly quota for [start, reset).
    {
        const ULONGLONG start = baseTime, reset = baseTime + kHistoryWeekMs, poll = 600000;
        auto reading = [](ULONGLONG time, double used) {
            HistorySample sample{};
            sample.accountId = 7; sample.provider = 1; sample.session = 1;
            sample.timeMs = time; sample.intervalMs = 600000;
            sample.meters[1] = {used, baseTime + kHistoryWeekMs, kHistoryWeekMs};
            return sample;
        };
        auto times = [](const BurnUpCurve& curve) {
            std::vector<ULONGLONG> result;
            for (const auto& point : curve.points) result.push_back(point.timeMs);
            return result;
        };
        auto joins = [](const BurnUpCurve& curve) {
            std::vector<bool> result;
            for (const auto& point : curve.points) result.push_back(point.connect);
            return result;
        };

        Run("burn-up/steady window keeps every reading", [&] {
            std::deque<HistorySample> samples;
            for (int i = 0; i < 5; ++i) samples.push_back(reading(start + hour + i * poll, 10 + i));
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK(curve.resetMs == reset); CHECK(curve.durationMs == kHistoryWeekMs);
            CHECK(curve.points.size() == 5);
            for (int i = 0; i < 5; ++i) {
                CHECK(curve.points[i].timeMs == start + hour + i * poll);
                CHECK(curve.points[i].used == 10 + i);
                CHECK(curve.points[i].connect == (i > 0));
            }
        });
        Run("burn-up/reset jitter stays one segment", [&] {
            std::deque<HistorySample> samples;
            for (int i = 0; i < 4; ++i) {
                samples.push_back(reading(start + hour + i * poll, 20 + i));
                samples.back().meters[1].resetMs = reset + (i % 2 ? 1000 : -1500);
            }
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK(curve.resetMs == reset + 1000);
            CHECK((joins(curve) == std::vector<bool>{false, true, true, true}));
        });
        Run("burn-up/genuine reset starts a new curve", [&] {
            std::deque<HistorySample> samples;
            double previous[] = {70, 85, 97};
            for (int i = 0; i < 3; ++i) {
                samples.push_back(reading(start - (3 - i) * poll, previous[i]));
                samples.back().meters[1].resetMs = start + 500;  // The previous week ends at start.
            }
            samples.push_back(reading(start + poll / 2, 1));
            samples.back().meters[1].resetMs = reset - 700;
            samples.push_back(reading(start + poll * 3 / 2, 3));
            samples.back().meters[1].resetMs = reset + 300;
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK((times(curve) == std::vector<ULONGLONG>{start + poll / 2, start + poll * 3 / 2}));
            CHECK((joins(curve) == std::vector<bool>{false, true}));
            CHECK(curve.points[0].used == 1);
        });
        Run("burn-up/moved deadline is another window", [&] {
            std::deque<HistorySample> samples{reading(start + hour, 40), reading(start + hour + poll, 41)};
            for (auto& sample : samples) sample.meters[1].resetMs = reset - 4 * kHistoryDayMs;
            samples.push_back(reading(start + hour + 2 * poll, 5));
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK((times(curve) == std::vector<ULONGLONG>{start + hour + 2 * poll}));
        });
        Run("burn-up/duration change is another window", [&] {
            std::deque<HistorySample> samples{reading(start + hour, 40), reading(start + hour + poll, 41)};
            for (auto& sample : samples) sample.meters[1].durationMs = kHistoryWeekMs - hour;
            samples.push_back(reading(start + hour + 2 * poll, 42));
            CHECK(BuildBurnUpCurve(samples, 1).points.size() == 1);
        });
        Run("burn-up/recording gap keeps the window as a new segment", [&] {
            std::deque<HistorySample> samples{
                reading(start + hour, 10), reading(start + hour + poll, 11),
                reading(start + hour + kHistoryDayMs, 30), reading(start + hour + kHistoryDayMs + poll, 31)};
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK(curve.points.size() == 4);
            CHECK((joins(curve) == std::vector<bool>{false, true, false, true}));
        });
        Run("burn-up/restart keeps the window as a new segment", [&] {
            std::deque<HistorySample> samples;
            for (int i = 0; i < 4; ++i) {
                samples.push_back(reading(start + hour + i * poll, 10 + i));
                samples.back().session = i < 2 ? 1 : 2;
            }
            CHECK((joins(BuildBurnUpCurve(samples, 1)) == std::vector<bool>{false, true, false, true}));
        });
        Run("burn-up/failed and missing readings break the line", [&] {
            std::deque<HistorySample> samples;
            for (int i = 0; i < 6; ++i) samples.push_back(reading(start + hour + i * poll, 10 + i));
            samples[2].failed = 1;
            samples[4].meters[1].value = -1;
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK((times(curve) == std::vector<ULONGLONG>{start + hour, start + hour + poll,
                                                          start + hour + 3 * poll, start + hour + 5 * poll}));
            CHECK((joins(curve) == std::vector<bool>{false, true, false, false}));
        });
        Run("burn-up/clock rollback never runs backwards", [&] {
            // Journal order: two readings stamped by a fast clock, then the corrected clock.
            std::deque<HistorySample> samples{reading(start + 3 * hour, 30), reading(start + 4 * hour, 40),
                                               reading(start + 2 * hour, 20)};
            CHECK((times(BuildBurnUpCurve(samples, 1)) == std::vector<ULONGLONG>{start + 2 * hour}));
            samples.push_back(reading(start + 2 * hour + poll, 21));
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK((times(curve) == std::vector<ULONGLONG>{start + 2 * hour, start + 2 * hour + poll}));
            CHECK((joins(curve) == std::vector<bool>{false, true}));
        });
        Run("burn-up/idle OpenAI deadlines before first use are not the window", [&] {
            std::deque<HistorySample> samples;
            for (ULONGLONG ago : {30 * 60000ULL, 20 * 60000ULL, 10 * 60000ULL}) {
                samples.push_back(reading(start - ago, 0));
                samples.back().meters[1].resetMs = start - ago + kHistoryWeekMs;  // Rolls until used.
            }
            samples.push_back(reading(start + 5 * 60000, 3));   // First use anchored the window.
            samples.push_back(reading(start + 15 * 60000, 5));
            for (auto& sample : samples) sample.provider = 2;
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK((times(curve) == std::vector<ULONGLONG>{start + 5 * 60000, start + 15 * 60000}));
            CHECK((joins(curve) == std::vector<bool>{false, true}));
            for (const auto& point : curve.points) CHECK(point.used > 0);
        });
        Run("burn-up/unused OpenAI window is only its current reading", [&] {
            std::deque<HistorySample> samples;
            for (ULONGLONG ago : {20 * 60000ULL, 10 * 60000ULL, 0ULL}) {
                samples.push_back(reading(start - ago, 0));
                samples.back().provider = 2;
                samples.back().meters[1].resetMs = start - ago + kHistoryWeekMs;
            }
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK((times(curve) == std::vector<ULONGLONG>{start}));
            CHECK(curve.points[0].used == 0);
        });
        Run("burn-up/first recorded reading is the origin", [&] {
            std::deque<HistorySample> samples{reading(start + 2 * kHistoryDayMs, 40),
                                               reading(start + 2 * kHistoryDayMs + poll, 41)};
            auto curve = BuildBurnUpCurve(samples, 1);
            CHECK(curve.points.size() == 2);
            CHECK(curve.points[0].timeMs == start + 2 * kHistoryDayMs);
            CHECK(curve.points[0].used == 40);
        });
        Run("burn-up/no window or no reading gives no curve", [&] {
            auto build = [&](auto change) {
                std::deque<HistorySample> samples{reading(start + hour, 10), reading(start + hour + poll, 11)};
                for (auto& sample : samples) change(sample);
                return BuildBurnUpCurve(samples, 1);
            };
            CHECK(build([](HistorySample& s) { s.meters[1].resetMs = 0; }).points.empty());
            CHECK(build([](HistorySample& s) { s.meters[1].durationMs = 0; }).points.empty());
            CHECK(build([](HistorySample& s) { s.meters[1].durationMs = 365 * kHistoryDayMs; }).points.empty());
            CHECK(build([](HistorySample& s) { s.failed = 1; }).points.empty());
            CHECK(build([](HistorySample&) {}).points.size() == 2);
            CHECK(BuildBurnUpCurve({}, 1).points.empty());
        });
        Run("burn-up/reduction keeps extrema, endpoints and breaks", [&] {
            auto at = [&](int i) { return start + poll + ULONGLONG(i) * poll; };
            auto value = [](int i) { return i == 300 ? 1.0 : i == 500 ? 99.0 : 20 + i * 0.03; };
            std::deque<HistorySample> samples;
            std::map<ULONGLONG, double> recorded;
            for (int i = 0; i < 1000; ++i) {
                if (i >= 800 && i < 900) continue;  // Recording gap.
                samples.push_back(reading(at(i), value(i)));
                samples.back().session = i < 600 ? 1 : 2;
                if (i == 200 || i == 699 || i == 701) samples.back().failed = 1;
                else recorded[at(i)] = value(i);
            }
            // Last reading before and first reading after each break.
            const std::pair<int, int> breaks[] = {{199, 201}, {599, 600}, {698, 700}, {700, 702}, {799, 900}};
            auto curve = BuildBurnUpCurve(samples, 1);
            const auto& points = curve.points;
            CHECK(!points.empty()); CHECK(points.size() <= 64);
            for (size_t k = 0; k < points.size(); ++k) {
                CHECK(recorded.contains(points[k].timeMs));
                CHECK(recorded[points[k].timeMs] == points[k].used);
                if (k) CHECK(points[k].timeMs > points[k - 1].timeMs);
            }
            auto kept = [&](int i) {
                return std::any_of(points.begin(), points.end(), [&](const BurnUpPoint& p) {
                    return p.timeMs == at(i) && p.used == value(i);
                });
            };
            CHECK(kept(300)); CHECK(kept(500));
            // Six segments fit the cap with their endpoints, including the isolated reading 700.
            for (int i : {0, 199, 201, 599, 600, 698, 700, 702, 799, 900, 999}) CHECK(kept(i));
            CHECK(!points.front().connect);
            for (auto [before, after] : breaks) {
                for (size_t k = 1; k < points.size(); ++k) {
                    if (points[k].connect) CHECK(!(points[k - 1].timeMs <= at(before) && points[k].timeMs >= at(after)));
                }
                auto next = std::find_if(points.begin(), points.end(),
                                         [&](const BurnUpPoint& p) { return p.timeMs >= at(after); });
                CHECK(next != points.end() && next != points.begin() && !next->connect);
            }
        });
        Run("burn-up/too many segments stay capped and unjoined", [&] {
            // 60 three-reading segments separated by failed polls: endpoints alone exceed 64.
            std::deque<HistorySample> samples;
            std::map<ULONGLONG, int> run;
            for (int i = 0; i < 240; ++i) {
                ULONGLONG time = start + hour + ULONGLONG(i) * poll;
                samples.push_back(reading(time, i == 101 ? 90.0 : 10 + i * 0.1));
                if (i % 4 == 3) samples.back().failed = 1;
                else run[time] = i / 4;
            }
            auto curve = BuildBurnUpCurve(samples, 1);
            const auto& points = curve.points;
            CHECK(!points.empty()); CHECK(points.size() <= 64);
            for (size_t k = 0; k < points.size(); ++k) {
                CHECK(run.contains(points[k].timeMs));
                if (k) CHECK(points[k].timeMs > points[k - 1].timeMs);
                if (k && points[k].connect) CHECK(run[points[k].timeMs] == run[points[k - 1].timeMs]);
            }
            CHECK(points.front().timeMs == start + hour);
            CHECK(points.back().timeMs == start + hour + 238 * poll);
            CHECK(std::any_of(points.begin(), points.end(), [&](const BurnUpPoint& p) {
                return p.timeMs == start + hour + 101 * poll && p.used == 90;
            }));
        });

        auto stored = [&](uint64_t historyId, uint32_t provider) {
            BurnUpAccountCurves account;
            account.historyId = historyId; account.provider = provider;
            account.meters[1] = {reset, kHistoryWeekMs, {{start + hour, 10, false}, {start + hour + poll, 12, true}}};
            return account;
        };
        Run("burn-up/displayed reading finds only its own window and tags", [&] {
            BurnUpStore store{3, 5, {stored(7, 1)}};
            auto find = [&](uint64_t epoch, ULONGLONG generation, uint64_t id, uint32_t provider,
                            int meter, ULONGLONG resetMs, ULONGLONG durationMs) {
                return FindBurnUpCurve(store, epoch, generation, id, provider, meter, resetMs, durationMs);
            };
            CHECK(find(3, 5, 7, 1, 1, reset, kHistoryWeekMs) == &store.accounts[0].meters[1]);
            CHECK(find(3, 5, 7, 1, 1, reset + 60000, kHistoryWeekMs) != nullptr);  // Jitter.
            CHECK(find(3, 5, 7, 1, 1, reset - 60000, kHistoryWeekMs) != nullptr);
            CHECK(find(3, 5, 7, 1, 1, reset + 600000, kHistoryWeekMs) == nullptr);  // Next idle roll.
            CHECK(find(3, 5, 7, 1, 1, reset, kHistoryWeekMs - hour) == nullptr);
            CHECK(find(3, 5, 7, 1, 1, 0, kHistoryWeekMs) == nullptr);
            CHECK(find(3, 5, 7, 1, 1, reset, 0) == nullptr);
            CHECK(find(4, 5, 7, 1, 1, reset, kHistoryWeekMs) == nullptr);  // Cleared since.
            CHECK(find(3, 6, 7, 1, 1, reset, kHistoryWeekMs) == nullptr);  // Policy changed since.
            CHECK(find(3, 5, 8, 1, 1, reset, kHistoryWeekMs) == nullptr);
            CHECK(find(3, 5, 7, 2, 1, reset, kHistoryWeekMs) == nullptr);
            CHECK(find(3, 5, 7, 1, 0, reset, kHistoryWeekMs) == nullptr);  // No curve for the meter.
            CHECK(find(3, 5, 7, 1, 6, reset, kHistoryWeekMs) == nullptr);  // USD has no chart.
            CHECK(find(3, 5, 7, 1, -1, reset, kHistoryWeekMs) == nullptr);
        });
        Run("burn-up/stale publications are rejected", [&] {
            BurnUpStore store{3, 5, {stored(7, 1)}};
            CHECK(!PublishBurnUpCurves(store, {stored(8, 1)}, true, 3, 5, 4, 5));
            CHECK(!PublishBurnUpCurves(store, {stored(8, 1)}, true, 3, 5, 3, 6));
            CHECK(store.epoch == 3); CHECK(store.generation == 5);
            CHECK(store.accounts.size() == 1); CHECK(store.accounts[0].historyId == 7);
        });
        Run("burn-up/publication merges, replaces and drops", [&] {
            BurnUpStore store{3, 5, {stored(7, 1)}};
            CHECK(PublishBurnUpCurves(store, {stored(8, 2)}, false, 3, 5, 3, 5));
            CHECK(store.accounts.size() == 2);
            auto replacement = stored(7, 1);
            replacement.meters[1].points.pop_back();
            CHECK(PublishBurnUpCurves(store, {replacement}, false, 3, 5, 3, 5));
            CHECK(store.accounts.size() == 2);
            for (const auto& account : store.accounts) {
                CHECK(account.meters[1].points.size() == (account.historyId == 7 ? 1u : 2u));
            }
            BurnUpAccountCurves empty;
            empty.historyId = 8; empty.provider = 2;
            CHECK(PublishBurnUpCurves(store, {empty}, false, 3, 5, 3, 5));
            CHECK(store.accounts.size() == 1); CHECK(store.accounts[0].historyId == 7);
            CHECK(PublishBurnUpCurves(store, {stored(9, 3)}, true, 3, 5, 3, 5));  // Full rebuild.
            CHECK(store.accounts.size() == 1); CHECK(store.accounts[0].historyId == 9);
            CHECK(PublishBurnUpCurves(store, {stored(10, 1)}, false, 3, 6, 3, 6));  // New policy.
            CHECK(store.generation == 6);
            CHECK(store.accounts.size() == 1); CHECK(store.accounts[0].historyId == 10);
        });

        // Journal tails: two interleaved accounts. A tail must hold its account's journal samples
        // from its scan start, in journal order. Changing an old journal entry (which never
        // happens for real) shows whether an update re-read it or kept its earlier copy.
        using Contents = std::vector<std::pair<ULONGLONG, double>>;
        auto contents = [](const std::deque<HistorySample>& samples) {
            Contents result;
            for (const auto& sample : samples) result.push_back({sample.timeMs, sample.meters[1].value});
            return result;
        };
        auto expected = [&](const std::deque<HistorySample>& journal, uint64_t id, ULONGLONG from) {
            Contents result;
            for (const auto& sample : journal) {
                if (sample.accountId == id && sample.timeMs >= from) result.push_back({sample.timeMs, sample.meters[1].value});
            }
            return result;
        };
        auto tailOf = [](const BurnUpSampleCache& cache, uint64_t id) -> const BurnUpAccountTail* {
            for (const auto& tail : cache.accounts) if (tail.historyId == id) return &tail;
            return nullptr;
        };
        auto tailContents = [&](const BurnUpSampleCache& cache, uint64_t id) {
            const BurnUpAccountTail* tail = tailOf(cache, id);
            if (!tail) throw std::runtime_error("missing tail " + std::to_string(id));
            return contents(tail->samples);
        };
        auto journalOf = [&](int count) {
            std::deque<HistorySample> journal;
            for (int i = 0; i < count; ++i) {
                journal.push_back(reading(start + hour + i * poll, i)); journal.back().accountId = 7;
                journal.push_back(reading(start + hour + i * poll + 1, 100 + i)); journal.back().accountId = 9;
            }
            return journal;
        };
        auto append = [&](std::deque<HistorySample>& journal, uint64_t id, ULONGLONG time, double used) {
            journal.push_back(reading(time, used)); journal.back().accountId = id;
        };
        const ULONGLONG from = start + hour;
        Run("burn-up/tails copy only new appends", [&] {
            auto journal = journalOf(10);
            BurnUpSampleCache cache;
            UpdateBurnUpTails(cache, journal, 1, 1, 20, {{7, 1, from}}, false);
            CHECK(tailContents(cache, 7) == expected(journal, 7, from));
            CHECK(!tailOf(cache, 9));
            journal[0].meters[1].value = -5;
            append(journal, 7, start + hour + 10 * poll, 10);
            append(journal, 9, start + hour + 10 * poll + 1, 110);
            UpdateBurnUpTails(cache, journal, 1, 1, 22, {{7, 1, from}}, false);
            auto tail = tailContents(cache, 7);
            CHECK(tail.size() == 11);
            CHECK(tail.front().second == 0);  // Kept as copied: not rescanned.
            CHECK(tail.back() == (std::pair<ULONGLONG, double>{start + hour + 10 * poll, 10}));
            UpdateBurnUpTails(cache, journal, 1, 1, 22, {{7, 1, from}, {9, 3, from}}, false);
            CHECK(tailContents(cache, 9) == expected(journal, 9, from));  // New tail scans.
            CHECK(tailContents(cache, 7) == tail);
        });
        Run("burn-up/tails rescan after the journal changes", [&] {
            for (int change = 0; change < 3; ++change) {
                auto journal = journalOf(10);
                BurnUpSampleCache cache;
                UpdateBurnUpTails(cache, journal, 1, 1, 20, {{7, 1, from}, {9, 3, from}}, false);
                journal[0].meters[1].value = -5;
                // Cap eviction or load (revision), Clear (epoch), or an invalidation's full rebuild.
                UpdateBurnUpTails(cache, journal, change == 1 ? 2 : 1, change == 0 ? 2 : 1, 20,
                                  {{7, 1, from}}, change == 2);
                CHECK(tailContents(cache, 7) == expected(journal, 7, from));
                CHECK(tailContents(cache, 7).front().second == -5);
                CHECK(!tailOf(cache, 9));  // Unrequested tails don't survive a journal change.
            }
        });
        Run("burn-up/tails follow their scan start", [&] {
            auto journal = journalOf(10);
            BurnUpSampleCache cache;
            UpdateBurnUpTails(cache, journal, 1, 1, 20, {{7, 1, from + 4 * poll}}, false);
            CHECK(tailContents(cache, 7) == expected(journal, 7, from + 4 * poll));
            UpdateBurnUpTails(cache, journal, 1, 1, 20, {{7, 1, from + 6 * poll}}, false);  // Window moved on.
            CHECK(tailContents(cache, 7) == expected(journal, 7, from + 6 * poll));
            UpdateBurnUpTails(cache, journal, 1, 1, 20, {{7, 1, from}}, false);  // Reaches further back.
            CHECK(tailContents(cache, 7) == expected(journal, 7, from));
            UpdateBurnUpTails(cache, journal, 1, 1, 20, {{7, 1, ULLONG_MAX}}, false);  // No window.
            CHECK(tailContents(cache, 7).empty());
            UpdateBurnUpTails(cache, journal, 1, 1, 20, {{7, 1, from}}, false);
            CHECK(tailContents(cache, 7) == expected(journal, 7, from));
        });
        Run("burn-up/unrequested tails still receive their appends", [&] {
            auto journal = journalOf(5);
            BurnUpSampleCache cache;
            UpdateBurnUpTails(cache, journal, 1, 1, 10, {{7, 1, from}, {9, 3, from}}, false);
            append(journal, 9, start + hour + 5 * poll + 1, 105);
            UpdateBurnUpTails(cache, journal, 1, 1, 11, {{7, 1, from}}, false);
            CHECK(tailContents(cache, 9) == expected(journal, 9, from));
            CHECK(tailContents(cache, 7) == expected(journal, 7, from));
        });
    }
