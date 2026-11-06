// What the history window and the flyout's burn-up tails read from the journal (history/journal.h,
// history/burn-up.h) on synthetic files: the window's memory-only peek and its pinned range, its
// explicit loads widening memory from the file, the status notifier, and burn-up tails refilled
// from the file when their window reaches back before memory. Hand-derived expectations.
    {
        const ULONGLONG now = kTestStart;
        auto at = [&](int i) { return now - 5 * kHour + ULONGLONG(i) * kHour; };
        auto times = [](const std::vector<HistorySample>& samples) {
            std::vector<ULONGLONG> result;
            for (const auto& sample : samples) result.push_back(sample.timeMs);
            return result;
        };
        // Account 7's readings at hours 0-4 of the last five, in memory.
        auto recent = [&] {
            std::vector<HistorySample> records;
            for (int i = 0; i < 5; ++i) records.push_back(Reading(7, at(i), 10.0 * i));
            WriteJournalFile(records);
            Load();
            return PeekHistorySnapshot(0, false, {}, 0).stamp;
        };
        // Account 7 every 6 hours over the last 100 days.
        auto hundred = [&] {
            std::vector<HistorySample> records;
            for (ULONGLONG t = now - 100 * kHistoryDayMs; t < now; t += 6 * kHour) records.push_back(Reading(7, t, double(t % 97)));
            WriteJournalFile(records);
            Load();
            return records;
        };
        const BurnUpArchiveReader reader = [](ULONGLONG from, const std::function<void(const HistorySample&)>& use) {
            return ReadHistoryRangeLocked(from, use) == HistoryPass::Done;
        };
        auto tails = [&](BurnUpSampleCache& cache, const std::vector<BurnUpRequest>& requests, bool rescan = false) {
            std::lock_guard lock(g_historyMutex);
            UpdateBurnUpTails(cache, g_historySamples, g_historyCacheFrom, g_historyEpoch, g_historyRevision,
                              g_historyAppendCount, requests, rescan, reader);
        };
        auto tailOf = [](const BurnUpSampleCache& cache, uint64_t id) -> const BurnUpAccountTail* {
            for (const auto& tail : cache.accounts) {
                if (tail.historyId == id) return &tail;
            }
            return nullptr;
        };
        auto tailSamples = [&](const BurnUpSampleCache& cache, uint64_t id) {
            const BurnUpAccountTail* tail = tailOf(cache, id);
            if (!tail) throw std::runtime_error("missing tail " + std::to_string(id));
            return Vector(tail->samples);
        };

        Run("journal/peek: the held stamp from the same or a later start replaces nothing; another stamp or an "
            "earlier start in memory replaces with every sample from the start, which pins it there; status only "
            "never", [&] {
            const HistoryJournalStamp held = recent();
            g_historyPinMs = at(2);
            CHECK(PeekHistorySnapshot(at(2), true, held, at(2)).stamp == held);
            CHECK(!PeekHistorySnapshot(at(2), true, held, at(2)).samples);
            CHECK(!PeekHistorySnapshot(at(3), true, held, at(2)).samples && g_historyPinMs == at(2));
            HistoryPeek earlier = PeekHistorySnapshot(at(1), true, held, at(2));
            CHECK(earlier.samples && (times(*earlier.samples) == std::vector<ULONGLONG>{at(1), at(2), at(3), at(4)}));
            CHECK(earlier.from == at(1) && !earlier.incomplete && g_historyPinMs == at(1));
            for (int field = 0; field < 4; ++field) {
                HistoryJournalStamp other = held;
                if (field == 0) other.loaded = false;
                if (field == 1) other.epoch = held.epoch - 1;
                if (field == 2) other.revision = held.revision - 1;
                if (field == 3) other.appended = held.appended + 1;
                auto peek = PeekHistorySnapshot(at(3), true, other, at(3));
                CHECK(peek.stamp == held && peek.samples && (times(*peek.samples) == std::vector<ULONGLONG>{at(3), at(4)}));
            }
            CHECK(g_historyPinMs == at(3));
            CHECK(!PeekHistorySnapshot(at(0), false, {}, ULLONG_MAX).samples && g_historyPinMs == at(3));
        });

        Run("journal/peek: an empty replacement differs from none; unloaded storage replaces nothing but reports its "
            "stamp, status and cap", [&] {
            HistoryJournalStamp held = recent();
            held.appended += 1;
            auto none = PeekHistorySnapshot(at(9), true, held, at(9));
            CHECK(none.samples && none.samples->empty());
            const HistoryJournalStamp before = PeekHistorySnapshot(0, false, {}, 0).stamp;
            HistoryJournalError error = HistoryJournalError::None;
            CHECK(ClearHistory(&error));
            auto cleared = PeekHistorySnapshot(at(0), true, before, at(0));
            CHECK(cleared.samples && cleared.samples->empty() && cleared.stamp.epoch == before.epoch + 1);
            ResetJournal();
            g_historyStatus = HistoryJournalError::InUse;
            g_historyCapped = true;
            auto unloaded = PeekHistorySnapshot(at(0), true, {}, ULLONG_MAX);
            CHECK(!unloaded.samples && !unloaded.stamp.loaded && unloaded.status == HistoryJournalError::InUse &&
                  unloaded.capped);
        });

        Run("journal/peek: before memory's start it is incomplete and reads nothing: the held range is replaced only "
            "when the journal changed, and the pin stays; only an explicit load reads further back", [&] {
            const std::vector<HistorySample> records = hundred();
            const ULONGLONG heldFrom = now - 90 * kHistoryDayMs;
            HistorySnapshot snapshot = ReadHistorySnapshot(heldFrom);
            CHECK(Same(snapshot.samples, Since(records, heldFrom)) && snapshot.from == heldFrom);
            long reads = g_testCalls[kTestRead];
            HistoryPeek same = PeekHistorySnapshot(now - 120 * kHistoryDayMs, true, snapshot.stamp, heldFrom);
            CHECK(same.incomplete && !same.samples && same.from == heldFrom && g_testCalls[kTestRead] == reads);
            HistorySample next = Reading(7, now, 1);
            Append({next});
            reads = g_testCalls[kTestRead];
            HistoryPeek changed = PeekHistorySnapshot(now - 120 * kHistoryDayMs, true, snapshot.stamp, heldFrom);
            std::vector<HistorySample> expected = Since(records, heldFrom);
            expected.push_back(next);
            CHECK(changed.incomplete && changed.samples && Same(*changed.samples, expected) && changed.from == heldFrom);
            CHECK(g_testCalls[kTestRead] == reads && g_historyPinMs == heldFrom);
            // Within memory again: complete.
            HistoryPeek later = PeekHistorySnapshot(now - 60 * kHistoryDayMs, true, changed.stamp, heldFrom);
            CHECK(!later.incomplete && !later.samples && g_historyPinMs == heldFrom);
        });

        Run("journal/explicit loads: an older range widens memory from the file (no revision change) and pins it; a "
            "narrower one moves the pin and trims memory back to the last 35 days at once; the window's end trims back "
            "too; a failed read leaves the snapshot starting where memory does", [&] {
            const std::vector<HistorySample> records = hundred();
            CHECK(g_historyCacheFrom == now - 35 * kHistoryDayMs);
            uint64_t revision = g_historyRevision;
            HistorySnapshot wide = ReadHistorySnapshot(now - 90 * kHistoryDayMs);
            CHECK(Same(wide.samples, Since(records, now - 90 * kHistoryDayMs)) && g_historyRevision == revision &&
                  g_historyCacheFrom == now - 90 * kHistoryDayMs && g_historyPinMs == now - 90 * kHistoryDayMs);
            CheckMemory();
            HistorySnapshot narrow = ReadHistorySnapshot(now - 7 * kHistoryDayMs);
            CHECK(Same(narrow.samples, Since(records, now - 7 * kHistoryDayMs)) && g_historyPinMs == now - 7 * kHistoryDayMs &&
                  g_historyCacheFrom == now - 35 * kHistoryDayMs && g_historyRevision == revision + 1 &&
                  narrow.stamp.revision == g_historyRevision);
            CheckMemory();
            Maintain();
            CHECK(g_historyCacheFrom == now - 35 * kHistoryDayMs && g_historyRevision == revision + 1);
            HistorySnapshot all = ReadHistorySnapshot(0);
            CHECK(Same(all.samples, records) && Same(Cached(), records));
            ReleaseHistoryPin();
            CHECK(g_historyPinMs == ULLONG_MAX && g_historyCacheFrom == now - 35 * kHistoryDayMs &&
                  g_historyRevision == revision + 2 && Same(Cached(), Since(records, now - 35 * kHistoryDayMs)));
            Fault(kTestRead, 1);
            HistorySnapshot failed = ReadHistorySnapshot(now - 90 * kHistoryDayMs);
            CHECK(failed.from == now - 35 * kHistoryDayMs && failed.status == HistoryJournalError::ReadFailed &&
                  Same(failed.samples, Since(records, now - 35 * kHistoryDayMs)));
            CHECK(g_historyStatus == HistoryJournalError::None && g_historyWritable &&
                  g_historyCacheFrom == now - 35 * kHistoryDayMs);
            CheckMemory();
        });

        Run("journal/peek: a refresh replacing the held range with a narrower one (a shorter retention after a change "
            "elsewhere) trims memory back at once; the stamp returned is the one after, so the next refresh replaces "
            "nothing", [&] {
            const std::vector<HistorySample> records = hundred();
            HistorySnapshot all = ReadHistorySnapshot(0);
            CHECK(Same(Cached(), records) && g_historyCacheFrom == 0);
            HistorySample next = Reading(7, now, 1);
            Append({next});  // Another stamp, as a deletion elsewhere would give
            const ULONGLONG week = now - 7 * kHistoryDayMs;
            HistoryPeek narrower = PeekHistorySnapshot(week, true, all.stamp, 0);
            std::vector<HistorySample> expected = Since(records, week);
            expected.push_back(next);
            CHECK(narrower.samples && Same(*narrower.samples, expected) && !narrower.incomplete);
            CHECK(g_historyPinMs == week && g_historyCacheFrom == now - 35 * kHistoryDayMs &&
                  narrower.stamp == PeekHistorySnapshot(0, false, {}, 0).stamp && narrower.stamp.revision == all.stamp.revision + 1);
            CHECK(!PeekHistorySnapshot(week, true, narrower.stamp, week).samples);
            CheckMemory();
        });

        Run("journal/status: every change of the status notifies once, an unchanged one doesn't; the storage error "
            "flag follows", [&] {
            SetHistoryStatus({});
            CHECK(settingsNotifications == 0 && !g_historyStorageError);
            SetHistoryStatus(HistoryJournalError::WriteFailed);
            CHECK(settingsNotifications == 1 && g_historyStorageError);
            SetHistoryStatus(HistoryJournalError::WriteFailed);
            CHECK(settingsNotifications == 1);
            SetHistoryStatus(HistoryJournalError::NotSaved);
            CHECK(settingsNotifications == 2 && g_historyStorageError);
            SetHistoryStatus({});
            CHECK(settingsNotifications == 3 && !g_historyStorageError && g_historyStatus == HistoryJournalError::None);
            SetHistoryStatus({});
            CHECK(settingsNotifications == 3);
        });

        Run("journal/peek: memory only, under the history lock, changing nothing and notifying nothing", [&] {
            const HistoryJournalStamp held = recent();
            g_historyStatus = HistoryJournalError::DamagedRecord;
            settingsNotifications = 0;
            const long reads = g_testCalls[kTestRead];
            std::atomic<bool> done{false};
            bool waited = false;
            std::thread peeker;
            {
                std::lock_guard lock(g_historyMutex);
                peeker = std::thread([&] {
                    auto peek = PeekHistorySnapshot(at(0), true, {}, ULLONG_MAX);
                    done = peek.samples && peek.samples->size() == 5;
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                waited = !done;  // Waiting for the lock this thread holds
            }
            peeker.join();
            CHECK(waited && done);
            auto peek = PeekHistorySnapshot(at(0), true, {}, ULLONG_MAX);
            CHECK(peek.stamp == held && peek.status == g_historyStatus && g_historySamples.size() == 5 &&
                  settingsNotifications == 0 && g_testCalls[kTestRead] == reads);
        });

        Run("burn-up/journal: a 32-day window with its reset tolerance comes from memory; a stale one reads its older "
            "part from the file, whole, once; appends extend it without reading", [&] {
            const std::vector<HistorySample> records = hundred();
            BurnUpSampleCache cache;
            const long reads = g_testCalls[kTestRead];
            const ULONGLONG window = now - kBurnUpMaxWindowMs - kHistoryResetToleranceMs;
            tails(cache, {{7, 1, window}});
            CHECK(g_testCalls[kTestRead] == reads && Same(tailSamples(cache, 7), Since(records, window)));
            tails(cache, {{7, 1, now - 50 * kHistoryDayMs}});
            CHECK(g_testCalls[kTestRead] > reads && Same(tailSamples(cache, 7), Since(records, now - 50 * kHistoryDayMs)));
            HistorySample next = Reading(7, now, 1);
            Append({next});
            const long refilled = g_testCalls[kTestRead];
            tails(cache, {{7, 1, now - 50 * kHistoryDayMs}});
            std::vector<HistorySample> expected = Since(records, now - 50 * kHistoryDayMs);
            expected.push_back(next);
            CHECK(g_testCalls[kTestRead] == refilled && Same(tailSamples(cache, 7), expected));
        });

        Run("burn-up/journal: a stale window whose file read fails part-way loses its tail, and the next request "
            "refills it whole; trims, retention and Clear refill tails from the journal as it is", [&] {
            std::vector<HistorySample> records = hundred();
            BurnUpSampleCache cache;
            const ULONGLONG stale = now - 50 * kHistoryDayMs;
            Fault(kTestRead, 1);
            tails(cache, {{7, 1, stale}}, true);
            CHECK(!tailOf(cache, 7) && g_historyStatus == HistoryJournalError::None && g_historyWritable);
            HistorySample next = Reading(7, now, 1);
            Append({next});
            records.push_back(next);
            tails(cache, {{7, 1, stale}});
            CHECK(Same(tailSamples(cache, 7), Since(records, stale)));
            // The cache's trim (a new revision) refills it from memory and the file again.
            g_testNow += 2 * kHistoryDayMs;
            Maintain();
            tails(cache, {{7, 1, stale}});
            CHECK(Same(tailSamples(cache, 7), Since(records, stale)));
            // Retention pruning: none of the pruned readings come back.
            g_historyRetentionDays = 40;
            g_testNow += kHour;
            Maintain();
            tails(cache, {{7, 1, stale}});
            CHECK(Same(tailSamples(cache, 7), Since(records, g_testNow - 40 * kHistoryDayMs)));
            HistoryJournalError error = HistoryJournalError::None;
            CHECK(ClearHistory(&error));
            tails(cache, {{7, 1, stale}});
            CHECK(tailSamples(cache, 7).empty());
        });
    }
