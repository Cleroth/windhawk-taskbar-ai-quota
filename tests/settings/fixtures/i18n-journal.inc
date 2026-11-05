// The history journal's coded errors on the production catalogs: each one's English as the journal
// wrote it before codes, German and Japanese, and the history window presenting them in its
// language (the storage bar, a failed Clear's reason). Expected text is written out by hand.

static void RunJournalErrorI18nTests() {
    using enum HistoryJournalError;

    Run("journal errors: each code's English as before; German and Japanese; none is empty", [] {
        const std::pair<HistoryJournalError, std::wstring_view> cases[] = {
            {Unavailable, L"History storage is unavailable."},
            {CannotCreateDirectory, L"Cannot create the local history directory."},
            {InUse, L"History storage is in use by another Explorer session, or is inaccessible."},
            {CannotRead, L"Cannot read history. Check storage permissions, then reopen the mod."},
            {CannotReadSize, L"Cannot read the history file size."},
            {Unsupported, L"Unsupported or damaged history file. Clear history to start again."},
            {ReadFailed, L"History read failed. Loaded samples remain available for export."},
            {DamagedRecord, L"Damaged history record. Export readable samples or clear history."},
            {TooLarge, L"History is too large to load. Clear history to start again."},
            {CannotWrite, L"Cannot write history. Check disk space and storage permissions."},
            {WriteFailed, L"History write failed; the previous journal was kept."},
            {NotSaved, L"History is not being saved to disk. Check disk space and permissions."},
            {AccountConfigured, L"An account in Settings uses this history again."},
        };
        for (const auto& [error, english] : cases) CHECK(HistoryJournalErrorText({}, error) == english);
        CHECK(HistoryJournalErrorText(TestLocale(L"de"), None).empty());
        // Der Verlaufsspeicher ist nicht verfügbar.
        CHECK(HistoryJournalErrorText(TestLocale(L"de"), Unavailable) == L"Der Verlaufsspeicher ist nicht verf\u00FCgba"
                                                                         L"r.");
        // 履歴の書き込みに失敗しました。以前のジャーナルは保持されています。
        CHECK(HistoryJournalErrorText(TestLocale(L"ja"), WriteFailed) ==
              L"\u5C65\u6B74\u306E\u66F8\u304D\u8FBC\u307F\u306B\u5931\u6557\u3057\u307E\u3057\u305F\u3002\u4EE5\u524D"
              L"\u306E\u30B8\u30E3\u30FC\u30CA\u30EB\u306F\u4FDD\u6301\u3055\u308C\u3066\u3044\u307E\u3059\u3002");
    });

    Run("history window: the storage bar in the window's language, re-labeled by a switch; a failed Clear's "
        "reason in the language it ran in", [] {
        TestHistoryOps ops = TestControllerOps();
        ops.status = DamagedRecord;
        HistoryController c(ops);
        c.Open();
        c.Load();
        CHECK(c.view.storageError == L"Damaged history record. Export readable samples or clear history.");
        ops.locale = TestLocale(L"de");
        c.Notify();
        CHECK(c.ProcessRefresh(false, false));
        // Beschädigter Verlaufseintrag. Exportieren Sie lesbare Messwerte oder löschen Sie den Verlauf.
        CHECK(c.view.storageError ==
              L"Besch\u00E4digter Verlaufseintrag. Exportieren Sie lesbare Messwerte oder l\u00F6schen Sie den Verlauf.");
        ops.clearResult = false;
        ops.clearError = InUse;
        c.Command(HistoryUiCommand::Menu, int(HistoryUiMenuItem::Clear));
        // The notice's own sentence is the history window's; its reason: Der Verlaufsspeicher wird von einer
        // anderen Explorer-Sitzung verwendet oder ist nicht zugänglich.
        CHECK(c.view.notice.text.ends_with(L"Der Verlaufsspeicher wird von einer anderen Explorer-Sitzung verwendet "
                                           L"oder ist nicht zug\u00E4nglich.") &&
              c.view.notice.severity == UiSeverity::Error);
    });
}
