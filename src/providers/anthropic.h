// Anthropic usage response parsing.

static bool ParseAnthropicUsage(const std::string& body, AccountData* d, std::wstring* error) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        JsonObject usage = root;
        if (!GetObj(usage, L"five_hour") && !GetObj(usage, L"seven_day")) {
            if (auto wrapped = GetObj(root, L"usage")) usage = wrapped;
            else if (auto wrapped = GetObj(root, L"data")) usage = wrapped;
        }

        if (auto fh = GetObj(usage, L"five_hour")) {
            d->win5h.pct = GetNum(fh, L"utilization");
            d->win5h.resetUnixMs = ParseIso8601Ms(GetStr(fh, L"resets_at"));
            d->win5h.windowDurationMs = kFiveHourWindowMs;
        }
        if (auto sd = GetObj(usage, L"seven_day")) {
            d->winWeek.pct = GetNum(sd, L"utilization");
            d->winWeek.resetUnixMs = ParseIso8601Ms(GetStr(sd, L"resets_at"));
            d->winWeek.windowDurationMs = kWeeklyWindowMs;
        }
        if (auto fable = GetObj(usage, L"seven_day_fable")) {
            double utilization = GetNum(fable, L"utilization");
            if (std::isfinite(utilization) && utilization >= 0) {
                d->fableWeek.pct = utilization;
                d->fableWeek.resetUnixMs = ParseIso8601Ms(GetStr(fable, L"resets_at"));
                d->fableWeek.windowDurationMs = kWeeklyWindowMs;
            }
        }
        if (d->fableWeek.pct < 0 && usage.HasKey(L"limits")) {
            auto limits = usage.GetNamedValue(L"limits");
            if (limits.ValueType() == JsonValueType::Array) {
                bool selectedActive = false;
                for (const auto& value : limits.GetArray()) {
                    if (value.ValueType() != JsonValueType::Object) continue;
                    JsonObject limit = value.GetObject();
                    if (GetStr(limit, L"kind") != L"weekly_scoped" &&
                        GetStr(limit, L"group") != L"weekly") {
                        continue;
                    }

                    auto scope = GetObj(limit, L"scope");
                    auto model = GetObj(scope, L"model");
                    std::wstring modelName = GetStr(model, L"display_name");
                    // Match the model family token so versioned names such as Fable 5.1 keep working.
                    std::transform(modelName.begin(), modelName.end(), modelName.begin(),
                                   [](wchar_t ch) { return (wchar_t)towlower(ch); });
                    size_t fablePos = modelName.find(L"fable");
                    bool isFable = fablePos != std::wstring::npos &&
                                    (fablePos == 0 || !iswalpha(modelName[fablePos - 1])) &&
                                    (fablePos + 5 == modelName.size() ||
                                     !iswalpha(modelName[fablePos + 5]));
                    if (!isFable) continue;

                    double percent = GetNum(limit, L"percent");
                    if (!std::isfinite(percent) || percent < 0) continue;
                    bool active = GetBool(limit, L"is_active");
                    if (d->fableWeek.pct >= 0 && !active && selectedActive) continue;
                    if (d->fableWeek.pct >= 0 && active == selectedActive &&
                        percent <= d->fableWeek.pct) {
                        continue;
                    }
                    d->fableWeek.pct = percent;
                    d->fableWeek.resetUnixMs = ParseIso8601Ms(GetStr(limit, L"resets_at"));
                    d->fableWeek.windowDurationMs = kWeeklyWindowMs;
                    selectedActive = active;
                }
            }
        }

        d->plan.clear();
        d->extraLines.clear();
        if (auto op = GetObj(usage, L"seven_day_opus"); op && GetNum(op, L"utilization") >= 0) {
            wchar_t line[64];
            swprintf(line, ARRAYSIZE(line), L"opus week: %.0f%%", GetNum(op, L"utilization"));
            d->extraLines += line;
        }
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
            if (limitIsNull) {
                if (!d->extraLines.empty()) d->extraLines += L"\n";
                d->extraLines += L"extra usage: unlimited";
            } else if (limitCents > 0) {
                if (!std::isfinite(utilization) || utilization < 0) {
                    utilization = usedCents > 0 ? usedCents * 100.0 / limitCents : 0;
                } else if (usedCents < 0) {
                    usedCents = utilization * limitCents / 100.0;
                }
                d->extraUsage.pct = utilization;
                d->extraUsedAmount = std::max(usedCents, 0.0) / 100.0;
                d->extraLimitAmount = limitCents / 100.0;
                d->extraUsage.resetUnixMs = ParseIso8601Ms(GetStr(eu, L"resets_at"));
                if (d->extraUsage.resetUnixMs) {
                    // The API omits the cycle start. Derive the previous monthly billing
                    // boundary from its authoritative next-reset timestamp.
                    ULONGLONG resetFileTime =
                        (d->extraUsage.resetUnixMs + kFileTimeUnixEpochOffsetMs) * 10000;
                    FILETIME resetFt{(DWORD)resetFileTime, (DWORD)(resetFileTime >> 32)};
                    SYSTEMTIME start{};
                    if (FileTimeToSystemTime(&resetFt, &start)) {
                        if (start.wMonth == 1) {
                            start.wMonth = 12;
                            start.wYear--;
                        } else {
                            start.wMonth--;
                        }
                        static constexpr int kDaysPerMonth[] = {
                            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                        int days = kDaysPerMonth[start.wMonth - 1];
                        if (start.wMonth == 2 &&
                            (start.wYear % 4 == 0 &&
                             (start.wYear % 100 != 0 || start.wYear % 400 == 0))) {
                            days = 29;
                        }
                        start.wDay = (WORD)std::min<int>(start.wDay, days);
                        FILETIME startFt;
                        if (SystemTimeToFileTime(&start, &startFt)) {
                            ULONGLONG startFileTime =
                                ((ULONGLONG)startFt.dwHighDateTime << 32) |
                                startFt.dwLowDateTime;
                            ULONGLONG startUnixMs =
                                startFileTime / 10000 - kFileTimeUnixEpochOffsetMs;
                            if (startUnixMs < d->extraUsage.resetUnixMs) {
                                d->extraUsage.windowDurationMs =
                                    d->extraUsage.resetUnixMs - startUnixMs;
                            }
                        }
                    }
                }
            }
        }
        bool parsed = d->win5h.pct >= 0 || d->winWeek.pct >= 0 || d->fableWeek.pct >= 0 ||
                      d->extraUsage.pct >= 0;
        if (!parsed && error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return parsed;
    } catch (...) {
        if (error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return false;
    }
}
