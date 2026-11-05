// Provider status pages: where each provider publishes its public status, which of its components
// the mod watches, the parser that turns a page into what the provider's accounts show, and the
// read rules (OpenCode's confirmation, the age past which a reading shows nothing). Windows.Data.Json
// only: no network, globals or windows (tests/settings extracts this fragment).

enum class StatusFeedKind : uint8_t {
    Statuspage,  // Atlassian Statuspage's summary.json: the watched component ids
    IncidentIo,  // incident.io's page feed: every component of the watched group
    OpenCode,    // OpenCode's status API: the watched service's component
};

struct ProviderStatusSource {
    PCWSTR provider;  // AccountConfig::provider
    PCWSTR host;
    PCWSTR path;
    PCWSTR pageUrl;   // The page the details flyout opens
    StatusFeedKind kind;
    // Statuspage: component ids; incident.io: the group's id, then its name (when the id is gone);
    // OpenCode: the service, then the name the flyout gives its component.
    std::array<PCWSTR, 3> watched;
    // Between reads; a reading older than two intervals shows nothing.
    ULONGLONG intervalMs;
    // A non-operational page shows only once a second read, kStatusConfirmReadMs later, agrees.
    bool confirm;
};

static constexpr ULONGLONG kStatusIntervalMs = 5ULL * 60 * 1000;
static constexpr ULONGLONG kOpenCodeStatusIntervalMs = 10ULL * 60 * 1000;
static constexpr ULONGLONG kStatusConfirmReadMs = 3ULL * 60 * 1000;

static constexpr ProviderStatusSource kProviderStatusSources[] = {
    {L"anthropic", L"status.claude.com", L"/api/v2/summary.json", L"https://status.claude.com",
     StatusFeedKind::Statuspage, {L"rwppv331jlwc", L"k8w3r06qmzrp", L"yyzkbfz2thpt"}, kStatusIntervalMs, false},
    {L"openai", L"status.openai.com", L"/proxy/status.openai.com", L"https://status.openai.com",
     StatusFeedKind::IncidentIo, {L"01KMKF9EBTCD8BN9PG8DJZXRSQ", L"Codex", nullptr}, kStatusIntervalMs, false},
    {L"copilot", L"www.githubstatus.com", L"/api/v2/summary.json", L"https://www.githubstatus.com",
     StatusFeedKind::Statuspage, {L"pjmpxvq2cmr2", L"cnnb39dkkk82", nullptr}, kStatusIntervalMs, false},
    {L"opencode", L"status.opencode.ai", L"/api/status", L"https://status.opencode.ai",
     StatusFeedKind::OpenCode, {L"go", L"OpenCode Go", nullptr}, kOpenCodeStatusIntervalMs, true},
};

// The provider's status source; none for Antigravity.
static const ProviderStatusSource* FindProviderStatusSource(std::wstring_view provider) {
    for (const ProviderStatusSource& source : kProviderStatusSources) {
        if (provider == source.provider) return &source;
    }
    return nullptr;
}

