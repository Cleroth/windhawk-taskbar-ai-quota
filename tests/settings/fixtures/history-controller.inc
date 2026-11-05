// The history window's controller (history/controller.h) against in-memory ops: open requests
// before and after the window is published, repeated and unlisted requests, removed accounts,
// each mode's view, the range under week comparison, close and unload, the view it fills from
// settings, polls and the journal, and Enable recording. Orchestration only: the production ops
// and the opener-to-window bridge are outside this suite. Hand-derived expectations. Live refresh:
// history-refresh.inc.

// In-memory ops: the open request (overwritten by each open, as the shared global is), a journal
// with its stamp, status and cap, settings with their polls, the clock, unload, the window's
// posted refresh messages and deferral timer, the consent transaction, the dialogs, the CSV write,
// Clear, Delete history and the folder, the current language, and an operation log.
struct TestHistoryOps final : HistoryOps {
    uint64_t request = 0;
    std::vector<HistorySample> journal;
    HistoryJournalStamp stamp{true, 1, 1, 0};
    HistoryJournalError status = HistoryJournalError::None;
    bool capped = false;
    // Kept for 90 days, so All retained spans a fixed period (kept forever: its own cases)
    HistorySettingsSnapshot settings = [] {
        HistorySettingsSnapshot s;
        s.settings.historyRetentionDays = 90;
        return s;
    }();
    ULONGLONG now = kWeekTo;
    bool unloading = false;
    // Refresh messages posted and not handled yet (failPost: posting fails), the timer, and what
    // happens once a peek returned
    int posted = 0;
    bool failPost = false, timer = false;
    std::function<void()> peeked;
    // Enable recording: the transaction's result once it holds the edit lock, and what happens
    // while it waits for the lock and once it notified
    HistoryConsentResult consent = HistoryConsentResult::Applied;
    std::function<void()> waitingForLock, notified;
    // Dialogs: each message box's answer in turn (IDOK once none are left; 0: it couldn't be
    // tracked), the save dialog's result, and what arrives while either shows (as messages the
    // modal loop dispatches); the boxes shown (text, type)
    std::deque<int> answers;
    HistoryDestination destination{HistoryDialogResult::Chosen, L"C:\\Exports\\usage.csv"};
    std::function<void()> asking, choosing;
    std::vector<std::pair<std::wstring, UINT>> asked;
    // The write: posted write messages (failPostWrite: posting fails), the scopes written, the
    // progress it reports (rows), what happens during it, and its result (rows unless set: one
    // per sample of the scope)
    int writePosts = 0;
    bool failPostWrite = false;
    std::vector<HistoryExportScope> written;
    std::vector<size_t> progressRows;
    std::function<void()> writing;
    HistoryCsvResult writeResult{SIZE_MAX, 0, true};
    // Clear's result (and its error; the epoch moves either way, as the journal's does), what
    // happens during it; whether the journal's folder exists
    bool clearResult = true;
    HistoryJournalError clearError = HistoryJournalError::InUse;
    std::function<void()> clearing;
    // Delete history's result (and its error; a success removes the account's samples and moves
    // the revision, as the journal's does), what happens during it
    bool deleteResult = true;
    HistoryJournalError deleteError = HistoryJournalError::InUse;
    std::function<void()> deleting;
    bool folder = true;
    // The current language (English unless a case switches it)
    I18nLocale locale;
    // "take", "settings", "snapshot <days before now>d", "peek" (status only),
    // "peek <days before now>d" (samples too), "consent", "notify", "post" ("post failed"),
    // "timer on", "timer off", "open settings", "open folder" ("no folder"), "ask <caption>",
    // "choose <title>", "post write" ("post write failed"), "write <path>", "clear", "invalidate",
    // "delete <history ID in hex>"
    std::vector<std::wstring> log;

    // A poll's reading recorded: appended, counted in the stamp.
    void Append(const HistorySample& sample) {
        journal.push_back(sample);
        stamp.appended++;
    }
    std::wstring Days(ULONGLONG from) const { return std::to_wstring((now - from) / kHistoryDayMs) + L"d"; }

