// Production store of the settings window's edit controller (settings/edits.h): the published
// settings and statuses, and one transaction per edit. Settings thread only.

struct SettingsProductionStore final : SettingsStore {
    SettingsBaseline Load() override {
        SettingsBaseline baseline;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            baseline.settings = g_settings;
        }
        SettingsStatus& status = baseline.status;
        status.recording = g_historyIdsPendingSave ? SettingsRecording::PendingSave :
                           g_historyStorageError   ? SettingsRecording::StorageError :
                                                     SettingsRecording::Recording;
        // Display N is the Nth taskbar in the order the taskbar code targets (primary first).
        std::vector<TaskbarDisplayInfo> taskbars = FindCurrentProcessTaskbarDisplays();
        for (size_t i = 0; i < taskbars.size(); i++) {
            const RECT& rect = taskbars[i].rect;
            status.displays.push_back({int(i) + 1, int(std::abs(rect.right - rect.left)),
                                       int(std::abs(rect.bottom - rect.top)), taskbars[i].primary});
        }
        // Where the journal lives (history/journal.h), whether or not anything was recorded.
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr,
                                           &localAppData))) {
            status.historyFolder = std::wstring(localAppData) + L"\\TaskbarAIQuota\\" WH_MOD_ID;
        }
        CoTaskMemFree(localAppData);
        wchar_t separator[8] = {};
        if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, separator,
                            ARRAYSIZE(separator)) > 1) {
            status.groupSeparator = separator[0];
        }
        return baseline;
    }

    // Under the edit lock: fresh settings, the requested field set, ApplyOwnedSettings. The
    // taskbar update runs after the lock is released.
    SettingsSaveResult Apply(SettingsField field, int value) override {
        std::unique_lock<std::mutex> configLock(g_configEditMutex);
        Settings settings;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            settings = g_settings;
        }
        SettingsAssign(settings, field, value);
        SettingsApplyResult result = ApplyOwnedSettings(std::move(settings));
        configLock.unlock();
        if (result == SettingsApplyResult::Failed) return SettingsSaveResult::Failed;
        if (result == SettingsApplyResult::Unchanged) return SettingsSaveResult::Unchanged;
        FinishSettingsApply();
        return SettingsSaveResult::Changed;
    }

    bool Unloading() override { return g_unloading || g_settingsWindowCancelRequested; }
};
