// The history window's live refresh and Enable recording through its controller, against the
// in-memory ops of history-controller.inc: notifications posted once, the live part (settings,
// polls, the journal's status) and the sample part (snapshot, times) by the journal's stamp, the
// causes that hold them (a modal, the open popup, an inspection of the chart) and the paths that
// resume them, with the no-stranding invariant checked after every event. Orchestration only: the
// production peek is tests/history's (journal.inc), the production ops and the window's message
// and timer plumbing are outside this suite. Hand-derived expectations.

// The window handles its posted refresh message, with the popup open or not and the pointer on
// the chart or not (default: where the controller last heard it was): whether it rebuilds.
static bool TestDeliver(HistoryController& c, TestHistoryOps& ops, bool popup = false,
                        std::optional<bool> pointer = std::nullopt) {
    CHECK(ops.posted > 0 && c.queued);
    ops.posted--;
    return c.ProcessRefresh(popup, pointer.value_or(c.pointerOnChart));
}

// After an event: outstanding work is posted, held by a modal or an operation or watched by the
// deferral timer (unless the window closes), and the ops saw the controller's posts and timer.
static void TestSettled(const HistoryController& c, const TestHistoryOps& ops) {
    CHECK(!(c.liveDirty || c.samplesDirty) || c.closeRequested || c.queued || c.modal > 0 ||
          c.operation != HistoryOperation::None || c.timer);
    CHECK(ops.timer == c.timer && (ops.posted > 0) == c.queued);
}

static size_t TestCount(const TestLog& log, std::wstring_view entry) {
    return size_t(std::count(log.begin(), log.end(), entry));
}

// A loaded window (All accounts, Weekly, 7 days) with its chart laid out, and the point of CC 1's
// 45% reading at hour 2 on it.
static D2D1_POINT_2F TestRefreshChart(HistoryController& c) {
    c.LayChart(TestGraphics(), {27, 59, 801, 359}, 1);
    CHECK(c.chart.lines.size() == 3 && c.chart.lines[0].historyId == 0xC1 && c.chart.render[0][0].points.size() == 2);
    return HistoryChartPoint(c.chart, c.chart.lines[0], c.chart.render[0][0].points[1]);
}