    uint64_t TakeRequest() override {
        log.push_back(L"take");
        return std::exchange(request, 0);
    }
    HistorySnapshot ExplicitSnapshot(ULONGLONG from) override {
        log.push_back(L"snapshot " + Days(from));
        HistorySnapshot snapshot;
        for (const HistorySample& sample : journal) {
            if (sample.timeMs >= from) snapshot.samples.push_back(sample);
        }
        snapshot.status = status;
        snapshot.capped = capped;
        snapshot.stamp = stamp;
        return snapshot;
    }
    // The journal's peek contract (tests/history checks the production one): a replacement only
    // with samples asked for, the journal loaded, and another stamp or a start further back.
    HistoryPeek PeekSnapshot(ULONGLONG from, bool samples, const HistoryJournalStamp& held,
                             ULONGLONG heldFrom) override {
        log.push_back(samples ? L"peek " + Days(from) : L"peek");
        HistoryPeek peek{stamp, status, capped, std::nullopt};
        if (samples && stamp.loaded && (stamp != held || from < heldFrom)) {
            peek.samples.emplace();
            for (const HistorySample& sample : journal) {
                if (sample.timeMs >= from) peek.samples->push_back(sample);
            }
        }
        if (peeked) peeked();
        return peek;
    }
    HistorySettingsSnapshot ReadSettings() override {
        log.push_back(L"settings");
        return settings;
    }
    HistoryConsentResult ApplyConsent() override {
        log.push_back(L"consent");
        if (waitingForLock) waitingForLock();
        if (unloading) return HistoryConsentResult::Stopped;
        if (consent == HistoryConsentResult::Applied) settings.settings.historyEnabled = true;
        log.push_back(L"notify");
        if (notified) notified();
        return consent;
    }
    void OpenSettings() override { log.push_back(L"open settings"); }
    bool OpenFolder() override {
        log.push_back(folder ? L"open folder" : L"no folder");
        return folder;
    }
    int Ask(const std::wstring& text, PCWSTR caption, UINT type) override {
        log.push_back(std::wstring(L"ask ") + caption);
        asked.push_back({text, type});
        if (asking) asking();
        if (answers.empty()) return IDOK;
        int answer = answers.front();
        answers.pop_front();
        return answer;
    }
    HistoryDestination ChooseDestination(const std::wstring& title) override {
        log.push_back(L"choose " + title);
        if (choosing) choosing();
        return destination;
    }
    bool PostWrite() override {
        log.push_back(failPostWrite ? L"post write failed" : L"post write");
        writePosts += !failPostWrite;
        return !failPostWrite;
    }
    HistoryCsvResult WriteCsv(const HistoryExportScope& scope, const std::wstring& path,
                              const std::function<void(size_t)>& progress) override {
        log.push_back(L"write " + path);
        written.push_back(scope);
        for (size_t rows : progressRows) progress(rows);
        if (writing) writing();
        HistoryCsvResult result = writeResult;
        if (result.rows == SIZE_MAX) {
            result.rows = 0;
            for (const HistorySample& sample : scope.snapshot->samples) {
                if (sample.accountId && (!scope.accountId || sample.accountId == scope.accountId) &&
                    sample.timeMs >= scope.from && sample.timeMs < scope.to) {
                    result.rows++;
                }
            }
        }
        return result;
    }
    bool Clear(HistoryJournalError& error) override {
        log.push_back(L"clear");
        if (clearing) clearing();
        stamp.epoch++;
        if (clearResult) journal.clear();
        else error = clearError;
        return clearResult;
    }
    void InvalidateCurves() override { log.push_back(L"invalidate"); }
    bool DeleteHistory(uint64_t historyId, HistoryJournalError& error) override {
        wchar_t entry[32];
        swprintf(entry, ARRAYSIZE(entry), L"delete %llx", static_cast<unsigned long long>(historyId));
        log.push_back(entry);
        if (deleting) deleting();
        if (!deleteResult) {
            error = deleteError;
            return false;
        }
        std::erase_if(journal, [&](const HistorySample& sample) { return sample.accountId == historyId; });
        stamp.revision++;
        return true;
    }
    bool PostRefresh() override {
        log.push_back(failPost ? L"post failed" : L"post");
        posted += !failPost;
        return !failPost;
    }
    void DeferralTimer(bool on) override {
        log.push_back(on ? L"timer on" : L"timer off");
        timer = on;
    }
    bool Unloading() override { return unloading; }
    ULONGLONG Now() override { return now; }
    SYSTEMTIME LocalTime(ULONGLONG ms) override { return TestZone()(ms); }
    I18nLocale Locale() override { return locale; }
};

