// Production ops of the usage history window's controller (history/controller.h): stateless
// adapters over the open request, the journal, the published settings and polls, recording
// consent, the settings window, the journal's folder, the window's dialogs (history/modal.h), the
// CSV file and the window's own refresh and write messages and deferral timer. History thread
// only.

struct HistoryProductionOps final : HistoryOps {
    // Wiring, set once the window exists: its host (the dialogs' owner; its popup and tooltip
    // close before each) and the context its dialogs run in.
    UiHost* host = nullptr;
    HistoryDialogContext dialogs;

    uint64_t TakeRequest() override { return g_historyRequestedAccount.exchange(0); }

    // An explicit viewer read also prunes retained history while recording is off. Each call
    // holds only the history leaf lock.
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

    // Enable recording: the edit lock, the unload check once it is held, consent through the
    // shared edit path (it persists this user's consent without changing account selections),
    // the lock released before marshaling to taskbar threads, then the windows notified whatever
    // the result.
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

    // As Settings > History's link: the folder from a link launch thread (ShellExecute's own
    // message loop would run this window's close, activation and refresh inside the command).
    bool OpenFolder() override {
        PWSTR localAppData = nullptr;
        std::wstring folder;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &localAppData))) {
            folder = std::wstring(localAppData) + L"\\TaskbarAIQuota\\" WH_MOD_ID;
        }
        CoTaskMemFree(localAppData);
        DWORD attributes = folder.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(folder.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        OpenUrl(folder.c_str());
        return true;
    }

    int Ask(const std::wstring& text, PCWSTR caption, UINT type) override {
        UiHostClosePopup(*host);
        HistoryDialogScope scope(dialogs);
        return scope.hook ? MessageBoxW(host->main, text.c_str(), caption, type) : 0;
    }

    // The export's save dialog; its overwrite prompt runs in the same scope.
    HistoryDestination ChooseDestination(const std::wstring& title) override {
        UiHostClosePopup(*host);
        HistoryDialogScope scope(dialogs);
        if (!scope.hook) return {};
        std::wstring path = L"ai-quota-history.csv";
        path.resize(32768);
        OPENFILENAMEW picker{};
        picker.lStructSize = sizeof(picker);
        picker.hwndOwner = host->main;
        picker.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0";
        picker.lpstrFile = path.data();
        picker.nMaxFile = DWORD(path.size());
        picker.lpstrDefExt = L"csv";
        picker.lpstrTitle = title.c_str();
        picker.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ENABLESIZING;
        if (GetSaveFileNameW(&picker)) return {HistoryDialogResult::Chosen, path.c_str()};
        DWORD error = CommDlgExtendedError();
        return {error ? HistoryDialogResult::Failed : HistoryDialogResult::Cancelled, {}, error};
    }

    bool PostWrite() override {
        HWND window = g_historyWindow.load();
        return window && PostMessageW(window, kHistoryWriteMessage, 0, 0);
    }

    // The export's file safety: a new temporary file beside the destination (never an existing
    // one), the rows, flushed and closed, then moved over the destination with write-through. On
    // any failure, unload or exception the temporary file is deleted and the destination keeps
    // its content. Progress goes out before each full piece.
    HistoryCsvResult WriteCsv(const HistoryExportScope& scope, const std::wstring& destination,
                              const std::function<void(size_t)>& progress) override {
        struct Temporary {
            HANDLE file = INVALID_HANDLE_VALUE;
            std::wstring path;
            ~Temporary() {
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                if (!path.empty()) DeleteFileW(path.c_str());
            }
        } temporary;
        HistoryCsvResult result;
        std::wstring candidate = destination + L".quota-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                 std::to_wstring(GetTickCount64()) + L".tmp";
        temporary.file = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                     nullptr);
        if (temporary.file == INVALID_HANDLE_VALUE) {
            result.error = GetLastError();
            result.created = false;
            return result;
        }
        temporary.path = std::move(candidate);
        auto cancelled = [] { return g_unloading || g_historyWindowCancelRequested; };
        ULONGLONG nextProgressMs = 0;
        HistoryCsvEnd end = HistoryCsvWrite(scope, result.rows, [&](const std::string& piece, bool last) {
            if (!last && GetTickCount64() >= nextProgressMs) {
                progress(result.rows);
                nextProgressMs = GetTickCount64() + 250;
            }
            for (size_t written = 0; written < piece.size();) {
                if (cancelled()) {
                    SetLastError(ERROR_CANCELLED);
                    return false;
                }
                DWORD count = DWORD(std::min(piece.size() - written, size_t(65536))), actual = 0;
                if (!WriteFile(temporary.file, piece.data() + written, count, &actual, nullptr)) return false;
                if (!actual) {
                    SetLastError(ERROR_WRITE_FAULT);
                    return false;
                }
                written += actual;
            }
            return true;
        }, cancelled);
        bool success = end == HistoryCsvEnd::Written && FlushFileBuffers(temporary.file);
        DWORD error = success ? ERROR_SUCCESS : end == HistoryCsvEnd::Cancelled ? DWORD(ERROR_CANCELLED) : GetLastError();
        if (!CloseHandle(std::exchange(temporary.file, INVALID_HANDLE_VALUE)) && success) {
            success = false;
            error = GetLastError();
        }
        if (success && cancelled()) {
            success = false;
            error = ERROR_CANCELLED;
        }
        if (success && !MoveFileExW(temporary.path.c_str(), destination.c_str(),
                                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            success = false;
            error = GetLastError();
        }
        if (success) temporary.path.clear();
        result.error = success ? 0 : error ? error : DWORD(ERROR_GEN_FAILURE);
        return result;
    }

    bool Clear(std::wstring& error) override { return ClearHistory(&error); }

    void InvalidateCurves() override { InvalidateBurnUpCurves(); }

    // Refused while an account in the settings holds the history ID: only a removal's settings
    // publication fences out batches still carrying its samples (DeleteAccountHistory).
    bool DeleteHistory(uint64_t historyId, std::wstring& error) override {
        {
            std::lock_guard lock(g_settingsMutex);
            if (std::any_of(g_settings.accounts.begin(), g_settings.accounts.end(),
                            [&](const AccountConfig& account) { return account.historyId == historyId; })) {
                error = L"An account in Settings uses this history again.";
                return false;
            }
        }
        return DeleteAccountHistory(historyId, &error);
    }

    bool PostRefresh() override {
        HWND window = g_historyWindow.load();
        return window && PostMessageW(window, kHistoryRefreshMessage, 0, 0);
    }

    void DeferralTimer(bool on) override {
        HWND window = g_historyWindow.load();
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
