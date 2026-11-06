// The journal on synthetic files (history/journal.h): loading, appends and their recovery, admission
// by the whole journal's summary, and storage compatibility. Hand-derived expectations.
    {
        const ULONGLONG now = kTestStart;
        // Account 7 every 6 hours from 100 days ago to an hour ago; account 9 only from 100 to 60 days ago.
        auto archive = [&] {
            std::vector<HistorySample> records;
            for (ULONGLONG t = now - 100 * kHistoryDayMs; t < now; t += 6 * kHour) {
                records.push_back(Reading(7, t, double(t % 97)));
                if (t < now - 60 * kHistoryDayMs) records.push_back(Reading(9, t + kMinute, 5));
            }
            return records;
        };

        Run("journal/load: an existing v1 journal loads whole into the summary and its last 35 days into memory; the "
            "file stays as it was", [&] {
            const std::vector<HistorySample> records = archive();
            WriteJournalFile(records);
            const std::string bytes = ReadJournalFile().bytes;
            Load();
            CHECK(g_historyLoaded && g_historyWritable && g_historyStatus == HistoryJournalError::None);
            CHECK(g_historyCommittedRecords == records.size() && g_historySummary.records == records.size());
            CHECK(g_historyCacheFrom == now - 35 * kHistoryDayMs);
            CHECK(Same(Cached(), Since(records, now - 35 * kHistoryDayMs)) && g_historySamples.size() == 35 * 4);
            CHECK(HistoryMayHaveSamples(9) && HistoryMayHaveSamples(7) && !HistoryMayHaveSamples(42));
            CHECK(ReadJournalFile().bytes == bytes);
            CheckMemory();
        });

        Run("journal/load: two journals with other archived prefixes and the same last 40 days hold the same memory; "
            "each summary counts its whole journal, an account only archived included", [&] {
            const std::vector<HistorySample> whole = archive();
            WriteJournalFile(whole);
            Load();
            const std::vector<HistorySample> cachedWhole = Cached();
            const uint64_t accounts = g_historySummary.accounts.size();
            ResetJournal();
            WriteJournalFile(Since(whole, now - 40 * kHistoryDayMs));
            Load();
            CHECK(Same(Cached(), cachedWhole) && accounts == 2 && g_historySummary.accounts.size() == 1);
            CHECK(g_historySummary.records == Since(whole, now - 40 * kHistoryDayMs).size() && !HistoryMayHaveSamples(9));
            CheckMemory();
        });

        Run("journal/load: a torn tail and a lone damaged last record are cut off by the next write, flushed; interior "
            "damage keeps the readable part, reports it and writes nothing until Clear", [&] {
            std::vector<HistorySample> records;
            for (int i = 0; i < 6; ++i) records.push_back(Reading(7, now - kHour + i * kMinute, i));
            WriteJournalFile({records.begin(), records.begin() + 5}, 100);
            Load();
            CHECK(g_historyCommittedRecords == 5 && g_historyWritable && g_historyStatus == HistoryJournalError::None);
            Append({records[5]});
            JournalFile file = ReadJournalFile();
            CHECK(Same(file.records, records) && !file.tail && g_testCalls[kTestFlush] == 1);
            CheckMemory();

            ResetJournal();
            std::vector<HistorySample> damaged = records;
            damaged[5].meters[1].value = 99;  // Its checksum no longer matches
            WriteJournalFile(damaged);
            Load();
            CHECK(g_historyCommittedRecords == 5 && g_historyWritable && g_historyStatus == HistoryJournalError::None);
            HistorySample next = Reading(7, now, 50);
            Append({next});
            std::vector<HistorySample> expected(records.begin(), records.begin() + 5);
            expected.push_back(next);
            CHECK(Same(ReadJournalFile().records, expected));
            CheckMemory();

            ResetJournal();
            damaged = records;
            damaged[2].meters[1].value = 99;
            WriteJournalFile(damaged);
            const std::string bytes = ReadJournalFile().bytes;
            Load();
            CHECK(g_historyCommittedRecords == 2 && !g_historyWritable && g_historyStatus == HistoryJournalError::DamagedRecord);
            Append({Reading(7, now + kMinute)});
            Maintain();
            CHECK(ReadJournalFile().bytes == bytes && Same(Cached(), {records[0], records[1]}));
            HistorySnapshot snapshot = ReadHistorySnapshot(0);
            CHECK(Same(snapshot.samples, {records[0], records[1]}) && snapshot.status == HistoryJournalError::DamagedRecord);
            HistoryJournalError error = HistoryJournalError::None;
            CHECK(ClearHistory(&error) && g_historyWritable && g_historyStatus == HistoryJournalError::None);
            JournalFile cleared = ReadJournalFile();
            CHECK(cleared.header && cleared.records.empty() && !cleared.tail && g_historySamples.empty());
            CheckMemory();
        });

        Run("journal/append: a failed write keeps the batch pending in memory (not saved); appends wait for the retry, "
            "which cuts off the torn part and saves every pending record once, flushed", [&] {
            std::vector<HistorySample> records;
            for (int i = 0; i < 6; ++i) records.push_back(Reading(7, now + i * kMinute, i));
            Append({records[0], records[1]});
            CHECK(Same(ReadJournalFile().records, {records[0], records[1]}) && g_historyCommittedRecords == 2);
            Fault(kTestWrite, 0);
            g_testPartialWrite = true;
            Append({records[2]});
            JournalFile torn = ReadJournalFile();
            CHECK(g_historyStatus == HistoryJournalError::NotSaved && g_historyPending.size() == 1 && torn.tail);
            CHECK(Same(Cached(), {records[0], records[1], records[2]}) && HistoryMayHaveSamples(7));
            long writes = g_testCalls[kTestWrite];
            Append({records[3]});
            CHECK(g_testCalls[kTestWrite] == writes && g_historyPending.size() == 2);
            g_testNow = now + kHour + 4 * kMinute;
            Append({records[4]});
            JournalFile file = ReadJournalFile();
            CHECK(Same(file.records, {records.begin(), records.begin() + 5}) && !file.tail);
            CHECK(g_historyPending.empty() && g_historyStatus == HistoryJournalError::None && g_testCalls[kTestFlush] == 1);
            Append({records[5]});
            CHECK(Same(ReadJournalFile().records, records) && g_testCalls[kTestFlush] == 1);
            CheckMemory();
        });

        Run("journal/append: a failed first header write leaves no journal the next write can't start over", [&] {
            HistorySample first = Reading(7, now), second = Reading(7, now + kMinute);
            Fault(kTestWrite, 0);
            g_testPartialWrite = true;
            Append({first});
            JournalFile partial = ReadJournalFile();
            CHECK(partial.exists && partial.bytes.size() == sizeof(HistoryFileHeader) / 2 && !g_historyHeaderValid);
            CHECK(g_historyStatus == HistoryJournalError::NotSaved && g_historyPending.size() == 1);
            g_testNow = now + kHour + kMinute;
            Append({second});
            JournalFile file = ReadJournalFile();
            CHECK(file.header && Same(file.records, {first, second}) && !file.tail && g_historyHeaderValid);
            CheckMemory();
        });

        Run("journal/append: a shortened or missing journal fails closed: nothing written, no hole, the records stay "
            "pending; a rewrite finds it lost and stops saving; Clear starts again", [&] {
            std::vector<HistorySample> records;
            for (int i = 0; i < 10; ++i) records.push_back(Reading(7, now - 40 * kHistoryDayMs + i * kHour, i));
            Append(records);
            CHECK(g_historyCommittedRecords == 10);
            const uint64_t shortened = sizeof(HistoryFileHeader) + 5 * sizeof(HistorySample);
            std::filesystem::resize_file(JournalPath(), shortened);
            Append({Reading(7, now)});
            CHECK(g_historyStatus == HistoryJournalError::CannotWrite && g_historyPending.size() == 1);
            CHECK(std::filesystem::file_size(JournalPath()) == shortened);
            g_testNow = now + kHour + kMinute;
            Append({Reading(7, g_testNow)});
            CHECK(g_historyStatus == HistoryJournalError::CannotWrite && g_historyPending.size() == 2 &&
                  std::filesystem::file_size(JournalPath()) == shortened);
            // A rewrite (retention pruning the 40-day-old records) finds it shorter than what it holds.
            g_testNow = now + 3 * kHour;
            g_historyRetentionDays = 30;
            Maintain();
            CHECK(!g_historyWritable && g_historyStatus == HistoryJournalError::ReadFailed &&
                  std::filesystem::file_size(JournalPath()) == shortened && !StagingFiles());
            std::filesystem::remove(JournalPath());
            Append({Reading(7, g_testNow)});
            CHECK(!std::filesystem::exists(JournalPath()) && g_historyPending.size() == 2);
            HistoryJournalError error = HistoryJournalError::None;
            CHECK(ClearHistory(&error) && g_historyWritable && g_historyPending.empty() && g_historySummary.records == 0);
            JournalFile cleared = ReadJournalFile();
            CHECK(cleared.header && cleared.records.empty() && !HistoryMayHaveSamples(7));
            HistorySample after = Reading(7, g_testNow + kMinute);
            Append({after});
            CHECK(Same(ReadJournalFile().records, {after}));
            CheckMemory();
        });

        Run("journal/append: pending records that age past memory's start before their write are saved once, then "
            "leave memory with a new revision", [&] {
            HistorySample first = Reading(7, now), second = Reading(7, now + kMinute);
            Append({first});
            Fault(kTestWrite, 0, 1000);
            Append({second});
            CHECK(g_historyPending.size() == 1);
            g_testNow = now + 40 * kHistoryDayMs;
            uint64_t revision = g_historyRevision;
            Maintain();  // Trims the hot window's start past both; the pending one stays.
            CHECK(g_historyCacheFrom == g_testNow - 35 * kHistoryDayMs && g_historyRevision == revision + 1);
            CHECK(Same(Cached(), {second}) && g_historyPending.size() == 1);
            ClearFaults();
            HistorySample third = Reading(7, g_testNow, 30);
            Append({third});
            CHECK(g_historyPending.size() == 2);  // The maintenance's failed write waits for its retry
            g_testNow += kHour + kMinute;
            HistorySample fourth = Reading(7, g_testNow, 40);
            revision = g_historyRevision;
            Append({fourth});
            CHECK(Same(ReadJournalFile().records, {first, second, third, fourth}) && g_historyPending.empty());
            CHECK(Same(Cached(), {third, fourth}) && g_historyRevision == revision + 1);
            CheckMemory();
        });

        Run("journal/append: admission follows the whole journal's summary: never before the last record (one only on "
            "disk too), one failed marker per outage, never before the retention's start", [&] {
            std::vector<HistorySample> old;
            for (int i = 0; i < 4; ++i) old.push_back(Reading(7, now - 100 * kHistoryDayMs + i * kHour, i, 1, 10 * kMinute, 5));
            old.back().failed = 1;
            old.back().meters = {};
            old.back().checksum = HistoryChecksum(old.back());
            WriteJournalFile(old);
            Load();
            CHECK(g_historySamples.empty() && g_historySummary.records == 4);
            Append({Reading(7, now - 150 * kHistoryDayMs)});
            CHECK(g_historySummary.records == 4);
            g_historyRetentionDays = 30;
            Append({Reading(8, now - 31 * kHistoryDayMs)});
            CHECK(g_historySummary.records == 4);
            g_historyRetentionDays = 0;
            HistorySample outage = old.back();
            outage.timeMs = now;
            Append({outage});  // The same outage, still
            CHECK(g_historySummary.records == 4);
            outage.session = 6;
            outage.checksum = HistoryChecksum(outage);
            HistorySample repeat = outage;
            repeat.timeMs = now + kMinute;
            Append({outage, repeat});
            CHECK(g_historySummary.records == 5 && Same(Cached(), {outage}));
            CheckMemory();
        });

        Run("journal/append: out of memory at any allocation admits nothing; enough memory admits the whole batch", [&] {
            Append({Reading(7, now - kHour)});
            bool admitted = false;
            for (long budget = 0; !admitted && budget < 200; ++budget) {
                const uint64_t records = g_historySummary.records, appended = g_historyAppendCount;
                const std::vector<HistorySample> cached = Cached();
                const ULONGLONG time = now + ULONGLONG(budget) * kMinute;
                const std::vector<HistorySample> batch{Reading(7, time), Reading(8, time)};
                g_testAllocations = budget;
                AppendHistory(batch, g_historyEpoch.load(), g_historySettingsGeneration.load());
                g_testAllocations = -1;
                admitted = g_historySummary.records != records;
                if (admitted) {
                    CHECK(g_historySummary.records == records + 2 && g_historyAppendCount == appended + 2 &&
                          g_historySamples.size() == cached.size() + 2 && g_historyPending.empty());
                } else {
                    CHECK(Same(Cached(), cached) && g_historyAppendCount == appended && g_historyPending.empty());
                }
                CheckMemory();
            }
            CHECK(admitted);
        });

        Run("journal/storage: unavailable storage, storage in use and a link in the journal's place are refused "
            "without writing; an unsupported header stays untouched", [&] {
            const std::wstring storage = g_testStorage;
            g_testStorage.clear();
            Append({Reading(7, now)});
            CHECK(!g_historyLoaded && !g_historyWritable && g_historyStatus == HistoryJournalError::Unavailable);
            ResetJournal();
            g_testStorage = storage;
            std::filesystem::create_directories(UserFolder());
            winrt::file_handle holder{CreateFileW((UserFolder() / L"history.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                                                  0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
            CHECK(bool(holder));
            Append({Reading(7, now)});
            CHECK(!g_historyLoaded && g_historyStatus == HistoryJournalError::InUse);
            HistorySnapshot snapshot = ReadHistorySnapshot(0);
            CHECK(!g_historyLoaded && snapshot.samples.empty() && !std::filesystem::exists(JournalPath()));
            holder.close();
            Append({Reading(7, now)});
            CHECK(g_historyLoaded && ReadJournalFile().records.size() == 1);

            ResetJournal();
            std::filesystem::remove(JournalPath());
            const std::filesystem::path other = UserFolder() / L"other.bin";
            WriteJournalFile({Reading(7, now - kHour)}, 0, other);
            CHECK(CreateHardLinkW(JournalPath().c_str(), other.c_str(), nullptr));
            const std::string bytes = ReadJournalFile(other).bytes;
            Append({Reading(7, now)});
            CHECK(g_historyLoaded && !g_historyWritable && g_historyStatus == HistoryJournalError::Unavailable);
            CHECK(ReadJournalFile(other).bytes == bytes);

            ResetJournal();
            std::filesystem::remove(JournalPath());
            WriteJournalFile({Reading(7, now - kHour)});
            std::string unsupported = ReadJournalFile().bytes;
            unsupported[0] ^= 1;
            {
                FILE* stream = _wfopen(JournalPath().c_str(), L"wb");
                fwrite(unsupported.data(), 1, unsupported.size(), stream);
                fclose(stream);
            }
            Append({Reading(7, now)});
            Maintain();
            CHECK(g_historyStatus == HistoryJournalError::Unsupported && ReadJournalFile().bytes == unsupported);
        });

        Run("journal/storage: an earlier version's journal still moves into the storage folder once, and loads", [&] {
            const std::filesystem::path legacy = std::filesystem::path(g_testLocalAppData) / L"TaskbarAIQuota" / WH_MOD_ID;
            const std::vector<HistorySample> records = archive();
            WriteJournalFile(records, 0, legacy / L"usage-history-v1.bin");
            const std::string bytes = ReadJournalFile(legacy / L"usage-history-v1.bin").bytes;
            PrepareHistoryStorage();
            CHECK(ReadJournalFile().bytes == bytes && !std::filesystem::exists(legacy) && g_historyLegacySettled);
            CHECK(!g_historyLoaded && g_testValues.empty());
            HistorySample next = Reading(7, now, 1);
            Append({next});
            std::vector<HistorySample> expected = records;
            expected.push_back(next);
            CHECK(Same(ReadJournalFile().records, expected) && g_historySummary.records == expected.size());
            CheckMemory();
        });
    }
