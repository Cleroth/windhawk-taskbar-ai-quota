// The journal's rewrite transactions on synthetic files (history/journal.h): retention, the size
// cap, Antigravity thinning (streamed through the thinning machine across chunks), Clear and an
// account's deletion, each a staging file replacing the journal or nothing changed; and what memory
// holds next to the file (OBSERVE lines: an observation, no limits).
    {
        const ULONGLONG now = kTestStart;
        auto megabytes = [](uint64_t records) { return double(records * sizeof(HistorySample)) / (1024 * 1024); };
        auto observe = [&](const char* state) {
            std::cout << "OBSERVE journal-memory state=" << state << " archive_records=" << g_historySummary.records
                      << " archive_mb=" << megabytes(g_historySummary.records)
                      << " cache_records=" << g_historySamples.size() << " cache_mb=" << megabytes(g_historySamples.size())
                      << '\n';
        };
        // `count` readings of accounts 1 and 2 in turn, evenly over `span` before now.
        auto spread = [&](size_t count, ULONGLONG span) {
            std::vector<HistorySample> records;
            records.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                records.push_back(Reading(1 + i % 2, now - span + ULONGLONG(i) * (span / count), double(i % 1000) / 10));
            }
            return records;
        };

        Run("journal/retention: maintenance prunes what the days no longer keep in one rewrite, memory and summary "
            "with it, under a new revision; with nothing to prune it rewrites nothing", [&] {
            std::vector<HistorySample> records;
            for (ULONGLONG t = now - 60 * kHistoryDayMs + 3 * kHour; t < now; t += 6 * kHour) records.push_back(Reading(7, t));
            WriteJournalFile(records);
            Load();
            const uint64_t revision = g_historyRevision;
            g_historyRetentionDays = 30;
            Maintain();
            CHECK(Same(ReadJournalFile().records, Since(records, now - 30 * kHistoryDayMs)) &&
                  g_historyRevision == revision + 1 && g_historySummary.oldestMs >= now - 30 * kHistoryDayMs);
            CheckMemory();
            const std::string bytes = ReadJournalFile().bytes;
            const long moves = g_testCalls[kTestMove];
            g_testNow += kHour;
            Maintain();
            CHECK(g_testCalls[kTestMove] == moves && ReadJournalFile().bytes == bytes);
        });

        Run("journal/cap: with a number of days, maintenance keeps the newest 250,000 of what the days keep, and an "
            "append past the cap evicts the oldest to 225,000, each in one rewrite", [&] {
            const std::vector<HistorySample> records = spread(300000, 100 * kHistoryDayMs);
            WriteJournalFile(records);
            Load();
            g_historyRetentionDays = 90;
            Maintain();
            std::vector<HistorySample> kept = Since(records, now - 90 * kHistoryDayMs);
            CHECK(kept.size() > 250000);
            kept.erase(kept.begin(), kept.end() - 250000);
            CHECK(Same(ReadJournalFile().records, kept) && g_historyCapped);
            CheckMemory();
            observe("capped");
            HistorySample next = Reading(1, now, 1);
            Append({next});
            kept.push_back(next);
            kept.erase(kept.begin(), kept.end() - 225000);
            CHECK(Same(ReadJournalFile().records, kept) && g_historyCommittedRecords == 225000 && g_historyPending.empty());
            CheckMemory();
            observe("capped-append");
        });

        Run("journal/forever: past 250,000 records nothing prunes or caps; memory holds the last 35 days, the history "
            "window's range while it is open, then the last 35 days again", [&] {
            const std::vector<HistorySample> records = spread(300000, 730 * kHistoryDayMs);
            WriteJournalFile(records);
            Load();
            CHECK(g_historyCommittedRecords == 300000 && Same(Cached(), Since(records, now - 35 * kHistoryDayMs)));
            observe("loaded");
            Maintain();
            HistorySample next = Reading(1, now, 1);
            Append({next});
            CHECK(g_testCalls[kTestMove] == 0 && g_historyCommittedRecords == 300001 && !g_historyCapped &&
                  std::filesystem::file_size(JournalPath()) == sizeof(HistoryFileHeader) + 300001 * sizeof(HistorySample));
            observe("appended");
            uint64_t revision = g_historyRevision;
            HistorySnapshot all = ReadHistorySnapshot(0);
            CHECK(all.samples.size() == 300001 && g_historySamples.size() == 300001 && g_historyRevision == revision &&
                  all.from == 0);
            observe("viewer-all-retained");
            ReleaseHistoryPin();
            CHECK(g_historyRevision == revision + 1 && g_historySamples.size() == Since(records, now - 35 * kHistoryDayMs).size() + 1);
            observe("viewer-closed");
            ReadHistorySnapshot(now - 90 * kHistoryDayMs);
            g_testNow += kHour;
            Maintain();
            CHECK(g_historyCacheFrom == now - 90 * kHistoryDayMs);
            observe("viewer-90-days-maintained");
            ReleaseHistoryPin();
            CHECK(g_historyCacheFrom == g_testNow - 35 * kHistoryDayMs);
            observe("viewer-closed-maintained");
            CheckMemory();
        });

        // Antigravity readings a minute apart from `from`: the 5-hour and weekly Gemini pools at steady levels, their
        // resets after the readings (4 days, a week), as Antigravity records them.
        auto antigravity = [](uint64_t account, ULONGLONG time, ULONGLONG from) {
            HistorySample sample{};
            sample.accountId = account;
            sample.provider = 3;
            sample.session = 1;
            sample.timeMs = time;
            sample.intervalMs = kMinute;
            sample.meters[0] = {40, from + 4 * kHistoryDayMs, 5 * kHour};
            sample.meters[1] = {20, from + kHistoryWeekMs, kHistoryWeekMs};
            sample.checksum = HistoryChecksum(sample);
            return sample;
        };

        Run("journal/thinning: maintenance streams Antigravity thinning across chunks: interleaved accounts, one ending "
            "early, each first and last older reading kept, other providers and recent readings untouched, the order "
            "kept and every record valid", [&] {
            const ULONGLONG from = now - 20 * kHistoryDayMs;
            // Account 31 each minute for 3000 minutes, 32 for the first 1500, Anthropic 9 every 10 minutes, all
            // interleaved; then 31's last hour.
            std::vector<HistorySample> records, expected;
            auto add = [&](const HistorySample& sample, bool kept, bool widened) {
                records.push_back(sample);
                if (!kept) return;
                HistorySample& result = expected.emplace_back(sample);
                if (widened) {
                    result.intervalMs = 10 * kMinute;
                    result.checksum = HistoryChecksum(result);
                }
            };
            for (ULONGLONG k = 0; k < 3000; ++k) {
                ULONGLONG time = from + k * kMinute;
                bool last = k == 2999;
                add(antigravity(31, time, from), k % 10 == 0 || last, k % 10 == 0 && !last);
                if (k < 1500) add(antigravity(32, time + 1000, from), k % 10 == 0 || k == 1499, k % 10 == 0 && k != 1499);
                if (k % 10 == 0) add(Reading(9, time + 2000), true, false);
            }
            for (ULONGLONG k = 0; k <= 60; ++k) add(antigravity(31, now - kHour + k * kMinute, now - kHour), true, false);
            CHECK(records.size() > 4 * kHistoryChunkRecords);
            WriteJournalFile(records);
            Maintain();
            CHECK(Same(ReadJournalFile().records, expected) && g_historyThinnedMs == now);
            CheckMemory();
            // Thinned once a day: the next day finds nothing more to drop, and rewrites nothing.
            const long moves = g_testCalls[kTestMove];
            g_testNow += kHistoryDayMs;
            Maintain();
            CHECK(g_testCalls[kTestMove] == moves && g_historyThinnedMs == g_testNow);
        });

        Run("journal/thinning: thinned again each day as readings age: each pass is final for its day, and the file "
            "matches thinning all at once", [&] {
            const ULONGLONG from = now - 20 * kHistoryDayMs;
            std::vector<HistorySample> records;
            for (ULONGLONG k = 0; k < 3 * 24 * 60; ++k) {
                records.push_back(antigravity(7, from + k * kMinute, from));
            }
            const ULONGLONG first = from + 15 * kHistoryDayMs + kHour;  // Only the first hour is 15 days old
            WriteJournalFile(records);
            for (ULONGLONG day = 0; day <= 3; ++day) {
                g_testNow = first + day * kHistoryDayMs;
                Maintain();
                const long moves = g_testCalls[kTestMove];
                g_historyThinnedMs = 0;
                Maintain();
                CHECK(g_testCalls[kTestMove] == moves);
            }
            const JournalFile daily = ReadJournalFile();
            CHECK(daily.records.size() == 3 * 24 * 6 + 1);
            ResetJournal();
            WriteJournalFile(records);
            Maintain();
            CHECK(ReadJournalFile().bytes == daily.bytes);
            CheckMemory();
        });

        Run("journal/deletion: an account's deletion rewrites the journal without its samples, on disk and in memory, "
            "under a new revision; the peek replaces, the burn-up tails refill; an absent account rewrites nothing", [&] {
            std::vector<HistorySample> records;
            for (int i = 0; i < 4; ++i) records.push_back(Reading(9, now - 100 * kHistoryDayMs + i * kHour));
            for (int i = 0; i < 5; ++i) {
                records.push_back(Reading(7, now - 5 * kHour + i * kHour, 10.0 * i));
                records.push_back(Reading(9, now - 5 * kHour + i * kHour + kHour / 2));
            }
            WriteJournalFile(records);
            Load();
            const HistoryJournalStamp held = PeekHistorySnapshot(0, false, {}, 0).stamp;
            const BurnUpArchiveReader reader = [](ULONGLONG from, const std::function<void(const HistorySample&)>& use) {
                return ReadHistoryRangeLocked(from, use) == HistoryPass::Done;
            };
            BurnUpSampleCache cache;
            {
                std::lock_guard lock(g_historyMutex);
                UpdateBurnUpTails(cache, g_historySamples, g_historyCacheFrom, g_historyEpoch, g_historyRevision,
                                  g_historyAppendCount, {{7, 1, now - kHistoryDayMs}, {9, 1, now - kHistoryDayMs}}, false,
                                  reader);
            }
            CHECK(cache.accounts.size() == 2 && cache.accounts[1].samples.size() == 5);
            settingsNotifications = 0;
            HistoryJournalError error = HistoryJournalError::None;
            CHECK(DeleteAccountHistory(9, &error));
            std::vector<HistorySample> sevens;
            for (const auto& record : records) {
                if (record.accountId == 7) sevens.push_back(record);
            }
            CHECK(Same(ReadJournalFile().records, sevens) && Same(Cached(), sevens) && !HistoryMayHaveSamples(9));
            CHECK(g_historyRevision == held.revision + 1 && settingsNotifications == 1);
            HistoryPeek peek = PeekHistorySnapshot(now - kHistoryDayMs, true, held, now - kHistoryDayMs);
            CHECK(peek.samples && Same(*peek.samples, sevens) && !peek.incomplete);
            {
                std::lock_guard lock(g_historyMutex);
                UpdateBurnUpTails(cache, g_historySamples, g_historyCacheFrom, g_historyEpoch, g_historyRevision,
                                  g_historyAppendCount, {{7, 1, now - kHistoryDayMs}}, false, reader);
            }
            CHECK(cache.accounts.size() == 1 && cache.accounts[0].historyId == 7 && cache.accounts[0].samples.size() == 5);
            const long moves = g_testCalls[kTestMove];
            CHECK(DeleteAccountHistory(42, &error) && g_testCalls[kTestMove] == moves && g_historyRevision == held.revision + 1);
            CheckMemory();
        });

        Run("journal/clear: Clear empties the file, memory, summary and the pending records under a new epoch", [&] {
            WriteJournalFile(spread(1000, 10 * kHistoryDayMs));
            Load();
            Fault(kTestWrite, 0);
            Append({Reading(1, now, 5)});
            CHECK(g_historyPending.size() == 1);
            const uint64_t epoch = g_historyEpoch;
            HistoryJournalError error = HistoryJournalError::None;
            CHECK(ClearHistory(&error) && g_historyEpoch == epoch + 1);
            JournalFile file = ReadJournalFile();
            CHECK(file.header && file.records.empty() && g_historySamples.empty() && g_historyPending.empty() &&
                  g_historySummary.records == 0 && g_historyStatus == HistoryJournalError::None && !HistoryMayHaveSamples(1));
            CheckMemory();
        });

        // Accounts 7 and 9 every 10 minutes over the last 20 days (5,760 records, across chunks), on disk.
        auto twenty = [&] {
            std::vector<HistorySample> records;
            for (ULONGLONG t = now - 20 * kHistoryDayMs; t < now; t += 10 * kMinute) {
                records.push_back(Reading(7, t, double(t % 89)));
                records.push_back(Reading(9, t + 1000, double(t % 83)));
            }
            return records;
        };

        Run("journal/rewrite: write, flush and replace failures, a failed read and unload part-way leave the file and "
            "memory as they were, report no success and leave no staging file; damage found part-way stops writes; a "
            "restart loads it whole and the retry succeeds", [&] {
            const std::vector<HistorySample> records = twenty();
            WriteJournalFile(records);
            Load();
            const std::string bytes = ReadJournalFile().bytes;
            const std::vector<HistorySample> cached = Cached();
            const uint64_t revision = g_historyRevision;
            auto unchanged = [&] {
                CHECK(ReadJournalFile().bytes == bytes && Same(Cached(), cached) && g_historyRevision == revision &&
                      g_historySummary.records == records.size() && g_historyCommittedRecords == records.size() &&
                      !StagingFiles());
            };
            HistoryJournalError error = HistoryJournalError::None;
            int reads = 0;
            g_testOnRead = [&] {
                if (++reads == 3) g_unloading = true;
            };
            CHECK(!DeleteAccountHistory(9, &error));
            unchanged();
            CHECK(g_historyStatus == HistoryJournalError::None && g_historyWritable);
            g_testOnRead = {};
            g_unloading = false;
            for (TestFileApi api : {kTestWrite, kTestFlush, kTestMove, kTestRead}) {
                Fault(api, api == kTestWrite ? 1 : api == kTestRead ? 3 : 0);
                CHECK(!DeleteAccountHistory(9, &error));
                unchanged();
                CHECK(g_historyStatus == HistoryJournalError::WriteFailed && g_historyWritable &&
                      error == HistoryJournalError::WriteFailed);
                ClearFaults();
            }
            ResetJournal();
            Load();
            CHECK(g_historyCommittedRecords == records.size() && Same(Cached(), cached));
            CheckMemory();
            CHECK(DeleteAccountHistory(9, &error));
            std::vector<HistorySample> sevens;
            for (const auto& record : records) {
                if (record.accountId == 7) sevens.push_back(record);
            }
            CHECK(Same(ReadJournalFile().records, sevens) && g_historyStatus == HistoryJournalError::None);
            CheckMemory();
            // A record changed on disk since: the pass stops at it and nothing is saved over the journal any more.
            std::string damaged = ReadJournalFile().bytes;
            damaged[sizeof(HistoryFileHeader) + 1500 * sizeof(HistorySample) + 40] ^= 1;
            {
                FILE* stream = _wfopen(JournalPath().c_str(), L"wb");
                fwrite(damaged.data(), 1, damaged.size(), stream);
                fclose(stream);
            }
            CHECK(!DeleteAccountHistory(7, &error) && error == HistoryJournalError::DamagedRecord && !g_historyWritable);
            CHECK(ReadJournalFile().bytes == damaged && !StagingFiles());
        });

        Run("journal/rewrite: unload during the last chunk's read or among the pending records cancels before the "
            "move: the journal isn't replaced, pending records stay, nothing is reported", [&] {
            std::vector<HistorySample> records = twenty();
            records.erase(records.begin(), records.end() - 600);  // One chunk
            WriteJournalFile(records);
            Load();
            const std::string bytes = ReadJournalFile().bytes;
            int reads = 0;
            g_testOnRead = [&] {
                if (++reads == 2) g_unloading = true;  // The header, then the only chunk
            };
            HistoryJournalError error = HistoryJournalError::None;
            CHECK(!DeleteAccountHistory(9, &error) && reads == 2);
            CHECK(ReadJournalFile().bytes == bytes && g_historyCommittedRecords == 600 && !StagingFiles() &&
                  g_historyStatus == HistoryJournalError::None);
            g_testOnRead = {};
            g_unloading = false;
            Fault(kTestWrite, 0);
            Append({Reading(7, now, 1)});
            CHECK(g_historyPending.size() == 1);
            g_unloading = true;
            const uint64_t epoch = g_historyEpoch;
            CHECK(!ClearHistory(&error) && g_historyEpoch == epoch + 1);
            CHECK(ReadJournalFile().bytes == bytes && g_historyPending.size() == 1 && g_historySummary.records == 601 &&
                  !StagingFiles());
            // A pending write stopped by unload writes nothing more and keeps its records.
            g_unloading = false;
            g_testNow += kHour + kMinute;
            g_testOnRead = [] { g_unloading = true; };
            Append({Reading(7, g_testNow, 2)});
            CHECK(ReadJournalFile().bytes == bytes && g_historyPending.size() == 2 && g_historyCommittedRecords == 600);
        });

        Run("journal/rewrite: unload among a large pending backlog stops the rewrite at the next record: nothing more "
            "is written and the journal isn't replaced", [&] {
            HistorySample first = Reading(7, now - kHour);
            Append({first});
            Fault(kTestWrite, 0, 1000);
            for (int round = 0; round < 50; ++round) {
                std::vector<HistorySample> batch;
                for (int i = 0; i < 100; ++i) batch.push_back(Reading(1000 + i, now + ULONGLONG(round) * kMinute));
                Append(batch);
            }
            ClearFaults();
            CHECK(g_historyPending.size() == 5000);
            const std::string bytes = ReadJournalFile().bytes;
            int writes = 0;
            g_testOnWrite = [&] {
                if (++writes == 2) g_unloading = true;  // The staging file's header, then its first full chunk
            };
            HistoryJournalError error = HistoryJournalError::None;
            CHECK(!DeleteAccountHistory(42, &error) && writes == 2);
            CHECK(ReadJournalFile().bytes == bytes && g_historyPending.size() == 5000 && !StagingFiles() &&
                  g_historyCommittedRecords == 1);
        });

        Run("journal/append: saved pending records release their storage, after a recovery as after Clear", [&] {
            Append({Reading(7, now - kHour)});
            Fault(kTestWrite, 0, 1000);
            std::vector<HistorySample> batch;
            for (int round = 0; round < 50; ++round) {
                batch.clear();
                for (int i = 0; i < 100; ++i) batch.push_back(Reading(1000 + i, now + ULONGLONG(round) * kMinute));
                Append(batch);
            }
            CHECK(g_historyPending.size() == 5000 && g_historyPending.capacity() >= 5000);
            ClearFaults();
            g_testNow = now + 2 * kHour;
            Append({Reading(7, g_testNow)});
            CHECK(g_historyPending.empty() && g_historyPending.capacity() == 0 && g_historyCommittedRecords == 5002);
            CheckMemory();
            Fault(kTestWrite, 0, 1000);
            Append(batch);  // Refused: not after the last record
            Append({Reading(7, g_testNow + kMinute), Reading(8, g_testNow + kMinute)});
            CHECK(g_historyPending.size() == 2 && g_historyPending.capacity() >= 2);
            ClearFaults();
            HistoryJournalError error = HistoryJournalError::None;
            CHECK(ClearHistory(&error) && g_historyPending.capacity() == 0);
        });

        Run("journal/maintenance: running out of memory in its thinning pass changes nothing and is retried at the next "
            "maintenance; memory is still trimmed", [&] {
            const ULONGLONG from = now - 20 * kHistoryDayMs;
            std::vector<HistorySample> records;
            for (ULONGLONG k = 0; k < 600; ++k) records.push_back(antigravity(31, from + k * kMinute, from));
            WriteJournalFile(records);
            Load();
            g_testNow = now + 2 * kHistoryDayMs;
            const std::string bytes = ReadJournalFile().bytes;
            g_testAllocations = 0;
            Maintain();
            g_testAllocations = -1;
            CHECK(ReadJournalFile().bytes == bytes && g_historyCommittedRecords == records.size() && !g_historyThinnedMs);
            CHECK(g_historyCacheFrom == g_testNow - 35 * kHistoryDayMs && !g_historyWriteRetryMs);
            CheckMemory();
            Maintain();
            CHECK(ReadJournalFile().records.size() == 61 && g_historyThinnedMs == g_testNow);
            CheckMemory();
        });

        Run("journal/rewrite: out of memory at any allocation of a deletion leaves the journal as it was; enough "
            "memory deletes", [&] {
            std::vector<HistorySample> records = twenty();
            records.erase(records.begin(), records.end() - 600);
            WriteJournalFile(records);
            Load();
            const std::string bytes = ReadJournalFile().bytes;
            const std::vector<HistorySample> cached = Cached();
            bool deleted = false;
            long budget = 0;
            for (; !deleted && budget < 5000; ++budget) {
                HistoryJournalError error = HistoryJournalError::None;
                g_testAllocations = budget;
                deleted = DeleteAccountHistory(9, &error);
                g_testAllocations = -1;
                if (!deleted) {
                    CHECK(ReadJournalFile().bytes == bytes && Same(Cached(), cached) && !StagingFiles() &&
                          g_historyCommittedRecords == records.size());
                }
            }
            CHECK(deleted && budget > 1);
            std::erase_if(records, [](const HistorySample& record) { return record.accountId == 9; });
            CHECK(Same(ReadJournalFile().records, records));
            CheckMemory();
        });
    }
