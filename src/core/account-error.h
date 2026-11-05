// Account errors' text (AccountError, core/model.h) in a language: presentation passes the
// reader's locale; logs and their dedupe pass English (the default locale), so a language switch
// never reads as a new error. Pure.

static std::wstring AccountErrorText(const I18nLocale& locale, const AccountError& error) {
    switch (error.code) {
        case AccountErrorCode::AntigravityNotRunning:
            return I18nText(locale, TrCtx(L"account-error", L"Antigravity not running"));
        default:
            return error.detail;
    }
}
