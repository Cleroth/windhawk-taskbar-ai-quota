// The history window's operations through its controller, against the in-memory ops of
// history-controller.inc: Export (its scope frozen across the large-export warning and the save
// dialog, the posted write and its progress, cancellations and failures), Clear all history (its
// confirmation, result and the burn-up curves), About these charts, and the close latch or unload
// arriving inside each dialog; loads, commands and refreshes meanwhile, with the no-stranding
// invariant (history-refresh.inc) checked after the events. Orchestration only: the production
// dialogs and their scope (host probe), the CSV file (tests/history: the writer) and the journal's
// Clear are outside this suite. Hand-derived expectations.

static const PCWSTR kTestExportAll = L"choose Export all accounts \u2014 all meters, selected range (both weeks if comparing)";
static const PCWSTR kTestExportOne =
    L"choose Export selected account \u2014 all meters, selected range (both weeks if comparing)";

// The controller's journal and 100,001 more CC 1 readings in the week, six seconds apart (no
// values): about 500,015 export rows for CC 1, past the warning's 500,000.
static TestHistoryOps TestLargeOps() {
    static const std::vector<HistorySample> more = [] {
        std::vector<HistorySample> samples;
        for (ULONGLONG i = 0; i <= 100000; i++) samples.push_back(TestReading(0xC1, 1, kWeekFrom + i * 6000, {}));
        return samples;
    }();
    TestHistoryOps ops = TestControllerOps();
    ops.journal.insert(ops.journal.end(), more.begin(), more.end());
    ops.stamp.appended = ops.journal.size();
    return ops;
}

// Loaded (All accounts, or `select`), the log cleared.
static void TestLoaded(HistoryController& c, TestHistoryOps& ops, uint64_t select = 0) {
    c.Open();
    c.Load();
    if (select) c.Command(HistoryUiCommand::Select, 0, select);
    ops.log.clear();
}

// Every control of the frame disabled but the legend's chips (and the chart, which only reads).
static void TestOnlyChips(const UiFrame& frame) {
    for (const std::vector<UiItem>* items : {&frame.nav, &frame.main, &frame.content}) {
        for (const UiItem& item : *items) {
            if (!UiInteractiveKind(item.kind) || item.kind == UiItemKind::Card) continue;
            bool chip = item.id >= kHistoryUiChip;
            CHECK(item.enabled == (chip || item.kind == UiItemKind::Chart || item.id == kHistoryUiProgress));
        }
    }
}

