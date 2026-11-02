// Production ops of the usage history window's controller (history/controller.h): stateless
// adapters over the open request, the journal, the published settings and polls, recording
// consent, the settings window and the window's own refresh message and deferral timer. History
// thread only.

struct HistoryProductionOps final : HistoryOps {
    uint64_t TakeRequest() override { return g_historyRequestedAccount.exchange(0); }

    // As the classic window's load: an explicit viewer read also prunes retained history while
    // recording is off. Each call holds only the history leaf lock.
    HistorySnapshot ExplicitSnapshot(ULONGLONG from, int retentionDays) override {
        MaintainHistory(retentionDays);
        return ReadHistorySnapshot(from);
    }

    HistoryPeek PeekSnapshot(ULONGLONG from, bool samples, const HistoryJournalStamp& held,
                             ULONGLONG heldFrom) override {
        return PeekHistorySnapshot(from, samples, held, heldFrom);
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

    // The classic window's Enable recording transaction (history/window.h): the edit lock, the
    // unload check once it is held, consent through the shared edit path (it persists this
    // user's consent without changing account selections), the lock released before marshaling
    // to taskbar threads, then the windows notified whatever the result.
    HistoryConsentResult ApplyConsent() override {
        SettingsApplyResult applied;
        {
            std::lock_guard configLock(g_configEditMutex);
            if (g_unloading) return HistoryConsentResult::Stopped;
            Settings settings;
            {
                std::lock_guard settingsLock(g_settingsMutex);
                settings = g_settings;
            }
            settings.historyEnabled = true;
            applied = ApplyOwnedSettings(std::move(settings));
        }
        if (applied == SettingsApplyResult::Changed) FinishSettingsApply();
        NotifySettingsWindowChanged(true);
        return applied == SettingsApplyResult::Failed ? HistoryConsentResult::Failed : HistoryConsentResult::Applied;
    }

    void OpenSettings() override { OpenSettingsWindow(); }

    bool PostRefresh() override {
        HWND window = g_historyUiWindow.load();
        return window && PostMessageW(window, kHistoryRefreshMessage, 0, 0);
    }

    void DeferralTimer(bool on) override {
        HWND window = g_historyUiWindow.load();
        if (!window) return;
        if (!on) KillTimer(window, kHistoryDeferralTimer);
        else if (!SetTimer(window, kHistoryDeferralTimer, 250, nullptr)) winrt::throw_last_error();
    }

    bool Unloading() override { return g_unloading || g_historyWindowCancelRequested; }

    ULONGLONG Now() override { return NowUnixMs(); }

    SYSTEMTIME LocalTime(ULONGLONG ms) override {
        SYSTEMTIME local{};
        return UnixMsToLocalSystemTime(ms, &local) ? local : SYSTEMTIME{};
    }
};
