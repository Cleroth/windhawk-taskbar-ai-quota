// GitHub Copilot usage response parsing (the monthly AI credits allowance) and token refresh.

// The allowance comes from premium_interactions while it has one (unlimited, or a positive
// entitlement), else, when premium is missing or reports an entitlement of 0, from the chat
// snapshot, where Copilot Free reports it, and last from legacy Free's monthly chat requests
// (monthly_quotas and limited_user_quotas); completions never count. A finite allowance gives the
// used percentage and amounts; an unlimited one (an organization's pooled budget) only its
// credits used, or that the pool is used up. A plan without any allowance parses with a note.
// Missing or malformed entitlements and percentages are errors, never 0 or 100%.
static bool ParseCopilotUsage(const std::string& body, AccountData* d, AccountError* error) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        // A number, or a fully numeric string (entitlements come as strings), within bounds that
        // keep amounts printable and Unix millisecond times.
        auto number = [](JsonObject const& o, PCWSTR name) -> std::optional<double> {
            if (!o || !o.HasKey(name)) return std::nullopt;
            auto v = o.GetNamedValue(name);
            double value = NAN;
            if (v.ValueType() == JsonValueType::Number) {
                value = v.GetNumber();
            } else if (v.ValueType() == JsonValueType::String) {
                std::wstring s(v.GetString());
                wchar_t* end = nullptr;
                value = wcstod(s.c_str(), &end);
                if (end == s.c_str() || *end) return std::nullopt;
            }
            if (!std::isfinite(value) || std::abs(value) >= 1e15) return std::nullopt;
            return value;
        };
        auto isFalse = [](JsonObject const& o, PCWSTR name) {
            if (!o || !o.HasKey(name)) return false;
            auto v = o.GetNamedValue(name);
            return v.ValueType() == JsonValueType::Boolean && !v.GetBoolean();
        };

        // Plan names as VS Code shows them: free SKUs first, then copilot_plan ("individual" is
        // Pro); an unknown plan as reported. GitHub's product names, the same in every language.
        std::wstring plan = GetStr(root, L"copilot_plan"), sku = GetStr(root, L"access_type_sku");
        d->plan = sku == L"free_limited_copilot" ? L"Free" :  // i18n-en
                  sku == L"free_educational_quota" || plan == L"individual_edu" ? L"Student" :  // i18n-en
                  plan == L"individual" ? L"Pro" :  // i18n-en
                  plan == L"individual_pro" ? L"Pro+" :  // i18n-en
                  plan == L"individual_max" ? L"Max" :  // i18n-en
                  plan == L"business" ? L"Business" :  // i18n-en
                  plan == L"enterprise" ? L"Enterprise" : plan;  // i18n-en
        // Billing in requests rather than AI credits only when explicitly not token based.
        if (isFalse(root, L"token_based_billing")) d->copilotUnit = CopilotUnit::PremiumRequests;

        auto snapshots = GetObj(root, L"quota_snapshots");
        auto premium = GetObj(snapshots, L"premium_interactions");
        auto chat = GetObj(snapshots, L"chat");
        std::optional<double> premiumEntitlement = number(premium, L"entitlement");
        std::optional<double> chatEntitlement = number(chat, L"entitlement");
        // A malformed premium entitlement is an error, not a missing allowance.
        bool fromPremium = GetBool(premium, L"unlimited") || premiumEntitlement.value_or(0) > 0;
        bool premiumNone = !premium || premiumEntitlement == 0.0;
        JsonObject quota = fromPremium ? premium :
                           premiumNone && chatEntitlement.value_or(0) > 0 ? chat : nullptr;
        std::optional<double> legacyMonthly = number(GetObj(root, L"monthly_quotas"), L"chat");
        std::optional<double> legacyLeft = number(GetObj(root, L"limited_user_quotas"), L"chat");
        bool legacy = !quota && premiumNone && legacyMonthly.value_or(0) > 0 && legacyLeft;
        if (!quota && !legacy) {
            // Never in chat requests: legacy Free's are an allowance.
            if (premiumEntitlement == 0.0 && chatEntitlement == 0.0) {
                d->note = d->copilotUnit == CopilotUnit::PremiumRequests ? AccountNote::NoPremiumRequestAllowance :
                                                                           AccountNote::NoAiCreditAllowance;
                return true;
            }
            if (error) *error = UnexpectedResponseFormat(body);
            return false;
        }

        if (legacy) {
            // Chat requests left of the month's.
            d->copilotUnit = CopilotUnit::ChatRequests;
            d->extraUsage.pct = std::clamp(100 * (1 - *legacyLeft / *legacyMonthly), 0.0, 100.0);
            d->extraUsedAmount = std::clamp(*legacyMonthly - *legacyLeft, 0.0, *legacyMonthly);
            d->extraLimitAmount = *legacyMonthly;
        } else if (GetBool(quota, L"unlimited")) {
            d->copilotUnlimited = true;
            d->copilotExhausted = isFalse(quota, L"has_quota");
            std::optional<double> used = number(quota, L"credits_used");
            if (!d->copilotExhausted && used && *used >= 0) d->extraUsedAmount = *used;
        } else {
            std::optional<double> percentRemaining = number(quota, L"percent_remaining");
            if (!percentRemaining) {
                if (error) *error = UnexpectedResponseFormat(body);
                return false;
            }
            double entitlement = fromPremium ? *premiumEntitlement : *chatEntitlement;
            double usedPct = 100 - std::clamp(*percentRemaining, 0.0, 100.0);
            // quota_remaining shares the entitlement's basis; credits_used doesn't.
            std::optional<double> remaining = number(quota, L"quota_remaining");
            double used = remaining && *remaining >= 0 ? entitlement - *remaining : entitlement * usedPct / 100;
            d->extraUsage.pct = usedPct;
            d->extraUsedAmount = std::max(used, 0.0);
            d->extraLimitAmount = entitlement;
        }
        std::optional<double> overage = number(quota, L"overage_count");
        if (overage && *overage > 0) d->copilotOverage = *overage;

        // The snapshot's own reset (Unix seconds) wins, then the account's: with a time, a date
        // (UTC midnight), then legacy Free's date. The allowance renews monthly.
        std::optional<double> resetAt = number(quota, L"quota_reset_at");
        if (resetAt && *resetAt > 0 && *resetAt < 1e14) {
            d->extraUsage.resetUnixMs = *resetAt > (double)kUnixTimestampMsThreshold ?
                                            (ULONGLONG)*resetAt : (ULONGLONG)(*resetAt * 1000);
        }
        for (PCWSTR name : {L"quota_reset_date_utc", L"quota_reset_date", L"limited_user_reset_date"}) {
            if (d->extraUsage.resetUnixMs) break;
            std::wstring date = GetStr(root, name);
            if (date.find(L'T') == std::wstring::npos) date += L"T00:00:00Z";
            d->extraUsage.resetUnixMs = ParseIso8601Ms(date);
        }
        d->extraUsage.windowDurationMs = MonthBeforeMs(d->extraUsage.resetUnixMs);
        return true;
    } catch (...) {
        if (error) *error = UnexpectedResponseFormat(body);
        return false;
    }
}