// What a status page shows `source`'s accounts: nothing (OutageLevel::None) while every watched
// component is operational. None (no value) when the page can't be trusted: invalid JSON, a missing
// or mistyped part that names a level, no watched component listed, or OpenCode's own stale flag.
// Status strings it doesn't know name no level. `problem` gets why a read failed, or the first
// unknown status of a watched component.
static std::optional<ProviderOutage> ParseProviderStatus(const ProviderStatusSource& source,
                                                         const std::string& body,
                                                         std::wstring* problem = nullptr) {
    auto fail = [&](PCWSTR why) -> std::optional<ProviderOutage> {
        if (problem) *problem = why;
        return std::nullopt;
    };
    auto levelOf = [&](const std::wstring& status) {
        if (status == L"operational") return OutageLevel::None;
        if (status == L"degraded_performance" || status == L"degraded") return OutageLevel::Degraded;
        if (status == L"partial_outage") return OutageLevel::Partial;
        if (status == L"major_outage" || status == L"full_outage" || status == L"outage") return OutageLevel::Major;
        if (status == L"under_maintenance" || status == L"maintenance") return OutageLevel::Maintenance;
        if (problem && problem->empty() && !status.empty()) *problem = L"unknown status \"" + status.substr(0, 40) + L"\"";
        return OutageLevel::None;
    };
    auto array = [](JsonObject const& o, PCWSTR name) -> JsonArray {
        if (!o || !o.HasKey(name)) return nullptr;
        IJsonValue value = o.GetNamedValue(name);
        return value.ValueType() == JsonValueType::Array ? value.GetArray() : nullptr;
    };
    auto object = [](IJsonValue const& value) -> JsonObject {
        return value.ValueType() == JsonValueType::Object ? value.GetObject() : nullptr;
    };
    auto isString = [](JsonObject const& o, PCWSTR name) {
        return o && o.HasKey(name) && o.GetNamedValue(name).ValueType() == JsonValueType::String;
    };
    // Provider dates, accepted only in a plausible range: Unix ms, or ISO 8601 with seconds and a
    // zone ("Z" or "+hh:mm"); anything else is an unknown time.
    constexpr double kEarliestMs = 1577836800000.0, kLatestMs = 4102444800000.0;  // 2020, 2100
    auto plausible = [&](double ms) -> ULONGLONG {
        return std::isfinite(ms) && ms >= kEarliestMs && ms < kLatestMs ? (ULONGLONG)ms : 0;
    };
    auto isoMs = [&](const std::wstring& s) -> ULONGLONG {
        auto digits = [&](size_t at, size_t count, int maximum) {
            if (at + count > s.size()) return false;
            int value = 0;
            for (size_t k = at; k < at + count; k++) {
                if (s[k] < L'0' || s[k] > L'9') return false;
                value = value * 10 + (s[k] - L'0');
            }
            return value <= maximum;
        };
        if (!digits(0, 4, 9999) || s.size() < 20 || s[4] != L'-' || !digits(5, 2, 12) || s[7] != L'-' ||
            !digits(8, 2, 31) || s[10] != L'T' || !digits(11, 2, 23) || s[13] != L':' || !digits(14, 2, 59) ||
            s[16] != L':' || !digits(17, 2, 60)) {
            return 0;
        }
        size_t zone = 19;
        if (s[zone] == L'.') {
            do zone++; while (zone < s.size() && s[zone] >= L'0' && s[zone] <= L'9');
            if (zone == 20) return 0;
        }
        bool valid = zone + 1 == s.size() ? s[zone] == L'Z' :
                     zone + 6 == s.size() && (s[zone] == L'+' || s[zone] == L'-') && digits(zone + 1, 2, 23) &&
                         s[zone + 3] == L':' && digits(zone + 4, 2, 59);
        return valid ? plausible((double)ParseIso8601Ms(s)) : 0;
    };

    try {
        JsonObject root = JsonObject::Parse(Utf8ToWide(body));
        ProviderOutage outage;

        if (source.kind == StatusFeedKind::Statuspage) {
            // Levels from the watched components; the title and start from an unresolved incident or
            // a maintenance in progress listing the worst one, else its last status change.
            JsonArray components = array(root, L"components");
            if (!components) return fail(L"no components");
            struct Watched {
                std::wstring id, name;
                OutageLevel level = OutageLevel::None;
                std::wstring updatedAt;
                bool found = false;
            };
            std::vector<Watched> watched;
            for (PCWSTR id : source.watched) {
                if (!id) continue;
                watched.emplace_back();
                watched.back().id = id;
            }
            for (IJsonValue const& value : components) {
                JsonObject component = object(value);
                if (!isString(component, L"id")) continue;
                std::wstring id = GetStr(component, L"id");
                auto entry = std::find_if(watched.begin(), watched.end(),
                                          [&](const Watched& w) { return w.id == id && !w.found; });
                if (entry == watched.end()) continue;
                if (!isString(component, L"status")) return fail(L"component status not a string");
                entry->found = true;
                entry->name = GetStr(component, L"name");
                if (entry->name.empty()) entry->name = id;
                entry->level = levelOf(GetStr(component, L"status"));
                entry->updatedAt = GetStr(component, L"updated_at");
            }
            const Watched* worst = nullptr;
            int nonOperational = 0;
            bool any = false;
            for (const Watched& w : watched) {
                any |= w.found;
                if (w.level == OutageLevel::None) continue;
                nonOperational++;
                if (!worst || w.level > worst->level) worst = &w;
            }
            if (!any) return fail(L"no watched component");
            if (!worst) return outage;
            outage.level = worst->level;
            outage.component = worst->name;
            outage.others = nonOperational - 1;
            auto lists = [&](JsonObject const& event) {
                JsonArray listed = array(event, L"components");
                if (!listed) return false;
                for (IJsonValue const& value : listed) {
                    JsonObject component = object(value);
                    if (component && GetStr(component, L"id") == worst->id) return true;
                }
                return false;
            };
            bool titled = false;
            if (JsonArray incidents = array(root, L"incidents")) {
                for (IJsonValue const& value : incidents) {
                    JsonObject incident = object(value);
                    std::wstring status = GetStr(incident, L"status");
                    if (!incident || status.empty() || status == L"resolved" || status == L"postmortem" ||
                        status == L"completed" || !lists(incident)) {
                        continue;
                    }
                    outage.title = GetStr(incident, L"name");
                    outage.sinceMs = isoMs(GetStr(incident, L"started_at"));
                    if (!outage.sinceMs) outage.sinceMs = isoMs(GetStr(incident, L"created_at"));
                    titled = true;
                    break;
                }
            }
            if (JsonArray maintenances = !titled ? array(root, L"scheduled_maintenances") : nullptr) {
                for (IJsonValue const& value : maintenances) {
                    JsonObject maintenance = object(value);
                    std::wstring status = GetStr(maintenance, L"status");
                    if (!maintenance || (status != L"in_progress" && status != L"verifying") || !lists(maintenance)) {
                        continue;
                    }
                    outage.title = GetStr(maintenance, L"name");
                    outage.sinceMs = isoMs(GetStr(maintenance, L"started_at"));
                    if (!outage.sinceMs) outage.sinceMs = isoMs(GetStr(maintenance, L"scheduled_for"));
                    titled = true;
                    break;
                }
            }
            if (!titled) outage.sinceMs = isoMs(worst->updatedAt);
            return outage;
        }

        if (source.kind == StatusFeedKind::IncidentIo) {
            // The watched group's components, by id or else by name, hidden ones (or ones whose
            // hidden flag isn't false) aside. Levels only from the components the page lists as
            // currently affected; an incident's own component statuses keep its worst impact after
            // recovery. The title from an unresolved incident, or a maintenance in progress, whose
            // current impact covers the worst component; the start from that impact.
            JsonObject summary = GetObj(root, L"summary");
            JsonArray items = array(GetObj(summary, L"structure"), L"items");
            if (!items) return fail(L"no structure");
            JsonObject group = nullptr;
            for (IJsonValue const& value : items) {
                JsonObject candidate = GetObj(object(value), L"group");
                if (!candidate) continue;
                if (GetStr(candidate, L"id") == source.watched[0]) {
                    group = candidate;
                    break;
                }
                if (!group && source.watched[1] && _wcsicmp(GetStr(candidate, L"name").c_str(), source.watched[1]) == 0) {
                    group = candidate;
                }
            }
            JsonArray members = array(group, L"components");
            if (!members) return fail(L"no watched group");
            struct Watched {
                std::wstring id, name;
                OutageLevel level = OutageLevel::None;
            };
            std::vector<Watched> watched;
            for (IJsonValue const& value : members) {
                JsonObject member = object(value);
                bool shown = member && (!member.HasKey(L"hidden") ||
                                        (member.GetNamedValue(L"hidden").ValueType() == JsonValueType::Boolean &&
                                         !member.GetNamedBoolean(L"hidden")));
                std::wstring id = GetStr(member, L"component_id");
                if (!shown || id.empty()) continue;
                if (std::any_of(watched.begin(), watched.end(), [&](const Watched& w) { return w.id == id; })) continue;
                watched.emplace_back();
                watched.back().id = id;
                watched.back().name = GetStr(member, L"name");
            }
            if (watched.empty()) return fail(L"no watched component");
            if (JsonArray named = array(summary, L"components")) {
                for (IJsonValue const& value : named) {
                    JsonObject component = object(value);
                    std::wstring id = GetStr(component, L"id");
                    for (Watched& w : watched) {
                        if (w.id == id && w.name.empty()) w.name = GetStr(component, L"name");
                    }
                }
            }
            JsonArray affected = array(summary, L"affected_components");
            if (!affected) return fail(L"no affected components");
            for (IJsonValue const& value : affected) {
                JsonObject entry = object(value);
                std::wstring id = GetStr(entry, L"component_id");
                if (id.empty()) continue;
                auto w = std::find_if(watched.begin(), watched.end(), [&](const Watched& w) { return w.id == id; });
                if (w == watched.end()) continue;
                if (!isString(entry, L"status")) return fail(L"affected component status not a string");
                w->level = std::max(w->level, levelOf(GetStr(entry, L"status")));
            }
            const Watched* worst = nullptr;
            int nonOperational = 0;
            for (const Watched& w : watched) {
                if (w.level == OutageLevel::None) continue;
                nonOperational++;
                if (!worst || w.level > worst->level) worst = &w;
            }
            if (!worst) return outage;
            outage.level = worst->level;
            outage.component = worst->name.empty() ? worst->id : worst->name;
            outage.others = nonOperational - 1;
            for (PCWSTR list : {L"ongoing_incidents", L"scheduled_maintenances"}) {
                JsonArray events = array(summary, list);
                for (uint32_t k = 0; events && k < events.Size() && outage.title.empty(); k++) {
                    JsonObject event = object(events.GetAt(k));
                    std::wstring status = GetStr(event, L"status");
                    bool maintenance = GetStr(event, L"type") == L"maintenance" ||
                                       std::wstring_view(list) == L"scheduled_maintenances";
                    if (maintenance ? status != L"maintenance_in_progress" :
                                      status.empty() || status == L"resolved" || status == L"closed" ||
                                          status == L"postmortem" || status.starts_with(L"maintenance")) {
                        continue;
                    }
                    bool current = false;
                    if (JsonArray components = array(event, L"affected_components")) {
                        for (IJsonValue const& value : components) {
                            JsonObject component = object(value);
                            current |= GetStr(component, L"component_id") == worst->id &&
                                       levelOf(GetStr(component, L"current_status")) != OutageLevel::None;
                        }
                    }
                    if (!current) continue;
                    outage.title = GetStr(event, L"name");
                    // The worst component's open impact (a maintenance in progress: its planned one):
                    // when it began, not when the event was posted.
                    if (JsonArray impacts = array(event, L"component_impacts")) {
                        for (IJsonValue const& value : impacts) {
                            JsonObject impact = object(value);
                            bool open = maintenance || !impact || !impact.HasKey(L"end_at") ||
                                        impact.GetNamedValue(L"end_at").ValueType() == JsonValueType::Null;
                            if (impact && open && GetStr(impact, L"component_id") == worst->id) {
                                outage.sinceMs = std::max(outage.sinceMs, isoMs(GetStr(impact, L"start_at")));
                            }
                        }
                    }
                }
            }
            return outage;
        }

        // OpenCode: the watched service's component, unless the page says its data is stale. Its
        // start and reason, or those of an ongoing incident of the service, with the models that
        // aren't operational.
        if (root.HasKey(L"stale")) {
            IJsonValue stale = root.GetNamedValue(L"stale");
            if (stale.ValueType() != JsonValueType::Boolean) return fail(L"stale flag not a boolean");
            if (stale.GetBoolean()) return fail(L"page reports stale data");
        }
        JsonArray components = array(root, L"components");
        if (!components) return fail(L"no components");
        JsonObject service = nullptr;
        for (IJsonValue const& value : components) {
            JsonObject component = object(value);
            if (component && GetStr(component, L"service") == source.watched[0]) {
                service = component;
                break;
            }
        }
        if (!service) return fail(L"no watched component");
        if (!isString(service, L"status")) return fail(L"component status not a string");
        outage.level = levelOf(GetStr(service, L"status"));
        if (outage.level == OutageLevel::None) return outage;
        outage.component = source.watched[1] ? source.watched[1] : source.watched[0];
        outage.sinceMs = plausible(GetNum(service, L"since"));
        std::wstring reason = GetStr(service, L"reason");
        std::vector<std::wstring> models;
        auto addModels = [&](JsonArray const& list, bool checkStatus) {
            for (uint32_t k = 0; list && k < list.Size() && models.size() < 3; k++) {
                JsonObject model = object(list.GetAt(k));
                std::wstring id = GetStr(model, L"id");
                if (id.empty() || (checkStatus && levelOf(GetStr(model, L"status")) == OutageLevel::None)) continue;
                if (std::find(models.begin(), models.end(), id) == models.end()) models.push_back(id);
            }
        };
        addModels(array(service, L"breakdown"), true);
        if (JsonArray incidents = array(root, L"incidents")) {
            for (IJsonValue const& value : incidents) {
                JsonObject incident = object(value);
                if (!incident || GetStr(incident, L"service") != source.watched[0]) continue;
                // Ongoing: no resolution time yet.
                if (incident.HasKey(L"resolvedAt") &&
                    incident.GetNamedValue(L"resolvedAt").ValueType() != JsonValueType::Null) {
                    continue;
                }
                if (!outage.sinceMs) outage.sinceMs = plausible(GetNum(incident, L"startedAt"));
                if (reason.empty() || reason == L"none") reason = GetStr(incident, L"reason");
                if (models.empty()) addModels(array(incident, L"models"), false);
                break;
            }
        }
        if (reason == L"latency") {
            outage.title = L"Slow responses";
        } else if (reason == L"errors") {
            outage.title = L"Elevated errors";
        } else if (reason == L"upstream") {
            outage.title = L"Upstream provider issues";
        } else if (reason == L"availability") {
            outage.title = L"Availability issues";
        } else if (reason != L"none") {
            outage.title = reason.substr(0, 40);
        }
        if (!models.empty()) {
            std::wstring list;
            for (const std::wstring& model : models) list += (list.empty() ? L"" : L", ") + model.substr(0, 40);
            outage.title += (outage.title.empty() ? L"" : L": ") + list;
        }
        return outage;
    } catch (...) {
        return fail(L"invalid JSON");
    }
}

