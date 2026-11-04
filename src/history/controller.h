// Usage history window controller: the selection by history ID, each mode's view, the range and
// the week comparison, the lines the legend hides, explicit loads with the open request they
// take, live refresh from notifications, Enable recording, the operations (Export, Clear, Delete
// history) and the More menu's dialogs, the view the frame builders read (history/view.h), and
// the chart laid out for its item with the lines the pointer picks on it (history/chart.h). Pure,
// like the settings edit controller: it talks to HistoryOps (production: history/ops.h; tests:
// in-memory ops) and reads no globals.

// What ReadSettings returns: the published settings (with this user's recording consent),
// whether the taskbar has yet to save account history IDs, the journal's storage error flag, and
// the published polls, parallel to the settings' accounts (possibly shorter).
struct HistorySettingsSnapshot {
    Settings settings;
    bool idsPendingSave = false;
    bool storageError = false;
    std::vector<AccountData> polls;
};

// Enable recording's consent transaction: stopped by unload before it changed or notified
// anything, failed (recording stays off), or applied.
enum class HistoryConsentResult : uint8_t { Stopped, Failed, Applied };

// The save dialog: a path chosen, cancelled, failed (`error`: its common dialog error), or never
// shown because its dialogs couldn't be tracked.
enum class HistoryDialogResult : uint8_t { Chosen, Cancelled, Failed, Untracked };
struct HistoryDestination {
    HistoryDialogResult result = HistoryDialogResult::Untracked;
    std::wstring path;
    DWORD error = 0;
};

// A CSV write: the rows written, and the Windows error that stopped it (0: the file replaced the
// destination; ERROR_CANCELLED: unload); `created`: its temporary file was created.
struct HistoryCsvResult {
    size_t rows = 0;
    DWORD error = 0;
    bool created = true;
};

// What the controller reads and asks for. Production ops are stateless adapters: every request,
// load, refresh, deferral, operation and close state is the controller's.
struct HistoryOps {
    // The latest open request's history ID (0: none), cleared.
    virtual uint64_t TakeRequest() = 0;
    // Prunes the journal to the published retention, then reads its samples from `from` on with
    // their stamp; may initialize or retry the storage.
    virtual HistorySnapshot ExplicitSnapshot(ULONGLONG from) = 0;
    // Memory only, never initializing or retrying the storage: the journal's stamp, status and
    // cap, and, when `samples` asks, a replacement for a snapshot held with `held` from
    // `heldFrom` (the journal loaded, and another stamp or `from` further back).
    virtual HistoryPeek PeekSnapshot(ULONGLONG from, bool samples, const HistoryJournalStamp& held,
                                     ULONGLONG heldFrom) = 0;
    virtual HistorySettingsSnapshot ReadSettings() = 0;
    // Turns this user's recording on through the guarded settings transaction, then notifies
    // the settings and history windows whatever the result, unless unload stopped it.
    virtual HistoryConsentResult ApplyConsent() = 0;
    virtual void OpenSettings() = 0;
    // The journal's folder in Explorer; false when nothing was ever recorded (no folder yet).
    virtual bool OpenFolder() = 0;
    // A message box over the window, its dialogs tracked (history/modal.h): the close latch or
    // unload ends it. Its answer; 0: it couldn't be shown safely.
    virtual int Ask(const std::wstring& text, PCWSTR caption, UINT type) = 0;
    // The save dialog for a CSV, titled `title`, tracked as Ask (its overwrite prompt too).
    virtual HistoryDestination ChooseDestination(const std::wstring& title) = 0;
    // Posts the window's write message (Write); false when it couldn't.
    virtual bool PostWrite() = 0;
    // Writes the scope's CSV to a new temporary file beside `destination` and replaces the
    // destination with it once it is whole and flushed; any failure or unload deletes it and
    // leaves the destination as it was. `progress` hears the rows written so far, at most every
    // 250 ms.
    virtual HistoryCsvResult WriteCsv(const HistoryExportScope& scope, const std::wstring& destination,
                                      const std::function<void(size_t)>& progress) = 0;
    // Clears the journal (`error`: why it couldn't), then the flyout's burn-up curves are dropped.
    virtual bool Clear(std::wstring& error) = 0;
    virtual void InvalidateCurves() = 0;
    // Deletes every sample of a removed account (`error`: why it couldn't, including an account in
    // the settings holding its history ID again).
    virtual bool DeleteHistory(uint64_t historyId, std::wstring& error) = 0;
    // Posts the window's refresh message (ProcessRefresh); false when it couldn't.
    virtual bool PostRefresh() = 0;
    // Starts or stops the window's 250 ms deferral check (DeferralTick). A check that can't
    // start throws: the window fault-closes rather than strand held work.
    virtual void DeferralTimer(bool on) = 0;
    // Unload or cancellation: nothing loads or acts any more.
    virtual bool Unloading() = 0;
    // Unix ms, and one as the user's local clock and zone (zero on failure).
    virtual ULONGLONG Now() = 0;
    virtual SYSTEMTIME LocalTime(ULONGLONG ms) = 0;

protected:
    ~HistoryOps() = default;
};

// What a composed series set was built for: the snapshot's generation, the settings' revision
// (labels, account recording and visibility, weekly selections, consent, red threshold), the
// times, the effective view, the selection (history ID) and the comparison.
struct HistorySeriesKey {
    uint64_t generation = 0, settings = 0;
    ULONGLONG from = 0, to = 0, readFrom = 0;
    HistoryView view = HistoryView::Weekly;
    uint64_t selected = 0;
    bool compare = false;
    bool operator==(const HistorySeriesKey&) const = default;
};