static AccountData TestPoll(double weekly, ULONGLONG lastSuccessMs) {
    AccountData poll;
    poll.winWeek.pct = weekly;
    poll.lastSuccessMs = lastSuccessMs;
    poll.stale = false;
    return poll;
}

// CC 1 (Anthropic, 0xC1) and OAI (OpenAI, 0x0A), both polled 5 minutes before now (Sat 31 Oct
// 14:32): weekly 47% and 62%. The journal: CC 1's weekly 30% three days before the week, then 40%
// and 45% at its hours 1 and 2; OAI's 60% at hour 1; a removed account (0xDEAD, Anthropic) 70% at
// hour 3.
static TestHistoryOps TestControllerOps() {
    TestHistoryOps ops;
    ops.settings.settings.accounts = {TestHistoryAccount(L"anthropic", L"CC 1", 0xC1),
                                      TestHistoryAccount(L"openai", L"OAI", 0x0A)};
    ops.settings.polls = {TestPoll(47, kWeekTo - 5 * kMinute), TestPoll(62, kWeekTo - 5 * kMinute)};
    ops.journal = {TestReading(0xC1, 1, kWeekFrom - 3 * kHistoryDayMs, {{1, 30}}),
                   TestReading(0xC1, 1, kWeekFrom + kHourMs, {{1, 40}}),
                   TestReading(0x0A, 2, kWeekFrom + kHourMs, {{1, 60}}),
                   TestReading(0xC1, 1, kWeekFrom + 2 * kHourMs, {{1, 45}}),
                   TestReading(0xDEAD, 1, kWeekFrom + 3 * kHourMs, {{1, 70}})};
    ops.stamp.appended = ops.journal.size();
    return ops;
}

using TestLog = std::vector<std::wstring>;