// grant_type=refresh_token, form-encoded, for sign-ins whose access tokens expire (GitHub issues
// a refresh token only with those). GitHub reports most failures as HTTP 200 with an error code:
// only a refresh token it no longer accepts, or a client error other than a rate limit (408,
// 429, 403 with Retry-After), needs a new sign-in. `err`: why it failed (none for a rejection
// without a request).
static TokenEndpointResult RefreshCopilotToken(StoredToken* tok, AccountError* err,
                                               int* retryAfterSec) {
    if (retryAfterSec) *retryAfterSec = 0;
    *err = {};
    if (tok->refreshToken.empty()) return TokenEndpointResult::Rejected;
    std::string body = "client_id=" + UrlEncode(WideToUtf8(kCopilotClientId)) +
                       "&grant_type=refresh_token&refresh_token=" +
                       UrlEncode(WideToUtf8(tok->refreshToken));
    HttpResult r = HttpRequest(L"POST", kCopilotTokenHost, kCopilotTokenPath, kOAuthUserAgent,
                               L"Content-Type: application/x-www-form-urlencoded\r\n"  // i18n-en
                               L"Accept: application/json\r\n",
                               body);
    if (!r.ok) {
        *err = {AccountErrorCode::Network};
        return TokenEndpointResult::TransientFailure;
    }
    if (r.status != 200) {
        if (retryAfterSec) *retryAfterSec = r.retryAfterSec;
        *err = OAuthFailure(r);
        bool rejected = r.status >= 400 && r.status < 500 && r.status != 408 &&
                        r.status != 429 && !(r.status == 403 && r.retryAfterSec > 0);
        return rejected ? TokenEndpointResult::Rejected : TokenEndpointResult::TransientFailure;
    }
    try {
        auto root = JsonObject::Parse(Utf8ToWide(r.body));
        std::wstring access = GetStr(root, L"access_token");
        if (access.empty()) {
            std::wstring code = GetStr(root, L"error");
            std::wstring detail = ParseOAuthError(r.body);
            *err = detail.empty() ? AccountError{AccountErrorCode::NoAccessToken} :
                                    AccountError{AccountErrorCode::Message, detail};
            return code == L"bad_refresh_token" || code == L"invalid_grant" ?
                       TokenEndpointResult::Rejected : TokenEndpointResult::TransientFailure;
        }
        tok->accessToken = access;
        std::wstring refresh = GetStr(root, L"refresh_token");
        if (!refresh.empty()) tok->refreshToken = refresh;
        double expiresIn = GetNum(root, L"expires_in", 0);
        tok->expiresMs = expiresIn > 0 && expiresIn < 1e9 ? NowUnixMs() + (ULONGLONG)(expiresIn * 1000) : 0;
        return TokenEndpointResult::Success;
    } catch (...) {
        *err = {AccountErrorCode::InvalidTokenResponse};
        return TokenEndpointResult::TransientFailure;
    }
}
