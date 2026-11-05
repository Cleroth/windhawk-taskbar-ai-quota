// OpenCode Go usage response parsing: its 5-hour, weekly and monthly windows.

// The usage object's rolling (5-hour), weekly and monthly windows, each a used percentage, its
// reset and its status, "rate-limited" once the limit is reached. The rolling window runs 5 hours
// from its first use (unused, its reset rolls a full window ahead), the weekly a calendar week and
// the monthly a month from the subscription's day; monthly goes in the extra-usage slot. Its
// start is estimated as the calendar month before its reset: up to three days early when the
// reset falls in a month without the subscription's day (the 29th to 31st). A window the
// response lacks isn't shown; one that isn't an object or lacks a valid percentage is an error,
// never 0 or 100%.
static bool ParseOpenCodeUsage(const std::string& body, AccountData* d, std::wstring* error) {
    auto fail = [&] {
        if (error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return false;
    };
    try {
        auto usage = GetObj(JsonObject::Parse(Utf8ToWide(body)), L"usage");
        const struct {
            PCWSTR name;
            int slot;
            WindowUsage* usage;
        } windows[] = {
            {L"rolling", kFiveHourBar, &d->win5h},
            {L"weekly", kWeeklyBar, &d->winWeek},
            {L"monthly", kExtraUsageBar, &d->extraUsage},
        };
        bool any = false;
        for (const auto& window : windows) {
            if (!usage || !usage.HasKey(window.name)) continue;
            auto o = GetObj(usage, window.name);
            double pct = GetNum(o, L"percent", NAN);
            if (!o || !(pct >= 0 && pct <= 1e6)) return fail();
            WindowUsage& wu = *window.usage;
            wu.pct = pct;
            wu.resetUnixMs = ParseIso8601Ms(GetStr(o, L"resetsAt"));
            wu.windowDurationMs = window.slot == kFiveHourBar ? kFiveHourWindowMs :
                                  window.slot == kWeeklyBar ? kWeeklyWindowMs : MonthBeforeMs(wu.resetUnixMs);
            d->opencodeLimited[window.slot] = GetStr(o, L"status") == L"rate-limited";
            any = true;
        }
        return any ? true : fail();
    } catch (...) {
        return fail();
    }
}
