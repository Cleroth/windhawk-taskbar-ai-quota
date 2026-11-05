// Usage-history model of the usage history window: meter and view tables, statistics,
// reduction, series building, account collection and selection resolution. Explicit inputs
// only; loops stop as soon as `cancelled` returns true.

// Percentages (meters 0-5) get " used (%)" or, in the Remaining bar mode, " left (%)" after them.
static constexpr PCWSTR kHistoryMeterNames[kHistoryMeterCount] = {
    L"Short-term", L"Weekly", L"Fable weekly", L"Extra budget", L"Claude/GPT short-term", L"Claude/GPT weekly",
    L"Extra spending (USD)", L"Credits remaining",
};
static constexpr const char* kHistoryMeterIds[kHistoryMeterCount] = {
    "short", "weekly", "fable_weekly", "extra_percent", "third_party_short",
    "third_party_weekly", "anthropic_extra_used", "openai_credit_balance",
};

// Views group existing journal lanes; raw meter indices and CSV remain unchanged.
enum class HistoryView { ShortTerm, Weekly, ExtraUsage, AllMeters };
static constexpr PCWSTR kHistoryViewNames[] = {L"Short-term", L"Weekly", L"Extra usage", L"All meters"};
static constexpr unsigned kHistoryViewMeters[] = {
    (1U << 0) | (1U << 4), (1U << 1) | (1U << 2) | (1U << 5),
    (1U << 3) | (1U << 6) | (1U << 7), (1U << kHistoryMeterCount) - 1,
};
// By HistoryProviderCode; Antigravity records its Gemini Argon weekly in the Fable weekly lane,
// GitHub Copilot only its AI credits used (%), OpenCode Go its monthly in the extra % lane.
static constexpr unsigned kHistoryProviderMeters[] = {
    0, (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3) | (1U << 6),
    (1U << 0) | (1U << 1) | (1U << 7), (1U << 0) | (1U << 1) | (1U << 2) | (1U << 4) | (1U << 5), 1U << 3,
    (1U << 0) | (1U << 1) | (1U << 3),
};
static constexpr COLORREF kHistoryColors[] = {
    RGB(0, 80, 230), RGB(220, 100, 0), RGB(0, 140, 40), RGB(200, 0, 160),
    RGB(115, 45, 200), RGB(0, 140, 170), RGB(35, 35, 35), RGB(220, 25, 35),
};

struct HistoryStatistics {
    bool hasValue = false;
    double peak = 0;
    ULONGLONG peakTime = 0;
    double latest = 0;
    ULONGLONG latestTime = 0;
    double coverageMs = 0;
    double nearLimitMs = 0;
    size_t validSamples = 0;
};

struct HistoryRenderPoint {
    int x = 0;
    double value = 0;
    size_t sourceRow = 0;  // Raw provenance: a pick reports this sample; paint never dereferences it.
    bool connect = false;
};

struct HistoryRenderSeries {
    std::vector<HistoryRenderPoint> points;
};

struct HistoryUiAccount {
    uint64_t id = 0;
    uint32_t provider = 0;
    std::wstring label;  // Removed accounts: account_<16 hex digits>
    bool configured = false;
    bool recording = false;
    bool hidden = false;
    bool showWeekly = true, showFableWeekly = true;
    std::vector<size_t> rows;
};

struct HistorySeries {
    size_t account = 0;
    int meter = 0;
    // Its short name (without the account in one account's view) and full title (with it)
    std::wstring legendTitle, hoverTitle;
    COLORREF color = 0;
    int stroke = 3;  // Logical pixels; scoped quotas use 2, never thin 1px dashes.
    bool plotted = true;
    HistoryStatistics current, previous;
    double low = 0, high = 0;  // Its readings' range in [readFrom, to), with 0
};

// What a series build reads besides the samples and accounts.
struct HistorySeriesQuery {
    int selected = -1;  // Account index; -1 is All accounts.
    HistoryView view = HistoryView::Weekly;
    ULONGLONG from = 0, to = 0, readFrom = 0;
    bool overlay = false;  // Compare previous week: [readFrom, from) is the previous period.
    int redThreshold = 90;
    bool remaining = false;  // Titles name percentages left (the Remaining bar mode)
};