// The running operation: Export (its warning and save dialog), its write (posted once the
// dialog chose a file), Clear (its confirmation, then the clear), Delete (its confirmation, then
// the deletion).
enum class HistoryOperation : uint8_t { None, Export, Writing, Clear, Delete };

// Operation notices shared by several of their ends. A cancelled operation shows none.
static constexpr PCWSTR kHistoryExportNotStarted = L"CSV export could not start; no file written.";
static constexpr PCWSTR kHistoryUntracked = L"Cannot track native dialogs safely; operation cancelled.";

// Samples in time order, journal order among equal times. The journal keeps that order, so
// this usually only checks it.
static void HistorySortSamples(std::vector<HistorySample>& samples) {
    auto earlier = [](const HistorySample& a, const HistorySample& b) { return a.timeMs < b.timeMs; };
    if (!std::is_sorted(samples.begin(), samples.end(), earlier)) {
        std::stable_sort(samples.begin(), samples.end(), earlier);
    }
}

struct HistoryController {
    explicit HistoryController(HistoryOps& ops) : ops(&ops) {}

    // Ops, and the view the frame is built from (all but its theme), recomposed by each event
    HistoryOps* ops;
    HistoryUiView view;

    // Selection (0: All accounts), each mode's view, the range (from the settings' default when the
    // window opens) and the week comparison (it forces 7 days; the range stays for afterwards)
    uint64_t selectedId = 0;
    HistoryView allView = HistoryView::Weekly, accountView = HistoryView::AllMeters;
    HistoryUiRange range = HistoryUiRange::Week;
    bool compare = false;

    // The last settings read and its revision (one per change). The snapshot (none before the
    // first load) with the stamp it was read with, from `heldFrom`, and its generation (one per
    // snapshot); the times shown ([readFrom, from) is the previous week while comparing) and the
    // accounts collected over them, whose rows index the snapshot. The journal's latest status
    // and cap, from the latest load or peek.
    HistorySettingsSnapshot settings;
    uint64_t settingsRevision = 0;
    std::shared_ptr<const HistorySnapshot> snapshot;
    ULONGLONG heldFrom = 0;
    uint64_t generation = 0;
    ULONGLONG from = 0, to = 0, readFrom = 0;
    std::vector<HistoryUiAccount> accounts;
    std::wstring journalStatus;
    bool journalCapped = false;

    // The series the view was composed from, and what for
    HistorySeriesSet series;
    HistorySeriesKey seriesKey;

    // Navigation slots by history ID, for the session (never reused); the lines the legend hides
    // for the session (history ID, meter), whatever the selection, view or range
    std::map<uint64_t, uint32_t> slots;
    std::vector<std::pair<uint64_t, int>> hidden;

    // An operation's notice, and a request to focus the selected entry the window takes
    HistoryUiNotice notice;
    bool focusSelected = false;

    // The chart as the last build laid it out, and the lines the pointer picks on it
    HistoryChart chart;
    std::vector<size_t> picked;

    // Live refresh. queued: a refresh message is posted and not handled yet. liveDirty: the live
    // part is outstanding (settings, polls, the journal's status); samplesDirty: the sample part
    // (snapshot, times). A modal or an operation holds both (its end resumes them), so does the
    // open popup (the deferral timer checks it); an inspection (the pointer on the chart, or a
    // readout showing) holds the sample part (its end resumes it; the timer also checks it).
    // Invariant: while work is outstanding and the window isn't closing, it is queued, a modal or
    // an operation runs, or the timer runs.
    bool queued = false, liveDirty = false, samplesDirty = false;
    int modal = 0;
    bool pointerOnChart = false, readoutShown = false;
    bool timer = false;

    // Operations, one at a time; during one, and during a modal, only the legend's chips act and
    // an explicit load the window asks for waits (loadPending: once after it, taking the latest
    // request). An export's scope, frozen at its command, its destination and the rows written.
    HistoryOperation operation = HistoryOperation::None;
    bool loadPending = false;
    std::optional<HistoryExportScope> exportScope;
    std::wstring destination;
    size_t exported = 0;

    // Set once the window closes (the first step of every close, before a running dialog is
    // cancelled): nothing loads or acts any more, and an operation ends at its next step
    bool closeRequested = false;

    // The window opened, on the settings' default range: until the first load, the pill and the
    // configured accounts come from the settings alone. The open request waits for that load.
    void Open() {
        if (closeRequested || ops->Unloading()) return;
        HistorySettingsSnapshot read = ops->ReadSettings();
        range = read.settings.historyDefaultRange;
        if (Adopt(std::move(read), nullptr, 0, 0, 0, 0, 0)) Compose();
    }

    // An explicit load (the first one, activation, refresh, a range or comparison change, after
    // Enable recording or a Clear): takes the open request, reads the settings and the journal
    // from the range's start (and the week before it while comparing) and collects the accounts
    // over them. A listed request selects its account; otherwise the selection stays while it is
    // listed, else All accounts. It satisfies outstanding refresh work and a waiting load.
    void Load() {
        if (closeRequested || ops->Unloading()) return;
        loadPending = false;
        uint64_t request = ops->TakeRequest();
        HistorySettingsSnapshot read = ops->ReadSettings();
        auto [start, end, readStart] = Times(read.settings.historyRetentionDays);
        auto loaded = std::make_shared<HistorySnapshot>(ops->ExplicitSnapshot(readStart));
        HistorySortSamples(loaded->samples);
        std::wstring status = loaded->status;
        bool capped = loaded->capped;
        if (!Adopt(std::move(read), std::move(loaded), readStart, start, end, readStart, request)) return;
        journalStatus = std::move(status);
        journalCapped = capped;
        liveDirty = samplesDirty = false;
        Timer(false);
        Compose();
    }

