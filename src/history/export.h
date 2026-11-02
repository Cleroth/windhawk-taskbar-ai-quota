// Native dialog tracking and CSV export.

// A thread-local CBT hook tracks native save/confirmation dialogs, including nested
// overwrite prompts. Owner-close defers destruction until the modal API has returned.
struct HistoryModalScope;
static thread_local HistoryModalScope* g_historyModalScope = nullptr;

struct HistoryModalScope {
    HistoryWindowState& state;
    HHOOK hook = nullptr;
    std::array<HWND, 16> dialogs{};
    size_t count = 0;
    explicit HistoryModalScope(HistoryWindowState& owner);
    ~HistoryModalScope() {
        if (!hook) return;  // A rejected nested scope must not clear its active outer scope.
        UnhookWindowsHookEx(hook);
        g_historyModalScope = nullptr;
        g_historyDialog.store(nullptr);
        state.modal = false;
        if (state.closing || g_unloading || g_historyWindowCancelRequested)
            PostMessageW(state.window, WM_CLOSE, 0, 0);
        else if (std::exchange(state.reloadAfterModal, false))
            PostMessageW(state.window, kHistoryLoadMessage, 1, 0);
    }
};

static LRESULT CALLBACK HistoryDialogCbtProc(int code, WPARAM wParam, LPARAM lParam) {
    // Only fixed-size storage and Win32 calls here: no exceptions across a hook boundary.
    auto* scope = g_historyModalScope;
    HWND window = reinterpret_cast<HWND>(wParam);
    if (scope && code == HCBT_ACTIVATE) {
        wchar_t name[24]{};
        if (GetClassNameW(window, name, ARRAYSIZE(name)) && wcscmp(name, L"#32770") == 0) {
            auto end = scope->dialogs.begin() + scope->count;
            if (std::find(scope->dialogs.begin(), end, window) == end && scope->count < scope->dialogs.size())
                scope->dialogs[scope->count++] = window;
            g_historyDialog.store(window);
            if (scope->state.closing || g_unloading || g_historyWindowCancelRequested) {
                PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
        }
    } else if (scope && code == HCBT_DESTROYWND) {
        for (size_t i = 0; i < scope->count; ++i) {
            if (scope->dialogs[i] != window) continue;
            for (size_t j = i + 1; j < scope->count; ++j) scope->dialogs[j - 1] = scope->dialogs[j];
            --scope->count;
            break;
        }
        HWND remaining = scope->count ? scope->dialogs[scope->count - 1] : nullptr;
        g_historyDialog.store(remaining);
        if (remaining && (scope->state.closing || g_unloading || g_historyWindowCancelRequested)) {
            PostMessageW(remaining, WM_COMMAND, IDCANCEL, 0);
            PostMessageW(remaining, WM_CLOSE, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

HistoryModalScope::HistoryModalScope(HistoryWindowState& owner) : state(owner) {
    if (state.modal || state.closing || g_unloading || g_historyWindowCancelRequested) return;
    HistoryClearHover(state);
    state.modal = true;
    g_historyModalScope = this;
    hook = SetWindowsHookExW(WH_CBT, HistoryDialogCbtProc, nullptr, GetCurrentThreadId());
    if (!hook) {
        g_historyModalScope = nullptr;
        state.modal = false;
        SetWindowTextW(state.status, L"ERROR: cannot track native dialogs safely; operation cancelled.");
    }
}

// Keep an existing CSV intact if writing fails. This owns the temporary file until
// a same-directory rename succeeds; labels never enter CSV or the suggested filename.
struct HistoryCsvFile {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring temporary;
    ~HistoryCsvFile() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (!temporary.empty()) DeleteFileW(temporary.c_str());
    }
    bool Write(const std::string& text) {
        size_t written = 0;
        while (written < text.size()) {
            if (g_unloading || g_historyWindowCancelRequested) {
                SetLastError(ERROR_CANCELLED);
                return false;
            }
            DWORD count = DWORD(std::min(text.size() - written, size_t(65536)));
            DWORD actual = 0;
            if (!WriteFile(handle, text.data() + written, count, &actual, nullptr)) return false;
            if (!actual) { SetLastError(ERROR_WRITE_FAULT); return false; }
            written += actual;
        }
        return true;
    }
    // Serialization boundary for both UI export and isolated file-content tests: the shared CSV
    // writer (history/csv.h) over the unreduced, stably timestamp-sorted snapshot (never the
    // render cache), with the status line's progress every 250 ms.
    bool WriteSnapshot(const HistoryWindowState& state, size_t* exported) {
        HistoryExportScope scope;
        // Borrowed (an empty owner): the window's state outlives the write.
        scope.snapshot = std::shared_ptr<const HistorySnapshot>(std::shared_ptr<const HistorySnapshot>{}, &state.snapshot);
        scope.accountId = state.selected >= 0 ? state.accounts.at(state.selected).id : 0;
        scope.from = state.readFrom;
        scope.to = state.to;
        ULONGLONG nextProgressMs = 0;
        HistoryCsvEnd end = HistoryCsvWrite(scope, *exported, [&](const std::string& buffer, bool last) {
            if (!last && state.status && GetTickCount64() >= nextProgressMs) {
                std::wstring progress = L"Exporting " + std::to_wstring(*exported) + L" rows...";
                SetWindowTextW(state.status, progress.c_str());
                UpdateWindow(state.status);
                nextProgressMs = GetTickCount64() + 250;
            }
            return Write(buffer);
        }, [] { return g_unloading || g_historyWindowCancelRequested; });
        if (end == HistoryCsvEnd::Cancelled) SetLastError(ERROR_CANCELLED);
        return end == HistoryCsvEnd::Written;
    }
};

static void HistoryExport(HistoryWindowState& state) {
    if ((state.selected >= 0 && size_t(state.selected) >= state.accounts.size()) || state.modal || state.loading) return;
    size_t estimatedRows = 0;
    uint64_t selectedId = state.selected < 0 ? 0 : state.accounts[state.selected].id;
    for (const auto& sample : state.snapshot.samples) {
        if ((!selectedId || sample.accountId == selectedId) &&
            sample.timeMs >= state.readFrom && sample.timeMs < state.to) {
            estimatedRows += sample.provider == 1 ? 5 : sample.provider == 2 ? 3 : 4;
        }
    }
    if (estimatedRows > 500000) {
        std::wstring warning = L"This export contains about " + std::to_wstring(estimatedRows) +
            L" rows. It may take a while and exceed spreadsheet row limits. "
            L"The history window will be busy while writing; Explorer's taskbar remains available. Continue?";
        HistoryModalScope modal(state);
        if (!modal.hook || MessageBoxW(state.window, warning.c_str(), L"Large history export",
                                      MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) return;
    }
    if (state.closing || g_unloading || g_historyWindowCancelRequested) return;
    wchar_t path[32768] = L"ai-quota-history.csv";
    DWORD dialogError = 0;
    bool accepted = false;
    {
        HistoryModalScope modal(state);
        if (!modal.hook) return;
        OPENFILENAMEW picker{};
        picker.lStructSize = sizeof(picker);
        picker.hwndOwner = state.window;
        picker.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0";
        picker.lpstrFile = path;
        picker.nMaxFile = ARRAYSIZE(path);
        picker.lpstrDefExt = L"csv";
        picker.lpstrTitle = state.selected < 0
            ? L"Export all accounts — all meters, selected range (both weeks if comparing)"
            : L"Export selected account — all meters, selected range (both weeks if comparing)";
        picker.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                       OFN_NOCHANGEDIR | OFN_ENABLESIZING;
        accepted = GetSaveFileNameW(&picker) != FALSE;
        if (!accepted) dialogError = CommDlgExtendedError();
    }
    if (state.closing || g_unloading || g_historyWindowCancelRequested) return;
    if (!accepted) {
        HistorySetStatus(state, dialogError ? L"ERROR: Save dialog failed (code " + std::to_wstring(dialogError) + L")."
                                            : L"CSV export cancelled; no file written.");
        return;
    }
    HistoryCsvFile output;
    std::wstring candidate = std::wstring(path) + L".quota-" + std::to_wstring(GetCurrentProcessId()) +
                             L"-" + std::to_wstring(GetTickCount64()) + L".tmp";
    output.handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output.handle == INVALID_HANDLE_VALUE) {
        HistorySetStatus(state, L"ERROR: cannot create CSV temporary file (Windows error " +
                         std::to_wstring(GetLastError()) + L"). Existing destination unchanged.");
        return;
    }
    output.temporary = std::move(candidate);
    size_t exported = 0;
    HistorySetStatus(state, L"Exporting local history...");
    UpdateWindow(state.status);
    bool success = output.WriteSnapshot(state, &exported);
    if (success) success = FlushFileBuffers(output.handle) != FALSE;
    DWORD error = success ? ERROR_SUCCESS : GetLastError();
    if (!CloseHandle(output.handle) && success) { success = false; error = GetLastError(); }
    output.handle = INVALID_HANDLE_VALUE;
    if (success && (g_unloading || g_historyWindowCancelRequested)) { success = false; error = ERROR_CANCELLED; }
    if (success) {
        success = MoveFileExW(output.temporary.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (success) output.temporary.clear();
        else error = GetLastError();
    }
    HistorySetStatus(state, success
        ? L"Exported " + std::to_wstring(exported) + (state.selected < 0 ? L" meter rows (all accounts) to " : L" meter rows (selected account) to ") +
          path + L". Labels omitted; times UTC." +
          (state.overlay ? L" Includes both elapsed 7-day periods." : L"")
        : L"ERROR: CSV export failed (Windows error " + std::to_wstring(error) + L"). Existing destination unchanged.");
}
