// Usage-history CSV export (pure): the rows an export writes, from a scope frozen when it starts,
// to a sink. The file around it is native (history/ops.h).

// What an export writes, fixed at its command: its own snapshot (time-sorted samples), the
// account (0: every account) and the times [from, to) (the range, and the week before it while
// comparing).
struct HistoryExportScope {
    std::shared_ptr<const HistorySnapshot> snapshot;
    uint64_t accountId = 0;
    ULONGLONG from = 0, to = 0;
    bool compare = false;
};

static std::string HistoryUtcDate(ULONGLONG time) {
    if (!time || time > 1833029933770955ULL) return {};
    ULONGLONG ticks = (time + 11644473600000ULL) * 10000;
    FILETIME file{DWORD(ticks), DWORD(ticks >> 32)};
    SYSTEMTIME utc{};
    if (!FileTimeToSystemTime(&file, &utc)) return {};
    char text[40];
    snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
             utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond,
             utc.wMilliseconds);
    return text;
}

enum class HistoryCsvEnd : uint8_t { Written, Cancelled, Failed };

// Writes the scope's CSV: a header, then one row per sample of the scope and supported meter
// lane, counted in `exported`, handed to `write` in pieces of at least 64 KiB (`last`: the final,
// shorter one). Labels never enter it; times are UTC. Stops when `cancelled` (checked per
// sample) or `write` (returning false) says so.
static HistoryCsvEnd HistoryCsvWrite(const HistoryExportScope& scope, size_t& exported,
                                     const std::function<bool(const std::string&, bool last)>& write,
                                     const std::function<bool()>& cancelled) {
    exported = 0;
    std::string buffer = "timestamp_utc,timestamp_unix_ms,account_id,provider,provider_id,meter_id,unit,value,reset_utc,reset_unix_ms,duration_ms,poll_interval_ms,session_id,failed,missing\r\n";
    for (const auto& sample : scope.snapshot->samples) {
        if (cancelled()) return HistoryCsvEnd::Cancelled;
        if (!sample.accountId || (scope.accountId && sample.accountId != scope.accountId) ||
            sample.timeMs < scope.from || sample.timeMs >= scope.to) continue;
        const char* provider = sample.provider == 1 ? "anthropic" : sample.provider == 2 ? "openai" :
                               sample.provider == 3 ? "antigravity" : "unknown";
        // Nonnumeric, non-formula text survives Excel's 15-digit number coercion.
        std::string id = HistoryCsvId("account_", sample.accountId);
        std::string session = HistoryCsvId("session_", sample.session);
        for (size_t meter = 0; meter < kHistoryMeterCount; ++meter) {
            // Omit structurally impossible lanes, but preserve missing observations
            // for each provider's supported meters so exports retain gap information.
            if ((sample.provider == 1 && (meter == 4 || meter == 5 || meter == 7)) ||
                (sample.provider == 2 && meter != 0 && meter != 1 && meter != 7) ||
                (sample.provider == 3 && meter != 0 && meter != 1 && meter != 4 && meter != 5)) continue;
            const auto& value = sample.meters[meter];
            bool present = HistoryHasValue(sample, int(meter));
            buffer += HistoryUtcDate(sample.timeMs) + "," + std::to_string(sample.timeMs) + "," +
                id + "," + provider + "," + std::to_string(sample.provider) + "," +
                kHistoryMeterIds[meter] + "," + (meter == 6 ? "USD" : meter == 7 ? "credits" : "percent") + "," +
                (present ? WideToUtf8(HistoryNumber(value.value)) : "") + "," + HistoryUtcDate(value.resetMs) + "," +
                std::to_string(value.resetMs) + "," + std::to_string(value.durationMs) + "," +
                std::to_string(sample.intervalMs) + "," + session + "," +
                std::to_string(sample.failed) + "," + (present ? "0" : "1") + "\r\n";
            ++exported;
        }
        if (buffer.size() >= 65536) {
            if (!write(buffer, false)) return HistoryCsvEnd::Failed;
            buffer.clear();
        }
    }
    return write(buffer, true) ? HistoryCsvEnd::Written : HistoryCsvEnd::Failed;
}