    // An explicit load the window asks for (its first, the activation request, F5, Ctrl+R): now,
    // or once a running modal or operation ends.
    void RequestLoad() {
        if (closeRequested || ops->Unloading()) return;
        if (Busy()) loadPending = true;
        else Load();
    }

    // A control's command (history/view.h): the selection, the view and a legend chip's line
    // recompose; the range, the comparison and Refresh load; Export, the More menu's Clear all
    // history and a removed account's Delete history start their operations; the menu's
    // Settings, Open history folder and About these charts open those; Enable recording turns
    // recording on. During a modal or an operation only the legend's chips act (the rest are
    // disabled or behind the modal).
    void Command(HistoryUiCommand command, int value = 0, uint64_t key = 0) {
        if (closeRequested || ops->Unloading() || (Busy() && command != HistoryUiCommand::Line)) return;
        switch (command) {
            case HistoryUiCommand::Select:
                if (key != selectedId) notice = {};
                selectedId = key;
                Compose();
                break;
            case HistoryUiCommand::View:
                (view.selected < 0 ? allView : accountView) =
                    HistoryView(std::clamp(value, 0, int(HistoryView::AllMeters)));
                Compose();
                break;
            case HistoryUiCommand::Range:
                range = HistoryUiRange(std::clamp(value, 0, int(HistoryUiRange::All)));
                Load();
                break;
            case HistoryUiCommand::Compare:
                compare = value != 0;
                Load();
                break;
            case HistoryUiCommand::Refresh:
                Load();
                break;
            case HistoryUiCommand::Export:
                Export();
                break;
            case HistoryUiCommand::Menu:
                switch (HistoryUiMenuItem(value)) {
                    case HistoryUiMenuItem::Settings:
                        ops->OpenSettings();
                        break;
                    case HistoryUiMenuItem::Folder:
                        if (!ops->OpenFolder()) {
                            notice = {L"Nothing has been recorded yet. The folder is created with the first recorded "
                                      L"sample."};
                            Compose();
                        }
                        break;
                    case HistoryUiMenuItem::About:
                        About();
                        break;
                    case HistoryUiMenuItem::Clear:
                        Clear();
                        break;
                }
                break;
            case HistoryUiCommand::EnableRecording:
                EnableRecording();
                break;
            case HistoryUiCommand::DeleteHistory:
                DeleteHistory(key);
                break;
            case HistoryUiCommand::Line: {
                auto line = std::find(hidden.begin(), hidden.end(), std::pair{key, value});
                if (line != hidden.end()) hidden.erase(line);
                else hidden.push_back({key, value});
                Compose();
                break;
            }
            case HistoryUiCommand::None:
                break;
        }
    }

    // Enable recording (the recording-off banner's button, once loaded): the consent
    // transaction, which notifies the windows. Unless unload stopped it or the window closed
    // meanwhile, an explicit load follows whatever the result; then a failure's notice (after the
    // load, so nothing replaces it), else the selected entry takes focus. A notice from before
    // goes: this is the next operation.
    void EnableRecording() {
        if (!snapshot) return;
        notice = {};
        HistoryConsentResult result = ops->ApplyConsent();
        if (result == HistoryConsentResult::Stopped || closeRequested || ops->Unloading()) return;
        Load();
        if (result == HistoryConsentResult::Failed) {
            notice = {L"Could not save recording consent. Recording is still off.", UiSeverity::Error};
            Compose();
        } else {
            focusSelected = true;
        }
    }

    // The window is closing (its close path's first step, before a running dialog is cancelled):
    // requests stay with the opener; outstanding refresh work, a waiting load and the deferral
    // timer go. A running operation ends when its dialog returns.
    void RequestClose() {
        closeRequested = true;
        liveDirty = samplesDirty = loadPending = false;
        Timer(false);
    }

    // Operations and dialogs -----------------------------------------------------------------------

