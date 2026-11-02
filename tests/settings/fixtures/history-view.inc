// The history window's view (history/view.h): compact formats in synthetic time zones, and
// summary cards built through the shared model (history/series.h) from synthetic readings.
// Hand-derived expectations.

static constexpr ULONGLONG kUnixEpochMs = 11644473600000ULL;  // 1601 to 1970
static constexpr ULONGLONG kHourMs = 3600000;

// Unix ms of a wall-clock time in a zone `offset` minutes east of UTC.
static ULONGLONG TestLocalMs(WORD year, WORD month, WORD day, WORD hour, WORD minute, int offset = 480) {
    SYSTEMTIME t{year, month, 0, day, hour, minute, 0, 0};
    FILETIME file{};
    CHECK(SystemTimeToFileTime(&t, &file));
    ULONGLONG ms = (ULONGLONG(file.dwHighDateTime) << 32 | file.dwLowDateTime) / 10000 - kUnixEpochMs;
    return ULONGLONG(int64_t(ms) - int64_t(offset) * 60000);
}

// A fixed-offset zone as the window's local time; the weekday it fills is deliberately wrong.
static HistoryUiLocalTime TestZone(int offset = 480) {
    return [offset](ULONGLONG ms) {
        ULONGLONG ticks = ULONGLONG(int64_t(ms + kUnixEpochMs) + int64_t(offset) * 60000) * 10000;
        FILETIME file{DWORD(ticks), DWORD(ticks >> 32)};
        SYSTEMTIME t{};
        FileTimeToSystemTime(&file, &t);
        t.wDayOfWeek = 9;
        return t;
    };
}

// The approved mockups' week: Sat 24 Oct 14:32 to Sat 31 Oct 14:32, UTC+8.
static const ULONGLONG kWeekFrom = TestLocalMs(2026, 10, 24, 14, 32);
static const ULONGLONG kWeekTo = kWeekFrom + kHistoryWeekMs;

// A successful hourly reading of the given meters (the others missing).
static HistorySample TestReading(uint64_t account, uint32_t provider, ULONGLONG time,
                                 std::initializer_list<std::pair<int, double>> values) {
    HistorySample sample;
    sample.accountId = account;
    sample.provider = provider;
    sample.timeMs = time;
    sample.intervalMs = kHourMs;
    sample.session = 1;
    for (auto [meter, value] : values) sample.meters[size_t(meter)].value = value;
    return sample;
}

static AccountConfig TestHistoryAccount(std::wstring provider, std::wstring label, uint64_t historyId) {
    AccountConfig account = Account(std::move(provider), std::move(label));
    account.historyId = historyId;
    return account;
}

// What the window's controller does for a load, on synthetic inputs: collects the accounts over
// the range (and the previous week while comparing), lists them in the view as the window shows
// them (slots in order, a removed one labelled by its journal ID, live state from `live`), builds
// the series and their cards at kWeekTo.
struct TestHistory {
    std::vector<AccountConfig> configured;
    std::vector<HistorySample> samples;
    HistoryView view = HistoryView::Weekly;
    int selected = -1;
    bool compare = false, enabled = true, skipHidden = true;
};

static HistoryUiView TestCards(const TestHistory& h,
                               const std::function<void(HistoryUiView&)>& live = {}) {
    auto never = [] { return false; };
    ULONGLONG readFrom = h.compare ? kWeekFrom - kHistoryWeekMs : kWeekFrom;
    std::vector<HistoryUiAccount> collected = HistoryCollectAccounts(h.configured, h.samples, readFrom, kWeekTo, never);
    HistoryUiView view;
    view.selected = h.selected;
    view.view = h.view;
    view.compare = h.compare;
    view.recordingEnabled = h.enabled;
    view.loading = false;
    for (size_t i = 0; i < collected.size(); i++) {
        const HistoryUiAccount& a = collected[i];
        HistoryUiViewAccount account;
        account.historyId = a.id;
        account.slot = uint32_t(i);
        account.provider = a.provider;
        account.label = a.label;
        account.removed = !a.configured;
        account.recordingOff = a.configured && !a.recording;
        account.paused = a.hidden && h.skipHidden;
        view.accounts.push_back(std::move(account));
    }
    if (live) live(view);
    HistorySeriesQuery query{h.selected, h.view, kWeekFrom, kWeekTo, readFrom, h.compare, 90};
    HistorySeriesSet set = HistoryBuildSeries(h.samples, collected, query, never);
    HistoryUiSetCards(view, set, kWeekFrom, kWeekTo, kWeekTo, TestZone());
    return view;
}

