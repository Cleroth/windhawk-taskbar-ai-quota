// Account errors' text (AccountError, core/model.h) in a language: presentation passes the
// reader's locale; logs and their dedupe pass English (the default locale), so a language switch
// never reads as a new error. Inline continues running text (the details flyout's "Error: ..."
// line, logs); Alone stands on its own, as a sentence begins (the Accounts page's state, the
// sign-in window's status line). A cause always reads inline, inside its error's text: {reason}
// and {shape} are complete phrases of their own, set apart (a colon, parentheses), never inflected
// by the text around them. Pure.

enum class AccountErrorForm : uint8_t { Inline, Alone };

static std::wstring AccountErrorText(const I18nLocale& locale, const AccountError& error,
                                     AccountErrorForm form = AccountErrorForm::Inline) {
    using enum AccountErrorCode;
    const bool alone = form == AccountErrorForm::Alone;
    auto text = [&](I18nKey inlineKey, I18nKey aloneKey) { return I18nText(locale, alone ? aloneKey : inlineKey); };
    std::wstring cause =
        error.cause == None ? std::wstring() : AccountErrorText(locale, {error.cause, error.detail, error.status});
    switch (error.code) {
        case None:
            return {};
        case Message:
            return error.detail;
        case NotSignedIn:
            return text(TrCtx(L"account-error", L"not signed in - click to sign in"),
                        TrCtx(L"account-error-alone", L"Not signed in - click to sign in"));
        case SessionExpired:
            return text(TrCtx(L"account-error", L"session expired - click to sign in"),
                        TrCtx(L"account-error-alone", L"Session expired - click to sign in"));
        case Unauthorized:
            return text(TrCtx(L"account-error", L"unauthorized - click to sign in"),
                        TrCtx(L"account-error-alone", L"Unauthorized - click to sign in"));
        case TokenRefreshFailed:
            if (cause.empty()) {
                return text(TrCtx(L"account-error", L"token refresh failed"),
                            TrCtx(L"account-error-alone", L"Token refresh failed"));
            }
            return I18nText(locale, alone ? TrCtx(L"account-error-alone", L"Token refresh failed: {reason}") :
                                            TrCtx(L"account-error", L"token refresh failed: {reason}"),
                            {{L"reason", cause}});
        case TokenSaveFailed:
            return text(TrCtx(L"account-error", L"could not save refreshed token"),
                        TrCtx(L"account-error-alone", L"Could not save refreshed token"));
        case Network:
            return text(TrCtx(L"account-error", L"network error"), TrCtx(L"account-error-alone", L"Network error"));
        case Http:
            return I18nText(locale, TrCtx(L"account-error", L"HTTP {status}"), {{L"status", error.status}});
        case RateLimited:
            return text(TrCtx(L"account-error", L"rate limited by API"),
                        TrCtx(L"account-error-alone", L"Rate limited by API"));
        case NoCopilotAccess:
            return text(TrCtx(L"account-error", L"no Copilot access on this GitHub account"),
                        TrCtx(L"account-error-alone", L"No Copilot access on this GitHub account"));
        case NoOpenCodeSubscription:
            return text(TrCtx(L"account-error", L"no OpenCode Go subscription on this key"),
                        TrCtx(L"account-error-alone", L"No OpenCode Go subscription on this key"));
        case UnexpectedResponse:
            if (cause.empty()) {
                return text(TrCtx(L"account-error", L"unexpected response format"),
                            TrCtx(L"account-error-alone", L"Unexpected response format"));
            }
            return I18nText(locale, alone ? TrCtx(L"account-error-alone", L"Unexpected response format ({shape})") :
                                            TrCtx(L"account-error", L"unexpected response format ({shape})"),
                            {{L"shape", cause}});
        case FetchThreadFailed:
            return text(TrCtx(L"account-error", L"fetch thread failed"),
                        TrCtx(L"account-error-alone", L"Fetch thread failed"));
        case AntigravityNotRunning:
            return I18nText(locale, TrCtx(L"account-error", L"Antigravity not running"));
        case AntigravityUnavailable:
            return I18nText(locale, TrCtx(L"account-error", L"Antigravity quota unavailable"));
        case LanguageServerNetwork:
            return text(TrCtx(L"account-error", L"network error (language server)"),
                        TrCtx(L"account-error-alone", L"Network error (language server)"));
        case LanguageServerHttp:
            return I18nText(locale, TrCtx(L"account-error", L"HTTP {status} from language server"),
                            {{L"status", error.status}});
        case LanguageServerResponse:
            return text(TrCtx(L"account-error", L"unexpected response"),
                        TrCtx(L"account-error-alone", L"Unexpected response"));
        case NoQuotaGroups:
            return text(TrCtx(L"account-error", L"no quota groups in response"),
                        TrCtx(L"account-error-alone", L"No quota groups in response"));
        case QuotaGroupsNotArray:
            return text(TrCtx(L"account-error", L"groups is not an array"),
                        TrCtx(L"account-error-alone", L"Groups is not an array"));
        case NoUsableQuotaBuckets:
            return text(TrCtx(L"account-error", L"no usable Antigravity quota buckets in response"),
                        TrCtx(L"account-error-alone", L"No usable Antigravity quota buckets in response"));
        case QuotaSummaryInvalid:
            return text(TrCtx(L"account-error", L"failed to parse quota summary JSON"),
                        TrCtx(L"account-error-alone", L"Failed to parse quota summary JSON"));
        case NoUserStatus:
            return text(TrCtx(L"account-error", L"no userStatus object"),
                        TrCtx(L"account-error-alone", L"No userStatus object"));
        case UserStatusInvalid:
            return text(TrCtx(L"account-error", L"failed to parse user status JSON"),
                        TrCtx(L"account-error-alone", L"Failed to parse user status JSON"));
        case NoUsableQuota:
            return text(TrCtx(L"account-error", L"no usable Antigravity quota in response"),
                        TrCtx(L"account-error-alone", L"No usable Antigravity quota in response"));
        // Token endpoint failures: only causes, logs and the sign-in window's retries show them.
        case NoAccessToken:
            return I18nText(locale, TrCtx(L"account-error", L"no access token in response"));
        case InvalidTokenResponse:
            return text(TrCtx(L"account-error", L"invalid token response"),
                        TrCtx(L"account-error-alone", L"Invalid token response"));
        case UnexpectedTokenResponse:
            return text(TrCtx(L"account-error", L"unexpected token response"),
                        TrCtx(L"account-error-alone", L"Unexpected token response"));
        case InternalError:
            return I18nText(locale, TrCtx(L"account-error", L"internal error"));
        // Response body shapes: only causes.
        case EmptyBody:
            return I18nText(locale, TrCtx(L"response-shape", L"empty body"));
        case NoBodyKeys:
            return I18nText(locale, TrCtx(L"response-shape", L"JSON object with no keys"));
        case BodyKeys:
            return I18nText(locale, TrCtx(L"response-shape", L"keys: {keys}"), {{L"keys", error.detail}});
        case InvalidJsonBody:
            return I18nText(locale, TrCtx(L"response-shape", L"non-object or invalid JSON body"));
    }
    return {};
}