    // Export (once loaded). Its scope freezes at the command: the selected account or every one,
    // the shown times with the week before while comparing, and the snapshot itself, so what
    // arrives during its dialogs (activation, refresh) changes nothing it writes. A large one asks
    // first (Cancel the default); then the save dialog titled by its scope. Every return from a
    // dialog checks the close latch and unload. With a file chosen the write is posted: the window
    // shows its progress before the write starts (Write).
    void Export() {
        if (!snapshot) return;
        notice = {};
        HistoryExportScope scope{snapshot, view.selected >= 0 ? selectedId : 0, readFrom, to, compare};
        size_t estimate = 0;
        for (const HistorySample& sample : snapshot->samples) {
            if ((!scope.accountId || sample.accountId == scope.accountId) && sample.timeMs >= scope.from &&
                sample.timeMs < scope.to) {
                estimate += sample.provider == 1 ? 5 : sample.provider == 2 ? 3 : sample.provider == 4 ? 1 : 4;
            }
        }
        operation = HistoryOperation::Export;
        if (estimate > 500000) {
            int answer = Ask(L"This export contains about " + std::to_wstring(estimate) +
                                 L" rows. It may take a while and exceed spreadsheet row limits. The history window "
                                 L"will be busy while writing; Explorer's taskbar remains available. Continue?",
                             L"Large history export", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
            if (Stopped()) return;
            if (!answer) return Finish({kHistoryUntracked, UiSeverity::Error});
            if (answer != IDOK) return Finish({});
        }
        ModalBegan();
        HistoryDestination chosen = ops->ChooseDestination(
            scope.accountId ? L"Export selected account \u2014 all meters, selected range (both weeks if comparing)" :
                              L"Export all accounts \u2014 all meters, selected range (both weeks if comparing)");
        ModalEnded();
        if (Stopped()) return;
        switch (chosen.result) {
            case HistoryDialogResult::Chosen:
                break;
            case HistoryDialogResult::Cancelled:
                return Finish({});
            case HistoryDialogResult::Failed:
                return Finish({L"Save dialog failed (code " + std::to_wstring(chosen.error) + L").", UiSeverity::Error});
            case HistoryDialogResult::Untracked:
                return Finish({kHistoryUntracked, UiSeverity::Error});
        }
        exportScope = std::move(scope);
        destination = std::move(chosen.path);
        exported = 0;
        operation = HistoryOperation::Writing;
        if (!ops->PostWrite()) return Finish({kHistoryExportNotStarted, UiSeverity::Error});
        Compose();
    }

    // The posted write, `shown`: the window painted its progress (else it is closing, or the frame
    // with the progress never came: nothing is written). It runs to its end without returning to
    // the window: each progress report sets the rows written and asks `repaint` to show them,
    // nothing else. Its notice: the rows and where they went, or why the destination is unchanged.
    void Write(bool shown, const std::function<void()>& repaint) {
        if (operation != HistoryOperation::Writing || !exportScope || Stopped()) return;
        if (!shown) return Finish({kHistoryExportNotStarted, UiSeverity::Error});
        HistoryCsvResult result = ops->WriteCsv(*exportScope, destination, [&](size_t rows) {
            exported = rows;
            repaint();
        });
        if (Stopped()) return;
        std::wstring error = std::to_wstring(result.error);
        if (!result.created) {
            return Finish({L"Cannot create the CSV temporary file (Windows error " + error +
                               L"). Existing destination unchanged.", UiSeverity::Error});
        }
        if (result.error) {
            return Finish({L"CSV export failed (Windows error " + error + L"). Existing destination unchanged.",
                           UiSeverity::Error});
        }
        Finish({L"Exported " + HistoryUiCount(result.rows) + L" meter rows (" +
                (exportScope->accountId ? L"selected account" : L"all accounts") + L") to " + destination +
                L". Labels omitted; times UTC." + (exportScope->compare ? L" Includes both elapsed 7-day periods." : L"")});
    }

    // Clear all history (the More menu): asked first (Cancel the default). Whatever its result the
    // flyout's burn-up curves are dropped (the journal's epoch moved); a success loads, a failure
    // doesn't (the next refresh replaces the snapshot by the new epoch). Its notice after either.
    void Clear() {
        notice = {};
        operation = HistoryOperation::Clear;
        int answer = Ask(L"Permanently clear ALL local usage history for this Windows user, including deleted "
                         L"accounts?\r\n\r\nThis cannot be undone. Settings and sign-ins are not changed. If recording "
                         L"stays enabled, future polls will record new history.",
                         L"Clear all usage history", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        if (Stopped()) return;
        if (!answer) return Finish({kHistoryUntracked, UiSeverity::Error});
        if (answer != IDOK) return Finish({});
        std::wstring error;
        bool cleared = ops->Clear(error);
        ops->InvalidateCurves();
        if (Stopped()) return;
        operation = HistoryOperation::None;
        if (!cleared) return Finish({L"Could not clear all history. " + error, UiSeverity::Error});
        Load();
        Finish({L"All local history cleared. Future polls can start recording again."});
    }

    // Delete history (a listed removed account's info bar): asked first, naming the account
    // (Cancel the default). A success loads, which no longer lists the account (All accounts is
    // selected); a failure keeps it selected. Its notice after either.
    void DeleteHistory(uint64_t id) {
        auto account = std::find_if(accounts.begin(), accounts.end(), [&](const HistoryUiAccount& a) { return a.id == id; });
        if (!snapshot || account == accounts.end() || account->configured) return;
        std::wstring label = account->label;
        notice = {};
        operation = HistoryOperation::Delete;
        int answer = Ask(L"Permanently delete all usage history of the removed " +
                             std::wstring(HistoryUiProviderName(account->provider)) + L" account " + label +
                             L"?\r\n\r\nEvery recorded sample of it is deleted, not only the selected range. This cannot "
                             L"be undone.",
                         L"Delete account history", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
        if (Stopped()) return;
        if (!answer) return Finish({kHistoryUntracked, UiSeverity::Error});
        if (answer != IDOK) return Finish({});
        std::wstring error;
        bool deleted = ops->DeleteHistory(id, error);
        if (Stopped()) return;
        operation = HistoryOperation::None;
        if (!deleted) return Finish({L"Could not delete this account's history. " + error, UiSeverity::Error});
        Load();
        Finish({L"Deleted the history of removed account " + label + L"."});
    }

    // About these charts (the More menu): how to read the window, the retention and the oldest
    // sample loaded.
    void About() {
        std::wstring text =
            L"Reading the charts\r\n"
            L"These are sampled quota levels, not tokens or daily consumption. Choose an account in the sidebar, or "
            L"All accounts, and a view: Short-term, Weekly, Extra usage or All meters. All accounts starts on Weekly, "
            L"one account on All meters; each keeps its view until the window closes. The window opens on the "
            L"Default time span set in Settings > History. Weekly follows each Anthropic "
            L"account's taskbar weekly selections; removed accounts keep both weeklies, and All meters includes "
            L"weeklies hidden on the taskbar. Antigravity Gemini and Claude/GPT quotas share the matching short-term "
            L"and weekly views. Each quota keeps its own line and resets; overlapping limits are never added or "
            L"merged. Short-window labels use recorded durations (or the current limit when unknown). Antigravity "
            L"shows limiting pools, which can change.\r\n\r\n"
            L"Legend and readout\r\n"
            L"Each chip above the chart names a line with its color and width: click it to hide or show the line; "
            L"point at it to emphasize the line. Point at the chart to read every nearby or overlapping line, "
            L"current and previous week, at the recorded reading nearest the drawn line, never an interpolated "
            L"value. Values are rounded for display; the CSV keeps them exact. Previous-week times are the original "
            L"sample times. Gaps are not lines and have no readings.\r\n\r\n"
            L"Axes and units\r\n"
            L"Percentages use the left axis; the shaded band marks your red threshold. In the Remaining bar mode "
            L"(Settings > Display) they show what is left, so lines fall as usage rises, going below 0% past the "
            L"limit, and the band sits at the bottom. Extra usage shows Anthropic budget used and GitHub Copilot AI "
            L"credits used (%) (left, in the Remaining mode) and OpenAI credits "
            L"remaining, which fall as usage rises, on their own axis. Dollar spending shows on the cards; All meters "
            L"also plots it on its own axis. Accounts without readings in the range are listed under No "
            L"readings.\r\n\r\n"
            L"Lines and markers\r\n"
            L"All accounts gives each account a color: primary quotas are thicker, scoped quotas thinner and tinted. "
            L"One account gives each meter its own color. Solid lines: current. Dashed lines: previous week. Dots "
            L"mark only isolated readings (filled: current, hollow: previous). Resets, missing or failed readings, "
            L"gaps, restarts and recording changes break lines without extra markers. Lines are reduced for drawing; "
            L"cards and the CSV use the raw samples.\r\n\r\n"
            L"Cards\r\n"
            L"The large value is the latest reading in the range; \"now\" means it is the account's latest fresh "
            L"poll. Peak is the highest recorded value; in the Remaining mode, Lowest is the least left. Coverage "
            L"counts connected intervals. Time at or above the red threshold (at or below what it leaves, in the "
            L"Remaining mode) is estimated within those intervals, never across gaps or beyond the last sample. "
            L"Dollar details report sampled last and peak spending, not total spending in the range. Times are local. Week "
            L"comparison uses two elapsed seven-day periods; daylight saving can shift local clock times.\r\n\r\n"
            L"Recording and export\r\n"
            L"Recording is on by default. When it is off, turn it on with the banner or in Settings; the choice "
            L"applies to the current Windows user only, and per-account recording and hidden-account rules still "
            L"apply. Recording starts with the next poll, once account history IDs are saved. No data can also mean "
            L"an unsupported meter, failed polls or expired history. Nothing is backfilled. The window follows new "
            L"polls (the chart waits while the pointer is on it); Refresh or F5 reads local history again. Export "
            L"writes every supported meter of the selected account, or of every account under All accounts, over "
            L"the selected range (both weeks when comparing), with UTC times and IDs instead of labels. Credits are "
            L"not dollars or a spending ledger.\r\n\r\n"
            L"Retention: " +
            (settings.settings.historyRetentionDays > 0 ?
                 std::to_wstring(settings.settings.historyRetentionDays) +
                     L" days, also limited to 250,000 records / about 60 MB." :
                 std::wstring(L"forever, with no size limit.")) +
            L" Turning recording off keeps saved history. Removed "
            L"accounts remain under their IDs until expiry or Clear, unless their history was deleted with them; "
            L"select one to delete its history.";
        if (snapshot && !snapshot->samples.empty()) {
            text += L"\r\nOldest loaded sample: " +
                    HistoryUiTime(snapshot->samples.front().timeMs, ops->Now(),
                                  [this](ULONGLONG ms) { return ops->LocalTime(ms); });
        }
        Ask(text, L"About these charts", MB_OK | MB_ICONINFORMATION);
    }

    // A message box: a modal meanwhile.
    int Ask(const std::wstring& text, PCWSTR caption, UINT type) {
        ModalBegan();
        int answer = ops->Ask(text, caption, type);
        ModalEnded();
        return answer;
    }

    // After a dialog or a step that can take a while: the close latch or unload ends the running
    // operation there, nothing more done or shown.
    bool Stopped() {
        if (!closeRequested && !ops->Unloading()) return false;
        operation = HistoryOperation::None;
        exportScope.reset();
        return true;
    }

    // The operation ends: what waited for it runs, then its notice shows (nothing that ran replaces
    // it).
    void Finish(HistoryUiNotice result) {
        operation = HistoryOperation::None;
        exportScope.reset();
        Resume();
        notice = std::move(result);
        Compose();
    }

    bool Busy() const { return modal > 0 || operation != HistoryOperation::None; }

    // A modal or an operation ended: a load that waited runs (taking the latest request), else held
    // refresh work is posted.
    void Resume() {
        if (Busy() || closeRequested || ops->Unloading()) return;
        if (loadPending) Load();
        else Schedule();
    }

    // Live refresh -------------------------------------------------------------------------------

    // A notification (settings, sign-ins, visibility, history IDs, the journal's status, consent,
    // a poll): both parts become outstanding; however many arrive before the refresh message is
    // handled, it is posted once.
    void Notify() {
        if (closeRequested || ops->Unloading()) return;
        liveDirty = samplesDirty = true;
        Schedule();
    }

    // The posted refresh message, with whether the dropdown popup (the More menu) is open and
    // whether the pointer is on the chart now (a scroll or rebuild can move the chart under a
    // still pointer without a hover report): the live part, then the sample part unless an
    // inspection holds it. A modal, an operation or the popup holds both (a rebuild would close
    // the popup). Returns whether the view or the chart changed: the window rebuilds.
    bool ProcessRefresh(bool popup, bool pointer) {
        queued = false;
        pointerOnChart = pointer;
        if (closeRequested || ops->Unloading()) {
            liveDirty = samplesDirty = false;
            return false;
        }
        if (Busy()) return false;
        if (popup) {
            if (liveDirty || samplesDirty) Timer(true);
            return false;
        }
        bool changed = false;
        if (liveDirty) {
            liveDirty = false;
            changed = Live();
        }
        if (samplesDirty && !Inspecting()) {
            samplesDirty = false;
            changed = Samples() || changed;
        }
        Timer(samplesDirty);
        return changed;
    }

    // The deferral timer's tick, with the popup's state and whether the pointer is on the chart:
    // once the popup is closed, held work is posted (the sample part only once the inspection
    // ended). It keeps ticking while work waits on the popup or an inspection, or a post failed.
    void DeferralTick(bool popup, bool pointer) {
        pointerOnChart = pointer;
        if (closeRequested || ops->Unloading() || !(liveDirty || samplesDirty)) {
            Timer(false);
            return;
        }
        if (popup) return;
        Schedule();
        Timer(!queued);
    }

    // The pointer over the chart (true) or off it; whether a readout shows comes with Hover. An
    // inspection ends once the pointer has left the chart and no readout shows: held samples are
    // posted.
    void Inspect(bool pointer) {
        pointerOnChart = pointer;
        if (!Inspecting()) Schedule();
    }

    // A modal runs on the window's thread: no refresh or load until it ends, which resumes them
    // (unless an operation still runs).
    void ModalBegan() { modal++; }
    void ModalEnded() {
        if (modal && !--modal) Resume();
    }

    bool Inspecting() const { return pointerOnChart || readoutShown; }

    // Posts the refresh message unless one is posted, while work can run: the live part, or the
    // sample part outside an inspection. A failed post leaves the retry to the deferral timer.
    void Schedule() {
        if (queued || closeRequested || ops->Unloading()) return;
        if (!liveDirty && !(samplesDirty && !Inspecting())) return;
        queued = ops->PostRefresh();
        if (!queued) Timer(true);
    }

    void Timer(bool on) {
        if (timer == on) return;
        timer = on;
        ops->DeferralTimer(on);
    }

    // The live part: the settings (labels, account recording and visibility, weekly selections,
    // consent), the polls and the journal's status; the samples and times stay. Accounts are
    // collected again only when the settings changed. Returns whether the view changed.
    bool Live() {
        HistorySettingsSnapshot read = ops->ReadSettings();
        if (snapshot) {
            HistoryPeek peek = ops->PeekSnapshot(readFrom, false, snapshot->stamp, heldFrom);
            journalStatus = std::move(peek.status);
            journalCapped = peek.capped;
        }
        if (read.settings == settings.settings) {
            settings = std::move(read);
        } else if (!Adopt(std::move(read), snapshot, heldFrom, from, to, readFrom, 0)) {
            return false;
        }
        return Compose();
    }

    // The sample part (after the first load): the times move to now, and the snapshot is
    // replaced when the journal holds other samples for them (another stamp, or a start further
    // back); its accounts, their rows and the selection are rebuilt in the same step, before
    // anything can paint or pick. Returns whether it ran.
    bool Samples() {
        if (!snapshot) return false;
        auto [start, end, readStart] = Times(settings.settings.historyRetentionDays);
        HistoryPeek peek = ops->PeekSnapshot(readStart, true, snapshot->stamp, heldFrom);
        journalStatus = peek.status;
        journalCapped = peek.capped;
        std::shared_ptr<const HistorySnapshot> next = snapshot;
        ULONGLONG nextFrom = heldFrom;
        if (peek.samples) {
            auto replacement = std::make_shared<HistorySnapshot>();
            replacement->samples = std::move(*peek.samples);
            HistorySortSamples(replacement->samples);
            replacement->status = std::move(peek.status);
            replacement->capped = peek.capped;
            replacement->stamp = peek.stamp;
            next = std::move(replacement);
            nextFrom = readStart;
        }
        if (!Adopt(settings, std::move(next), nextFrom, start, end, readStart, 0)) return false;
        Compose();
        return true;
    }

    // The shown times at the clock's now: [start, now) over the range (All: the retention's days,
    // or start 0 when kept forever, which Adopt resolves; comparing: 7 days), read from the week
    // before start while comparing.
    std::array<ULONGLONG, 3> Times(int retention) {
        static constexpr int kDays[] = {1, 7, 30, 90};
        ULONGLONG now = ops->Now();
        ULONGLONG span = (compare                         ? 7 :
                          range == HistoryUiRange::All    ? ULONGLONG(std::max(0, retention)) :
                                                            ULONGLONG(kDays[int(range)])) * kHistoryDayMs;
        ULONGLONG start = span && now > span ? now - span : 0;
        return {start, now, compare && start > kHistoryWeekMs ? start - kHistoryWeekMs : start};
    }

    // Takes `read` and the `next` snapshot (read from `nextFrom`) for the times [start, end) read
    // from `readStart`: collects the accounts over them and resolves the selection (a listed
    // request wins; else it stays while listed, else All accounts; a change drops the notice).
    // Start 0 (All kept forever) begins at the oldest sample held, showing at least a day.
    // False when unload cut the collection short: nothing changes then.
    bool Adopt(HistorySettingsSnapshot read, std::shared_ptr<const HistorySnapshot> next, ULONGLONG nextFrom,
               ULONGLONG start, ULONGLONG end, ULONGLONG readStart, uint64_t request) {
        if (!start && end) {
            start = end > kHistoryDayMs ? end - kHistoryDayMs : 0;
            if (next && !next->samples.empty()) start = std::min(start, next->samples.front().timeMs);
        }
        const std::vector<HistorySample> none;
        std::vector<HistoryUiAccount> collected = HistoryCollectAccounts(
            read.settings.accounts, next ? next->samples : none, readStart, end, [this] { return ops->Unloading(); });
        if (ops->Unloading()) return false;
        int selected = HistoryResolveSelection(collected, selectedId, request);
        uint64_t id = selected >= 0 ? collected[size_t(selected)].id : 0;
        if (id != selectedId) notice = {};
        selectedId = id;
        if (read.settings != settings.settings) settingsRevision++;
        if (next != snapshot) generation++;
        settings = std::move(read);
        snapshot = std::move(next);
        heldFrom = nextFrom;
        from = start;
        to = end;
        readFrom = readStart;
        accounts = std::move(collected);
        return true;
    }

    // The view from the state: the accounts with their slots, recording state and latest polls,
    // the current mode's view, the settings, the pill, status and notice, then the summary cards
    // once loaded (from the series, built again only when their key changed). A selection that
    // isn't listed is All accounts. Returns whether the view changed.
    bool Compose() {
        const Settings& s = settings.settings;
        ULONGLONG now = ops->Now();
        MeterDisplay display = MeterDisplayFor(s, now, 1, 0);
        std::map<uint64_t, size_t> configured;
        for (size_t i = 0; i < s.accounts.size(); i++) configured.try_emplace(s.accounts[i].historyId, i);
        HistoryUiView v;
        for (const HistoryUiAccount& a : accounts) {
            HistoryUiViewAccount& account = v.accounts.emplace_back();
            account.historyId = a.id;
            account.slot = slots.try_emplace(a.id, uint32_t(slots.size())).first->second;
            account.provider = a.provider;
            account.label = a.label;
            account.removed = !a.configured;
            account.recordingOff = a.configured && !a.recording;
            account.paused = a.hidden && s.skipHiddenPolling;
            // The flyout's weekly row (GitHub Copilot's: its AI credits row): the latest poll's
            // used % and its staleness.
            auto index = a.configured ? configured.find(a.id) : configured.end();
            if (index != configured.end() && index->second < settings.polls.size()) {
                const AccountData& poll = settings.polls[index->second];
                account.weeklyPct = a.provider == 4 ? poll.extraUsage.pct : poll.winWeek.pct;
                account.stale = AccountMeterState(display, s.barLayout, s.accounts[index->second], poll, 0, 0).stale;
                account.lastPollMs = poll.lastSuccessMs;
            }
            if (a.id == selectedId) v.selected = int(v.accounts.size()) - 1;
        }
        if (v.selected < 0) selectedId = 0;
        v.view = v.selected < 0 ? allView : accountView;
        v.range = range;
        v.compare = compare;
        v.recordingEnabled = s.historyEnabled;
        v.canEnableRecording = snapshot != nullptr;
        v.retentionDays = s.historyRetentionDays;
        v.yellowThreshold = display.yellowThreshold;
        v.orangeThreshold = display.orangeThreshold;
        v.redThreshold = display.redThreshold;
        v.colorblind = s.colorblindMode;
        v.remaining = s.barMode == BarMode::Remaining;
        v.logoBrands = s.logoBrands;
        v.material = s.material;
        v.notice = notice;
        v.canExport = snapshot != nullptr;
        v.exporting = operation == HistoryOperation::Writing;
        // Not saving: the journal's error (before the first load, the storage flag); waiting:
        // account IDs not saved yet, or no sample in range.
        bool storageError = snapshot ? !journalStatus.empty() : settings.storageError;
        bool waiting = settings.idsPendingSave ||
                       (snapshot && std::all_of(accounts.begin(), accounts.end(),
                                                [](const HistoryUiAccount& a) { return a.rows.empty(); }));
        v.pill = !s.historyEnabled ? HistoryUiPill::Off :
                 storageError      ? HistoryUiPill::NotSaving :
                 waiting           ? HistoryUiPill::Waiting : HistoryUiPill::Recording;
        v.loading = !snapshot;
        if (snapshot) {
            HistoryUiLocalTime localTime = [this](ULONGLONG ms) { return ops->LocalTime(ms); };
            v.status = L"Updated " + HistoryUiTime(to, now, localTime);
            v.storageError = journalStatus;
            v.storageCapped = journalCapped;
            HistorySeriesKey key{generation, settingsRevision, from, to, readFrom, v.view,
                                 v.selected >= 0 ? selectedId : 0, compare};
            if (!(key == seriesKey)) {
                HistorySeriesQuery query{v.selected, v.view, from, to, readFrom, compare, v.redThreshold, v.remaining};
                series = HistoryBuildSeries(snapshot->samples, accounts, query, [this] { return ops->Unloading(); });
                // A build cut short is never taken for a complete one.
                seriesKey = ops->Unloading() ? HistorySeriesKey{} : key;
            }
            HistoryUiSetCards(v, series, from, to, now, localTime);
            // The chart's lines: the plotted series in order, named as their cards, hidden as the
            // legend left them.
            for (size_t i = 0; i < series.series.size(); i++) {
                const HistorySeries& line = series.series[i];
                if (!line.plotted || line.account >= v.accounts.size()) continue;
                const HistoryUiViewAccount& account = v.accounts[line.account];
                HistoryUiLine& shown = v.lines.emplace_back();
                shown.chip = kHistoryUiChip + account.slot * 8 + uint32_t(line.meter);
                shown.historyId = account.historyId;
                shown.meter = line.meter;
                shown.series = i;
                shown.account = line.account;
                shown.name = line.legendTitle;
                shown.title = line.hoverTitle;
                shown.color = line.color;
                shown.stroke = line.stroke;
                shown.low = line.low;
                shown.high = line.high;
                shown.hidden = std::find(hidden.begin(), hidden.end(), std::pair{shown.historyId, shown.meter}) !=
                               hidden.end();
            }
        }
        bool changed = !(v == view);
        view = std::move(v);
        return changed;
    }

    // Lays the chart out in its item's rectangle at `scale` (each build that shows it; never a
    // paint): the view's lines, axes and plot, then the visible lines' periods reduced to the
    // plot's device pixels, again only when that reduction's key changed (HistoryChartKey), and the
    // time ticks. The chart keeps the snapshot its reduction indexes: a later replacement can't
    // reach its painting or picking before the next build lays it out again. A build ended any
    // hover: nothing is picked.
    void LayChart(UiGraphics& g, D2D1_RECT_F rect, float scale) {
        HistoryChart& c = chart;
        bool hiddenAll = std::all_of(view.lines.begin(), view.lines.end(), [](const HistoryUiLine& l) { return l.hidden; });
        c.lines = view.lines;
        c.from = from;
        c.to = to;
        c.readFrom = readFrom;
        c.compare = compare;
        c.redThreshold = view.redThreshold;
        c.remaining = view.remaining;
        c.message = view.lines.empty() ? (view.cards.empty() ? L"No readings in this range." :
                                              L"No percentage readings to plot. Dollar spending is shown below.") :
                    hiddenAll ? L"All lines are hidden" : L"";
        c.axes = HistoryChartAxes(c.lines, c.remaining);
        float plotWidth = c.plot.right - c.plot.left;
        HistoryChartLayout(g, c, rect, scale);
        HistoryChartKey key{generation, settingsRevision, from, to, readFrom, view.selected >= 0 ? selectedId : 0,
                            view.view, compare, {}, c.width};
        for (const HistoryUiLine& line : c.lines) {
            if (!line.hidden) key.lines.push_back(line.chip);
        }
        bool reduce = !(key == c.key) && snapshot;
        if (reduce) {
            c.key = key;
            c.snapshot = snapshot;
            c.generation = generation;
            c.render.assign(c.lines.size(), {});
            auto cancelled = [this] { return ops->Unloading(); };
            for (size_t i = 0; i < c.lines.size(); i++) {
                const HistoryUiLine& line = c.lines[i];
                if (line.hidden || line.account >= accounts.size()) continue;
                const std::vector<size_t>& rows = accounts[line.account].rows;
                c.render[i][0] = HistoryReduceRows(snapshot->samples, rows, line.meter, from, to, c.width, cancelled);
                if (compare) {
                    c.render[i][1] = HistoryReduceRows(snapshot->samples, rows, line.meter, readFrom, from, c.width,
                                                       cancelled);
                }
            }
            // A reduction cut short is never taken for a complete one.
            if (cancelled()) c.key = {};
        }
        if (reduce || plotWidth != c.plot.right - c.plot.left) {
            c.ticks = HistoryChartTicks(from, to, c.plot.right - c.plot.left,
                                        [this](ULONGLONG ms) { return ops->LocalTime(ms); });
        }
        picked.clear();
    }

    // The pointer over the chart at `at` (item coordinates; nullopt: its hover ended): the lines
    // it picks, emphasized (true when they changed), and their readout in `readout` unless that
    // already holds it (the same key). Whether a readout shows is part of the inspection.
    bool Hover(std::optional<D2D1_POINT_2F> at, UiTooltipContent& readout, const UiPalette& palette) {
        std::vector<HistoryChartHit> hits;
        if (at && !closeRequested && !ops->Unloading()) hits = HistoryChartPick(chart, *at);
        std::vector<size_t> lines;
        for (const HistoryChartHit& hit : hits) {
            if (lines.empty() || lines.back() != hit.line) lines.push_back(hit.line);
        }
        UiTooltipContent content = HistoryChartReadout(chart, hits, palette, view.selected >= 0, ops->Now(),
                                                       [this](ULONGLONG ms) { return ops->LocalTime(ms); });
        readoutShown = content.key != 0;
        if (content.key != readout.key) readout = std::move(content);
        bool changed = lines != picked;
        picked = std::move(lines);
        return changed;
    }
};