struct HistorySeriesSet {
    std::vector<HistorySeries> series;
    std::vector<size_t> noReadingAccounts;  // Accounts without readings, by account index
    int redThreshold = 90;  // The threshold the statistics' near-limit times used
};

// Formatting stays independent of locale for exact values/CSV.
static std::wstring HistoryNumber(double value) {
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) return L"?";
    return std::wstring(buffer, result.ptr);
}

static std::string HistoryCsvId(const char* prefix, uint64_t id) {
    char text[48];
    snprintf(text, sizeof(text), "%s%016llx", prefix, static_cast<unsigned long long>(id));
    return text;
}

static std::wstring HistoryHours(double ms) {
    wchar_t text[40];
    swprintf(text, ARRAYSIZE(text), L"%.2f h", ms / 3600000.0);
    return text;
}

// Reduction boundary, also callable by isolated structural tests. Each continuous
// segment/pixel keeps its first, minimum, maximum and last raw samples in temporal
// order. A break flushes the bucket even inside the same pixel, so no extrema or
// segment endpoints are lost and no fabricated bridge crosses a reset/failure/gap.
// A cancelled reduction returns no points.
static HistoryRenderSeries HistoryReduceRows(const std::vector<HistorySample>& samples,
                                             const std::vector<size_t>& rows,
                                             int meter, ULONGLONG from,
                                             ULONGLONG to, int width,
                                             const std::function<bool()>& cancelled) {
    HistoryRenderSeries result;
    if (width <= 0 || to <= from) return result;
    result.points.reserve(std::min(rows.size(), size_t(width) * 4));
    int bucketX = -1;
    size_t first = 0, minimum = 0, maximum = 0, last = 0;
    bool bucketConnect = false;
    const HistorySample* previous = nullptr;
    auto flush = [&] {
        if (bucketX < 0) return;
        std::array<size_t, 4> positions{first, minimum, maximum, last};
        std::sort(positions.begin(), positions.end());
        auto end = std::unique(positions.begin(), positions.end());
        bool connect = bucketConnect;
        for (auto it = positions.begin(); it != end; ++it) {
            size_t row = rows[*it];
            result.points.push_back({bucketX, samples[row].meters[meter].value, row, connect});
            connect = true;
        }
        bucketX = -1;
    };
    // rows follow timestamp order: callers stable-sort the samples before collecting accounts.
    // The number of occupied segment/pixel buckets is bounded by plot width + structural breaks.
    for (size_t position = 0; position < rows.size(); ++position) {
        if (cancelled()) return {};
        const auto& sample = samples[rows[position]];
        if (sample.timeMs < from || sample.timeMs >= to) continue;
        int x = std::clamp(int(double(sample.timeMs - from) / double(to - from) * width), 0, width - 1);
        bool valid = HistoryHasValue(sample, meter);
        HistoryBreak reason = previous ? HistoryConnection(*previous, sample, meter) : HistoryBreak::None;
        bool connect = previous && reason == HistoryBreak::None;
        if (!valid || !connect || x != bucketX) flush();
        if (valid) {
            if (bucketX < 0) {
                bucketX = x;
                first = minimum = maximum = last = position;
                bucketConnect = connect;
            } else {
                double value = sample.meters[meter].value;
                if (value < samples[rows[minimum]].meters[meter].value) minimum = position;
                if (value > samples[rows[maximum]].meters[meter].value) maximum = position;
                last = position;
            }
        }
        previous = &sample;
    }
    flush();
    return result;
}