// The fetch thread's record of one provider's reads.
struct ProviderStatusTrack {
    int streak = 0;            // Consecutive non-operational reads, at most 2
    ULONGLONG streakTick = 0;  // When the latest of them was read (GetTickCount64)
    ULONGLONG delayMs = 0;     // Until the next read, unless the provider asks for longer
};

// What a provider shows after a read at `tick` (GetTickCount64): `read` is the parsed page, none for
// a failed read. A failed read or an operational page shows nothing at once. A non-operational page
// shows, unless its source confirms: then only from the second such read in a row, taken within two
// intervals of the first, which comes kStatusConfirmReadMs after it.
static ProviderOutage ShownAfterRead(const ProviderStatusSource& source, ProviderStatusTrack& track,
                                     const std::optional<ProviderOutage>& read, ULONGLONG tick) {
    track.delayMs = source.intervalMs;
    if (!read || read->level == OutageLevel::None) {
        track.streak = 0;
        return {};
    }
    bool recent = track.streak > 0 && tick - track.streakTick <= 2 * source.intervalMs;
    track.streak = recent ? std::min(track.streak + 1, 2) : 1;
    track.streakTick = tick;
    if (!source.confirm || track.streak >= 2) return *read;
    track.delayMs = kStatusConfirmReadMs;
    return {};
}

// Whether a reading taken at `readTick` (GetTickCount64; 0: none) still shows at `tick`.
static bool ProviderStatusCurrent(const ProviderStatusSource& source, ULONGLONG readTick, ULONGLONG tick) {
    return readTick && tick >= readTick && tick - readTick <= 2 * source.intervalMs;
}
