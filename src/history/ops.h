// Production ops of the usage history window's controller (history/controller.h): adapters over
// the open request, the journal, the published settings and polls, recording consent, the
// settings window, the journal's folder, the window's dialogs (history/modal.h), the window's own
// refresh message and deferral timer, and the current language; and the owner of the export's
// CSV write, its worker thread and poll timer. History thread only, except the worker.

// An export's write, shared by the history thread and its worker: the frozen scope and the
// destination, the rows written so far, the cancel request, and the result, published before
// `done` (release) and read after it (acquire).
struct HistoryWriteJob {
    HistoryExportScope scope;
    std::wstring destination;
    std::atomic<size_t> rows{0};
    std::atomic<bool> cancel{false}, done{false};
    HistoryCsvResult result;
};

struct HistoryProductionOps final : HistoryOps {
    // Wiring, set once the window exists: its host (the dialogs' owner; its popup and tooltip
    // close before each) and the context its dialogs run in.
    UiHost* host = nullptr;
    HistoryDialogContext dialogs;

    // The latest write and its worker (none: null), kept until the worker is joined
    std::unique_ptr<HistoryWriteJob> job;
    HANDLE writer = nullptr;

    // Every exit of the window's thread (UiHostRun's return or an exception) cancels the write and
    // waits for its worker, and releases the window's range from the journal's memory. The window
    // may close before: the worker never touches it.
    ~HistoryProductionOps() {
        if (job) job->cancel = true;
        JoinWriter();
        ReleaseHistoryPin();
    }

    uint64_t TakeRequest() override { return g_historyRequestedAccount.exchange(0); }