static HistoryStatistics HistoryAnalyzeRows(const std::vector<HistorySample>& samples,
                                            const std::vector<size_t>& rows, int meter,
                                            ULONGLONG from, ULONGLONG to, int redThreshold,
                                            const std::function<bool()>& cancelled) {
    HistoryStatistics result;
    const HistorySample* previous = nullptr;
    for (size_t row : rows) {
        if (cancelled()) break;
        const auto& sample = samples[row];
        if (sample.timeMs < from || sample.timeMs >= to) continue;
        if (HistoryHasValue(sample, meter)) {
            double value = sample.meters[meter].value;
            result.latest = value;
            result.latestTime = sample.timeMs;
            ++result.validSamples;
            if (!result.hasValue || value > result.peak) {
                result.hasValue = true;
                result.peak = value;
                result.peakTime = sample.timeMs;
            }
            if (previous && HistoryConnection(*previous, sample, meter) == HistoryBreak::None) {
                double elapsed = double(sample.timeMs - previous->timeMs);
                result.coverageMs += elapsed;
                if (meter < 6) {
                    double a = previous->meters[meter].value;
                    double b = value;
                    if (a >= redThreshold && b >= redThreshold) {
                        result.nearLimitMs += elapsed;
                    } else if ((a >= redThreshold) != (b >= redThreshold)) {
                        // Ratio form avoids overflow when finite endpoints are very far apart.
                        double low = std::min(a, b), high = std::max(a, b);
                        double divisor = std::max({1.0, std::abs(low), std::abs(high)});
                        double fraction = (high / divisor - redThreshold / divisor) /
                                          (high / divisor - low / divisor);
                        result.nearLimitMs += elapsed * std::clamp(fraction, 0.0, 1.0);
                    }
                }
            }
        }
        // Failures/missing values remain the predecessor, so a line never bridges them.
        previous = &sample;
    }
    return result;
}

// Series of the selected account (or every account) in the query's view: one per meter with
// readings in either period, in account then meter order. An account with none is listed
// without readings.
static HistorySeriesSet HistoryBuildSeries(const std::vector<HistorySample>& samples,
                                           const std::vector<HistoryUiAccount>& accounts,
                                           const HistorySeriesQuery& query,
                                           const std::function<bool()>& cancelled) {
    HistorySeriesSet result;
    result.redThreshold = query.redThreshold;
    for (size_t i = 0; i < accounts.size(); ++i) {
        if (cancelled()) break;
        if (query.selected >= 0 && size_t(query.selected) != i) continue;
        const auto& account = accounts[i];
        // A provider this version doesn't know (a later version's) gets the lanes it recorded in
        // [readFrom, to), under their generic names.
        unsigned meters = 0;
        if (account.provider < ARRAYSIZE(kHistoryProviderMeters)) {
            meters = kHistoryProviderMeters[account.provider];
        } else {
            for (size_t row : account.rows) {
                if (cancelled()) break;
                const auto& sample = samples[row];
                if (sample.timeMs < query.readFrom || sample.timeMs >= query.to) continue;
                for (int meter = 0; meter < int(kHistoryMeterCount); ++meter) {
                    if (HistoryHasValue(sample, meter)) meters |= 1U << meter;
                }
            }
        }
        meters &= kHistoryViewMeters[size_t(query.view)];
        // Only Weekly follows taskbar choices; All meters and removed accounts stay unfiltered.
        if (query.view == HistoryView::Weekly && account.provider == 1 && account.configured) {
            if (!account.showWeekly) meters &= ~(1U << 1);
            if (!account.showFableWeekly) meters &= ~(1U << 2);
        }
        if (!meters) continue;
        size_t firstSeries = result.series.size();
        for (int meter = 0; meter < int(kHistoryMeterCount); ++meter) {
            if (cancelled()) break;
            if (!(meters & (1U << meter))) continue;
            HistorySeries series;
            series.account = i;
            series.meter = meter;
            series.current = HistoryAnalyzeRows(samples, account.rows, meter, query.from, query.to,
                                                query.redThreshold, cancelled);
            series.previous = query.overlay
                ? HistoryAnalyzeRows(samples, account.rows, meter, query.readFrom, query.from,
                                     query.redThreshold, cancelled)
                : HistoryStatistics{};
            // No empty per-meter legends. A USD-only account still has real detail data.
            if (!series.current.hasValue && !series.previous.hasValue) continue;
            series.plotted = meter != 6 || query.view == HistoryView::AllMeters;
            bool scoped = meter == 2 || meter == 4 || meter == 5;
            series.stroke = scoped ? 2 : 3;
            series.color = kHistoryColors[(query.selected < 0 ? i : size_t(meter)) % ARRAYSIZE(kHistoryColors)];
            if (query.selected < 0 && scoped) {
                // Keep scoped quotas in the account's color family; width survives high contrast.
                COLORREF color = series.color;
                series.color = RGB((GetRValue(color) * 3 + 255) / 4,
                                   (GetGValue(color) * 3 + 255) / 4,
                                   (GetBValue(color) * 3 + 255) / 4);
            }
            std::wstring label = kHistoryMeterNames[meter];
            if (account.provider == 3 && meter == 1) label = L"Gemini weekly";
            if (account.provider == 3 && meter == 2) label = L"Argon weekly";
            if (account.provider == 4 && meter == 3) label = L"AI credits";
            if (account.provider == 5 && meter == 3) label = L"Monthly";
            ULONGLONG shortDuration = 0;
            bool hasDuration = false, mixedDuration = false;
            for (size_t row : account.rows) {
                if (cancelled()) break;
                const auto& sample = samples[row];
                if (sample.timeMs < query.readFrom || sample.timeMs >= query.to ||
                    !HistoryHasValue(sample, meter)) continue;
                double value = sample.meters[meter].value;
                series.low = std::min(series.low, value);
                series.high = std::max(series.high, value);
                if (meter == 0 || meter == 4) {
                    ULONGLONG duration = sample.meters[meter].durationMs;
                    if (hasDuration && shortDuration != duration) mixedDuration = true;
                    shortDuration = duration;
                    hasDuration = true;
                }
            }
            if (meter == 0 || meter == 4) {
                label = mixedDuration ? L"Short-term (varying windows)" : !shortDuration ? L"Current limit" :
                    shortDuration % 3600000 == 0 ? std::to_wstring(shortDuration / 3600000) + L"h" :
                    HistoryHours(double(shortDuration));
                if (account.provider == 3) {
                    label.front() = wchar_t(towlower(label.front()));
                    label = (meter == 4 ? L"Claude/GPT " : L"Gemini ") + label;
                }
            }
            series.legendTitle = (query.selected < 0 ? account.label + L" · " : L"") + label;
            if (meter < 6) label += query.remaining ? L" left (%)" : L" used (%)";
            series.hoverTitle = account.label + L" · " + label;
            result.series.push_back(std::move(series));
        }
        if (result.series.size() == firstSeries) result.noReadingAccounts.push_back(i);
    }
    return result;
}

