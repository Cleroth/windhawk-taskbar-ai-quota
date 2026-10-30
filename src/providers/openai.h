// OpenAI usage response parsing and the user-set credits ceiling.

static constexpr ULONGLONG kUnixTimestampMsThreshold = 100000000000ULL;

static bool ParseOpenAiUsage(const std::string& body, AccountData* d, std::wstring* error) {
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

        auto applyWindow = [&](JsonObject const& window, WindowUsage* fallback) {
            if (!window) return;
            WindowUsage* target = fallback;
            double windowSeconds = GetNum(window, L"limit_window_seconds", 0);
            if (windowSeconds == 5 * 60 * 60) target = &d->win5h;
            else if (windowSeconds == 7 * 24 * 60 * 60) target = &d->winWeek;

            double pct = GetNum(window, L"used_percent");
            if (pct < 0) {
                pct = GetNum(window, L"percent_left");
                if (pct >= 0) pct = 100 - pct;
            }
            if (pct < 0) {
                pct = GetNum(window, L"remaining_percent");
                if (pct >= 0) pct = 100 - pct;
            }
            if (pct < 0) return;
            target->pct = pct;
            target->resetUnixMs = resetUnixMs(window);
            target->windowDurationMs = target == &d->winWeek ? kWeeklyWindowMs : kFiveHourWindowMs;
            if (std::isfinite(windowSeconds) && windowSeconds > 0 &&
                windowSeconds <= 366.0 * 24 * 60 * 60) {
                target->windowDurationMs = (ULONGLONG)(windowSeconds * 1000);
            }
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
        d->openAiExtraLimitLines.clear();
        d->extraLines.clear();
        // Every additional_rate_limits entry (Codex Spark, hidden model lanes such as
        // gpt-reserve, ...) is an opt-in tooltip line; only the primary windows drive bars.
        int extraLimitLineCount = 0;
        auto addLimitLine = [&](JsonObject const& item) {
            auto itemRl = GetObj(item, L"rate_limit");
            auto pw = GetObj(itemRl, L"primary_window");
            auto sw = GetObj(itemRl, L"secondary_window");
            std::wstring name = GetStr(item, L"limit_name");
            if (name.empty() || (!pw && !sw) || extraLimitLineCount >= 6) return;

            wchar_t line[128];
            swprintf(line, ARRAYSIZE(line), L"%s: 5h %.0f%% | wk %.0f%%",
                     name.c_str(), GetNum(pw, L"used_percent", 0),
                     GetNum(sw, L"used_percent", 0));
            if (!d->openAiExtraLimitLines.empty()) d->openAiExtraLimitLines += L"\n";
            d->openAiExtraLimitLines += line;
            extraLimitLineCount++;
        };
        if (usage.HasKey(L"additional_rate_limits")) {
            auto limits = usage.GetNamedValue(L"additional_rate_limits");
            if (limits.ValueType() == JsonValueType::Array) {
                auto arr = limits.GetArray();
                for (uint32_t i = 0; i < arr.Size(); i++) {
                    if (arr.GetAt(i).ValueType() == JsonValueType::Object) addLimitLine(arr.GetAt(i).GetObject());
                }
            } else if (limits.ValueType() == JsonValueType::Object) {
                addLimitLine(limits.GetObject());
            }
        }

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
        if (parsed && !hasWindowUsage) d->extraLines = L"rate limits: none reported";
        if (!parsed && error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return parsed;
    } catch (...) {
        if (error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return false;
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