static void RunHistoryControllerTests() {
    Run("history controller: opening reads the settings only; until the first load the pill and the "
        "configured accounts come from them", [] {
        TestHistoryOps ops = TestControllerOps();
        ops.request = 0xC1;
        ops.settings.idsPendingSave = true;
        HistoryController c(ops);
        c.Open();
        CHECK(ops.log == TestLog{L"settings"} && ops.request == 0xC1);
        const HistoryUiView& v = c.view;
        CHECK(v.loading && v.status.empty() && v.cards.empty() && v.noReadings.empty());
        CHECK(v.pill == HistoryUiPill::Waiting && v.selected == -1 && v.view == HistoryView::Weekly &&
              v.range == HistoryUiRange::Week && !v.compare);
        CHECK(v.accounts.size() == 2 && v.accounts[0].label == L"CC 1" && v.accounts[1].label == L"OAI");
        CHECK(v.accounts[0].weeklyPct == 47 && !v.accounts[0].stale && v.accounts[1].weeklyPct == 62);
        ops.settings.idsPendingSave = false;
        ops.settings.storageError = true;
        c.Open();
        CHECK(c.view.pill == HistoryUiPill::NotSaving);
        ops.settings.settings.historyEnabled = false;
        c.Open();
        CHECK(c.view.pill == HistoryUiPill::Off && !c.view.recordingEnabled && !c.view.canEnableRecording);
        ops.settings.storageError = false;
        ops.settings.settings.historyEnabled = true;
        c.Open();
        CHECK(c.view.pill == HistoryUiPill::Recording);
    });

    Run("history controller: the window opens on the settings' default range; a later default leaves "
        "the open window's range", [] {
        TestHistoryOps ops = TestControllerOps();
        ops.settings.settings.historyDefaultRange = HistoryUiRange::Month;
        HistoryController c(ops);
        c.Open();
        CHECK(c.range == HistoryUiRange::Month && c.view.range == HistoryUiRange::Month);
        c.Load();
        CHECK(ops.log.back() == L"snapshot 30d" && c.from == kWeekTo - 30 * kHistoryDayMs);
        // A new default arrives as a settings notification, then with a load: the range stays.
        ops.settings.settings.historyDefaultRange = HistoryUiRange::Day;
        c.Notify();
        c.ProcessRefresh(false, false);
        CHECK(c.settings.settings.historyDefaultRange == HistoryUiRange::Day && c.view.range == HistoryUiRange::Month);
        c.Load();
        CHECK(ops.log.back() == L"snapshot 30d" && c.view.range == HistoryUiRange::Month);
        // The user's range wins until the window opens again.
        c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Quarter));
        CHECK(ops.log.back() == L"snapshot 90d" && c.view.range == HistoryUiRange::Quarter);
        HistoryController reopened(ops);
        reopened.Open();
        reopened.Load();
        CHECK(ops.log.back() == L"snapshot 1d" && reopened.view.range == HistoryUiRange::Day);
    });

    Run("history controller: a request stored before the window is published is the first load's; one "
        "after it the activation's load; loads without one keep the selection", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        ops.request = 0xC1;
        c.Open();
        c.Load();
        CHECK((ops.log == TestLog{L"settings", L"take", L"settings", L"snapshot 7d"}));
        CHECK(ops.request == 0 && c.selectedId == 0xC1 && c.view.selected == 0 && c.view.view == HistoryView::AllMeters);
        CHECK(!c.view.loading && c.view.status == L"Updated Sat 31 Oct 14:32" && c.view.pill == HistoryUiPill::Recording);
        CHECK(c.from == kWeekFrom && c.to == kWeekTo && c.readFrom == kWeekFrom);
        ops.request = 0x0A;
        c.Load();
        CHECK(ops.request == 0 && c.selectedId == 0x0A && c.view.selected == 1);
        c.Load();
        CHECK(c.selectedId == 0x0A && std::count(ops.log.begin(), ops.log.end(), L"take") == 3);
    });

    Run("history controller: repeated requests, the last wins: each activation's load takes what is "
        "stored by then", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        // Two opens before the window handles either activation: the second's request stays.
        ops.log.clear();
        ops.request = 0xC1;
        ops.request = 0x0A;
        c.Load();
        c.Load();
        CHECK(c.selectedId == 0x0A);
        CHECK((ops.log == TestLog{L"take", L"settings", L"snapshot 7d", L"take", L"settings",
                                  L"snapshot 7d"}));
        // One activation after the other: each selects its account.
        ops.request = 0xC1;
        c.Load();
        CHECK(c.selectedId == 0xC1);
        ops.request = 0xDEAD;
        c.Load();
        CHECK(c.selectedId == 0xDEAD);
    });

    Run("history controller: an unlisted request is taken and keeps the selection; an account no "
        "longer listed falls back to All accounts", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        c.Command(HistoryUiCommand::Select, 0, 0xC1);
        ops.request = 0x999;
        c.Load();
        CHECK(ops.request == 0 && c.selectedId == 0xC1 && c.view.selected == 0);
        // Removed from the settings, CC 1 stays listed (as removed) while it has samples in range.
        ops.settings.settings.accounts.erase(ops.settings.settings.accounts.begin());
        ops.settings.polls.erase(ops.settings.polls.begin());
        c.Load();
        CHECK(c.view.accounts.size() == 3 && c.view.selected == 1);
        CHECK(c.view.accounts[1].removed && c.view.accounts[1].label == L"account_00000000000000c1");
        // In 24 hours nothing of it is: All accounts, in its own view.
        c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day));
        CHECK(c.view.accounts.size() == 1 && c.view.selected == -1 && c.selectedId == 0 &&
              c.view.view == HistoryView::Weekly);
    });

    Run("history controller: removed accounts with samples in range are listed, selectable and "
        "preselectable", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        CHECK(c.view.accounts.size() == 3);
        const HistoryUiViewAccount& removed = c.view.accounts[2];
        CHECK(removed.removed && removed.historyId == 0xDEAD && removed.provider == 1 && removed.slot == 2 &&
              removed.label == L"account_000000000000dead" && removed.weeklyPct < 0 && !removed.lastPollMs);
        // All accounts names it by its journal ID.
        CHECK(c.view.cards.size() == 3 && c.view.cards[0].title == L"CC 1 \u00B7 Weekly" &&
              c.view.cards[1].title == L"OAI \u00B7 Weekly" &&
              c.view.cards[2].title == L"account_000000000000dead \u00B7 Weekly");
        ops.request = 0xDEAD;
        c.Load();
        CHECK(c.view.selected == 2 && c.view.view == HistoryView::AllMeters);
        CHECK(c.view.cards.size() == 1 && c.view.cards[0].title == L"Weekly" && c.view.cards[0].value == L"70%" &&
              c.view.cards[0].when == L"Sat 24 Oct 17:32");
        c.Command(HistoryUiCommand::Select);
        CHECK(c.view.selected == -1 && c.selectedId == 0);
        c.Command(HistoryUiCommand::Select, 0, 0xDEAD);
        CHECK(c.view.selected == 2 && c.selectedId == 0xDEAD);
    });

    Run("history controller: All accounts starts on Weekly, one account on All meters; each mode keeps "
        "its view; commands neither load nor take requests", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        ops.log.clear();
        ops.request = 0x0A;
        CHECK(c.view.view == HistoryView::Weekly);
        c.Command(HistoryUiCommand::Select, 0, 0xC1);
        CHECK(c.view.view == HistoryView::AllMeters);
        c.Command(HistoryUiCommand::View, int(HistoryView::ShortTerm));
        CHECK(c.view.view == HistoryView::ShortTerm);
        c.Command(HistoryUiCommand::Select);
        CHECK(c.view.view == HistoryView::Weekly);
        c.Command(HistoryUiCommand::View, int(HistoryView::ExtraUsage));
        CHECK(c.view.view == HistoryView::ExtraUsage);
        // One account's view is every account's.
        c.Command(HistoryUiCommand::Select, 0, 0xDEAD);
        CHECK(c.view.view == HistoryView::ShortTerm);
        c.Command(HistoryUiCommand::Select);
        CHECK(c.view.view == HistoryView::ExtraUsage);
        CHECK(ops.log.empty() && ops.request == 0x0A);
        // Loads keep both.
        ops.request = 0;
        c.Load();
        CHECK(c.view.view == HistoryView::ExtraUsage);
        c.Command(HistoryUiCommand::Select, 0, 0xC1);
        CHECK(c.view.view == HistoryView::ShortTerm);
        // Out-of-range values clamp to the first or last view; the other mode keeps its own.
        c.Command(HistoryUiCommand::View, 99);
        CHECK(c.view.view == HistoryView::AllMeters);
        c.Command(HistoryUiCommand::Select);
        CHECK(c.view.view == HistoryView::ExtraUsage);
        c.Command(HistoryUiCommand::View, -2);
        CHECK(c.view.view == HistoryView::ShortTerm && c.accountView == HistoryView::AllMeters);
    });

    Run("history controller: comparing loads 7 days and the week before, shows the range as 7 days "
        "(disabled), and gives the user's range back afterwards", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        auto ranges = [&] {
            UiFrame frame;
            HistoryUiBuildFrame(TestGraphics(), c.view, {1100, 790}, frame);
            const UiItem* item = UiFindItem(frame, kHistoryUiRanges);
            CHECK(item);
            return std::pair{item->enabled, UiSelectedSegment(*item)};
        };
        c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Month));
        CHECK(ops.log.back() == L"snapshot 30d" && c.view.range == HistoryUiRange::Month && !c.view.compare);
        CHECK(c.from == kWeekTo - 30 * kHistoryDayMs && c.readFrom == c.from);
        CHECK((ranges() == std::pair{true, int(HistoryUiRange::Month)}));
        c.Command(HistoryUiCommand::Compare, 1);
        CHECK(ops.log.back() == L"snapshot 14d" && c.view.compare && c.view.range == HistoryUiRange::Month);
        CHECK(c.from == kWeekFrom && c.readFrom == kWeekFrom - kHistoryWeekMs);
        CHECK((ranges() == std::pair{false, int(HistoryUiRange::Week)}));
        // The cards hold both periods.
        c.Command(HistoryUiCommand::Select, 0, 0xC1);
        CHECK(c.view.cards.size() == 1 && c.view.cards[0].lines.size() == 6);
        CHECK(c.view.cards[0].lines[0] == L"Current: Peak 45% \u00B7 Sat 24 Oct 16:32" &&
              c.view.cards[0].lines[3] == L"Previous: Peak 30% \u00B7 Wed 21 Oct 14:32");
        c.Command(HistoryUiCommand::Compare, 0);
        CHECK(ops.log.back() == L"snapshot 30d" && !c.view.compare && c.view.range == HistoryUiRange::Month);
        CHECK((ranges() == std::pair{true, int(HistoryUiRange::Month)}));
        // All retained follows the retention; comparing still loads 7 days and the week before.
        ops.settings.settings.historyRetentionDays = 120;
        c.Command(HistoryUiCommand::Range, int(HistoryUiRange::All));
        CHECK(ops.log.back() == L"snapshot 120d");
        c.Command(HistoryUiCommand::Compare, 1);
        CHECK(ops.log.back() == L"snapshot 14d" && c.view.range == HistoryUiRange::All);
        // Kept forever, All retained reads everything and starts at the oldest sample held (three
        // days before the week); with none, it shows the last day.
        c.Command(HistoryUiCommand::Compare, 0);
        ops.settings.settings.historyRetentionDays = 0;
        c.Load();
        CHECK(ops.log.back() == L"snapshot " + std::to_wstring(kWeekTo / kHistoryDayMs) + L"d");
        CHECK(c.from == kWeekFrom - 3 * kHistoryDayMs && c.readFrom == 0 && c.view.retentionDays == 0);
        ops.journal.clear();
        ops.stamp.revision++;
        c.Load();
        CHECK(c.from == kWeekTo - kHistoryDayMs && c.readFrom == 0);
        // Comparing still loads 7 days and the week before.
        c.Command(HistoryUiCommand::Compare, 1);
        CHECK(ops.log.back() == L"snapshot 14d" && c.from == kWeekFrom);
    });

    Run("history controller: after the close request nothing loads or acts, and a pending request stays "
        "unconsumed; unload stops it the same way", [] {
        for (bool unload : {false, true}) {
            TestHistoryOps ops = TestControllerOps();
            HistoryController c(ops);
            c.Open();
            c.Load();
            c.Command(HistoryUiCommand::Select, 0, 0xC1);
            ops.log.clear();
            ops.request = 0x0A;
            if (unload) ops.unloading = true;
            else c.RequestClose();
            c.Load();
            c.Open();
            c.Command(HistoryUiCommand::Refresh);
            c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Month));
            c.Command(HistoryUiCommand::Compare, 1);
            c.Command(HistoryUiCommand::View, int(HistoryView::ShortTerm));
            c.Command(HistoryUiCommand::Select, 0, 0x0A);
            c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Settings));
            CHECK(ops.log.empty() && ops.request == 0x0A);
            CHECK(c.selectedId == 0xC1 && c.range == HistoryUiRange::Week && !c.compare &&
                  c.accountView == HistoryView::AllMeters && c.view.selected == 0);
        }
    });

    Run("history controller: the view's polls (weekly used %, staleness), usage colors, pill, status and "
        "journal state; cards analyzed at the settings' red threshold", [] {
        TestHistoryOps ops = TestControllerOps();
        Settings& s = ops.settings.settings;
        s.barMode = BarMode::Remaining;
        s.usagePalette = UsagePalette::Colorblind;
        s.yellowThreshold = 40;
        s.orangeThreshold = 60;
        s.redThreshold = 80;
        // OAI's poll is three polls old: stale.
        ops.settings.polls[1].lastSuccessMs = kWeekTo - 30 * kMinute;
        // OAI 60% to 100% in an hour: above 80% for its second half.
        ops.journal.push_back(TestReading(0x0A, 2, kWeekFrom + 2 * kHourMs, {{1, 100}}));
        HistoryController c(ops);
        c.Open();
        c.Load();
        const HistoryUiView& v = c.view;
        CHECK(v.remaining && v.yellowThreshold == 40 && v.orangeThreshold == 60 && v.redThreshold == 80);
        CHECK((v.usageColors == std::array<uint32_t, 4>{0xFF0072B2, 0xFF56B4E9, 0xFFE69F00, 0xFFD55E00}));
        CHECK(v.accounts[0].weeklyPct == 47 && !v.accounts[0].stale && v.accounts[0].lastPollMs == kWeekTo - 5 * kMinute);
        CHECK(v.accounts[1].weeklyPct == 62 && v.accounts[1].stale);
        CHECK(v.cards.size() == 3 && v.cards[1].title == L"OAI \u00B7 Weekly");
        // Remaining mode: none left at the lowest, and the time at or below the 20% the threshold leaves.
        CHECK((v.cards[1].lines == std::vector<std::wstring>{L"Lowest 0% \u00B7 Sat 24 Oct 16:32",
                                                              L"Coverage 1 h (0.6%)", L"Est. \u226420%: 0.5 h"}));
        CHECK(v.cards[1].when == L"Sat 24 Oct 16:32");  // Stale: never "now"
        CHECK(v.pill == HistoryUiPill::Recording && v.status == L"Updated Sat 31 Oct 14:32" && v.storageError.empty() &&
              !v.storageCapped);
        // The journal's error and its cap, apart.
        ops.status = HistoryJournalError::WriteFailed;
        ops.capped = true;
        c.Load();
        CHECK(c.view.pill == HistoryUiPill::NotSaving && c.view.storageError ==
              L"History write failed; the previous journal was kept." &&
              c.view.storageCapped);
        // No sample in range, or account IDs not saved yet: waiting.
        ops.status = HistoryJournalError::None;
        ops.capped = false;
        ops.journal.clear();
        c.Load();
        CHECK(c.view.pill == HistoryUiPill::Waiting && c.view.cards.empty() && c.view.noReadings.size() == 2 &&
              c.view.accounts.size() == 2);
        ops.journal = TestControllerOps().journal;
        ops.settings.idsPendingSave = true;
        c.Load();
        CHECK(c.view.pill == HistoryUiPill::Waiting && c.view.cards.size() == 3);
        // Each account's recording state: its Record history off; paused while hidden accounts
        // aren't polled, not while they are; a removed account neither. Its first card says so.
        s.accounts[0].recordHistory = false;
        s.accounts[1].hidden = true;
        c.Load();
        CHECK(c.view.accounts[0].recordingOff && !c.view.accounts[0].paused && c.view.accounts[1].paused &&
              !c.view.accounts[1].recordingOff && !c.view.accounts[2].recordingOff && !c.view.accounts[2].paused);
        CHECK(c.view.cards[0].status == L"recording off" && c.view.cards[1].status == L"paused" &&
              c.view.cards[2].status.empty());
        s.skipHiddenPolling = false;
        c.Load();
        CHECK(!c.view.accounts[1].paused && c.view.cards[1].status.empty());
        // Recording off for this user: off, whatever else holds.
        s.historyEnabled = false;
        c.Load();
        CHECK(c.view.pill == HistoryUiPill::Off && !c.view.recordingEnabled);
    });

    Run("history controller: a GitHub Copilot account's sidebar value is its AI credits' used %, the others' "
        "their weekly one", [] {
        // Work (Copilot, 0xC0) 33% used; Org (Copilot, 0xC2) an unlimited pool without a percentage;
        // CC 1's extra usage at 80% changes nothing. A removed Copilot account (0xC9) has no poll.
        TestHistoryOps ops = TestControllerOps();
        ops.settings.settings.accounts.push_back(TestHistoryAccount(L"copilot", L"Work", 0xC0));
        ops.settings.settings.accounts.push_back(TestHistoryAccount(L"copilot", L"Org", 0xC2));
        AccountData credits = TestPoll(-1, kWeekTo - 5 * kMinute);
        credits.extraUsage.pct = 33;
        AccountData pooled = TestPoll(-1, kWeekTo - 5 * kMinute);
        pooled.copilotUnlimited = true;
        pooled.extraUsedAmount = 12.5;
        ops.settings.polls.push_back(credits);
        ops.settings.polls.push_back(pooled);
        ops.settings.polls[0].extraUsage.pct = 80;
        ops.journal.push_back(TestReading(0xC0, 4, kWeekFrom + 4 * kHourMs, {{3, 20}}));
        ops.journal.push_back(TestReading(0xC9, 4, kWeekFrom + 5 * kHourMs, {{3, 25}}));
        ops.stamp.appended = ops.journal.size();
        HistoryController c(ops);
        c.Open();
        const std::vector<HistoryUiViewAccount>* a = &c.view.accounts;
        CHECK(a->size() == 4 && (*a)[2].label == L"Work" && (*a)[2].weeklyPct == 33 && (*a)[3].weeklyPct < 0);
        CHECK((*a)[0].weeklyPct == 47 && (*a)[1].weeklyPct == 62);
        c.Load();
        a = &c.view.accounts;
        CHECK(a->size() == 6 && (*a)[2].provider == 4 && (*a)[2].weeklyPct == 33 && !(*a)[2].stale &&
              (*a)[2].lastPollMs == kWeekTo - 5 * kMinute && (*a)[3].weeklyPct < 0);
        CHECK((*a)[0].weeklyPct == 47 && (*a)[1].weeklyPct == 62);
        CHECK((*a)[5].historyId == 0xC9 && (*a)[5].removed && (*a)[5].provider == 4 && (*a)[5].weeklyPct < 0);
    });

    Run("history controller: navigation slots stay with their history IDs across loads and reorders", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        CHECK(c.view.accounts[0].slot == 0 && c.view.accounts[1].slot == 1 && c.view.accounts[2].slot == 2);
        std::swap(ops.settings.settings.accounts[0], ops.settings.settings.accounts[1]);
        std::swap(ops.settings.polls[0], ops.settings.polls[1]);
        ops.journal.push_back(TestReading(0xBEEF, 3, kWeekFrom + 4 * kHourMs, {{1, 5}}));
        c.Load();
        const std::vector<HistoryUiViewAccount>& a = c.view.accounts;
        CHECK(a.size() == 4 && a[0].historyId == 0x0A && a[0].slot == 1 && a[0].weeklyPct == 62);
        CHECK(a[1].historyId == 0xC1 && a[1].slot == 0 && a[1].weeklyPct == 47);
        CHECK(a[2].slot == 2 && a[3].historyId == 0xBEEF && a[3].slot == 3);
    });

    Run("history controller: the More menu's Settings opens the settings window, Open history folder the "
        "journal's folder (a notice when nothing was recorded yet); an unknown item does nothing; Export only "
        "once loaded", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Command(HistoryUiCommand::Export);
        CHECK(ops.log == TestLog{L"settings"} && !c.view.canExport);
        c.Load();
        CHECK(c.view.canExport && !c.view.exporting);
        ops.log.clear();
        c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Settings));
        c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Folder));
        CHECK((ops.log == TestLog{L"open settings", L"open folder"}) && c.view.notice.text.empty());
        ops.folder = false;
        c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Folder));
        CHECK(c.view.notice.text == L"Nothing has been recorded yet. The folder is created with the first recorded "
                                    L"sample." && c.view.notice.severity == UiSeverity::Info);
        c.Command(HistoryUiCommand::Menu, 7);
        CHECK(ops.log.size() == 3);
    });
}