// Configured accounts in settings order (zero and duplicate history IDs skipped), then removed
// accounts with samples in [readFrom, to), in order of their first sample, labelled by their
// journal ID. Rows index `samples`.
static std::vector<HistoryUiAccount> HistoryCollectAccounts(const std::vector<AccountConfig>& configured,
                                                            const std::vector<HistorySample>& samples,
                                                            ULONGLONG readFrom, ULONGLONG to,
                                                            const std::function<bool()>& cancelled) {
    std::vector<HistoryUiAccount> accounts;
    std::map<uint64_t, size_t> index;
    for (const auto& config : configured) {
        if (cancelled()) break;
        if (!config.historyId || index.contains(config.historyId)) continue;
        HistoryUiAccount account;
        account.id = config.historyId;
        account.provider = HistoryProviderCode(config.provider);
        account.configured = true;
        account.recording = config.recordHistory;
        account.hidden = config.hidden;
        account.showWeekly = config.showBars[kWeeklyBar];
        account.showFableWeekly = config.showBars[kFableWeeklyBar];
        account.label = config.label;
        index.emplace(account.id, accounts.size());
        accounts.push_back(std::move(account));
    }
    for (size_t row = 0; row < samples.size(); ++row) {
        if (cancelled()) break;
        const auto& sample = samples[row];
        if (!sample.accountId || sample.timeMs < readFrom || sample.timeMs >= to) continue;
        auto [it, inserted] = index.emplace(sample.accountId, accounts.size());
        if (inserted) {
            HistoryUiAccount account;
            account.id = sample.accountId;
            account.provider = sample.provider;
            account.label = Utf8ToWide(HistoryCsvId("account_", sample.accountId));
            accounts.push_back(std::move(account));
        }
        accounts[it->second].rows.push_back(row);
    }
    return accounts;
}

// The account index to select after a reload (-1: All accounts), by history ID. A listed
// request wins; a request that isn't listed (or 0) keeps the current account, which falls back
// to All accounts once it is no longer listed.
static int HistoryResolveSelection(const std::vector<HistoryUiAccount>& accounts, uint64_t currentId,
                                   uint64_t requestId) {
    int selected = -1, requested = -1;
    for (size_t i = 0; i < accounts.size(); ++i) {
        if (accounts[i].id == currentId) selected = int(i);
        if (accounts[i].id == requestId) requested = int(i);
    }
    return requested >= 0 ? requested : selected;
}
