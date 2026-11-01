// Production ops of the usage history window's controller (history/controller.h): stateless
// adapters over the open request, the journal, the published settings and polls, and the settings
// window. History thread only.

struct HistoryProductionOps final : HistoryOps {
    uint64_t TakeRequest() override { return g_historyRequestedAccount.exchange(0); }

    // As the classic window's load: an explicit viewer read also prunes retained history while
    // recording is off. Each call holds only the history leaf lock.
    HistorySnapshot ExplicitSnapshot(ULONGLONG from, int retentionDays) override {
        MaintainHistory(retentionDays);
        return ReadHistorySnapshot(from);
    }

    // Settings, then the polls published in account order, in lock order; never inside the
    // history lock.
    HistorySettingsSnapshot ReadSettings() override {
        HistorySettingsSnapshot snapshot;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            snapshot.settings = g_settings;
            std::lock_guard<std::mutex> dataLock(g_dataMutex);
            snapshot.polls = g_data;
        }
        snapshot.idsPendingSave = g_historyIdsPendingSave.load();
        snapshot.storageError = g_historyStorageError.load();
        return snapshot;
    }

    void OpenSettings() override { OpenSettingsWindow(); }

    bool Unloading() override { return g_unloading || g_historyWindowCancelRequested; }

    ULONGLONG Now() override { return NowUnixMs(); }

    SYSTEMTIME LocalTime(ULONGLONG ms) override {
        SYSTEMTIME local{};
        return UnixMsToLocalSystemTime(ms, &local) ? local : SYSTEMTIME{};
    }
};
