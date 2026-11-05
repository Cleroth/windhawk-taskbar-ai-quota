// OpenAI usage and free-reset response parsing, and the user-set credits ceiling.

static constexpr ULONGLONG kUnixTimestampMsThreshold = 100000000000ULL;

static bool ParseOpenAiUsage(const std::string& body, AccountData* d, AccountError* error) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        auto hasWindows = [](JsonObject const& o) -> bool {
            return GetObj(o, L"primary_window") || GetObj(o, L"secondary_window") ||
                   GetObj(o, L"primary") || GetObj(o, L"secondary") ||
                   GetObj(o, L"five_hour") || GetObj(o, L"weekly") ||
                   GetObj(o, L"five_hour_limit") || GetObj(o, L"weekly_limit");
        };

        JsonObject usage = root;
        if (!GetObj(root, L"rate_limit") && !hasWindows(root)) {
            if (auto wrapped = GetObj(root, L"usage");
                wrapped && (GetObj(wrapped, L"rate_limit") || hasWindows(wrapped))) {
                usage = wrapped;
            } else if (auto wrapped = GetObj(root, L"data");
                       wrapped && (GetObj(wrapped, L"rate_limit") || hasWindows(wrapped))) {
                usage = wrapped;
            }
        }

        auto rl = GetObj(usage, L"rate_limit");
        if (!rl && hasWindows(usage)) rl = usage;

        auto resetUnixMs = [](JsonObject const& window) -> ULONGLONG {
            double resetAt = GetNum(window, L"reset_time_ms", 0);
            if (resetAt > 0) return (ULONGLONG)resetAt;

            resetAt = GetNum(window, L"reset_at", 0);
            if (resetAt > (double)kUnixTimestampMsThreshold) return (ULONGLONG)resetAt;
            if (resetAt > 0) return (ULONGLONG)(resetAt * 1000);

            double resetAfter = GetNum(window, L"reset_after_seconds", 0);
            return resetAfter > 0 ? NowUnixMs() + (ULONGLONG)(resetAfter * 1000) : 0ULL;
        };

        // A window's usage (pct -1 without one); its length is `defaultMs` unless reported.
        auto readWindow = [&](JsonObject const& window, ULONGLONG defaultMs) {
            WindowUsage wu;
            if (!window) return wu;
            double pct = GetNum(window, L"used_percent");
            if (pct < 0) {
                pct = GetNum(window, L"percent_left");
                if (pct >= 0) pct = 100 - pct;
            }
            if (pct < 0) {
                pct = GetNum(window, L"remaining_percent");
                if (pct >= 0) pct = 100 - pct;
            }
            if (pct < 0) return wu;
            wu.pct = pct;
            wu.resetUnixMs = resetUnixMs(window);
            wu.windowDurationMs = defaultMs;
            double windowSeconds = GetNum(window, L"limit_window_seconds", 0);
            if (std::isfinite(windowSeconds) && windowSeconds > 0 &&
                windowSeconds <= 366.0 * 24 * 60 * 60) {
                wu.windowDurationMs = (ULONGLONG)(windowSeconds * 1000);
            }
            return wu;
        };
        auto applyWindow = [&](JsonObject const& window, WindowUsage* fallback) {
            WindowUsage* target = fallback;
            double windowSeconds = GetNum(window, L"limit_window_seconds", 0);
            if (windowSeconds == 5 * 60 * 60) target = &d->win5h;
            else if (windowSeconds == 7 * 24 * 60 * 60) target = &d->winWeek;
            WindowUsage wu =
                readWindow(window, target == &d->winWeek ? kWeeklyWindowMs : kFiveHourWindowMs);
            if (wu.pct >= 0) *target = wu;
        };

        applyWindow(GetObj(rl, L"primary_window"), &d->win5h);
        applyWindow(GetObj(rl, L"secondary_window"), &d->winWeek);
        applyWindow(GetObj(rl, L"primary"), &d->win5h);
        applyWindow(GetObj(rl, L"secondary"), &d->winWeek);
        applyWindow(GetObj(rl, L"five_hour"), &d->win5h);
        applyWindow(GetObj(rl, L"weekly"), &d->winWeek);
        applyWindow(GetObj(rl, L"five_hour_limit"), &d->win5h);
        applyWindow(GetObj(rl, L"weekly_limit"), &d->winWeek);

        d->plan = GetStr(usage, L"plan_type");
        d->note = AccountNote::None;
        // Each window of an additional_rate_limits entry (Codex Spark, hidden model lanes such as
        // gpt-reserve, ...) is an extra quota, shown with its Display setting; only the primary
        // windows drive bars.
        auto addLimit = [&](JsonObject const& item) {
            std::wstring name = GetStr(item, L"limit_name");
            auto itemRl = GetObj(item, L"rate_limit");
            AddExtraQuota(d, name, readWindow(GetObj(itemRl, L"primary_window"), kFiveHourWindowMs));
            AddExtraQuota(d, name, readWindow(GetObj(itemRl, L"secondary_window"), kWeeklyWindowMs));
        };
        if (usage.HasKey(L"additional_rate_limits")) {
            auto limits = usage.GetNamedValue(L"additional_rate_limits");
            if (limits.ValueType() == JsonValueType::Array) {
                auto arr = limits.GetArray();
                for (uint32_t i = 0; i < arr.Size(); i++) {
                    if (arr.GetAt(i).ValueType() == JsonValueType::Object) addLimit(arr.GetAt(i).GetObject());
                }
            } else if (limits.ValueType() == JsonValueType::Object) {
                addLimit(limits.GetObject());
            }
        }
        // Free resets: their count; FetchAccount reads the next one's expiry.
        auto resetCredits = GetObj(usage, L"rate_limit_reset_credits");
        if (!resetCredits) resetCredits = GetObj(root, L"rate_limit_reset_credits");
        double available = GetNum(resetCredits, L"available_count");
        if (std::isfinite(available) && available >= 0) d->freeResets = (int)std::min(available, 999.0);

        // credits: {has_credits, unlimited, balance: string|null}. The tooltip formats this
        // on the UI thread since the display depends on the account's credits max. The
        // balance is read even when has_credits is false so a depleted "0" still yields a
        // full bar instead of hiding it.
        if (auto cr = GetObj(usage, L"credits")) {
            d->hasCredits = GetBool(cr, L"has_credits");
            d->creditsUnlimited = GetBool(cr, L"unlimited");
            double balance = GetNum(cr, L"balance", -1);
            if (balance < 0) {
                // Only a fully numeric string counts; a formatted "$2.50" must not become 0.
                std::wstring s = GetStr(cr, L"balance");
                size_t first = s.find_first_not_of(L" \t");
                if (first != std::wstring::npos) {
                    wchar_t* end = nullptr;
                    balance = wcstod(s.c_str() + first, &end);
                    if (end == s.c_str() + first ||
                        s.find_first_not_of(L" \t", end - s.c_str()) != std::wstring::npos) {
                        balance = -1;
                    }
                }
            }
            if (std::isfinite(balance) && balance >= 0) d->creditsBalance = balance;
        }
        // A credits-only payload (no rate-limit windows) is still usable data, but say so in
        // the tooltip: the 5h/weekly bars auto-hide, and without this line a reshaped
        // rate_limit object would look identical to an account that simply has none.
        bool hasWindowUsage = d->win5h.pct >= 0 || d->winWeek.pct >= 0;
        bool parsed = hasWindowUsage || d->creditsBalance >= 0 || d->creditsUnlimited ||
                      d->hasCredits;
        if (parsed && !hasWindowUsage) d->note = AccountNote::NoRateLimits;
        if (!parsed && error) *error = UnexpectedResponseFormat(body);
        return parsed;
    } catch (...) {
        if (error) *error = UnexpectedResponseFormat(body);
        return false;
    }
}

