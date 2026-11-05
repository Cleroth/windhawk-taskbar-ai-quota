// Provider status pages (fetch/status-feeds.h). Captured pages in fixtures/status, read 2026-11-05:
// Claude's and GitHub's Statuspage summaries, OpenAI's incident.io feed and OpenCode's status API
// with every watched component operational, and Supabase's incident.io feed (update texts removed)
// with an incident in monitoring after its components recovered. Variants replace one entry of a
// captured page; the incident.io incident below is HashiCorp's live one of the same day, its
// component ids moved onto a Codex component. Expected values are read off the pages by hand.

static std::filesystem::path g_statusSamples;

static std::string Sample(const char* name) {
    std::ifstream file(g_statusSamples / name, std::ios::binary);
    if (!file) throw std::runtime_error(std::string("missing sample ") + name);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

// `text` with its first `from` replaced by `to`; it must hold `from`.
static std::string Replaced(std::string text, std::string_view from, std::string_view to) {
    size_t at = text.find(from);
    if (at == std::string::npos) throw std::runtime_error("sample lacks " + std::string(from));
    return text.replace(at, from.size(), to);
}

static const ProviderStatusSource& Source(PCWSTR provider) {
    const ProviderStatusSource* source = FindProviderStatusSource(provider);
    if (!source) throw std::runtime_error("no status source");
    return *source;
}

// Within a second of `expected`: dates are parsed to the millisecond through floating point.
static bool NearMs(ULONGLONG actual, ULONGLONG expected) {
    return actual + 1000 > expected && actual < expected + 1000;
}

static std::optional<ProviderOutage> Parse(PCWSTR provider, const std::string& page, std::wstring* problem = nullptr) {
    return ParseProviderStatus(Source(provider), page, problem);
}

// Claude's watched components and an unwatched one, as the captured page lists them.
static constexpr std::string_view kClaudeAi = R"("id":"rwppv331jlwc","name":"claude.ai","status":"operational")";
static constexpr std::string_view kClaudeCode = R"("id":"yyzkbfz2thpt","name":"Claude Code","status":"operational")";
static constexpr std::string_view kClaudeConsole =
    R"j("id":"0qbwn08sd68x","name":"Claude Console (platform.claude.com)","status":"operational")j";

// The captured Claude page with `entry` (one of the above) in `status`.
static std::string Claude(std::string_view entry, std::string_view status, std::string page = Sample("claude-summary.json")) {
    std::string changed(entry);
    changed.replace(changed.rfind("operational"), 11, status);
    return Replaced(std::move(page), entry, changed);
}

// OpenAI's Codex components (its Codex group), and components outside the group.
static constexpr std::string_view kCodexApi = "01KMP3KP5MGE23B80K1EK4S8PV";
static constexpr std::string_view kCodexCli = "01KMKFAMWKNQ84Z1766MV08ZDE";
static constexpr std::string_view kConversations = "01JMXBNJXGV1T5GT2M9XA83XNG";
static constexpr std::string_view kCodexDesktop = "01KMKFAMWKQ81YWSE1Z18R6VHR";  // In the ChatGPT group

// The captured OpenAI feed with `affected` as its currently affected components and `incidents` as
// its ongoing incidents (JSON array contents).
static std::string OpenAi(std::string_view affected, std::string_view incidents = "",
                          std::string page = Sample("openai-proxy.json")) {
    page = Replaced(std::move(page), R"("affected_components":[])", "\"affected_components\":[" + std::string(affected) + "]");
    return Replaced(std::move(page), R"("ongoing_incidents":[])", "\"ongoing_incidents\":[" + std::string(incidents) + "]");
}

static std::string Affected(std::string_view id, std::string_view status) {
    return R"({"component_id":")" + std::string(id) + R"(","status":")" + std::string(status) + R"("})";
}

// HashiCorp's ongoing incident (2026-11-05), on Codex API, with its component's current status.
static std::string HashiCorpIncident(std::string_view currentStatus = "degraded_performance") {
    std::string id(kCodexApi);
    return R"({"component_impacts":[{"start_at":"2026-11-05T08:13:42.056Z","id":"01M6NCANS8A65XSYMXHMB7CBSR",)"
           R"("component_id":")" + id + R"(","status_page_incident_id":"01M6NCANS8TCXFRV81SFRQ1NYR",)"
           R"("status":"degraded_performance"}],"status_summaries":[{"start_at":"2026-11-05T08:13:42.056Z",)"
           R"("worst_component_status":"degraded_performance"}],"published_at":"2026-11-05T08:13:42.056Z",)"
           R"("id":"01M6NCANS8TCXFRV81SFRQ1NYR","status_page_id":"01K7FBWXHZPP52EWA3EGJ2SNVA",)"
           R"("name":"HCP Terraform \u2013 Elevated 502 Bad Gateway Errors","status":"investigating",)"
           R"("affected_components":[{"component_id":")" + id + R"(","status":"degraded_performance",)"
           R"("current_status":")" + std::string(currentStatus) + R"("}],"type":"incident"})";
}

// The captured OpenCode page with the Go service's entry replaced from its start.
static constexpr std::string_view kGo = R"({"service":"go","status":"operational","reason":"none","since":1793863048523)";
static std::string OpenCode(std::string_view go, std::string page = Sample("opencode-status.json")) {
    return Replaced(std::move(page), kGo, go);
}

static void RunStatusTests() {
    Run("status sources: the four providers' pages; none for Antigravity", [] {
        struct Expected {
            PCWSTR provider, host, path, page;
            StatusFeedKind kind;
            ULONGLONG intervalMinutes;
            bool confirm;
        };
        const Expected expected[] = {
            {L"anthropic", L"status.claude.com", L"/api/v2/summary.json", L"https://status.claude.com",
             StatusFeedKind::Statuspage, 5, false},
            {L"openai", L"status.openai.com", L"/proxy/status.openai.com", L"https://status.openai.com",
             StatusFeedKind::IncidentIo, 5, false},
            {L"copilot", L"www.githubstatus.com", L"/api/v2/summary.json", L"https://www.githubstatus.com",
             StatusFeedKind::Statuspage, 5, false},
            {L"opencode", L"status.opencode.ai", L"/api/status", L"https://status.opencode.ai",
             StatusFeedKind::OpenCode, 10, true},
        };
        for (const Expected& e : expected) {
            const ProviderStatusSource& source = Source(e.provider);
            CHECK(std::wstring_view(source.host) == e.host && std::wstring_view(source.path) == e.path &&
                  std::wstring_view(source.pageUrl) == e.page && source.kind == e.kind &&
                  source.intervalMs == e.intervalMinutes * 60000 && source.confirm == e.confirm);
        }
        CHECK(!FindProviderStatusSource(L"antigravity") && !FindProviderStatusSource(L""));
        CHECK(std::size(kProviderStatusSources) == 4);
    });

    Run("captured pages: every watched component operational shows nothing", [] {
        const std::pair<PCWSTR, const char*> pages[] = {{L"anthropic", "claude-summary.json"},
                                                        {L"copilot", "github-summary.json"},
                                                        {L"openai", "openai-proxy.json"},
                                                        {L"opencode", "opencode-status.json"}};
        for (auto [provider, name] : pages) {
            std::wstring problem;
            std::optional<ProviderOutage> read = Parse(provider, Sample(name), &problem);
            CHECK(read && *read == ProviderOutage{} && problem.empty());
        }
    });

    Run("Statuspage: a watched component's level and name, the others counted; unwatched ones ignored", [] {
        const std::pair<std::string_view, OutageLevel> levels[] = {{"degraded_performance", OutageLevel::Degraded},
                                                                   {"partial_outage", OutageLevel::Partial},
                                                                   {"major_outage", OutageLevel::Major},
                                                                   {"under_maintenance", OutageLevel::Maintenance}};
        for (auto [status, level] : levels) {
            std::optional<ProviderOutage> read = Parse(L"anthropic", Claude(kClaudeCode, status));
            CHECK(read && read->level == level && read->component == L"Claude Code" && read->others == 0);
        }
        // The worst of two; on a tie the first watched.
        std::optional<ProviderOutage> read =
            Parse(L"anthropic", Claude(kClaudeCode, "major_outage", Claude(kClaudeAi, "degraded_performance")));
        CHECK(read && read->level == OutageLevel::Major && read->component == L"Claude Code" && read->others == 1);
        read = Parse(L"anthropic", Claude(kClaudeCode, "partial_outage", Claude(kClaudeAi, "partial_outage")));
        CHECK(read && read->level == OutageLevel::Partial && read->component == L"claude.ai" && read->others == 1);
        CHECK(Parse(L"anthropic", Claude(kClaudeConsole, "major_outage")) == ProviderOutage{});
        // A status it doesn't know names no level, and is reported.
        std::wstring problem;
        read = Parse(L"anthropic", Claude(kClaudeCode, "on_fire"), &problem);
        CHECK(read && read->level == OutageLevel::None && problem == L"unknown status \"on_fire\"");
        read = Parse(L"copilot", Replaced(Sample("github-summary.json"),
                                          R"("id":"cnnb39dkkk82","name":"Copilot AI Model Providers","status":"operational")",
                                          R"("id":"cnnb39dkkk82","name":"Copilot AI Model Providers","status":"partial_outage")"));
        CHECK(read && read->level == OutageLevel::Partial && read->component == L"Copilot AI Model Providers");
    });

    Run("Statuspage: an unresolved incident or a maintenance in progress on the worst component titles it", [] {
        auto with = [](std::string_view incidents, std::string_view maintenances = "") {
            std::string page = Claude(kClaudeCode, "partial_outage");
            page = Replaced(page, R"("incidents":[])", "\"incidents\":[" + std::string(incidents) + "]");
            return Replaced(page, R"("scheduled_maintenances":[])",
                            "\"scheduled_maintenances\":[" + std::string(maintenances) + "]");
        };
        const std::string onCode = R"("components":[{"id":"yyzkbfz2thpt","name":"Claude Code"}])";
        const std::string onApi = R"("components":[{"id":"k8w3r06qmzrp","name":"Claude API"}])";
        std::string investigating = R"({"name":"Elevated errors on Claude Code","status":"investigating",)"
                                    R"("started_at":"2026-11-05T13:05:00.000Z",)" + onCode + "}";
        std::optional<ProviderOutage> read = Parse(L"anthropic", with(investigating));
        CHECK(read && read->title == L"Elevated errors on Claude Code" && read->sinceMs == 1793883900000ULL);
        // Resolved ones, and ones on another component, don't; then its last status change dates it.
        std::string resolved = R"({"name":"Old","status":"resolved","started_at":"2026-11-05T09:30:00Z",)" + onCode + "}";
        std::string elsewhere = R"({"name":"API errors","status":"identified","started_at":"2026-11-05T09:30:00Z",)" + onApi + "}";
        read = Parse(L"anthropic", with(resolved + "," + elsewhere));
        CHECK(read && read->title.empty() && NearMs(read->sinceMs, 1793217918151ULL));
        read = Parse(L"anthropic", with(resolved + "," + investigating));
        CHECK(read && read->title == L"Elevated errors on Claude Code");
        // A maintenance in progress (or verifying); scheduled ones don't; an incident comes first.
        std::string maintenance = R"({"name":"Database upgrade","status":"in_progress","scheduled_for":"2026-11-05T09:00:00Z",)"
                                  R"("started_at":"2026-11-05T09:30:00Z",)" + onCode + "}";
        std::string scheduled = R"({"name":"Later","status":"scheduled","scheduled_for":"2026-11-06T09:00:00Z",)" + onCode + "}";
        read = Parse(L"anthropic", with("", scheduled + "," + maintenance));
        CHECK(read && read->title == L"Database upgrade" && read->sinceMs == 1793871000000ULL);
        read = Parse(L"anthropic", with("", Replaced(maintenance, "in_progress", "verifying")));
        CHECK(read && read->title == L"Database upgrade");
        read = Parse(L"anthropic", with("", scheduled));
        CHECK(read && read->title.empty());
        read = Parse(L"anthropic", with(investigating, maintenance));
        CHECK(read && read->title == L"Elevated errors on Claude Code");
    });

    Run("dates: ISO 8601 with seconds and a zone, from 2020 to 2100; anything else is unknown", [] {
        auto since = [](std::string_view startedAt) {
            std::string page = Claude(kClaudeCode, "partial_outage");
            // Without the component's last change, an unusable start leaves the time unknown.
            page = Replaced(page, R"("updated_at":"2026-10-28T20:05:18.151Z")", R"("updated_at":"")");
            page = Replaced(page, R"("incidents":[])",
                            R"("incidents":[{"name":"x","status":"investigating","started_at":")" + std::string(startedAt) +
                                R"(","components":[{"id":"yyzkbfz2thpt"}]}])");
            std::optional<ProviderOutage> read = Parse(L"anthropic", page);
            return read ? read->sinceMs : ~0ULL;
        };
        CHECK(since("2026-11-05T13:05:00Z") == 1793883900000ULL);
        CHECK(since("2026-11-05T06:05:00-07:00") == 1793883900000ULL);
        CHECK(since("2026-11-05T18:35:00+05:30") == 1793883900000ULL);
        CHECK(since("2026-11-05T13:05:00.250Z") == 1793883900250ULL);
        for (std::string_view bad : {"2026-11-05T13:05:00", "2026-11-05T13:05:00garbage", "2026-11-05T13:05:00+99:99",
                                     "2026-11-05T13:05Z", "2026-13-05T13:05:00Z", "2026-11-05 13:05:00Z",
                                     "2026-11-05T13:05:00.Z", "1999-12-31T23:59:59Z", "2100-01-01T00:00:00Z", ""}) {
            CHECK(since(bad) == 0);
        }
    });

    Run("Statuspage: pages that can't be trusted show nothing", [] {
        std::wstring problem;
        auto fails = [&](const std::string& page, std::wstring_view why) {
            problem.clear();
            return !Parse(L"anthropic", page, &problem) && problem == why;
        };
        CHECK(fails("", L"invalid JSON"));
        CHECK(fails("{\"components\":", L"invalid JSON"));
        CHECK(fails("[]", L"invalid JSON"));
        CHECK(fails("{}", L"no components"));
        CHECK(fails(R"({"components":{}})", L"no components"));
        CHECK(fails(R"({"components":[{"id":"other","status":"major_outage"}]})", L"no watched component"));
        CHECK(fails(Replaced(Sample("claude-summary.json"), kClaudeCode, R"("id":"yyzkbfz2thpt","name":"Claude Code","status":3)"),
                    L"component status not a string"));
        // A watched component the page no longer lists leaves the others.
        std::optional<ProviderOutage> read =
            Parse(L"anthropic", R"({"components":[{"id":"k8w3r06qmzrp","name":"Claude API","status":"degraded_performance"}]})");
        CHECK(read && read->level == OutageLevel::Degraded && read->component == L"Claude API");
    });

    Run("incident.io: levels only from the components listed as affected now, in the watched group", [] {
        const std::pair<std::string_view, OutageLevel> levels[] = {{"degraded_performance", OutageLevel::Degraded},
                                                                   {"partial_outage", OutageLevel::Partial},
                                                                   {"full_outage", OutageLevel::Major},
                                                                   {"under_maintenance", OutageLevel::Maintenance}};
        for (auto [status, level] : levels) {
            std::optional<ProviderOutage> read = Parse(L"openai", OpenAi(Affected(kCodexApi, status)));
            CHECK(read && read->level == level && read->component == L"Codex API" && read->others == 0 &&
                  read->title.empty() && read->sinceMs == 0);
        }
        std::optional<ProviderOutage> read =
            Parse(L"openai", OpenAi(Affected(kCodexCli, "degraded_performance") + "," + Affected(kCodexApi, "full_outage")));
        CHECK(read && read->level == OutageLevel::Major && read->component == L"Codex API" && read->others == 1);
        // Listed twice: one component.
        read = Parse(L"openai", OpenAi(Affected(kCodexCli, "degraded_performance") + "," + Affected(kCodexCli, "partial_outage")));
        CHECK(read && read->level == OutageLevel::Partial && read->component == L"CLI" && read->others == 0);
        // Outside the group, including Codex in ChatGPT Desktop.
        CHECK(Parse(L"openai", OpenAi(Affected(kConversations, "full_outage"))) == ProviderOutage{});
        CHECK(Parse(L"openai", OpenAi(Affected(kCodexDesktop, "full_outage"))) == ProviderOutage{});
        // An ongoing incident's components never name a level: theirs keep its worst impact.
        CHECK(Parse(L"openai", OpenAi("", HashiCorpIncident())) == ProviderOutage{});
    });

    Run("incident.io: an ongoing incident whose current impact covers the worst component titles it", [] {
        std::optional<ProviderOutage> read = Parse(L"openai", OpenAi(Affected(kCodexApi, "degraded_performance"), HashiCorpIncident()));
        CHECK(read && read->level == OutageLevel::Degraded &&
              read->title == L"HCP Terraform \u2013 Elevated 502 Bad Gateway Errors" && NearMs(read->sinceMs, 1793866422056ULL));
        // Recovered on its component (still ongoing, in monitoring): no title.
        read = Parse(L"openai", OpenAi(Affected(kCodexApi, "degraded_performance"), HashiCorpIncident("operational")));
        CHECK(read && read->level == OutageLevel::Degraded && read->title.empty());
        // On another watched component than the worst one: no title.
        read = Parse(L"openai", OpenAi(Affected(kCodexApi, "degraded_performance") + "," + Affected(kCodexCli, "full_outage"),
                                       HashiCorpIncident()));
        CHECK(read && read->component == L"CLI" && read->title.empty());
        // Its start is its open impact's on the component, not when it was posted.
        std::string closed = Replaced(HashiCorpIncident(), R"("start_at":"2026-11-05T08:13:42.056Z","id")",
                                      R"("start_at":"2026-11-05T08:13:42.056Z","end_at":"2026-11-05T09:00:00Z","id")");
        read = Parse(L"openai", OpenAi(Affected(kCodexApi, "degraded_performance"), closed));
        CHECK(read && !read->title.empty() && read->sinceMs == 0);
    });

    Run("incident.io: Supabase's incident in monitoring after recovery shows nothing", [] {
        // Its components (us-east-1 and eu-central-1 of the Storage group) still carry the incident's
        // degraded impact, but their current status, like the page's affected list, says operational.
        const ProviderStatusSource storage{L"supabase", L"status.supabase.com", L"/proxy/status.supabase.com",
                                           L"https://status.supabase.com", StatusFeedKind::IncidentIo,
                                           {L"01M32R99QD6031NTQDBBP6NCEF", L"Storage", nullptr}, kStatusIntervalMs, false};
        const std::string page = Sample("supabase-incidentio.json");
        std::wstring problem;
        std::optional<ProviderOutage> read = ParseProviderStatus(storage, page, &problem);
        CHECK(read && *read == ProviderOutage{} && problem.empty());
        // Listed as affected now: shown, without the recovered incident's title.
        read = ParseProviderStatus(storage, Replaced(page, R"("affected_components":[])",
                                                     "\"affected_components\":[" +
                                                         Affected("01M32S9NWPXWX4RVRAYF06CAS6", "degraded_performance") + "]"));
        CHECK(read && read->level == OutageLevel::Degraded && read->component == L"us-east-1" && read->title.empty());
    });

    Run("incident.io: the group by id, else by name; hidden components aside; untrusted pages", [] {
        std::string renamed = Replaced(OpenAi(Affected(kCodexCli, "partial_outage")), R"("id":"01KMKF9EBTCD8BN9PG8DJZXRSQ")",
                                       R"("id":"01ZZZZZZZZZZZZZZZZZZZZZZZZ")");
        std::optional<ProviderOutage> read = Parse(L"openai", renamed);
        CHECK(read && read->level == OutageLevel::Partial && read->component == L"CLI");
        std::wstring problem;
        CHECK(!Parse(L"openai", Replaced(renamed, R"("name":"Codex")", R"("name":"Codex tools")"), &problem) &&
              problem == L"no watched group");
        std::string hidden = Replaced(OpenAi(Affected(kCodexCli, "partial_outage")),
                                      R"("name":"CLI","hidden":false)", R"("name":"CLI","hidden":true)");
        CHECK(Parse(L"openai", hidden) == ProviderOutage{});
        CHECK(!Parse(L"openai", Replaced(Sample("openai-proxy.json"), R"("affected_components":[],)", ""), &problem) &&
              problem == L"no affected components");
        CHECK(!Parse(L"openai", OpenAi(R"({"component_id":"01KMKFAMWKNQ84Z1766MV08ZDE","status":null})"), &problem) &&
              problem == L"affected component status not a string");
        CHECK(!Parse(L"openai", R"({"summary":{}})", &problem) && problem == L"no structure");
        CHECK(!Parse(L"openai", "{}", &problem) && problem == L"no structure");
        // Components without an id are neither watched nor matched; a hidden flag that isn't false
        // leaves its component out.
        CHECK(!Parse(L"openai", R"({"summary":{"structure":{"items":[{"group":{"id":"01KMKF9EBTCD8BN9PG8DJZXRSQ",)"
                                R"("components":[{"component_id":""}]}}]},"affected_components":[{"status":"full_outage"}]}})",
                     &problem) &&
              problem == L"no watched component");
        CHECK(Parse(L"openai", OpenAi(R"({"status":"full_outage"})")) == ProviderOutage{});
        CHECK(Parse(L"openai", Replaced(OpenAi(Affected(kCodexCli, "full_outage")), R"("name":"CLI","hidden":false)",
                                        R"("name":"CLI","hidden":"false")")) == ProviderOutage{});
    });

    Run("incident.io: a maintenance titles the worst component only while in progress", [] {
        auto maintenance = [](std::string_view status) {
            std::string id(kCodexApi);
            return R"({"component_impacts":[{"start_at":"2026-11-05T09:30:00Z","end_at":"2026-11-05T12:00:00Z",)"
                   R"("component_id":")" + id + R"(","status":"under_maintenance"}],"published_at":"2026-10-28T21:58:20.325Z",)"
                   R"("id":"m1","name":"Codex maintenance","status":")" + std::string(status) +
                   R"(","affected_components":[{"component_id":")" + id +
                   R"(","status":"under_maintenance","current_status":"under_maintenance"}],"type":"maintenance"})";
        };
        auto page = [](const std::string& event) {
            return Replaced(OpenAi(Affected(kCodexApi, "under_maintenance")), R"("scheduled_maintenances":[])",
                            "\"scheduled_maintenances\":[" + event + "]");
        };
        std::optional<ProviderOutage> read = Parse(L"openai", page(maintenance("maintenance_in_progress")));
        CHECK(read && read->level == OutageLevel::Maintenance && read->title == L"Codex maintenance" &&
              NearMs(read->sinceMs, 1793871000000ULL));
        read = Parse(L"openai", page(maintenance("maintenance_scheduled")));
        CHECK(read && read->level == OutageLevel::Maintenance && read->title.empty() && read->sinceMs == 0);
        // Listed among the ongoing incidents, the same.
        read = Parse(L"openai", OpenAi(Affected(kCodexApi, "under_maintenance"), maintenance("maintenance_scheduled")));
        CHECK(read && read->title.empty());
        read = Parse(L"openai", OpenAi(Affected(kCodexApi, "under_maintenance"), maintenance("maintenance_in_progress")));
        CHECK(read && read->title == L"Codex maintenance");
        // A resolved incident doesn't title it either.
        read = Parse(L"openai", OpenAi(Affected(kCodexApi, "degraded_performance"),
                                       Replaced(HashiCorpIncident(), R"("status":"investigating")", R"("status":"resolved")")));
        CHECK(read && read->title.empty());
    });

    Run("OpenCode: the Go service's level, start, reason and the models that aren't operational", [] {
        std::optional<ProviderOutage> read =
            Parse(L"opencode", OpenCode(R"({"service":"go","status":"degraded","reason":"latency","since":1793863048523)"));
        CHECK(read && read->level == OutageLevel::Degraded && read->component == L"OpenCode Go" &&
              read->title == L"Slow responses" && read->sinceMs == 1793863048523ULL && read->others == 0);
        // Its first listed degraded model (the Go breakdown comes before pay-as-you-go's).
        std::string model = Replaced(OpenCode(R"({"service":"go","status":"degraded","reason":"errors","since":1793863048523)"),
                                     R"({"id":"deepseek-v4.1-flash","status":"operational")",
                                     R"({"id":"deepseek-v4.1-flash","status":"degraded")");
        read = Parse(L"opencode", model);
        CHECK(read && read->title == L"Elevated errors: deepseek-v4.1-flash");
        const std::pair<std::string_view, OutageLevel> levels[] = {{"outage", OutageLevel::Major},
                                                                   {"maintenance", OutageLevel::Maintenance}};
        for (auto [status, level] : levels) {
            read = Parse(L"opencode", OpenCode(R"({"service":"go","status":")" + std::string(status) +
                                               R"(","reason":"availability","since":1793863048523)"));
            CHECK(read && read->level == level && read->title == L"Availability issues");
        }
        // Pay-as-you-go isn't watched.
        read = Parse(L"opencode", Replaced(Sample("opencode-status.json"), R"({"service":"payg","status":"operational")",
                                           R"({"service":"payg","status":"degraded")"));
        CHECK(read && *read == ProviderOutage{});
        std::wstring problem;
        read = Parse(L"opencode", OpenCode(R"({"service":"go","status":"wobbly","reason":"none","since":1793863048523)"), &problem);
        CHECK(read && read->level == OutageLevel::None && problem == L"unknown status \"wobbly\"");
    });

    Run("OpenCode: an ongoing incident of the service fills in a missing start and reason", [] {
        std::string page = OpenCode(R"({"service":"go","status":"degraded","reason":"none","since":null)");
        std::string ongoing = R"({"id":"go-1","event":"go-1","service":"go","startedAt":1793866000000,"resolvedAt":null,)"
                              R"("status":"degraded","reason":"errors","models":[{"id":"glm-5.3-flash","reason":"errors"}]},)";
        std::optional<ProviderOutage> read = Parse(L"opencode", Replaced(page, R"("incidents":[)", R"("incidents":[)" + ongoing));
        CHECK(read && read->level == OutageLevel::Degraded && read->title == L"Elevated errors: glm-5.3-flash" &&
              read->sinceMs == 1793866000000ULL);
        // Without a resolution time at all, too; a resolved one (every captured incident) doesn't.
        std::string unresolved = Replaced(ongoing, R"("resolvedAt":null,)", "");
        read = Parse(L"opencode", Replaced(page, R"("incidents":[)", R"("incidents":[)" + unresolved));
        CHECK(read && read->title == L"Elevated errors: glm-5.3-flash");
        read = Parse(L"opencode", page);
        CHECK(read && read->level == OutageLevel::Degraded && read->title.empty() && read->sinceMs == 0);
    });

    Run("OpenCode: a stale or mistyped page shows nothing", [] {
        const std::string degraded = OpenCode(R"({"service":"go","status":"degraded","reason":"latency","since":1793863048523)");
        std::wstring problem;
        CHECK(!Parse(L"opencode", Replaced(degraded, R"("stale":false)", R"("stale":true)"), &problem) &&
              problem == L"page reports stale data");
        CHECK(!Parse(L"opencode", Replaced(degraded, R"("stale":false)", R"("stale":"true")"), &problem) &&
              problem == L"stale flag not a boolean");
        CHECK(!Parse(L"opencode", Replaced(degraded, R"({"service":"go",)", R"({"service":"go2",)"), &problem) &&
              problem == L"no watched component");
        CHECK(!Parse(L"opencode", OpenCode(R"({"service":"go","status":2,"reason":"latency","since":1793863048523)"), &problem) &&
              problem == L"component status not a string");
        CHECK(!Parse(L"opencode", R"({"stale":false})", &problem) && problem == L"no components");
        // A stale flag left out is no claim of staleness.
        CHECK(Parse(L"opencode", Replaced(degraded, R"("stale":false,)", ""))->level == OutageLevel::Degraded);
    });

    Run("reads: a failure or an operational page clears at once; OpenCode shows from a second agreeing read", [] {
        const ProviderStatusSource& claude = Source(L"anthropic");
        const ProviderStatusSource& opencode = Source(L"opencode");
        const ProviderOutage degraded{OutageLevel::Degraded, L"Go", 0, L"", 0};
        const ProviderOutage major{OutageLevel::Major, L"Go", 0, L"", 0};
        const std::optional<ProviderOutage> failed, operational = ProviderOutage{};
        constexpr ULONGLONG kMin = 60000, t0 = 1000000;
        ProviderStatusTrack track;
        CHECK(ShownAfterRead(claude, track, degraded, t0) == degraded && track.delayMs == 5 * kMin);
        CHECK(ShownAfterRead(claude, track, failed, t0 + 5 * kMin) == ProviderOutage{});
        CHECK(ShownAfterRead(claude, track, degraded, t0 + 10 * kMin) == degraded);
        CHECK(ShownAfterRead(claude, track, operational, t0 + 15 * kMin) == ProviderOutage{});

        // The first non-operational read shows nothing and asks for the next in 3 minutes; the
        // second shows the latest; it stays while they agree; clearing is immediate.
        track = {};
        CHECK(ShownAfterRead(opencode, track, degraded, t0) == ProviderOutage{} && track.delayMs == 3 * kMin);
        CHECK(ShownAfterRead(opencode, track, major, t0 + 3 * kMin) == major && track.delayMs == 10 * kMin);
        CHECK(ShownAfterRead(opencode, track, degraded, t0 + 13 * kMin) == degraded && track.streak == 2);
        CHECK(ShownAfterRead(opencode, track, operational, t0 + 23 * kMin) == ProviderOutage{} && track.delayMs == 10 * kMin);
        CHECK(ShownAfterRead(opencode, track, degraded, t0 + 33 * kMin) == ProviderOutage{});
        // A failed read between two breaks the run.
        track = {};
        ShownAfterRead(opencode, track, degraded, t0);
        CHECK(ShownAfterRead(opencode, track, failed, t0 + 3 * kMin) == ProviderOutage{} && track.delayMs == 10 * kMin);
        CHECK(ShownAfterRead(opencode, track, degraded, t0 + 13 * kMin) == ProviderOutage{});
        // So does a gap of more than two intervals: that read starts a new run, confirmed 3 minutes on.
        track = {};
        ShownAfterRead(opencode, track, degraded, t0);
        CHECK(ShownAfterRead(opencode, track, degraded, t0 + 20 * kMin + 1) == ProviderOutage{} && track.streak == 1);
        CHECK(ShownAfterRead(opencode, track, degraded, t0 + 23 * kMin) == degraded);
        track = {};
        ShownAfterRead(opencode, track, degraded, t0);
        CHECK(ShownAfterRead(opencode, track, degraded, t0 + 20 * kMin) == degraded);
    });

    Run("age: a reading shows for two of its source's intervals", [] {
        const ProviderStatusSource& claude = Source(L"anthropic");
        const ProviderStatusSource& opencode = Source(L"opencode");
        constexpr ULONGLONG kMin = 60000, t0 = 1000000;
        CHECK(!ProviderStatusCurrent(claude, 0, t0));
        CHECK(ProviderStatusCurrent(claude, t0, t0) && ProviderStatusCurrent(claude, t0, t0 + 10 * kMin));
        CHECK(!ProviderStatusCurrent(claude, t0, t0 + 10 * kMin + 1));
        CHECK(ProviderStatusCurrent(opencode, t0, t0 + 20 * kMin) && !ProviderStatusCurrent(opencode, t0, t0 + 20 * kMin + 1));
    });
}
