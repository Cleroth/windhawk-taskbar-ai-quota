// Antigravity thinning (history/thinning.h) on synthetic minute-by-minute readings: hand-derived
// expectations of which readings stay and how lines connect and break through the thinned ones.
    {
        constexpr ULONGLONG minute = 60000;
        const ULONGLONG now = baseTime + 40 * kHistoryDayMs;
        const ULONGLONG start = now - 20 * kHistoryDayMs;  // Past the 15-day age
        // `count` readings of Antigravity account `account` a minute apart from `from`: the 5-hour
        // and weekly Gemini pools at steady levels and resets, each changed by `observe` (with its
        // index), then checksummed.
        auto run = [](uint64_t account, ULONGLONG from, size_t count,
                      const std::function<void(HistorySample&, size_t)>& observe = {}) {
            std::deque<HistorySample> samples;
            for (size_t k = 0; k < count; ++k) {
                HistorySample sample{};
                sample.accountId = account; sample.provider = 3; sample.session = 1;
                sample.timeMs = from + k * minute; sample.intervalMs = minute;
                sample.meters[0] = {40, from + 5 * hour, 5 * hour};
                sample.meters[1] = {20, from + kHistoryWeekMs, kHistoryWeekMs};
                if (observe) observe(sample, k);
                sample.checksum = HistoryChecksum(sample);
                samples.push_back(sample);
            }
            return samples;
        };
        // An account's readings from `from` on, in whole minutes after it.
        auto minutes = [](const std::deque<HistorySample>& samples, uint64_t account, ULONGLONG from) {
            std::vector<ULONGLONG> result;
            for (const auto& sample : samples) {
                if (sample.accountId == account && sample.timeMs >= from) result.push_back((sample.timeMs - from) / minute);
            }
            return result;
        };
        // How an account's consecutive readings connect on `meter`.
        auto links = [](const std::deque<HistorySample>& samples, uint64_t account, int meter) {
            std::vector<HistoryBreak> result;
            const HistorySample* previous = nullptr;
            for (const auto& sample : samples) {
                if (sample.accountId != account) continue;
                if (previous) result.push_back(HistoryConnection(*previous, sample, meter));
                previous = &sample;
            }
            return result;
        };
        auto all = [](const std::vector<HistoryBreak>& breaks, HistoryBreak kind) {
            return std::all_of(breaks.begin(), breaks.end(), [&](HistoryBreak b) { return b == kind; });
        };
        auto checksummed = [](const std::deque<HistorySample>& samples) {
            return std::all_of(samples.begin(), samples.end(),
                               [](const HistorySample& sample) { return sample.checksum == HistoryChecksum(sample); });
        };
        auto at = [](const std::deque<HistorySample>& samples, uint64_t account, ULONGLONG time) -> const HistorySample& {
            auto it = std::find_if(samples.begin(), samples.end(), [&](const HistorySample& sample) {
                return sample.accountId == account && sample.timeMs == time;
            });
            if (it == samples.end()) throw std::runtime_error("Missing reading");
            return *it;
        };

        Run("thinning/steady readings keep one per 10 minutes, the first and the last; all but the last widened to "
            "10 minutes and checksummed again; the lines stay whole", [&] {
            auto thinned = ThinHistorySamples(run(7, start, 121), now);
            CHECK(thinned);
            CHECK(minutes(*thinned, 7, start) == (std::vector<ULONGLONG>{0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120}));
            for (size_t i = 0; i < thinned->size(); ++i) {
                CHECK((*thinned)[i].intervalMs == (i + 1 < thinned->size() ? 10 * minute : minute));
            }
            CHECK(checksummed(*thinned));
            CHECK(all(links(*thinned, 7, 0), HistoryBreak::None) && all(links(*thinned, 7, 1), HistoryBreak::None));
            CHECK(all(links(*thinned, 7, 2), HistoryBreak::Unavailable));
        });
        Run("thinning/a reset drifting 30 s a reading stays connected: a kept reading reaches at most 2 minutes of "
            "drift", [&] {
            auto thinned = ThinHistorySamples(run(7, start, 61, [](HistorySample& sample, size_t k) {
                sample.meters[0].resetMs += k * 30000;
            }), now);
            CHECK(thinned);
            CHECK(minutes(*thinned, 7, start) ==
                  (std::vector<ULONGLONG>{0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60}));
            CHECK(all(links(*thinned, 7, 0), HistoryBreak::None) && all(links(*thinned, 7, 1), HistoryBreak::None));
        });
        Run("thinning/a 3-minute gap stays a gap: both its readings kept at their 1-minute interval; past it, the "
            "unwidened reading reaches only 2 minutes", [&] {
            // Minutes 0-30, then 33-62.
            auto thinned = ThinHistorySamples(run(7, start, 61, [](HistorySample& sample, size_t k) {
                if (k > 30) sample.timeMs += 2 * minute;
            }), now);
            CHECK(thinned);
            CHECK(minutes(*thinned, 7, start) == (std::vector<ULONGLONG>{0, 10, 20, 30, 33, 35, 45, 55, 62}));
            const HistorySample& before = at(*thinned, 7, start + 30 * minute);
            const HistorySample& after = at(*thinned, 7, start + 33 * minute);
            CHECK(before.intervalMs == minute && after.intervalMs == minute);
            CHECK(HistoryConnection(before, after, 0) == HistoryBreak::Gap);
            auto breaks = links(*thinned, 7, 0);
            CHECK(std::count(breaks.begin(), breaks.end(), HistoryBreak::Gap) == 1 &&
                  std::count(breaks.begin(), breaks.end(), HistoryBreak::None) == ptrdiff_t(breaks.size()) - 1);
            CHECK(checksummed(*thinned));
        });
        Run("thinning/a failed reading stays with both neighbours, its interval and flag unchanged; the lines break "
            "only there", [&] {
            auto thinned = ThinHistorySamples(run(7, start, 41, [](HistorySample& sample, size_t k) {
                if (k == 20) {
                    sample.failed = 1;
                    sample.meters = {};
                }
            }), now);
            CHECK(thinned);
            CHECK(minutes(*thinned, 7, start) == (std::vector<ULONGLONG>{0, 10, 19, 20, 21, 31, 40}));
            const HistorySample& failed = at(*thinned, 7, start + 20 * minute);
            CHECK(failed.failed == 1 && failed.intervalMs == minute);
            auto breaks = links(*thinned, 7, 0);
            CHECK((breaks == std::vector<HistoryBreak>{HistoryBreak::None, HistoryBreak::None, HistoryBreak::Unavailable,
                                                       HistoryBreak::Unavailable, HistoryBreak::None, HistoryBreak::None}));
        });
        Run("thinning/a meter gaining or losing its value, and a real reset, keep both readings around the change", [&] {
            auto thinned = ThinHistorySamples(run(7, start, 61, [&](HistorySample& sample, size_t k) {
                // The third-party 5-hour pool reads until minute 24; the Gemini 5-hour pool resets at 45.
                if (k < 25) sample.meters[4] = {10, start + 5 * hour, 5 * hour};
                if (k >= 45) sample.meters[0] = {0, start + 10 * hour, 5 * hour};
            }), now);
            CHECK(thinned);
            CHECK(minutes(*thinned, 7, start) == (std::vector<ULONGLONG>{0, 10, 20, 24, 25, 35, 44, 45, 55, 60}));
            auto third = links(*thinned, 7, 4);
            CHECK((third == std::vector<HistoryBreak>{HistoryBreak::None, HistoryBreak::None, HistoryBreak::None,
                                                      HistoryBreak::Unavailable, HistoryBreak::Unavailable,
                                                      HistoryBreak::Unavailable, HistoryBreak::Unavailable,
                                                      HistoryBreak::Unavailable, HistoryBreak::Unavailable}));
            auto gemini = links(*thinned, 7, 0);
            CHECK(std::count(gemini.begin(), gemini.end(), HistoryBreak::Reset) == 1 &&
                  HistoryConnection(at(*thinned, 7, start + 44 * minute), at(*thinned, 7, start + 45 * minute), 0) ==
                      HistoryBreak::Reset);
            CHECK(all(links(*thinned, 7, 1), HistoryBreak::None));
        });
        Run("thinning/only readings older than 15 days: one exactly 15 days old and later ones stay as they were, "
            "the last older one kept at its interval", [&] {
            const ULONGLONG cutoff = now - 15 * kHistoryDayMs;
            const ULONGLONG from = cutoff - 30 * minute;
            auto samples = run(7, from, 41);
            auto thinned = ThinHistorySamples(samples, now);
            CHECK(thinned);
            CHECK(minutes(*thinned, 7, from) ==
                  (std::vector<ULONGLONG>{0, 10, 20, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40}));
            CHECK(at(*thinned, 7, from).intervalMs == 10 * minute && at(*thinned, 7, cutoff - minute).intervalMs == minute);
            for (size_t k = 30; k < samples.size(); ++k) {
                CHECK(!memcmp(&at(*thinned, 7, samples[k].timeMs), &samples[k], sizeof(HistorySample)));
            }
        });
        Run("thinning/each Antigravity account thinned on its own; other providers' and recent readings untouched; "
            "the journal's order kept", [&] {
            // Antigravity accounts 7 and 8 and Anthropic account 9 polled each minute in turn, then an hour of
            // recent readings of account 7.
            auto first = run(7, start, 31), second = run(8, start, 31);
            auto other = run(9, start, 31, [](HistorySample& sample, size_t) { sample.provider = 1; });
            std::deque<HistorySample> samples;
            for (size_t k = 0; k < 31; ++k) {
                samples.push_back(first[k]);
                samples.push_back(second[k]);
                samples.push_back(other[k]);
            }
            const ULONGLONG recent = now - hour;
            auto latest = run(7, recent, 61);
            samples.insert(samples.end(), latest.begin(), latest.end());
            auto thinned = ThinHistorySamples(samples, now);
            CHECK(thinned);
            std::vector<ULONGLONG> old = minutes(*thinned, 7, start);
            old.resize(4);
            CHECK(old == (std::vector<ULONGLONG>{0, 10, 20, 30}));
            CHECK(minutes(*thinned, 8, start) == (std::vector<ULONGLONG>{0, 10, 20, 30}));
            CHECK(minutes(*thinned, 7, recent).size() == 61 && minutes(*thinned, 9, start).size() == 31);
            for (const auto& sample : samples) {
                if (sample.accountId == 9 || sample.timeMs >= recent) {
                    CHECK(!memcmp(&at(*thinned, sample.accountId, sample.timeMs), &sample, sizeof(HistorySample)));
                }
            }
            // A subsequence of the journal: each kept reading in its place.
            size_t next = 0;
            for (const auto& kept : *thinned) {
                while (next < samples.size() &&
                       (samples[next].accountId != kept.accountId || samples[next].timeMs != kept.timeMs)) {
                    ++next;
                }
                CHECK(next < samples.size());
                ++next;
            }
        });
        Run("thinning/thinned again each day as readings age: each pass is final for its day, and the last matches "
            "thinning all at once", [&] {
            // Three days of minute readings, their resets after them; on the first day only their first hour is
            // 15 days old.
            auto samples = run(7, baseTime, 3 * 24 * 60, [](HistorySample& sample, size_t) {
                sample.meters[0].resetMs = baseTime + 4 * kHistoryDayMs;
            });
            const ULONGLONG first = baseTime + 15 * kHistoryDayMs + hour;
            std::deque<HistorySample> daily = samples;
            for (int day = 0; day <= 3; ++day) {
                const ULONGLONG when = first + ULONGLONG(day) * kHistoryDayMs;
                if (auto thinned = ThinHistorySamples(daily, when)) daily = std::move(*thinned);
                CHECK(!ThinHistorySamples(daily, when));
            }
            auto once = ThinHistorySamples(samples, first + 3 * kHistoryDayMs);
            CHECK(once && daily.size() == once->size());
            for (size_t i = 0; i < daily.size() && i < once->size(); ++i) {
                CHECK(!memcmp(&daily[i], &(*once)[i], sizeof(HistorySample)));
            }
            CHECK(minutes(daily, 7, baseTime).size() == 3 * 24 * 6 + 1);
            CHECK(all(links(daily, 7, 0), HistoryBreak::None));
        });
        Run("thinning/nothing older than 15 days, or no samples: nothing changes", [&] {
            CHECK(!ThinHistorySamples(run(7, now - 2 * hour, 121), now));
            CHECK(!ThinHistorySamples({}, now));
        });
    }