// /wham/rate-limit-reset-credits: {available_count, credits[{id, reset_type, status, granted_at,
// expires_at, title, description}]}. The nearest future expiry of the available free resets, 0
// when none expires; nullopt for a malformed body.
static std::optional<ULONGLONG> ParseOpenAiResetsExpiry(const std::string& body) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        if (!root.HasKey(L"credits") ||
            root.GetNamedValue(L"credits").ValueType() != JsonValueType::Array) {
            return std::nullopt;
        }
        ULONGLONG nowMs = NowUnixMs();
        ULONGLONG soonestMs = 0;
        for (const auto& value : root.GetNamedArray(L"credits")) {
            if (value.ValueType() != JsonValueType::Object) continue;
            JsonObject credit = value.GetObject();
            if (GetStr(credit, L"status") != L"available") continue;
            ULONGLONG expiresMs = ParseIso8601Ms(GetStr(credit, L"expires_at"));
            if (expiresMs > nowMs && (!soonestMs || expiresMs < soonestMs)) soonestMs = expiresMs;
        }
        return soonestMs;
    } catch (...) {
        return std::nullopt;
    }
}

// OpenAI has no server-side credits ceiling, so a user-set max turns the prepaid balance
// into a used-percent bar in the extra-usage slot; thresholds, percent text, and
// notifications then apply unchanged. No reset window, so pace ticks stay hidden. Runs on
// fresh fetch results and again from PublishSettings, because settings changes do not
// re-poll and the bar must follow a new max immediately.
static void ApplyCreditsMax(const AccountConfig& acc, AccountData* d) {
    if (acc.provider != L"openai") return;
    d->extraUsage = {};
    d->extraUsedAmount = -1;
    d->extraLimitAmount = -1;
    if (acc.creditsMax <= 0 || d->creditsUnlimited || d->creditsBalance < 0) return;
    d->extraUsage.pct =
        std::clamp(100.0 * (1.0 - d->creditsBalance / acc.creditsMax), 0.0, 100.0);
    d->extraUsedAmount = acc.creditsMax - d->creditsBalance;
    d->extraLimitAmount = acc.creditsMax;
}