    // An explicit viewer read also prunes retained history while recording is off; it pins the
    // window's range in the journal's memory. Each call holds only the history leaf lock.
    HistorySnapshot ExplicitSnapshot(ULONGLONG from) override {
        MaintainHistory();
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
        std::wstring folder = UserStorageFolder();
        DWORD attributes = folder.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(folder.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        OpenUrl(folder.c_str());
        return true;
    }

    int Ask(const std::wstring& text, const std::wstring& caption, UINT type) override {
        UiHostClosePopup(*host);
        HistoryDialogScope scope(dialogs);
        return scope.hook ? MessageBoxW(host->main, text.c_str(), caption.c_str(), type) : 0;
    }

    // The export's save dialog; its overwrite prompt runs in the same scope. Its file types: each
    // a label in `locale`'s language and a pattern, both NUL-terminated; an empty one ends them.
    HistoryDestination ChooseDestination(const std::wstring& title, const I18nLocale& locale) override {
        UiHostClosePopup(*host);
        HistoryDialogScope scope(dialogs);
        if (!scope.hook) return {};
        std::wstring path = L"ai-quota-history.csv";  // i18n-en
        path.resize(32768);
        std::wstring filter;
        for (auto [label, pattern] : {std::pair{TrCtx(L"file-type", L"CSV files (*.csv)"), L"*.csv"},
                                      std::pair{TrCtx(L"file-type", L"All files (*.*)"), L"*.*"}}) {
            filter += I18nText(locale, label) + L'\0' + pattern + L'\0';
        }
        filter += L'\0';
        OPENFILENAMEW picker{};
        picker.lStructSize = sizeof(picker);
        picker.hwndOwner = host->main;
        picker.lpstrFilter = filter.c_str();
        picker.lpstrFile = path.data();
        picker.nMaxFile = DWORD(path.size());
        picker.lpstrDefExt = L"csv";
        picker.lpstrTitle = title.c_str();
        picker.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ENABLESIZING;
        if (GetSaveFileNameW(&picker)) return {HistoryDialogResult::Chosen, path.c_str()};
        DWORD error = CommDlgExtendedError();
        return {error ? HistoryDialogResult::Failed : HistoryDialogResult::Cancelled, {}, error};
    }

    // A new write once the previous worker has exited (one cancelled and still running refuses
    // it; the window's thread never waits for it): the poll timer first, so a write never starts
    // without it, then the worker.
    bool StartWrite(const HistoryExportScope& scope, const std::wstring& destination) override {
        if (writer && WaitForSingleObject(writer, 0) != WAIT_OBJECT_0) return false;
        JoinWriter();
        job = std::make_unique<HistoryWriteJob>();
        job->scope = scope;
        job->destination = destination;
        HWND window = g_historyWindow.load();
        if (!window || !SetTimer(window, kHistoryWriteTimer, 250, nullptr)) {
            job.reset();
            return false;
        }
        writer = CreateThread(nullptr, 0, WriteThread, job.get(), 0, nullptr);
        if (!writer) {
            Wh_Log(L"Could not create the export thread: %lu", GetLastError());
            KillTimer(window, kHistoryWriteTimer);
            job.reset();
            return false;
        }
        return true;
    }

    // Never waits: the write ends once its result is published and its worker is no longer
    // running (thread teardown can lag the result); only a worker known to have exited is reaped.
    std::optional<HistoryCsvResult> PollWrite(size_t& rows) override {
        if (!job) return HistoryCsvResult{0, ERROR_GEN_FAILURE, true};
        rows = job->rows.load(std::memory_order_relaxed);
        if (!job->done.load(std::memory_order_acquire)) return std::nullopt;
        DWORD wait = WaitForSingleObject(writer, 0);
        if (wait == WAIT_TIMEOUT) return std::nullopt;
        if (HWND window = g_historyWindow.load()) KillTimer(window, kHistoryWriteTimer);
        HistoryCsvResult result = job->result;
        if (wait == WAIT_OBJECT_0) JoinWriter();
        return result;
    }

    void CancelWrite() override {
        if (job) job->cancel = true;
        if (HWND window = g_historyWindow.load()) KillTimer(window, kHistoryWriteTimer);
    }

    // The worker, catching everything; it reads only its job and the unload flags. The export's
    // file safety: a new temporary file beside the destination (never an existing one), the rows,
    // flushed and closed, then moved over the destination with write-through (the commit point:
    // a cancellation after it leaves the file written). On any failure, cancellation, unload or
    // exception the temporary file is deleted and the destination keeps its content; the result
    // is published only after that cleanup. The rows written go out before each piece.
    static DWORD WINAPI WriteThread(LPVOID parameter) {
        HistoryWriteJob& job = *static_cast<HistoryWriteJob*>(parameter);
        HistoryCsvResult result;
        try {
            struct Temporary {
                HANDLE file = INVALID_HANDLE_VALUE;
                std::wstring path;
                ~Temporary() {
                    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                    if (!path.empty()) DeleteFileW(path.c_str());
                }
            } temporary;
            std::wstring candidate = job.destination + L".quota-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                     std::to_wstring(GetTickCount64()) + L".tmp";
            temporary.file = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
            if (temporary.file == INVALID_HANDLE_VALUE) {
                result.error = GetLastError();
                result.created = false;
            } else {
                temporary.path = std::move(candidate);
                auto cancelled = [&job] { return job.cancel || g_unloading || g_historyWindowCancelRequested; };
                HistoryCsvEnd end = HistoryCsvWrite(job.scope, result.rows, [&](const std::string& piece, bool) {
                    job.rows.store(result.rows, std::memory_order_relaxed);
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
                DWORD error = success ? ERROR_SUCCESS :
                              end == HistoryCsvEnd::Cancelled ? DWORD(ERROR_CANCELLED) : GetLastError();
                if (!CloseHandle(std::exchange(temporary.file, INVALID_HANDLE_VALUE)) && success) {
                    success = false;
                    error = GetLastError();
                }
                if (success && cancelled()) {
                    success = false;
                    error = ERROR_CANCELLED;
                }
                if (success && !MoveFileExW(temporary.path.c_str(), job.destination.c_str(),
                                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    success = false;
                    error = GetLastError();
                }
                if (success) temporary.path.clear();
                result.error = success ? 0 : error ? error : DWORD(ERROR_GEN_FAILURE);
            }
        } catch (...) {
            result.error = ERROR_GEN_FAILURE;
        }
        job.result = result;
        job.done.store(true, std::memory_order_release);
        return 0;
    }

    // Waits for the worker to exit, cancelling its blocking file calls every 100 ms (a stalled
    // destination), then drops it and its job. Only its exit ends the wait: a failed wait is
    // retried, never taken for one (unload must not outlive the worker).
    void JoinWriter() {
        if (writer) {
            // Windhawk's bundled import library lacks CancelSynchronousIo.
            using CancelIo_t = BOOL(WINAPI*)(HANDLE);
            static const auto cancelIo = reinterpret_cast<CancelIo_t>(
                GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CancelSynchronousIo"));
            bool logged = false;
            for (DWORD wait; (wait = WaitForSingleObject(writer, 100)) != WAIT_OBJECT_0;) {
                if (wait == WAIT_TIMEOUT) {
                    if (cancelIo) cancelIo(writer);
                    continue;
                }
                if (!std::exchange(logged, true)) Wh_Log(L"Could not wait for the export thread: %lu", GetLastError());
                Sleep(100);
            }
            CloseHandle(std::exchange(writer, nullptr));
        }
        job.reset();
    }

    bool Clear(HistoryJournalError& error) override { return ClearHistory(&error); }

    void InvalidateCurves() override { InvalidateBurnUpCurves(); }

    // Refused while an account in the settings holds the history ID: only a removal's settings
    // publication fences out batches still carrying its samples (DeleteAccountHistory).
    bool DeleteHistory(uint64_t historyId, HistoryJournalError& error) override {
        {
            std::lock_guard lock(g_settingsMutex);
            if (std::any_of(g_settings.accounts.begin(), g_settings.accounts.end(),
                            [&](const AccountConfig& account) { return account.historyId == historyId; })) {
                error = HistoryJournalError::AccountConfigured;
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

    I18nLocale Locale() override { return CurrentLocale(); }
};