// CC 2 (Anthropic, history ID 0xC2) over the week: weekly 40%, 95% and 93% at hours 1-3 (Fable
// weekly 10, 20, 30), its latest poll recorded at hour 3.
static TestHistory TestOneAccount() {
    TestHistory h;
    h.configured = {TestHistoryAccount(L"anthropic", L"CC 2", 0xC2)};
    double weekly[] = {40, 95, 93}, fable[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) {
        h.samples.push_back(TestReading(0xC2, 1, kWeekFrom + ULONGLONG(i + 1) * kHourMs,
                                        {{1, weekly[i]}, {2, fable[i]}}));
    }
    h.selected = 0;
    return h;
}

static std::function<void(HistoryUiView&)> TestPolled(ULONGLONG lastPollMs, bool stale = false) {
    return [=](HistoryUiView& view) {
        for (HistoryUiViewAccount& account : view.accounts) {
            account.lastPollMs = lastPollMs;
            account.stale = stale;
        }
    };
}

static void RunHistoryViewTests() {
    Run("history formats: compact local times, the weekday from the date; outside the current year "
        "the year in the weekday's place; any zone", [] {
        HistoryUiLocalTime zone = TestZone();
        ULONGLONG now = TestLocalMs(2026, 10, 31, 14, 32);
        CHECK(HistoryUiTime(TestLocalMs(2026, 10, 31, 14, 31), now, zone) == L"Sat 31 Oct 14:31");
        CHECK(HistoryUiTime(TestLocalMs(2026, 10, 26, 11, 31), now, zone) == L"Mon 26 Oct 11:31");
        CHECK(HistoryUiTime(TestLocalMs(2026, 11, 1, 0, 5), now, zone) == L"Sun 1 Nov 00:05");
        CHECK(HistoryUiTime(TestLocalMs(2026, 1, 1, 0, 0), now, zone) == L"Thu 1 Jan 00:00");
        CHECK(HistoryUiTime(TestLocalMs(2025, 12, 17, 9, 12), now, zone) == L"17 Dec 2025 09:12");
        CHECK(HistoryUiTime(TestLocalMs(2028, 2, 29, 8, 8), now, zone) == L"29 Feb 2028 08:08");
        // One instant in three zones: the local day and the current year are the zone's.
        ULONGLONG instant = TestLocalMs(2026, 10, 31, 23, 30, 0);
        CHECK(HistoryUiTime(instant, now, TestZone(13 * 60)) == L"Sun 1 Nov 12:30");
        CHECK(HistoryUiTime(instant, now, TestZone(-5 * 60)) == L"Sat 31 Oct 18:30");
        // Across the year boundary: ten minutes apart, the earlier one in the previous year.
        ULONGLONG newYear = TestLocalMs(2027, 1, 1, 0, 10);
        CHECK(HistoryUiTime(TestLocalMs(2026, 12, 31, 23, 50), newYear, zone) == L"31 Dec 2026 23:50");
        CHECK(HistoryUiTime(TestLocalMs(2027, 1, 1, 0, 5), newYear, zone) == L"Fri 1 Jan 00:05");
        // The same instants a minute before midnight on 31 December: both in the current year.
        ULONGLONG yearEnd = TestLocalMs(2026, 12, 31, 23, 59);
        CHECK(HistoryUiTime(TestLocalMs(2026, 12, 31, 23, 50), yearEnd, zone) == L"Thu 31 Dec 23:50");
        CHECK(HistoryUiTime(TestLocalMs(2027, 1, 1, 0, 5), yearEnd, zone) == L"1 Jan 2027 00:05");
        // The year is the zone's: at 16:30 UTC on 31 December UTC+8 is already in 2027, so 15:50
        // UTC (23:50 there) is in its previous year.
        ULONGLONG utc = TestLocalMs(2026, 12, 31, 16, 30, 0);
        CHECK(HistoryUiTime(utc - 40 * kMinute, utc, TestZone(8 * 60)) == L"31 Dec 2026 23:50");
        CHECK(HistoryUiTime(utc - 40 * kMinute, utc, TestZone(0)) == L"Thu 31 Dec 15:50");
        CHECK(HistoryUiTime(1, now, [](ULONGLONG) { return SYSTEMTIME{}; }) == L"\u2014");
    });

    Run("history formats: numbers rounded, abbreviated from 10,000 with carry; units; hours", [] {
        CHECK(HistoryUiNumber(9999.99, 2) == L"9999.99");
        CHECK(HistoryUiNumber(9999.996, 2) == L"10.0K");
        CHECK(HistoryUiNumber(10000, 2) == L"10.0K");
        CHECK(HistoryUiNumber(12345, 2) == L"12.3K");
        CHECK(HistoryUiNumber(999949.99, 2) == L"999.9K");
        CHECK(HistoryUiNumber(999950, 2) == L"1.0M");
        CHECK(HistoryUiNumber(1.2e6, 2) == L"1.2M");
        CHECK(HistoryUiNumber(123.4e9, 2) == L"123.4G");
        CHECK(HistoryUiNumber(1e12, 2) == L"1.0T");
        CHECK(HistoryUiNumber(-12345, 2) == L"-12.3K");
        CHECK(HistoryUiNumber(0, 2) == L"0.00");
        CHECK(HistoryUiNumber(62.4, 0) == L"62");
        CHECK(HistoryUiNumber(112.4, 0) == L"112");
        CHECK(HistoryUiNumber(9999.4, 0) == L"9999");
        CHECK(HistoryUiNumber(9999.6, 0) == L"10.0K");
        CHECK(HistoryUiNumber(98.81, 1) == L"98.8");
        CHECK(HistoryUiNumber(std::nan(""), 1) == L"\u2014");
        CHECK(HistoryUiValue(1, 93.04, 0) == L"93%");
        CHECK(HistoryUiValue(5, 38.04, 1) == L"38.0%");
        CHECK(HistoryUiValue(1, 19.999999999999996, 1) == L"20.0%");
        CHECK(HistoryUiValue(6, 10, 0) == L"USD 10.00");
        CHECK(HistoryUiValue(6, 9999.996, 0) == L"USD 10.0K");
        CHECK(HistoryUiValue(7, 1240, 0) == L"1240.00 credits");
        CHECK(HistoryUiValue(7, 25000, 0) == L"25.0K credits");
        CHECK(HistoryUiHours(166 * double(kHourMs)) == L"166 h");
        CHECK(HistoryUiHours(0.4 * double(kHourMs)) == L"0.4 h");
        CHECK(HistoryUiHours(23.2 * double(kHourMs)) == L"23.2 h");
        CHECK(HistoryUiHours(165.96 * double(kHourMs)) == L"166 h");
        CHECK(HistoryUiHours(0.04 * double(kHourMs)) == L"0 h");
        CHECK(HistoryUiHours(0) == L"0 h");
        CHECK(HistoryUiHours(87600 * double(kHourMs)) == L"87600 h");
    });

    Run("history cards: one account's headline, peak, coverage and time at the red threshold; "
        "\"now\" only for its latest fresh poll", [] {
        ULONGLONG recorded = kWeekFrom + 3 * kHourMs;
        HistoryUiView view = TestCards(TestOneAccount(), TestPolled(recorded - 5));
        CHECK(view.cards.size() == 2 && view.noReadings.empty());
        const HistoryUiCard& weekly = view.cards[0];
        CHECK(weekly.title == L"Weekly" && weekly.status.empty() && !weekly.detailsOnly);
        CHECK(weekly.color == kHistoryColors[1]);
        CHECK(weekly.value == L"93%" && weekly.when == L"now");
        // 40 to 95 crosses 90 a eleventh into its hour; 95 to 93 stays above.
        CHECK((weekly.lines == std::vector<std::wstring>{L"Peak 95% \u00B7 Sat 24 Oct 16:32",
                                                          L"Coverage 2 h (1.2%)", L"Est. \u226590%: 1.1 h"}));
        const HistoryUiCard& fable = view.cards[1];
        CHECK(fable.title == L"Fable weekly" && fable.color == kHistoryColors[2] && fable.value == L"30%");
        CHECK((fable.lines == std::vector<std::wstring>{L"Peak 30% \u00B7 Sat 24 Oct 17:32",
                                                         L"Coverage 2 h (1.2%)", L"Est. \u226590%: 0 h"}));
        // A stale poll, a later unrecorded poll and no poll show the reading's time.
        for (auto live : {TestPolled(recorded - 5, true), TestPolled(recorded + kHourMs), TestPolled(0)}) {
            CHECK(TestCards(TestOneAccount(), live).cards[0].when == L"Sat 24 Oct 17:32");
        }
        // A lone reading has no coverage: its time at the threshold is unknown.
        TestHistory lone = TestOneAccount();
        lone.samples.resize(1);
        CHECK((TestCards(lone).cards[0].lines ==
               std::vector<std::wstring>{L"Peak 40% \u00B7 Sat 24 Oct 15:32", L"Coverage 0 h (0.0%)",
                                         L"Est. \u226590%: \u2014"}));
    });

    Run("history cards: comparing lists the current then the previous period, each first line "
        "prefixed; a period without readings has no peak", [] {
        TestHistory h = TestOneAccount();
        h.compare = true;
        ULONGLONG previous = kWeekFrom - kHistoryWeekMs;
        h.samples.insert(h.samples.begin(), {TestReading(0xC2, 1, previous + kHourMs, {{1, 50}}),
                                             TestReading(0xC2, 1, previous + 2 * kHourMs, {{1, 60}})});
        HistoryUiView view = TestCards(h, TestPolled(kWeekFrom + 3 * kHourMs));
        CHECK(view.cards.size() == 2);
        CHECK(view.cards[0].value == L"93%" && view.cards[0].when == L"now");
        CHECK((view.cards[0].lines ==
               std::vector<std::wstring>{L"Current: Peak 95% \u00B7 Sat 24 Oct 16:32", L"Coverage 2 h (1.2%)",
                                         L"Est. \u226590%: 1.1 h", L"Previous: Peak 60% \u00B7 Sat 17 Oct 16:32",
                                         L"Coverage 1 h (0.6%)", L"Est. \u226590%: 0 h"}));
        CHECK(view.cards[1].lines.size() == 4 && view.cards[1].lines[3] == L"Previous: Peak \u2014");
        // Only previous readings: no headline.
        h.samples.resize(2);
        view = TestCards(h);
        CHECK(view.cards.size() == 1 && view.cards[0].value.empty() && view.cards[0].when.empty());
        CHECK(view.cards[0].lines.front() == L"Current: Peak \u2014");
    });

    Run("history cards: dollars are details only outside All meters (the headline holds the last "
        "spend, the previous period its own); credits and plotted dollars get coverage only", [] {
        TestHistory h;
        h.configured = {TestHistoryAccount(L"anthropic", L"CC 1", 0xC1)};
        double spend[] = {6, 11, 10};
        for (int i = 0; i < 3; i++) {
            h.samples.push_back(TestReading(0xC1, 1, kWeekFrom + ULONGLONG(i + 1) * kHourMs, {{6, spend[i]}}));
        }
        h.view = HistoryView::ExtraUsage;
        h.selected = 0;
        HistoryUiView view = TestCards(h);
        CHECK(view.cards.size() == 1);
        const HistoryUiCard& usd = view.cards[0];
        CHECK(usd.title == L"Extra spending (USD) \u2014 details only" && usd.detailsOnly);
        CHECK(usd.value == L"USD 10.00" && usd.when == L"Sat 24 Oct 17:32");
        CHECK((usd.lines == std::vector<std::wstring>{L"Peak USD 11.00 \u00B7 Sat 24 Oct 16:32"}));
        h.compare = true;
        view = TestCards(h);
        CHECK((view.cards[0].lines == std::vector<std::wstring>{L"Current: Peak USD 11.00 \u00B7 Sat 24 Oct 16:32",
                                                                 L"Previous: No dollar readings in this period."}));
        h.samples.insert(h.samples.begin(), TestReading(0xC1, 1, kWeekFrom - kHistoryWeekMs + kHourMs, {{6, 4.5}}));
        view = TestCards(h);
        CHECK((view.cards[0].lines ==
               std::vector<std::wstring>{L"Current: Peak USD 11.00 \u00B7 Sat 24 Oct 16:32",
                                         L"Previous: Last recorded spend: USD 4.50 \u00B7 Sat 17 Oct 15:32",
                                         L"Peak USD 4.50 \u00B7 Sat 17 Oct 15:32"}));
        h.compare = false;
        h.view = HistoryView::AllMeters;
        view = TestCards(h);
        CHECK(view.cards.size() == 1 && !view.cards[0].detailsOnly && view.cards[0].title == L"Extra spending (USD)");
        CHECK((view.cards[0].lines == std::vector<std::wstring>{L"Peak USD 11.00 \u00B7 Sat 24 Oct 16:32",
                                                                 L"Coverage 2 h (1.2%)"}));
        TestHistory credits;
        credits.configured = {TestHistoryAccount(L"openai", L"OAI", 0x0A)};
        credits.samples = {TestReading(0x0A, 2, kWeekFrom + kHourMs, {{7, 2500}}),
                           TestReading(0x0A, 2, kWeekFrom + 2 * kHourMs, {{7, 1240}})};
        credits.view = HistoryView::ExtraUsage;
        credits.selected = 0;
        view = TestCards(credits);
        CHECK(view.cards.size() == 1 && view.cards[0].title == L"Credits remaining" && view.cards[0].value == L"1240.00 credits");
        CHECK((view.cards[0].lines == std::vector<std::wstring>{L"Peak 2500.00 credits \u00B7 Sat 24 Oct 15:32",
                                                                 L"Coverage 1 h (0.6%)"}));
    });

    Run("history cards: All accounts names each account (a removed one by its journal ID), its "
        "recording status once; accounts without readings in account order, unsupported ones left out", [] {
        TestHistory h;
        AccountConfig off = TestHistoryAccount(L"anthropic", L"CC 1", 0xC1);
        off.recordHistory = false;
        AccountConfig paused = TestHistoryAccount(L"antigravity", L"Go", 0x60);
        paused.hidden = true;
        h.configured = {off, TestHistoryAccount(L"anthropic", L"CC 2", 0xC2), paused,
                        TestHistoryAccount(L"openai", L"OAI", 0x0A)};
        for (int i = 1; i <= 2; i++) {
            h.samples.push_back(TestReading(0xC1, 1, kWeekFrom + ULONGLONG(i) * kHourMs, {{1, 30.0 + i}, {2, 5.0 * i}}));
            h.samples.push_back(TestReading(0x3F9C1A2B4D5E6F70, 1, kWeekFrom + ULONGLONG(i) * kHourMs + 60000, {{1, 70.0}}));
        }
        HistoryUiView view = TestCards(h, TestPolled(kWeekTo));
        CHECK(view.accounts.size() == 5 && view.accounts[4].removed &&
              view.accounts[4].label == L"account_3f9c1a2b4d5e6f70");
        CHECK(view.cards.size() == 3);
        CHECK(view.cards[0].title == L"CC 1 \u00B7 Weekly" && view.cards[0].status == L"recording off");
        CHECK(view.cards[1].title == L"CC 1 \u00B7 Fable weekly" && view.cards[1].status.empty());
        CHECK(view.cards[2].title == L"account_3f9c1a2b4d5e6f70 \u00B7 Weekly" && view.cards[2].status.empty());
        CHECK(view.cards[2].when == L"Sat 24 Oct 16:33");  // Polled since
        CHECK(view.noReadings.size() == 3);
        CHECK(view.noReadings[0].label == L"CC 2" && view.noReadings[0].provider == 1 &&
              view.noReadings[0].detail == L"Anthropic");
        CHECK(view.noReadings[1].label == L"Go" && view.noReadings[1].detail == L"Google Antigravity \u00B7 paused");
        CHECK(view.noReadings[2].label == L"OAI" && view.noReadings[2].detail == L"OpenAI");
        // Recording off for this user: no account status.
        h.enabled = false;
        view = TestCards(h);
        CHECK(view.cards[0].status.empty() && view.noReadings[1].detail == L"Google Antigravity");
        // Extra usage: Antigravity has no such meter; CC 1 and CC 2 have no readings of it.
        h.enabled = true;
        h.view = HistoryView::ExtraUsage;
        view = TestCards(h);
        CHECK(view.cards.empty() && view.noReadings.size() == 4);
        CHECK(view.noReadings[0].detail == L"Anthropic \u00B7 recording off" && view.noReadings[2].label == L"OAI" &&
              view.noReadings[3].label == L"account_3f9c1a2b4d5e6f70");
        // One account: only its own entry, its status in the header instead.
        h.view = HistoryView::Weekly;
        h.selected = 2;
        view = TestCards(h);
        CHECK(view.cards.empty() && view.noReadings.size() == 1 && view.noReadings[0].label == L"Go");
        h.selected = 4;
        view = TestCards(h);
        CHECK(view.cards.size() == 1 && view.cards[0].title == L"Weekly" && view.noReadings.empty());
    });

    Run("history cards: accounts without readings by the set's account indices, whatever their labels; the "
        "red threshold the set was analyzed with", [] {
        // "X" has readings; "X  [old]", whose label reads like a status suffix, has none.
        TestHistory h;
        h.configured = {TestHistoryAccount(L"openai", L"X", 0x01), TestHistoryAccount(L"openai", L"X  [old]", 0x02)};
        h.samples = {TestReading(0x01, 2, kWeekFrom + kHourMs, {{1, 10}})};
        HistoryUiView view = TestCards(h);
        CHECK(view.cards.size() == 1 && view.cards[0].title == L"X \u00B7 Weekly");
        CHECK(view.noReadings.size() == 1 && view.noReadings[0].label == L"X  [old]" &&
              view.noReadings[0].detail == L"OpenAI");
        // The view's own threshold colors navigation values only.
        view = TestCards(TestOneAccount(), [](HistoryUiView& v) { v.redThreshold = 70; });
        CHECK(view.cards[0].lines[2] == L"Est. \u226590%: 1.1 h");
    });
}
