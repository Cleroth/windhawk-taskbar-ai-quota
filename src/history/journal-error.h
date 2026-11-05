// The history journal's errors' text (HistoryJournalError, history/types.h) in the reader's
// language. Pure.

static std::wstring HistoryJournalErrorText(const I18nLocale& locale, HistoryJournalError error) {
    using enum HistoryJournalError;
    switch (error) {
        case None:
            return {};
        case Unavailable:
            return I18nText(locale, TrCtx(L"history-storage", L"History storage is unavailable."));
        case CannotCreateDirectory:
            return I18nText(locale, TrCtx(L"history-storage", L"Cannot create the local history directory."));
        case InUse:
            return I18nText(locale, TrCtx(L"history-storage", L"History storage is in use by another Explorer session, "
                                                              L"or is inaccessible."));
        case CannotRead:
            return I18nText(locale, TrCtx(L"history-storage",
                                          L"Cannot read history. Check storage permissions, then reopen the mod."));
        case CannotReadSize:
            return I18nText(locale, TrCtx(L"history-storage", L"Cannot read the history file size."));
        case Unsupported:
            return I18nText(locale, TrCtx(L"history-storage",
                                          L"Unsupported or damaged history file. Clear history to start again."));
        case ReadFailed:
            return I18nText(locale, TrCtx(L"history-storage",
                                          L"History read failed. Loaded samples remain available for export."));
        case DamagedRecord:
            return I18nText(locale, TrCtx(L"history-storage",
                                          L"Damaged history record. Export readable samples or clear history."));
        case TooLarge:
            return I18nText(locale, TrCtx(L"history-storage",
                                          L"History is too large to load. Clear history to start again."));
        case CannotWrite:
            return I18nText(locale, TrCtx(L"history-storage",
                                          L"Cannot write history. Check disk space and storage permissions."));
        case WriteFailed:
            return I18nText(locale, TrCtx(L"history-storage", L"History write failed; the previous journal was kept."));
        case NotSaved:
            return I18nText(locale, TrCtx(L"history-storage",
                                          L"History is not being saved to disk. Check disk space and permissions."));
        case AccountConfigured:
            return I18nText(locale, TrCtx(L"history-storage", L"An account in Settings uses this history again."));
    }
    return {};
}