static void RunHistoryOperationsTests() {
    Run("history export: the scope freezes at the command (one account, the shown times, the snapshot itself); "
        "an activation, a poll and clicks inside the warning and the save dialog change neither it nor the "
        "window; the write is posted with the progress shown and every control but the chips disabled; the "
        "waiting load runs once after it, taking the latest request", [] {
        TestHistoryOps ops = TestLargeOps();
        HistoryController c(ops);
        TestLoaded(c, ops, 0xC1);
        std::shared_ptr<const HistorySnapshot> held = c.snapshot;
        ULONGLONG readFrom = c.readFrom, to = c.to;
        uint64_t generation = c.generation;
        auto arrive = [&] {
            CHECK(c.modal == 1 && c.Busy() && c.operation == HistoryOperation::Export);
            ops.request = 0x0A;
            c.RequestLoad();
            ops.now += kMinute;
            ops.Append(TestReading(0xC1, 1, ops.now - 1000, {{1, 50}}));
            c.Notify();
            TestSettled(c, ops);
            CHECK(!TestDeliver(c, ops));
            TestSettled(c, ops);
            c.Command(HistoryUiCommand::Select, 0, 0x0A);
            c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Month));
            c.Command(HistoryUiCommand::Export);
        };
        ops.asking = arrive;
        ops.choosing = arrive;
        c.Command(HistoryUiCommand::Export);
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"ask Large history export", L"post", kTestExportOne, L"post", L"post write"}));
        CHECK(ops.asked.size() == 1 && ops.asked[0].second == (MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2));
        CHECK(ops.asked[0].first == L"This export contains about 500015 rows. It may take a while and exceed "
                                    L"spreadsheet row limits. The history window will be busy while writing; "
                                    L"Explorer's taskbar remains available. Continue?");
        CHECK(c.operation == HistoryOperation::Writing && c.loadPending && c.modal == 0 && ops.writePosts == 1);
        CHECK(c.exportScope->snapshot == held && c.exportScope->accountId == 0xC1 && c.exportScope->from == readFrom &&
              c.exportScope->to == to && !c.exportScope->compare && c.destination == L"C:\\Exports\\usage.csv");
        CHECK(c.snapshot == held && c.generation == generation && c.selectedId == 0xC1 &&
              c.range == HistoryUiRange::Week && ops.request == 0x0A && c.exported == 0);
        CHECK(c.view.exporting && c.view.notice.text.empty());
        UiFrame frame = TestHistoryFrame(c.view);
        CHECK(UiFindItem(frame, kHistoryUiProgress) && UiFindItem(frame, c.view.lines.at(0).chip));
        TestOnlyChips(frame);

        int repaints = 0;
        ops.log.clear();
        c.Write(true, [&] { repaints++; });
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"write C:\\Exports\\usage.csv", L"take", L"settings", L"snapshot 7d keep 90"}));
        CHECK(ops.written.size() == 1 && ops.written[0].snapshot == held && ops.written[0].accountId == 0xC1 &&
              ops.written[0].from == readFrom && ops.written[0].to == to);
        CHECK(c.operation == HistoryOperation::None && !c.loadPending && !c.exportScope && !c.view.exporting);
        CHECK(c.selectedId == 0x0A && ops.request == 0 && c.generation == generation + 1 && !c.liveDirty &&
              !c.samplesDirty);
        // The notice comes after the waiting load: its selection change doesn't take it away.
        CHECK(c.view.notice.text == L"Exported 100,003 meter rows (selected account) to C:\\Exports\\usage.csv. "
                                    L"Labels omitted; times UTC." && c.view.notice.severity == UiSeverity::Info);
        CHECK(repaints == 0 && UiFindItem(TestHistoryFrame(c.view), kHistoryUiExport)->enabled);
    });

    Run("history export: progress reports only set the rows written and ask for a repaint; nothing is read or "
        "composed until the write ends", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        TestLoaded(c, ops);
        c.Command(HistoryUiCommand::Export);
        CHECK((ops.log == TestLog{kTestExportAll, L"post write"}) && ops.asked.empty());
        HistoryUiView before = c.view;
        ops.log.clear();
        ops.progressRows = {1000, 2000};
        std::vector<size_t> seen;
        c.Write(true, [&] {
            seen.push_back(c.exported);
            CHECK(c.view == before && c.operation == HistoryOperation::Writing &&
                  ops.log == TestLog{L"write C:\\Exports\\usage.csv"});
        });
        CHECK((seen == std::vector<size_t>{1000, 2000}));
        CHECK(c.view.notice.text == L"Exported 4 meter rows (all accounts) to C:\\Exports\\usage.csv. Labels omitted; "
                                    L"times UTC.");
        // The write message only acts while its write is pending.
        ops.log.clear();
        c.Write(true, [] {});
        CHECK(ops.log.empty());
    });

    Run("history export: cancelled at the warning or in the save dialog, nothing is written; a failed save dialog, "
        "dialogs that can't be tracked and a write that can't be posted say so; each ends the operation", [] {
        struct Case {
            std::deque<int> answers;
            HistoryDialogResult result;
            bool failPost;
            PCWSTR notice;
            UiSeverity severity;
            TestLog log;
        };
        const Case cases[] = {
            {{IDCANCEL}, HistoryDialogResult::Chosen, false, L"CSV export cancelled; no file written.",
             UiSeverity::Info, {L"ask Large history export"}},
            {{0}, HistoryDialogResult::Chosen, false, L"Cannot track native dialogs safely; operation cancelled.",
             UiSeverity::Error, {L"ask Large history export"}},
            {{}, HistoryDialogResult::Cancelled, false, L"CSV export cancelled; no file written.", UiSeverity::Info,
             {L"ask Large history export", kTestExportOne}},
            {{}, HistoryDialogResult::Failed, false, L"Save dialog failed (code 12291).", UiSeverity::Error,
             {L"ask Large history export", kTestExportOne}},
            {{}, HistoryDialogResult::Untracked, false, L"Cannot track native dialogs safely; operation cancelled.",
             UiSeverity::Error, {L"ask Large history export", kTestExportOne}},
            {{}, HistoryDialogResult::Chosen, true, L"CSV export could not start; no file written.", UiSeverity::Error,
             {L"ask Large history export", kTestExportOne, L"post write failed"}},
        };
        TestHistoryOps ops = TestLargeOps();
        HistoryController c(ops);
        TestLoaded(c, ops, 0xC1);
        for (const Case& test : cases) {
            ops.log.clear();
            ops.answers = test.answers;
            ops.destination = {test.result, test.result == HistoryDialogResult::Chosen ? L"C:\\Exports\\usage.csv" : L"",
                               test.result == HistoryDialogResult::Failed ? DWORD(12291) : DWORD(0)};
            ops.failPostWrite = test.failPost;
            c.Command(HistoryUiCommand::Export);
            TestSettled(c, ops);
            CHECK(ops.log == test.log && ops.written.empty());
            CHECK(c.operation == HistoryOperation::None && !c.exportScope && !c.view.exporting && c.view.canExport);
            CHECK(c.view.notice.text == test.notice && c.view.notice.severity == test.severity);
        }
    });

    Run("history export: a close latched or unload inside the warning or the save dialog ends it: nothing written, "
        "shown or loaded after it (a load asked for before is dropped)", [] {
        {
            TestHistoryOps ops = TestLargeOps();
            HistoryController c(ops);
            TestLoaded(c, ops, 0xC1);
            ops.asking = [&] {
                c.RequestLoad();
                c.RequestClose();
            };
            c.Command(HistoryUiCommand::Export);
            CHECK(ops.log == TestLog{L"ask Large history export"});
            CHECK(c.operation == HistoryOperation::None && !c.loadPending && c.view.notice.text.empty());
        }
        for (bool unload : {false, true}) {
            TestHistoryOps ops = TestControllerOps();
            HistoryController c(ops);
            TestLoaded(c, ops);
            ops.choosing = [&] {
                c.RequestLoad();
                if (unload) ops.unloading = true;
                else c.RequestClose();
            };
            c.Command(HistoryUiCommand::Export);
            CHECK(ops.log == TestLog{kTestExportAll});
            CHECK(c.operation == HistoryOperation::None && c.view.notice.text.empty() && ops.writePosts == 0);
            c.Write(true, [] {});
            CHECK(ops.log.size() == 1 && ops.written.empty());
        }
    });

    Run("history export: a close before the posted write, or the frame with its progress never shown, writes "
        "nothing", [] {
        for (bool close : {false, true}) {
            TestHistoryOps ops = TestControllerOps();
            HistoryController c(ops);
            TestLoaded(c, ops);
            c.Command(HistoryUiCommand::Export);
            ops.log.clear();
            if (close) c.RequestClose();
            c.Write(close, [] {});
            CHECK(ops.log.empty() && ops.written.empty() && c.operation == HistoryOperation::None);
            CHECK(c.view.notice.text == (close ? L"" : L"CSV export cancelled; no file written."));
        }
    });

    Run("history export: a failed write keeps the destination and says why (a temporary file that can't be "
        "created too); a repeat replaces the notice; unload during the write shows and loads nothing", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        TestLoaded(c, ops);
        auto run = [&] {
            c.Command(HistoryUiCommand::Export);
            c.Write(true, [] {});
            TestSettled(c, ops);
            CHECK(c.operation == HistoryOperation::None);
        };
        ops.writeResult = {0, ERROR_ACCESS_DENIED, false};
        run();
        CHECK(c.view.notice.text == L"Cannot create the CSV temporary file (Windows error 5). Existing destination "
                                    L"unchanged." && c.view.notice.severity == UiSeverity::Error);
        ops.writeResult = {120, ERROR_DISK_FULL, true};
        run();
        PCWSTR failed = L"CSV export failed (Windows error 112). Existing destination unchanged.";
        CHECK(c.view.notice.text == failed && c.view.notice.severity == UiSeverity::Error);
        run();
        UiFrame frame = TestHistoryFrame(c.view);
        CHECK(std::count_if(frame.content.begin(), frame.content.end(),
                            [&](const UiItem& item) { return UiRunsText(item.runs) == failed; }) == 1);
        // Unload mid-write: its cancellation reported, nothing shown or loaded.
        c.Command(HistoryUiCommand::Export);
        c.RequestLoad();
        CHECK(c.loadPending);
        ops.log.clear();
        ops.writeResult = {12, ERROR_CANCELLED, true};
        ops.writing = [&] { ops.unloading = true; };
        c.Write(true, [] {});
        CHECK(ops.log == TestLog{L"write C:\\Exports\\usage.csv"});
        CHECK(c.operation == HistoryOperation::None && c.view.notice.text.empty());
    });

    Run("history export: comparing writes both weeks; All accounts every account; the notice names them and goes "
        "with a selection change", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        TestLoaded(c, ops);
        c.Command(HistoryUiCommand::Compare, 1);
        ops.log.clear();
        c.Command(HistoryUiCommand::Export);
        CHECK((ops.log == TestLog{kTestExportAll, L"post write"}));
        c.Write(true, [] {});
        const HistoryExportScope& scope = ops.written.at(0);
        CHECK(scope.accountId == 0 && scope.compare && scope.from == kWeekFrom - kHistoryWeekMs && scope.to == kWeekTo);
        CHECK(c.view.notice.text == L"Exported 5 meter rows (all accounts) to C:\\Exports\\usage.csv. Labels omitted; "
                                    L"times UTC. Includes both elapsed 7-day periods.");
        c.Command(HistoryUiCommand::View, int(HistoryView::AllMeters));
        CHECK(!c.view.notice.text.empty());
        c.Command(HistoryUiCommand::Select, 0, 0x0A);
        CHECK(c.view.notice.text.empty());
    });

    Run("history export: one operation at a time; while the write waits only the legend's chips act, explicit "
        "loads wait for it and run once after, refreshes are held and resume after it", [] {
        for (bool load : {false, true}) {
            TestHistoryOps ops = TestControllerOps();
            HistoryController c(ops);
            TestLoaded(c, ops, 0xC1);
            c.Command(HistoryUiCommand::Export);
            CHECK(c.operation == HistoryOperation::Writing);
            ops.log.clear();
            HistoryUiLine line = c.view.lines.at(0);
            for (HistoryUiCommand command : {HistoryUiCommand::Export, HistoryUiCommand::Select, HistoryUiCommand::View,
                                             HistoryUiCommand::Range, HistoryUiCommand::Compare,
                                             HistoryUiCommand::Refresh, HistoryUiCommand::EnableRecording}) {
                c.Command(command, 1, 0x0A);
            }
            for (HistoryUiMenuItem item : {HistoryUiMenuItem::Settings, HistoryUiMenuItem::Folder,
                                           HistoryUiMenuItem::About, HistoryUiMenuItem::Clear}) {
                c.Command(HistoryUiCommand::Menu, int(item));
            }
            CHECK(ops.log.empty() && c.selectedId == 0xC1 && c.view.view == HistoryView::AllMeters &&
                  c.range == HistoryUiRange::Week && !c.compare && c.operation == HistoryOperation::Writing);
            c.Command(HistoryUiCommand::Line, line.meter, line.historyId);
            CHECK(c.view.lines.at(0).hidden && c.view.exporting);
            if (load) c.RequestLoad();
            c.Notify();
            TestSettled(c, ops);
            CHECK(!TestDeliver(c, ops));
            TestSettled(c, ops);
            CHECK(c.loadPending == load && c.liveDirty && c.samplesDirty);
            ops.log.clear();
            c.Write(true, [] {});
            TestSettled(c, ops);
            if (load) {
                CHECK((ops.log == TestLog{L"write C:\\Exports\\usage.csv", L"take", L"settings", L"snapshot 7d keep 90"}));
                CHECK(!c.liveDirty && !c.samplesDirty && ops.posted == 0);
            } else {
                CHECK((ops.log == TestLog{L"write C:\\Exports\\usage.csv", L"post"}));
                CHECK(TestDeliver(c, ops));
                TestSettled(c, ops);
                CHECK(TestCount(ops.log, L"peek 7d") == 1 && !c.liveDirty && !c.samplesDirty);
            }
            CHECK(c.view.lines.at(0).hidden);
        }
    });

    // Clear all history ---------------------------------------------------------------------------

    Run("history clear: confirmed (Cancel the default), it clears, drops the burn-up curves and loads, then says "
        "so; a failure drops them too, says why and doesn't load (the next refresh replaces the snapshot by the "
        "new epoch); Cancel and dialogs that can't be tracked clear nothing", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        TestLoaded(c, ops, 0xC1);
        c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Clear));
        TestSettled(c, ops);
        CHECK((ops.log == TestLog{L"ask Clear all usage history", L"clear", L"invalidate", L"take", L"settings",
                                  L"snapshot 7d keep 90"}));
        CHECK(ops.asked.at(0).second == (MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2));
        CHECK(ops.asked.at(0).first == L"Permanently clear ALL local usage history for this Windows user, including "
                                       L"deleted accounts?\r\n\r\nThis cannot be undone. Settings and sign-ins are not "
                                       L"changed. If recording stays enabled, future polls will record new history.");
        CHECK(c.view.notice.text == L"All local history cleared. Future polls can start recording again." &&
              c.view.notice.severity == UiSeverity::Info);
        CHECK(c.snapshot->samples.empty() && c.view.cards.empty() && c.operation == HistoryOperation::None);

        TestHistoryOps failing = TestControllerOps();
        failing.clearResult = false;
        HistoryController f(failing);
        TestLoaded(f, failing, 0xC1);
        uint64_t generation = f.generation;
        f.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Clear));
        CHECK((failing.log == TestLog{L"ask Clear all usage history", L"clear", L"invalidate"}));
        CHECK(f.view.notice.text == L"Could not clear all history. History storage is in use by another Explorer "
                                    L"session, or is inaccessible." && f.view.notice.severity == UiSeverity::Error);
        CHECK(f.generation == generation && f.snapshot->samples.size() == 4);
        f.Notify();
        TestDeliver(f, failing);
        TestSettled(f, failing);
        CHECK(f.generation == generation + 1 && f.snapshot->samples.size() == 4 && !f.view.notice.text.empty());

        for (int answer : {IDCANCEL, 0}) {
            ops.log.clear();
            ops.answers = {answer};
            c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Clear));
            CHECK(ops.log == TestLog{L"ask Clear all usage history"});
            CHECK(c.view.notice.text == (answer ? L"Clear history cancelled." :
                                                  L"Cannot track native dialogs safely; operation cancelled."));
            CHECK(c.view.notice.severity == (answer ? UiSeverity::Info : UiSeverity::Error));
        }
    });

    Run("history clear: a close latched or unload inside the confirmation clears nothing; unload during the clear "
        "still drops the curves but neither loads nor shows; an activation inside the confirmation loads once "
        "after it, and its notice stays", [] {
        for (bool unload : {false, true}) {
            TestHistoryOps ops = TestControllerOps();
            HistoryController c(ops);
            TestLoaded(c, ops);
            ops.asking = [&] {
                if (unload) ops.unloading = true;
                else c.RequestClose();
            };
            c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Clear));
            CHECK(ops.log == TestLog{L"ask Clear all usage history"});
            CHECK(c.operation == HistoryOperation::None && c.view.notice.text.empty());
        }
        {
            TestHistoryOps ops = TestControllerOps();
            HistoryController c(ops);
            TestLoaded(c, ops);
            ops.clearing = [&] { ops.unloading = true; };
            c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Clear));
            CHECK((ops.log == TestLog{L"ask Clear all usage history", L"clear", L"invalidate"}));
            CHECK(c.operation == HistoryOperation::None && c.view.notice.text.empty());
        }
        for (bool cleared : {false, true}) {
            TestHistoryOps ops = TestControllerOps();
            ops.clearResult = cleared;
            HistoryController c(ops);
            TestLoaded(c, ops, 0xC1);
            ops.asking = [&] {
                ops.request = 0x0A;
                c.RequestLoad();
                c.Notify();
                TestSettled(c, ops);
            };
            c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Clear));
            TestSettled(c, ops);
            CHECK(TestCount(ops.log, L"take") == 1 && c.selectedId == 0x0A && !c.loadPending);
            CHECK(!c.view.notice.text.empty() && c.view.notice.severity == (cleared ? UiSeverity::Info : UiSeverity::Error));
            CHECK(!c.liveDirty && !c.samplesDirty);
        }
    });

    // About these charts ----------------------------------------------------------------------------

    Run("history about: a message box with how to read the window, the retention and the oldest loaded sample; "
        "a notice stays; a load asked for meanwhile runs once after it", [] {
        TestHistoryOps ops = TestControllerOps();
        HistoryController c(ops);
        TestLoaded(c, ops);
        ops.folder = false;
        c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Folder));
        ops.log.clear();
        ops.asking = [&] {
            CHECK(c.modal == 1 && c.operation == HistoryOperation::None);
            c.RequestLoad();
            c.RequestLoad();
        };
        c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::About));
        CHECK((ops.log == TestLog{L"ask About these charts", L"take", L"settings", L"snapshot 7d keep 90"}));
        CHECK(ops.asked.at(0).second == (MB_OK | MB_ICONINFORMATION));
        const std::wstring& text = ops.asked.at(0).first;
        CHECK(text.starts_with(L"Reading the charts\r\n") && text.find(L"Legend and readout\r\n") != std::wstring::npos);
        CHECK(text.find(L"Retention: 90 days, also limited to 250,000 records") != std::wstring::npos);
        CHECK(text.ends_with(L"\r\nOldest loaded sample: Sat 24 Oct 15:32"));
        CHECK(c.modal == 0 && !c.loadPending &&
              c.view.notice.text == L"Nothing has been recorded yet. The folder is created with the first recorded sample.");
        // Before the first load: no oldest sample.
        TestHistoryOps fresh = TestControllerOps();
        HistoryController f(fresh);
        f.Open();
        f.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::About));
        CHECK(fresh.asked.size() == 1 && fresh.asked[0].first.find(L"Oldest loaded sample") == std::wstring::npos);
    });

    Run("history operations: after the close request or unload nothing exports, clears, asks or opens", [] {
        for (bool unload : {false, true}) {
            TestHistoryOps ops = TestControllerOps();
            HistoryController c(ops);
            TestLoaded(c, ops);
            if (unload) ops.unloading = true;
            else c.RequestClose();
            c.Command(HistoryUiCommand::Export);
            for (HistoryUiMenuItem item : {HistoryUiMenuItem::Settings, HistoryUiMenuItem::Folder,
                                           HistoryUiMenuItem::About, HistoryUiMenuItem::Clear}) {
                c.Command(HistoryUiCommand::Menu, int(item));
            }
            c.RequestLoad();
            CHECK(ops.log.empty() && c.operation == HistoryOperation::None && !c.loadPending);
        }
    });
}