static void RunHistoryRefreshTests() {
    const UiPalette palette = UiPaletteFor(UiThemeInput{});

    Run("history refresh: notifications are posted once until handled; unchanged samples keep the snapshot and "
        "its stamp while the times move to now; refreshes take no open request and post nothing by themselves", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        std::shared_ptr<const HistorySnapshot> held = c.snapshot;
        ops.log.clear();
        ops.request = 0x0A;
        c.Notify();
        TestSettled(c, ops);
        c.Notify();
        TestSettled(c, ops);
        CHECK(ops.log == TestLog{L"post"} && c.liveDirty && c.samplesDirty);
        ops.now += 5 * kMinute;
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"post", L"settings", L"peek", L"peek 7d"}));
        CHECK(c.snapshot == held && c.generation == 1 && c.heldFrom == kWeekFrom && c.snapshot->stamp == ops.stamp);
        CHECK(c.to == kWeekTo + 5 * kMinute && c.from == kWeekFrom + 5 * kMinute && c.readFrom == c.from);
        CHECK(c.view.status == L"Updated Sat 31 Oct 14:37" && c.view.cards.size() == 3);
        CHECK(ops.request == 0x0A && c.selectedId == 0 && ops.posted == 0 && !c.timer);
    });

    Run("history refresh: a recorded poll, Clear (an empty replacement) and a damaged journal replace the snapshot "
        "whole; an unloaded journal replaces nothing and the snapshot keeps its own stamp", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        auto refresh = [&] {
            c.Notify();
            TestDeliver(c, ops);
            TestSettled(c, ops);
        };
        ops.Append(TestReading(0x0A, 2, kWeekTo - kMinute, {{1, 64}}));
        refresh();
        CHECK(c.generation == 2 && c.snapshot->samples.size() == 5 && c.snapshot->stamp == ops.stamp);
        CHECK(c.view.cards[1].title == L"OAI \u00B7 Weekly" && c.view.cards[1].value == L"64%");
        // Clear: another epoch and nothing left.
        ops.journal.clear();
        ops.stamp.epoch++;
        refresh();
        CHECK(c.generation == 3 && c.snapshot->samples.empty() && c.snapshot->stamp == ops.stamp);
        CHECK(c.view.cards.empty() && c.view.noReadings.size() == 2 && c.view.accounts.size() == 2 &&
              c.view.pill == HistoryUiPill::Waiting);
        // Unloaded (ownership lost): its status shows; samples and stamp stay the last load's.
        std::shared_ptr<const HistorySnapshot> held = c.snapshot;
        HistoryJournalStamp stamp = held->stamp;
        ops.Append(TestReading(0x0A, 2, kWeekTo - kMinute, {{1, 64}}));
        ops.stamp.loaded = false;
        ops.status = L"History storage is in use by another Explorer session, or is inaccessible.";
        refresh();
        CHECK(c.snapshot == held && c.snapshot->stamp == stamp && c.generation == 3);
        CHECK(c.view.storageError == ops.status && c.view.pill == HistoryUiPill::NotSaving);
        // Loaded again, damaged: loaded isn't health; its samples replace, its error shows.
        ops.stamp.loaded = true;
        ops.stamp.revision++;
        ops.status = L"Damaged history record. Export readable samples or clear history.";
        refresh();
        CHECK(c.generation == 4 && c.snapshot->samples.size() == 1 && c.view.storageError == ops.status &&
              c.view.pill == HistoryUiPill::NotSaving);
    });

    Run("history refresh: the journal's status alone updates its bar and pill; an unchanged one rebuilds nothing",
        [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        std::shared_ptr<const HistorySnapshot> held = c.snapshot;
        c.Inspect(true);  // The samples hold: only the live part runs.
        auto refresh = [&] {
            c.Notify();
            bool rebuild = TestDeliver(c, ops);
            TestSettled(c, ops);
            return rebuild;
        };
        ops.status = L"History write failed; the previous journal was kept.";
        CHECK(refresh() && c.view.storageError == ops.status && c.view.pill == HistoryUiPill::NotSaving);
        CHECK(!refresh());
        ops.status = L"History is not being saved to disk. Check disk space and permissions.";
        CHECK(refresh() && c.view.storageError == ops.status);
        ops.status.clear();
        ops.capped = true;
        CHECK(refresh() && c.view.storageError.empty() && c.view.storageCapped &&
              c.view.pill == HistoryUiPill::Recording);
        CHECK(c.snapshot == held && TestCount(ops.log, L"peek 7d") == 0);
    });

    Run("history refresh: storage that failed to initialize is never loaded or retried by refreshes, and the "
        "failed attempt's notifications post one refresh, then nothing", [] {
        TestHistoryOps ops = TestControllerOps();
        ops.journal.clear();
        ops.stamp = {false, 1, 2, 0};
        ops.status = L"History storage is in use by another Explorer session, or is inaccessible.";
        HistoryController c(ops);
        c.Open();
        c.Load();
        CHECK(c.view.pill == HistoryUiPill::NotSaving && c.view.storageError == ops.status);
        ops.log.clear();
        // An explicit load's failed attempt notifies twice (status cleared, then set again).
        c.Notify();
        c.Notify();
        TestDeliver(c, ops);
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"post", L"settings", L"peek", L"peek 7d"}));
        CHECK(ops.posted == 0 && !c.timer && c.view.storageError == ops.status);
    });

    Run("history refresh: retention pruning (another revision) replaces the snapshot without the pruned readings; "
        "rows are rebuilt with it, and the laid-out chart keeps its own snapshot until it is laid out again", [palette] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        D2D1_POINT_2F at = TestRefreshChart(c);
        std::shared_ptr<const HistorySnapshot> laid = c.chart.snapshot;
        // CC 1's readings three days before the week and at hour 1 pruned: every row moves.
        ops.journal.erase(ops.journal.begin(), ops.journal.begin() + 2);
        ops.stamp.revision++;
        c.Notify();
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK(c.generation == 2 && c.snapshot->samples.size() == 3);
        for (const HistoryUiAccount& account : c.accounts) {
            for (size_t row : account.rows) {
                CHECK(row < c.snapshot->samples.size() && c.snapshot->samples[row].accountId == account.id);
            }
        }
        CHECK(c.accounts[0].rows == std::vector<size_t>{1});
        // Before the next layout the chart picks and reads out its own snapshot's reading.
        CHECK(c.chart.snapshot == laid && c.chart.generation == 1);
        UiTooltipContent readout;
        c.Hover(at, readout, palette);
        CHECK(readout.rows.size() == 1 && UiRunsText(readout.rows[0].detail) == L"[Current] 45.0% \u00B7 Sat 24 Oct 16:32");
        c.LayChart(TestGraphics(), {27, 59, 801, 359}, 1);
        CHECK(c.chart.snapshot == c.snapshot && c.chart.generation == 2 && c.chart.render[0][0].points.size() == 1 &&
              c.chart.render[0][0].points[0].sourceRow == 1);
        UiTooltipContent again;
        c.Hover(at, again, palette);
        CHECK(again.rows.size() == 1 && UiRunsText(again.rows[0].detail) == UiRunsText(readout.rows[0].detail));
    });

    Run("history refresh: a range reaching further back refills without a journal change; a shorter one keeps the "
        "snapshot; a clock moved back refills", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        c.Command(HistoryUiCommand::Range, int(HistoryUiRange::All));
        CHECK(ops.log.back() == L"snapshot 90d" && c.generation == 2 && c.heldFrom == kWeekTo - 90 * kHistoryDayMs);
        auto refresh = [&] {
            c.Notify();
            TestDeliver(c, ops);
            TestSettled(c, ops);
        };
        ops.settings.settings.historyRetentionDays = 120;
        refresh();
        CHECK(ops.log.back() == L"peek 120d" && c.generation == 3 && c.heldFrom == kWeekTo - 120 * kHistoryDayMs &&
              c.from == c.heldFrom && c.view.retentionDays == 120);
        std::shared_ptr<const HistorySnapshot> held = c.snapshot;
        ops.settings.settings.historyRetentionDays = 60;
        refresh();
        CHECK(ops.log.back() == L"peek 60d" && c.snapshot == held && c.generation == 3 &&
              c.heldFrom == kWeekTo - 120 * kHistoryDayMs && c.from == kWeekTo - 60 * kHistoryDayMs);
        // 7 days, then the clock a day back: the week starts before the held snapshot.
        c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Week));
        CHECK(c.generation == 4 && c.heldFrom == kWeekFrom);
        ops.now -= kHistoryDayMs;
        refresh();
        CHECK(c.generation == 5 && c.heldFrom == kWeekFrom - kHistoryDayMs && c.to == kWeekTo - kHistoryDayMs);
    });

    Run("history refresh: a modal holds both parts, nested modals until the last ends, which posts them once", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        ops.log.clear();
        c.ModalBegan();
        c.ModalBegan();
        c.Notify();
        c.Notify();
        TestSettled(c, ops);
        CHECK(!TestDeliver(c, ops));
        TestSettled(c, ops);
        c.Notify();
        CHECK(!TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"post", L"post"}) && c.liveDirty && c.samplesDirty && !c.timer);
        c.ModalEnded();
        TestSettled(c, ops);
        CHECK(ops.posted == 0);
        c.ModalEnded();
        TestSettled(c, ops);
        CHECK(ops.posted == 1);
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK(TestCount(ops.log, L"settings") == 1 && TestCount(ops.log, L"peek 7d") == 1 && !c.liveDirty && !c.samplesDirty);
    });

    Run("history refresh: the open popup holds both parts; the deferral timer, started once, posts them once it "
        "finds the popup closed", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        ops.log.clear();
        c.Notify();
        CHECK(!TestDeliver(c, ops, true));
        TestSettled(c, ops);
        c.Notify();
        CHECK(!TestDeliver(c, ops, true));
        TestSettled(c, ops);
        c.DeferralTick(true, false);
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"post", L"timer on", L"post"}));
        c.DeferralTick(false, false);
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"post", L"timer on", L"post", L"post", L"timer off"}));
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK(TestCount(ops.log, L"settings") == 1 && TestCount(ops.log, L"peek 7d") == 1 && !c.timer);
    });

    Run("history refresh: an explicit load satisfies outstanding work and stops the timer", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        c.Notify();
        CHECK(!TestDeliver(c, ops, true) && c.timer);
        c.Notify();
        c.Load();
        TestSettled(c, ops);
        CHECK(!c.liveDirty && !c.samplesDirty && !c.timer);
        ops.log.clear();
        CHECK(!TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK(ops.log.empty());
    });

    Run("history refresh: a failed post leaves the retry to the deferral timer", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        ops.log.clear();
        ops.failPost = true;
        c.Notify();
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"post failed", L"timer on"}) && !c.queued);
        ops.failPost = false;
        c.DeferralTick(false, false);
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"post failed", L"timer on", L"post", L"timer off"}));
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
    });

    Run("history refresh: settings-derived labels and weekly selections, nav values and turning recording off "
        "follow at once, also while the chart is inspected", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        c.Inspect(true);
        auto refresh = [&] {
            c.Notify();
            bool rebuild = TestDeliver(c, ops);
            TestSettled(c, ops);
            return rebuild;
        };
        AccountConfig& cc1 = ops.settings.settings.accounts[0];
        cc1.label = L"CC main";
        cc1.showBars[kWeeklyBar] = false;
        ops.settings.polls[0].winWeek.pct = 52;
        CHECK(refresh());
        CHECK(c.view.accounts[0].label == L"CC main" && c.view.accounts[0].weeklyPct == 52);
        // Its weekly bar off: CC 1 has no Weekly line any more.
        CHECK(c.view.lines.size() == 2 && c.view.lines[0].historyId == 0x0A && c.view.lines[1].historyId == 0xDEAD);
        CHECK(c.generation == 1 && c.samplesDirty && c.timer);
        // Recording turned off in Settings: the banner, its button and the pill, without a load.
        ops.settings.settings.historyEnabled = false;
        CHECK(refresh());
        CHECK(!c.view.recordingEnabled && c.view.canEnableRecording && c.view.pill == HistoryUiPill::Off);
        UiFrame frame = TestHistoryFrame(c.view);
        const UiItem* enable = UiFindItem(frame, kHistoryUiEnableRecording);
        CHECK(enable && enable->enabled && TestFind(frame.content, L"History recording is off. Saved history stays; "
                                                                   L"nothing new is recorded."));
        CHECK(TestCount(ops.log, L"snapshot 7d") == 1 && TestCount(ops.log, L"peek 7d") == 0);
    });

    Run("history refresh: during one inspection, two polls handled apart update the live values twice and hold the "
        "samples; hiding the readout keeps the inspection; the pointer leaving replaces them once", [palette] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        D2D1_POINT_2F at = TestRefreshChart(c);
        UiTooltipContent readout;
        c.Hover(at, readout, palette);
        c.Inspect(true);
        CHECK(readout.key && c.readoutShown && c.Inspecting());
        std::shared_ptr<const HistorySnapshot> laid = c.chart.snapshot;
        ops.log.clear();
        auto poll = [&](double weekly) {
            ops.now += 2 * kMinute;
            ops.settings.polls[0].winWeek.pct = weekly;
            ops.settings.polls[0].lastSuccessMs = ops.now;
            ops.Append(TestReading(0xC1, 1, ops.now - kMinute, {{1, weekly}}));
            c.Notify();
            TestSettled(c, ops);
        };
        poll(52);
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK(c.view.accounts[0].weeklyPct == 52 && c.generation == 1 && c.to == kWeekTo && c.samplesDirty && c.timer);
        poll(55);
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK(c.view.accounts[0].weeklyPct == 55 && c.generation == 1 && c.to == kWeekTo);
        CHECK(TestCount(ops.log, L"post") == 2 && TestCount(ops.log, L"settings") == 2 &&
              TestCount(ops.log, L"peek 7d") == 0);
        // Held samples alone never post: a tick with the pointer still on the chart.
        c.DeferralTick(false, true);
        TestSettled(c, ops);
        CHECK(TestCount(ops.log, L"post") == 2 && c.timer);
        // The readout hides (a hide trigger) with the pointer still on the chart.
        c.Hover(std::nullopt, readout, palette);
        c.Inspect(true);
        TestSettled(c, ops);
        CHECK(!c.readoutShown && c.Inspecting() && TestCount(ops.log, L"post") == 2);
        CHECK(c.chart.snapshot == laid);
        // The pointer leaves: one replacement, the times at now.
        c.Inspect(false);
        TestSettled(c, ops);
        CHECK(TestCount(ops.log, L"post") == 3);
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK(c.generation == 2 && c.to == ops.now && c.snapshot->samples.size() == 6 && !c.samplesDirty && !c.timer);
        CHECK(TestCount(ops.log, L"peek 7d") == 1 && TestCount(ops.log, L"settings") == 2);
        CHECK(c.chart.snapshot == laid);
        c.LayChart(TestGraphics(), {27, 59, 801, 359}, 1);
        CHECK(c.chart.snapshot == c.snapshot && c.chart.render[0][0].points.size() == 4);
    });

    Run("history refresh: two notifications before the refresh is handled during an inspection: one post, one live "
        "update; the samples wait for the inspection's end, then one replacement", [palette] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        UiTooltipContent readout;
        c.Hover(TestRefreshChart(c), readout, palette);
        c.Inspect(true);
        ops.log.clear();
        ops.Append(TestReading(0xC1, 1, kWeekTo - kMinute, {{1, 52}}));
        ops.settings.polls[0].winWeek.pct = 52;
        c.Notify();
        ops.settings.polls[0].winWeek.pct = 55;
        c.Notify();
        TestSettled(c, ops);
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK(c.view.accounts[0].weeklyPct == 55 && c.generation == 1);
        CHECK((ops.log == TestLog{L"post", L"settings", L"peek", L"timer on"}));
        // The pointer leaves the chart, its readout with it (the host's leave).
        c.Hover(std::nullopt, readout, palette);
        c.Inspect(false);
        TestSettled(c, ops);
        CHECK(TestDeliver(c, ops));
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"post", L"settings", L"peek", L"timer on", L"post", L"peek 7d", L"timer off"}));
        CHECK(c.generation == 2 && c.snapshot->samples.size() == 5);
    });

    Run("history refresh: an inspection holds while either the pointer is on the chart or a readout shows; one "
        "whose leave went unreported (after a hide trigger) ends at the deferral timer's tick", [palette] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        UiTooltipContent readout;
        c.Hover(TestRefreshChart(c), readout, palette);
        c.Inspect(true);
        c.Notify();
        TestDeliver(c, ops);
        TestSettled(c, ops);
        ops.log.clear();
        // The pointer reported off the chart while its readout still shows: held.
        c.Inspect(false);
        TestSettled(c, ops);
        CHECK(c.Inspecting() && c.samplesDirty && ops.log.empty());
        // A press: the readout hides with the pointer on the chart; its later leave never reports.
        c.Hover(std::nullopt, readout, palette);
        c.Inspect(true);
        TestSettled(c, ops);
        c.DeferralTick(false, true);
        TestSettled(c, ops);
        CHECK(c.samplesDirty && ops.log.empty());
        c.DeferralTick(false, false);
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"post", L"timer off"}));
        TestDeliver(c, ops);
        TestSettled(c, ops);
        CHECK(TestCount(ops.log, L"peek 7d") == 1 && !c.samplesDirty && !c.timer);
    });

    Run("history refresh: a chart scrolled under a still pointer holds the samples without a hover report; the "
        "deferral timer finds the pointer gone", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        CHECK(!c.pointerOnChart);
        ops.log.clear();
        c.Notify();
        // The window reads the pointer's place as it handles the refresh.
        CHECK(!TestDeliver(c, ops, false, true));
        TestSettled(c, ops);
        CHECK(c.pointerOnChart && c.samplesDirty && c.timer && TestCount(ops.log, L"peek 7d") == 0);
        c.DeferralTick(false, true);
        TestSettled(c, ops);
        c.DeferralTick(false, false);
        TestSettled(c, ops);
        TestDeliver(c, ops, false, false);
        TestSettled(c, ops);
        CHECK(TestCount(ops.log, L"peek 7d") == 1 && !c.samplesDirty && !c.timer);
    });

    Run("history refresh: unload while a replacement's accounts are collected changes nothing: the snapshot and "
        "its rows stay paired", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        c.Open();
        c.Load();
        std::shared_ptr<const HistorySnapshot> held = c.snapshot;
        ops.Append(TestReading(0x0A, 2, kWeekFrom, {{1, 58}}));
        ops.now += kMinute;
        ops.peeked = [&] {
            if (ops.log.back() != L"peek") ops.unloading = true;
        };
        c.Notify();
        TestDeliver(c, ops);
        CHECK(ops.log.back() == L"peek 7d" && ops.unloading);
        CHECK(c.snapshot == held && c.generation == 1 && c.from == kWeekFrom && c.to == kWeekTo && c.accounts.size() == 3);
        for (const HistoryUiAccount& account : c.accounts) {
            for (size_t row : account.rows) {
                CHECK(row < held->samples.size() && held->samples[row].accountId == account.id);
            }
        }
    });

    Run("history refresh: closing drops outstanding work and the timer; nothing posts or reads after it; unload "
        "the same", [] {
        for (bool unload : {false, true}) {
            TestHistoryOps ops = TestControllerOps();
            HistoryController c(ops);
            c.Open();
            c.Load();
            c.Notify();
            CHECK(!TestDeliver(c, ops, true) && c.timer);
            c.Notify();
            ops.log.clear();
            if (unload) ops.unloading = true;
            else c.RequestClose();
            TestSettled(c, ops);
            c.Notify();
            c.DeferralTick(false, false);
            c.Inspect(false);
            CHECK(!TestDeliver(c, ops));
            TestSettled(c, ops);
            CHECK(!c.liveDirty && !c.samplesDirty && !c.timer && ops.posted == 0);
            CHECK(ops.log == TestLog{L"timer off"});
        }
    });

    // Enable recording ---------------------------------------------------------------------------

    // Recording off for this user, loaded with CC 1 selected.
    auto off = [](TestHistoryOps& ops, HistoryController& c) {
        ops.settings.settings.historyEnabled = false;
        c.Open();
        CHECK(!c.view.canEnableRecording);
        c.Command(HistoryUiCommand::EnableRecording);  // Not before the first load
        CHECK(ops.log == TestLog{L"settings"});
        c.Load();
        c.Command(HistoryUiCommand::Select, 0, 0xC1);
        CHECK(c.view.pill == HistoryUiPill::Off && !c.view.recordingEnabled && c.view.canEnableRecording);
        ops.log.clear();
    };

    Run("history recording: Enable recording applies consent, which notifies the windows, then loads; the "
        "selected entry takes focus", [off] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        off(ops, c);
        ops.request = 0x0A;
        c.Command(HistoryUiCommand::EnableRecording);
        CHECK((ops.log == TestLog{L"consent", L"notify", L"take", L"settings", L"snapshot 7d"}));
        CHECK(c.view.recordingEnabled && c.view.pill == HistoryUiPill::Recording && c.view.notice.text.empty());
        // The load took the open request; the window focuses the selected entry.
        CHECK(c.selectedId == 0x0A && c.focusSelected);
    });

    Run("history recording: a failed consent still notifies and loads, then shows its error notice; focus "
        "stays; a repeat replaces it; the next success or a selection change clears it", [off] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        off(ops, c);
        ops.consent = HistoryConsentResult::Failed;
        PCWSTR failed = L"Could not save recording consent. Recording is still off.";
        c.Command(HistoryUiCommand::EnableRecording);
        CHECK((ops.log == TestLog{L"consent", L"notify", L"take", L"settings", L"snapshot 7d"}));
        CHECK(c.view.notice.text == failed && c.view.notice.severity == UiSeverity::Error && !c.focusSelected);
        CHECK(!c.view.recordingEnabled && c.view.pill == HistoryUiPill::Off);
        // The notice is the first info bar, above the recording-off banner with its button.
        UiFrame frame = TestHistoryFrame(c.view);
        const UiItem* notice = TestFind(frame.content, failed);
        const UiItem* enable = UiFindItem(frame, kHistoryUiEnableRecording);
        CHECK(notice && enable && enable->enabled && notice->rect.bottom < enable->rect.top);
        // A repeated failure: one notice, the same.
        c.Command(HistoryUiCommand::EnableRecording);
        CHECK(c.view.notice.text == failed);
        frame = TestHistoryFrame(c.view);
        CHECK(std::count_if(frame.content.begin(), frame.content.end(),
                            [&](const UiItem& item) { return UiRunsText(item.runs) == failed; }) == 1);
        // Another account selected: gone.
        c.Command(HistoryUiCommand::Select, 0, 0x0A);
        CHECK(c.view.notice.text.empty());
        c.Command(HistoryUiCommand::EnableRecording);
        CHECK(c.view.notice.text == failed);
        c.Command(HistoryUiCommand::View, int(HistoryView::Weekly));  // Not a selection change
        CHECK(c.view.notice.text == failed);
        // The next operation, a success: gone.
        ops.consent = HistoryConsentResult::Applied;
        c.Command(HistoryUiCommand::EnableRecording);
        CHECK(c.view.notice.text.empty() && c.view.recordingEnabled && c.focusSelected);
    });

    Run("history recording: unload while Enable recording waits for the edit lock: nothing notified, loaded "
        "or shown; a close during the transaction leaves its load and notice out", [off] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        off(ops, c);
        ops.waitingForLock = [&] { ops.unloading = true; };
        c.Command(HistoryUiCommand::EnableRecording);
        CHECK(ops.log == TestLog{L"consent"});
        CHECK(c.view.notice.text.empty() && !c.focusSelected && !c.view.recordingEnabled);
        for (HistoryConsentResult result : {HistoryConsentResult::Applied, HistoryConsentResult::Failed}) {
            TestHistoryOps closing = TestControllerOps();
            HistoryController w(closing);
            off(closing, w);
            closing.consent = result;
            closing.notified = [&] { w.RequestClose(); };
            w.Command(HistoryUiCommand::EnableRecording);
            CHECK((closing.log == TestLog{L"consent", L"notify"}));
            CHECK(w.view.notice.text.empty() && !w.focusSelected);
        }
    });
}
