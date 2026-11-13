// Anthropic usage and profile response parsing, and the user agent of those requests.

// Claude Code's user agent: the usage response reports free resets (cedar_ember) only to a recent
// Claude Code CLI. Keep it at a recent Claude Code release.
static constexpr PCWSTR kClaudeCliUserAgent = L"claude-cli/2.1.323 (external, cli)";  // i18n-en

static bool ParseAnthropicUsage(const std::string& body, AccountData* d, AccountError* error) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        JsonObject usage = root;
        if (!GetObj(usage, L"five_hour") && !GetObj(usage, L"seven_day")) {
            if (auto wrapped = GetObj(root, L"usage")) usage = wrapped;
            else if (auto wrapped = GetObj(root, L"data")) usage = wrapped;
        }

        // A window that hasn't started reports no usage and an explicitly null reset; a missing or
        // malformed reset is only unknown.
        auto readWindow = [](JsonObject const& window, double pct, ULONGLONG durationMs) {
            WindowUsage wu;
            wu.pct = pct;
            wu.resetUnixMs = ParseIso8601Ms(GetStr(window, L"resets_at"));
            wu.windowDurationMs = durationMs;
            wu.notYetStarted = pct == 0 && window.HasKey(L"resets_at") &&
                               window.GetNamedValue(L"resets_at").ValueType() == JsonValueType::Null;
            return wu;
        };
        // A model family's token, also in versioned names such as Fable 5.1, never inside a word.
        auto hasFamily = [](std::wstring name, std::wstring_view family) {
            std::transform(name.begin(), name.end(), name.begin(),
                           [](wchar_t ch) { return (wchar_t)towlower(ch); });
            for (size_t pos = name.find(family); pos != std::wstring::npos;
                 pos = name.find(family, pos + 1)) {
                if ((pos == 0 || !iswalpha(name[pos - 1])) &&
                    (pos + family.size() == name.size() || !iswalpha(name[pos + family.size()]))) {
                    return true;
                }
            }
            return false;
        };

        if (auto fh = GetObj(usage, L"five_hour")) {
            d->win5h = readWindow(fh, GetNum(fh, L"utilization"), kFiveHourWindowMs);
        }
        if (auto sd = GetObj(usage, L"seven_day")) {
            d->winWeek = readWindow(sd, GetNum(sd, L"utilization"), kWeeklyWindowMs);
        }
        if (auto fable = GetObj(usage, L"seven_day_fable")) {
            double utilization = GetNum(fable, L"utilization");
            if (std::isfinite(utilization) && utilization >= 0) {
                d->fableWeek = readWindow(fable, utilization, kWeeklyWindowMs);
            }
        }

        // limits[]: the main windows (kinds session and weekly_all) and per-model and per-surface
        // allowances. Without seven_day_fable, Fable's weekly allowance is its quota, the active one
        // first, then the most used; every other allowance is an extra quota.
        bool opusListed = false;
        bool sonnetListed = false;
        if (usage.HasKey(L"limits") &&
            usage.GetNamedValue(L"limits").ValueType() == JsonValueType::Array) {
            bool reportedFable = d->fableWeek.pct >= 0;
            bool selectedActive = false;
            for (const auto& value : usage.GetNamedArray(L"limits")) {
                if (value.ValueType() != JsonValueType::Object) continue;
                JsonObject limit = value.GetObject();
                double percent = GetNum(limit, L"percent");
                if (!std::isfinite(percent) || percent < 0) continue;
                std::wstring kind = GetStr(limit, L"kind");
                std::wstring group = GetStr(limit, L"group");
                bool weekly = kind == L"weekly_scoped" || group == L"weekly";

                auto scope = GetObj(limit, L"scope");
                std::wstring modelName = GetStr(GetObj(scope, L"model"), L"display_name");
                if (hasFamily(modelName, L"fable")) {
                    if (!weekly || reportedFable) continue;
                    bool active = GetBool(limit, L"is_active");
                    if (d->fableWeek.pct >= 0 && !active && selectedActive) continue;
                    if (d->fableWeek.pct >= 0 && active == selectedActive &&
                        percent <= d->fableWeek.pct) {
                        continue;
                    }
                    d->fableWeek = readWindow(limit, percent, kWeeklyWindowMs);
                    selectedActive = active;
                    continue;
                }
                if (kind == L"session" || kind == L"weekly_all") continue;

                std::wstring name = !modelName.empty() ? modelName :
                                    GetStr(GetObj(scope, L"surface"), L"display_name");
                ULONGLONG durationMs = weekly || kind.starts_with(L"weekly") ? kWeeklyWindowMs :
                                       group == L"session" || kind.starts_with(L"session") ?
                                           kFiveHourWindowMs : 0;
                opusListed |= hasFamily(modelName, L"opus");
                sonnetListed |= hasFamily(modelName, L"sonnet");
                AddExtraQuota(d, name, readWindow(limit, percent, durationMs));
            }
        }
        // Responses without those model allowances report the Opus and Sonnet weeklies here.
        for (auto [key, name, listed] : {std::tuple{L"seven_day_opus", L"Opus", opusListed},  // i18n-en
                                         std::tuple{L"seven_day_sonnet", L"Sonnet", sonnetListed}}) {  // i18n-en
            auto legacy = GetObj(usage, key);
            double utilization = GetNum(legacy, L"utilization");
            if (listed || !legacy || !std::isfinite(utilization) || utilization < 0) continue;
            AddExtraQuota(d, name, readWindow(legacy, utilization, kWeeklyWindowMs));
        }

        d->plan.clear();
        d->note = AccountNote::None;
        if (auto eu = GetObj(usage, L"extra_usage"); eu && GetBool(eu, L"is_enabled")) {
            // monthly_limit/used_credits are cents; an explicit null limit means unlimited (an
            // absent or malformed key must not claim that). utilization is null until the
            // first spend of the cycle, so gate the bar on the limit instead and treat the
            // missing value as 0% or the bar would vanish every month start. Either of
            // utilization/used_credits derives the other so the bar and amount text agree.
            double limitCents = GetNum(eu, L"monthly_limit");
            double usedCents = GetNum(eu, L"used_credits");
            double utilization = GetNum(eu, L"utilization");
            bool limitIsNull = eu.HasKey(L"monthly_limit") &&
                               eu.GetNamedValue(L"monthly_limit").ValueType() == JsonValueType::Null;
            // The reset the response names, back to the same day of the month before; without one,
            // extra usage runs over the calendar month (UTC).
            d->extraUsage.resetUnixMs = ParseIso8601Ms(GetStr(eu, L"resets_at"));
            ULONGLONG cycleMs = MonthBeforeMs(d->extraUsage.resetUnixMs);
            if (!d->extraUsage.resetUnixMs) {
                const UtcMonth month = UtcMonthOf(NowUnixMs());
                d->extraUsage.resetUnixMs = month.endMs;
                d->extraUsage.calendarMonth = true;
                cycleMs = month.durationMs;
            }
            if (limitIsNull) {
                // Reported spending is usable history even without a percentage ceiling.
                if (std::isfinite(usedCents) && usedCents >= 0) {
                    d->extraUsedAmount = usedCents / 100.0;
                }
                d->note = AccountNote::ExtraUsageUnlimited;
            } else if (limitCents > 0) {
                if (!std::isfinite(utilization) || utilization < 0) {
                    utilization = usedCents > 0 ? usedCents * 100.0 / limitCents : 0;
                } else if (usedCents < 0) {
                    usedCents = utilization * limitCents / 100.0;
                }
                d->extraUsage.pct = utilization;
                d->extraUsedAmount = std::max(usedCents, 0.0) / 100.0;
                d->extraLimitAmount = limitCents / 100.0;
                d->extraUsage.windowDurationMs = cycleMs;
            }
        }

        // Free resets, reported with ?cedar_ember=1: {eligible, grants[{id, resets_left, ends_at,
        // paused, usable_now, gives_back_weekly, type, clears[]}], next_grant_id}. As Claude Code
        // counts them: the resets left of the live grants (unpaused, usable now, not ended), none
        // unless the next grant is live. And as it names their kinds: a grant that gives back to
        // the week is a Fable reset; another has its type (session, weekly, full) or, without
        // one, what it clears (five_hour alone, weekly limits alone, else everything).
        auto cedarEmber = GetObj(usage, L"cedar_ember");
        if (!cedarEmber) cedarEmber = GetObj(root, L"cedar_ember");
        if (cedarEmber && GetBool(cedarEmber, L"eligible")) {
            std::wstring nextId = GetStr(cedarEmber, L"next_grant_id");
            ULONGLONG nowMs = NowUnixMs();
            double count = 0;
            bool nextLive = false;
            ULONGLONG nextEndsMs = 0;
            decltype(d->freeResetKinds) kinds{};
            if (cedarEmber.HasKey(L"grants") &&
                cedarEmber.GetNamedValue(L"grants").ValueType() == JsonValueType::Array) {
                for (const auto& value : cedarEmber.GetNamedArray(L"grants")) {
                    if (value.ValueType() != JsonValueType::Object) continue;
                    JsonObject grant = value.GetObject();
                    std::wstring id = GetStr(grant, L"id");
                    double left = GetNum(grant, L"resets_left");
                    if (id.empty() || !std::isfinite(left) || left < 0 || left != std::floor(left) ||
                        GetBool(grant, L"paused") || !GetBool(grant, L"usable_now")) {
                        continue;
                    }
                    ULONGLONG endsMs = 0;
                    if (grant.HasKey(L"ends_at") &&
                        grant.GetNamedValue(L"ends_at").ValueType() != JsonValueType::Null) {
                        endsMs = ParseIso8601Ms(GetStr(grant, L"ends_at"));
                        if (endsMs <= nowMs) continue;
                    }
                    count += left;
                    if (id == nextId) {
                        nextLive = true;
                        nextEndsMs = endsMs;
                    }

                    std::wstring type = GetStr(grant, L"type");
                    bool fiveHour = type == L"session";
                    bool week = type == L"weekly";
                    if (!fiveHour && !week && type != L"full" && grant.HasKey(L"clears") &&
                        grant.GetNamedValue(L"clears").ValueType() == JsonValueType::Array) {
                        for (const auto& limit : grant.GetNamedArray(L"clears")) {
                            if (limit.ValueType() != JsonValueType::String) continue;
                            auto name = limit.GetString();
                            fiveHour |= name == L"five_hour";
                            week |= std::wstring_view(name).starts_with(L"seven_day");
                        }
                    }
                    auto& kind = kinds[GetBool(grant, L"gives_back_weekly") ? kFableReset :
                                       fiveHour == week                     ? kFullReset :
                                       fiveHour                             ? kFiveHourReset :
                                                                              kWeeklyReset];
                    kind.count = (int)std::min(kind.count + left, 999.0);
                    if (left > 0 && endsMs && (!kind.expiryMs || endsMs < kind.expiryMs)) kind.expiryMs = endsMs;
                }
            }
            d->freeResets = nextLive ? (int)std::min(count, 999.0) : 0;
            d->freeResetsExpiryMs = nextLive ? nextEndsMs : 0;
            if (nextLive) d->freeResetKinds = kinds;
        }

        bool parsed = d->win5h.pct >= 0 || d->winWeek.pct >= 0 || d->fableWeek.pct >= 0 ||
                      d->extraUsage.pct >= 0 || d->extraUsedAmount >= 0;
        if (!parsed && error) *error = UnexpectedResponseFormat(body);
        return parsed;
    } catch (...) {
        if (error) *error = UnexpectedResponseFormat(body);
        return false;
    }
}

// The OAuth profile's plan: organization.organization_type ("claude_pro", "claude_max",
// "claude_team", ...) as "Pro", "Max", "Team", .... Anthropic sells the Max type's 20x
// rate_limit_tier ("default_claude_max_20x") as Ultra. Empty for a profile without one; nullopt
// for a malformed body.
static std::optional<std::wstring> ParseAnthropicPlan(const std::string& body) {
    try {
        auto organization = GetObj(JsonObject::Parse(Utf8ToWide(body)), L"organization");
        std::wstring type = GetStr(organization, L"organization_type");
        if (type.starts_with(L"claude_")) type.erase(0, 7);
        std::wstring plan;
        bool wordStart = true;
        for (wchar_t ch : type) {
            plan += ch == L'_' ? L' ' : wordStart ? (wchar_t)towupper(ch) : ch;
            wordStart = ch == L'_';
        }
        if (plan == L"Max" && GetStr(organization, L"rate_limit_tier").find(L"20x") != std::wstring::npos)  // i18n-en
            plan = L"Ultra";  // i18n-en
        return plan;
    } catch (...) {
        return std::nullopt;
    }
}
