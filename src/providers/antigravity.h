// Google Antigravity local language-server discovery and quota queries.

enum class AntigravityQuotaSource {
    Summary,
    UserStatus,
};

struct AntigravityServerInfo {
    int port = 0;
    bool secure = true;
    std::wstring csrfToken;
    AntigravityQuotaSource quotaSource = AntigravityQuotaSource::Summary;
    ULONGLONG discoveredMs = 0;
};

static AntigravityServerInfo g_antigravityCachedInfo;

// Parse --flag value and --flag=value, including quoted values.
static std::wstring ParseCmdLineFlag(const std::wstring& cmdLine, const std::wstring& flag) {
    const std::wstring needle = L"--" + flag;
    size_t searchFrom = 0;
    while (true) {
        size_t pos = cmdLine.find(needle, searchFrom);
        if (pos == std::wstring::npos) return {};
        size_t after = pos + needle.size();
        bool startsToken = pos == 0 || iswspace(cmdLine[pos - 1]);
        bool hasSeparator = after < cmdLine.size() &&
                            (cmdLine[after] == L'=' || iswspace(cmdLine[after]));
        if (!startsToken || !hasSeparator) {
            searchFrom = after;
            continue;
        }

        size_t valStart = after;
        if (cmdLine[valStart] == L'=') valStart++;
        while (valStart < cmdLine.size() && iswspace(cmdLine[valStart])) valStart++;
        if (valStart == cmdLine.size()) return {};

        wchar_t quote = 0;
        if (cmdLine[valStart] == L'"' || cmdLine[valStart] == L'\'') {
            quote = cmdLine[valStart++];
        }
        size_t valEnd = valStart;
        if (quote) {
            valEnd = cmdLine.find(quote, valStart);
            if (valEnd == std::wstring::npos) return {};
        } else {
            while (valEnd < cmdLine.size() && !iswspace(cmdLine[valEnd])) valEnd++;
        }
        return cmdLine.substr(valStart, valEnd - valStart);
    }
}

// Querying ntdll avoids slow, non-cancellable WMI calls during Explorer unload.
static std::wstring GetProcessCommandLine(DWORD pid) {
    std::wstring result;
    if (g_unloading) return result;

    using NtQueryInformationProcess_t = NTSTATUS(NTAPI*)(
        HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    static auto ntQueryInformationProcess = reinterpret_cast<NtQueryInformationProcess_t>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (!ntQueryInformationProcess) return result;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return result;

    constexpr PROCESSINFOCLASS kProcessCommandLineInformation =
        static_cast<PROCESSINFOCLASS>(60);
    ULONG size = 4096;
    for (int attempt = 0; attempt < 2 && !g_unloading; attempt++) {
        std::vector<BYTE> buffer(size);
        ULONG required = 0;
        NTSTATUS status = ntQueryInformationProcess(
            process, kProcessCommandLineInformation, buffer.data(), size, &required);
        if (status >= 0) {
            auto* commandLine = reinterpret_cast<UNICODE_STRING*>(buffer.data());
            uintptr_t begin = reinterpret_cast<uintptr_t>(buffer.data());
            uintptr_t text = reinterpret_cast<uintptr_t>(commandLine->Buffer);
            uintptr_t end = begin + buffer.size();
            if (commandLine->Buffer && commandLine->Length % sizeof(wchar_t) == 0 &&
                text >= begin && text <= end && commandLine->Length <= end - text) {
                result.assign(commandLine->Buffer, commandLine->Length / sizeof(wchar_t));
            }
            break;
        }
        if (required <= size || required > 1024 * 1024) break;
        size = required;
    }

    CloseHandle(process);
    return result;
}

// Find all TCP listeners owned by an Antigravity process. The explicit command-line
// ports are preferred, but current and older builds don't expose the same flags.
static std::vector<int> FindAntigravityListeningPorts(DWORD pid) {
    std::vector<int> ports;
    for (int attempt = 0; attempt < 3 && !g_unloading; attempt++) {
        ULONG size = 0;
        DWORD rc = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                                       TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (rc != ERROR_INSUFFICIENT_BUFFER || size == 0) break;

        std::vector<BYTE> buffer(size);
        rc = GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET,
                                 TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (rc == ERROR_INSUFFICIENT_BUFFER) continue;
        if (rc != NO_ERROR) break;

        auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; i++) {
            if (table->table[i].dwOwningPid == pid) {
                ports.push_back(ntohs((u_short)table->table[i].dwLocalPort));
            }
        }
        break;
    }

    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

static HttpResult HttpRequestLocal(int port, bool secure, PCWSTR path, PCWSTR csrfToken,
                                   DWORD timeoutMs);
static bool ParseAntigravityQuotaSummary(const std::string& body, AccountData* d,
                                         std::wstring* error);
static bool ParseAntigravityUserStatus(const std::string& body, AccountData* d,
                                       std::wstring* error);

static bool DiscoverAntigravityServer(AntigravityServerInfo* info, bool* foundServer = nullptr) {
    *info = {};
    if (foundServer) *foundServer = false;
    if (g_unloading) return false;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    struct Candidate {
        DWORD pid;
        std::wstring mainCsrfToken;
        std::wstring extensionCsrfToken;
        std::vector<int> explicitMainPorts;
        int extensionPort = 0;
        bool cli = false;
        bool richQuotaLikely = false;
    };
    std::vector<Candidate> candidates;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD currentSessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId)) {
        CloseHandle(snap);
        return false;
    }

    if (Process32FirstW(snap, &pe)) {
        do {
            if (g_unloading) break;
            std::wstring exeName = pe.szExeFile;
            std::transform(exeName.begin(), exeName.end(), exeName.begin(),
                           [](wchar_t ch) { return (wchar_t)towlower(ch); });
            bool cli = exeName == L"agy.exe" || exeName == L"antigravity-cli.exe" ||
                       exeName == L"antigravity_cli.exe";
            bool languageServer = exeName.rfind(L"language_server", 0) == 0 ||
                                  exeName == L"language-server.exe";
            if (!cli && !languageServer) {
                continue;
            }
            DWORD processSessionId = 0;
            if (!ProcessIdToSessionId(pe.th32ProcessID, &processSessionId) ||
                processSessionId != currentSessionId) {
                continue;
            }

            std::wstring cmdLine = GetProcessCommandLine(pe.th32ProcessID);
            if (cmdLine.empty()) continue;
            std::wstring lowerCmdLine = cmdLine;
            std::transform(lowerCmdLine.begin(), lowerCmdLine.end(), lowerCmdLine.begin(),
                           [](wchar_t ch) { return (wchar_t)towlower(ch); });
            if (languageServer &&
                (lowerCmdLine.find(L"antigravity-cli") != std::wstring::npos ||
                 lowerCmdLine.find(L"antigravity_cli") != std::wstring::npos)) {
                cli = true;
            }
            if (!cli && lowerCmdLine.find(L"antigravity") == std::wstring::npos) continue;

            std::wstring mainCsrf;
            for (PCWSTR flag : std::array<PCWSTR, 2>{L"csrf_token", L"csrf-token"}) {
                mainCsrf = ParseCmdLineFlag(cmdLine, flag);
                if (!mainCsrf.empty()) break;
            }
            std::wstring extensionCsrf;
            for (PCWSTR flag : std::array<PCWSTR, 2>{
                     L"extension_server_csrf_token", L"extension-server-csrf-token"}) {
                extensionCsrf = ParseCmdLineFlag(cmdLine, flag);
                if (!extensionCsrf.empty()) break;
            }
            if ((!mainCsrf.empty() && mainCsrf.find_first_of(L"\r\n") != std::wstring::npos) ||
                (!extensionCsrf.empty() &&
                 extensionCsrf.find_first_of(L"\r\n") != std::wstring::npos)) {
                continue;
            }
            if (!cli && mainCsrf.empty() && extensionCsrf.empty()) continue;

            Candidate candidate;
            candidate.pid = pe.th32ProcessID;
            candidate.mainCsrfToken = std::move(mainCsrf);
            candidate.extensionCsrfToken = std::move(extensionCsrf);
            candidate.cli = cli;
            bool ide = lowerCmdLine.find(L"antigravity-ide") != std::wstring::npos ||
                       lowerCmdLine.find(L"antigravity ide") != std::wstring::npos ||
                       lowerCmdLine.find(L"\\extensions\\antigravity\\") != std::wstring::npos ||
                       lowerCmdLine.find(L"/extensions/antigravity/") != std::wstring::npos;
            candidate.richQuotaLikely = cli || !ide;
            for (PCWSTR flag : std::array<PCWSTR, 4>{
                     L"https_server_port", L"server_port",
                     L"https-server-port", L"server-port"}) {
                std::wstring value = ParseCmdLineFlag(cmdLine, flag);
                if (value.empty()) continue;
                wchar_t* end = nullptr;
                unsigned long port = wcstoul(value.c_str(), &end, 10);
                if (end && !*end && port > 0 && port <= 65535 &&
                    std::find(candidate.explicitMainPorts.begin(),
                              candidate.explicitMainPorts.end(), (int)port) ==
                        candidate.explicitMainPorts.end()) {
                    candidate.explicitMainPorts.push_back((int)port);
                }
            }
            for (PCWSTR flag : std::array<PCWSTR, 2>{
                     L"extension_server_port", L"extension-server-port"}) {
                std::wstring value = ParseCmdLineFlag(cmdLine, flag);
                if (value.empty()) continue;
                wchar_t* end = nullptr;
                unsigned long port = wcstoul(value.c_str(), &end, 10);
                if (end && !*end && port > 0 && port <= 65535) {
                    candidate.extensionPort = (int)port;
                    break;
                }
            }
            candidates.push_back(std::move(candidate));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         return a.richQuotaLikely && !b.richQuotaLikely;
                     });

    AntigravityServerInfo legacyInfo;
    for (auto& candidate : candidates) {
        if (g_unloading) return false;
        if (!candidate.richQuotaLikely && legacyInfo.port > 0) {
            *info = std::move(legacyInfo);
            return true;
        }

        struct Endpoint {
            int port;
            bool secure;
            const std::wstring* csrfToken;
        };
        std::vector<Endpoint> endpoints;
        std::vector<int> ports = candidate.explicitMainPorts;
        ports.erase(std::remove(ports.begin(), ports.end(), candidate.extensionPort),
                    ports.end());
        for (int port : FindAntigravityListeningPorts(candidate.pid)) {
            if (port == candidate.extensionPort) continue;
            if (std::find(ports.begin(), ports.end(), port) == ports.end()) ports.push_back(port);
        }
        if (candidate.cli || !candidate.mainCsrfToken.empty()) {
            for (int port : ports) {
                endpoints.push_back({port, true, &candidate.mainCsrfToken});
                endpoints.push_back({port, false, &candidate.mainCsrfToken});
            }
        }
        if (candidate.extensionPort > 0 && !candidate.extensionCsrfToken.empty()) {
            endpoints.push_back(
                {candidate.extensionPort, false, &candidate.extensionCsrfToken});
        }

        for (const auto& endpoint : endpoints) {
            if (g_unloading) return false;
            HttpResult quota = HttpRequestLocal(
                endpoint.port, endpoint.secure,
                L"/exa.language_server_pb.LanguageServerService/RetrieveUserQuotaSummary",
                endpoint.csrfToken->c_str(), 1500);
            if (!quota.ok) continue;
            if (foundServer) *foundServer = true;

            AccountData probeData;
            if (quota.status == 200 &&
                ParseAntigravityQuotaSummary(quota.body, &probeData, nullptr)) {
                info->port = endpoint.port;
                info->secure = endpoint.secure;
                info->csrfToken = *endpoint.csrfToken;
                info->quotaSource = AntigravityQuotaSource::Summary;
                return true;
            }

            HttpResult status = HttpRequestLocal(
                endpoint.port, endpoint.secure,
                L"/exa.language_server_pb.LanguageServerService/GetUserStatus",
                endpoint.csrfToken->c_str(), 1500);
            if (!status.ok) continue;
            if (foundServer) *foundServer = true;
            probeData = {};
            if (status.status == 200 &&
                ParseAntigravityUserStatus(status.body, &probeData, nullptr) &&
                (probeData.win5h.pct >= 0 || probeData.winWeek.pct >= 0 ||
                 probeData.antigravityThirdParty5h.pct >= 0 ||
                 probeData.antigravityThirdPartyWeek.pct >= 0)) {
                legacyInfo.port = endpoint.port;
                legacyInfo.secure = endpoint.secure;
                legacyInfo.csrfToken = *endpoint.csrfToken;
                legacyInfo.quotaSource = AntigravityQuotaSource::UserStatus;
                if (!candidate.richQuotaLikely) {
                    *info = std::move(legacyInfo);
                    return true;
                }
            }
        }
    }
    if (legacyInfo.port > 0) {
        *info = std::move(legacyInfo);
        return true;
    }
    return false;
}

// The language server is loopback-only and may use a self-signed HTTPS certificate.
static HttpResult HttpRequestLocal(int port, bool secure, PCWSTR path, PCWSTR csrfToken,
                                   DWORD timeoutMs) {
    HttpResult res;
    HINTERNET ses = WinHttpOpen(L"taskbar-ai-quota/0.11", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (ses && !TrackHttpHandle(ses)) ses = nullptr;
    HINTERNET con = nullptr;
    HINTERNET req = nullptr;

    if (ses && !g_unloading) {
        WinHttpSetTimeouts(ses, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
        con = WinHttpConnect(ses, L"127.0.0.1", (INTERNET_PORT)port, 0);
        if (con && !TrackHttpHandle(con)) con = nullptr;
        req = con ? WinHttpOpenRequest(con, L"POST", path, nullptr,
                                         WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         secure ? WINHTTP_FLAG_SECURE : 0)
                  : nullptr;
        if (req && !TrackHttpHandle(req)) req = nullptr;
    }

    if (req && secure) {
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                      SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    }

    std::string body =
        R"({"metadata":{"ideName":"antigravity","extensionName":"antigravity","ideVersion":"unknown","locale":"en"}})";
    std::wstring headers = L"Content-Type: application/json\r\nConnect-Protocol-Version: 1\r\n";
    if (csrfToken && *csrfToken) {
        headers += L"X-Codeium-Csrf-Token: ";
        headers += csrfToken;
        headers += L"\r\n";
    }

    if (!g_unloading && req &&
        WinHttpSendRequest(req, headers.c_str(), (DWORD)headers.size(),
                           (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                            WINHTTP_NO_HEADER_INDEX);
        res.status = (int)status;

        bool bodyOk = true;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(req, &available)) { bodyOk = false; break; }
            if (!available) break;
            size_t prev = res.body.size();
            if (available > 2 * 1024 * 1024 - prev) { bodyOk = false; break; }
            res.body.resize(prev + available);
            DWORD read = 0;
            if (!WinHttpReadData(req, res.body.data() + prev, available, &read)) {
                res.body.resize(prev); bodyOk = false; break;
            }
            res.body.resize(prev + read);
            if (!read) break;
        }
        res.ok = bodyOk;
    }

    if (req && UntrackHttpHandle(req)) WinHttpCloseHandle(req);
    if (con && UntrackHttpHandle(con)) WinHttpCloseHandle(con);
    if (ses && UntrackHttpHandle(ses)) WinHttpCloseHandle(ses);
    return res;
}

static bool ParseAntigravityQuotaSummary(const std::string& body, AccountData* d, std::wstring* error) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        JsonObject summary = root;
        if (auto response = GetObj(root, L"response")) summary = response;
        else if (auto wrapped = GetObj(root, L"summary")) summary = wrapped;

        if (!summary.HasKey(L"groups")) {
            if (error) *error = L"no quota groups in response";
            return false;
        }
        auto groupsVal = summary.GetNamedValue(L"groups");
        if (groupsVal.ValueType() != JsonValueType::Array) {
            if (error) *error = L"groups is not an array";
            return false;
        }

        auto groups = groupsVal.GetArray();
        bool parsed = false;
        auto endsWith = [](const std::wstring& value, PCWSTR suffix) {
            size_t suffixLength = wcslen(suffix);
            return value.size() >= suffixLength &&
                   value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
        };
        for (uint32_t i = 0; i < groups.Size(); ++i) {
            if (groups.GetAt(i).ValueType() != JsonValueType::Object) continue;
            auto grp = groups.GetAt(i).GetObject();
            std::wstring dispName = GetStr(grp, L"displayName");
            std::transform(dispName.begin(), dispName.end(), dispName.begin(),
                           [](wchar_t ch) { return (wchar_t)towlower(ch); });
            bool geminiGroup = dispName.find(L"gemini") != std::wstring::npos;
            bool thirdPartyGroup = dispName.find(L"claude") != std::wstring::npos ||
                                   dispName.find(L"gpt") != std::wstring::npos;

            if (!grp.HasKey(L"buckets")) continue;
            auto bucketsVal = grp.GetNamedValue(L"buckets");
            if (bucketsVal.ValueType() != JsonValueType::Array) continue;

            auto buckets = bucketsVal.GetArray();
            for (uint32_t j = 0; j < buckets.Size(); ++j) {
                if (buckets.GetAt(j).ValueType() != JsonValueType::Object) continue;
                auto bucket = buckets.GetAt(j).GetObject();
                if (GetBool(bucket, L"disabled")) continue;
                std::wstring bucketId = GetStr(bucket, L"bucketId");
                std::wstring window = GetStr(bucket, L"window");
                std::transform(bucketId.begin(), bucketId.end(), bucketId.begin(),
                               [](wchar_t ch) { return (wchar_t)towlower(ch); });
                std::transform(window.begin(), window.end(), window.begin(),
                               [](wchar_t ch) { return (wchar_t)towlower(ch); });
                bool isWeekly = window == L"weekly" || endsWith(bucketId, L"-weekly");
                bool is5h = window == L"5h" || endsWith(bucketId, L"-5h");
                bool is3h = window == L"3h" || endsWith(bucketId, L"-3h");
                bool isHourly = window == L"hourly" || window == L"1h" ||
                                endsWith(bucketId, L"-hourly") ||
                                endsWith(bucketId, L"-1h");
                bool isSession = window == L"session" || endsWith(bucketId, L"-session");
                bool isShort = is5h || is3h || isHourly || isSession;
                if (isWeekly) isShort = false;
                if (!isShort && !isWeekly) continue;

                bool geminiBucket = bucketId.rfind(L"gemini-", 0) == 0 ||
                                     (bucketId.rfind(L"3p-", 0) != 0 && geminiGroup);
                bool thirdPartyBucket = bucketId.rfind(L"3p-", 0) == 0 ||
                                         (!geminiBucket && thirdPartyGroup);
                WindowUsage* target = nullptr;
                if (geminiBucket) {
                    target = isShort ? &d->win5h : &d->winWeek;
                } else if (thirdPartyBucket) {
                    target = isShort ? &d->antigravityThirdParty5h :
                                       &d->antigravityThirdPartyWeek;
                }
                if (!target) continue;

                double remaining = GetNum(bucket, L"remainingFraction", -1);
                std::wstring resetTimeStr = GetStr(bucket, L"resetTime");
                if (auto nested = GetObj(bucket, L"remaining")) {
                    if (remaining < 0) remaining = GetNum(nested, L"remainingFraction", -1);
                    if (remaining < 0 && GetStr(nested, L"case") == L"remainingFraction") {
                        remaining = GetNum(nested, L"value", -1);
                    }
                    if (resetTimeStr.empty()) resetTimeStr = GetStr(nested, L"resetTime");
                }
                if (!std::isfinite(remaining) || remaining < 0 || remaining > 1) continue;

                ULONGLONG resetMs = ParseIso8601Ms(resetTimeStr);
                double usedPct = std::clamp((1.0 - remaining) * 100.0, 0.0, 100.0);
                // Duplicate rows can occur during model rollouts; keep the limiting pool.
                if (target->pct < usedPct) {
                    target->pct = usedPct;
                    target->resetUnixMs = resetMs;
                    target->windowDurationMs = isWeekly ? kWeeklyWindowMs :
                                               is5h ? kFiveHourWindowMs :
                                               is3h ? 3ULL * 60 * 60 * 1000 :
                                               isHourly ? 60ULL * 60 * 1000 : 0;
                }
                parsed = true;
            }
        }

        if (!parsed && error) {
            *error = L"no usable Antigravity quota buckets in response";
        }
        return parsed;
    } catch (...) {
        if (error) *error = L"failed to parse quota summary JSON";
        return false;
    }
}

static bool ParseAntigravityUserStatus(const std::string& body, AccountData* d,
                                       std::wstring* error) {
    try {
        bool hasSummaryGeminiShort = d->win5h.pct >= 0;
        bool hasSummaryThirdPartyShort = d->antigravityThirdParty5h.pct >= 0;
        auto root = JsonObject::Parse(Utf8ToWide(body));
        JsonObject payload = root;
        if (auto response = GetObj(root, L"response")) payload = response;
        auto userStatus = GetObj(payload, L"userStatus");
        if (!userStatus) {
            if (error) *error = L"no userStatus object";
            return false;
        }
        std::wstring plan;
        auto tier = GetObj(userStatus, L"userTier");
        if (tier) plan = GetStr(tier, L"name");
        auto planInfo = GetObj(payload, L"planInfo");
        if (!planInfo) {
            if (auto planStatus = GetObj(userStatus, L"planStatus")) {
                planInfo = GetObj(planStatus, L"planInfo");
            }
        }
        if (plan.empty()) plan = GetStr(planInfo, L"planDisplayName");
        if (plan.empty()) plan = GetStr(planInfo, L"planName");
        if (plan.empty()) plan = GetStr(planInfo, L"displayName");
        if (plan.empty()) plan = GetStr(planInfo, L"productName");
        if (plan.empty()) plan = GetStr(planInfo, L"planShortName");
        if (plan.empty()) plan = GetStr(tier, L"description");
        if (plan.rfind(L"Google AI ", 0) == 0) plan.erase(0, 10);
        if (!plan.empty()) d->plan = std::move(plan);

        auto configData = GetObj(userStatus, L"cascadeModelConfigData");
        if (configData && configData.HasKey(L"clientModelConfigs")) {
            auto configsValue = configData.GetNamedValue(L"clientModelConfigs");
            if (configsValue.ValueType() == JsonValueType::Array) {
                auto configs = configsValue.GetArray();
                for (uint32_t i = 0; i < configs.Size(); i++) {
                    if (configs.GetAt(i).ValueType() != JsonValueType::Object) continue;
                    auto config = configs.GetAt(i).GetObject();
                    auto quota = GetObj(config, L"quotaInfo");
                    if (!quota) continue;

                    std::wstring identity = GetStr(config, L"label");
                    if (auto modelOrAlias = GetObj(config, L"modelOrAlias")) {
                        identity += L" " + GetStr(modelOrAlias, L"model");
                    }
                    std::transform(identity.begin(), identity.end(), identity.begin(),
                                   [](wchar_t ch) { return (wchar_t)towlower(ch); });
                    if (identity.find(L"lite") != std::wstring::npos ||
                        identity.find(L"autocomplete") != std::wstring::npos ||
                        identity.find(L"image") != std::wstring::npos) {
                        continue;
                    }

                    WindowUsage* target = nullptr;
                    if (identity.find(L"gemini") != std::wstring::npos) {
                        target = &d->win5h;
                    } else if (identity.find(L"claude") != std::wstring::npos ||
                               identity.find(L"gpt") != std::wstring::npos ||
                               identity.find(L"openai") != std::wstring::npos) {
                        target = &d->antigravityThirdParty5h;
                    }
                    if (!target || (target == &d->win5h && hasSummaryGeminiShort) ||
                        (target == &d->antigravityThirdParty5h &&
                         hasSummaryThirdPartyShort)) {
                        continue;
                    }

                    std::wstring resetTime = GetStr(quota, L"resetTime");
                    double remaining = GetNum(quota, L"remainingFraction", -1);
                    // QuotaInfo.remaining_fraction is an implicit-presence proto3 scalar:
                    // an exhausted zero is omitted while reset_time remains populated.
                    if (remaining < 0 && !resetTime.empty()) remaining = 0;
                    if (!std::isfinite(remaining) || remaining < 0 || remaining > 1) continue;

                    double usedPct = std::clamp((1.0 - remaining) * 100.0, 0.0, 100.0);
                    if (target->pct < usedPct) {
                        target->pct = usedPct;
                        target->resetUnixMs = ParseIso8601Ms(resetTime);
                        // Legacy model quota can mirror either limiting cadence.
                        target->windowDurationMs = 0;
                    }
                }
            }
        }
        return true;
    } catch (...) {
        if (error) *error = L"failed to parse user status JSON";
        return false;
    }
}

static void FetchAntigravityAccount(AccountData* d) {
    d->error.clear();
    d->retryDeadlineMs = 0;
    d->needsLogin = false;
    if (g_unloading) return;

    AntigravityServerInfo info;
    bool useCached = false;
    if (g_antigravityCachedInfo.port > 0) {
        info = g_antigravityCachedInfo;
        useCached = true;
        ULONGLONG now = NowUnixMs();
        if (info.quotaSource == AntigravityQuotaSource::UserStatus &&
            (info.discoveredMs == 0 || now < info.discoveredMs ||
             now - info.discoveredMs >= 5ULL * 60 * 1000)) {
            AntigravityServerInfo refreshedInfo;
            if (DiscoverAntigravityServer(&refreshedInfo)) {
                refreshedInfo.discoveredMs = now;
                info = std::move(refreshedInfo);
                g_antigravityCachedInfo = info;
            } else if (!g_unloading) {
                info.discoveredMs = now;
                g_antigravityCachedInfo = info;
            }
        }
    }

    if (!useCached) {
        bool foundServer = false;
        if (!DiscoverAntigravityServer(&info, &foundServer)) {
            if (g_unloading) return;
            d->stale = true;
            d->error = foundServer ? L"Antigravity quota unavailable" :
                                     L"Antigravity not running";
            return;
        }
        info.discoveredMs = NowUnixMs();
        g_antigravityCachedInfo = info;
    }

    auto fetchFromServer = [&](const AntigravityServerInfo& server, AccountData* fresh,
                               HttpResult* summaryResult, HttpResult* statusResult,
                               bool* summaryParsed, std::wstring* parseError) {
        *fresh = {};
        fresh->plan = d->plan;
        *summaryResult = {};
        *statusResult = {};
        *summaryParsed = false;
        parseError->clear();

        if (server.quotaSource == AntigravityQuotaSource::Summary) {
            *summaryResult = HttpRequestLocal(
                server.port, server.secure,
                L"/exa.language_server_pb.LanguageServerService/RetrieveUserQuotaSummary",
                server.csrfToken.c_str(), 5000);
            if (summaryResult->ok && summaryResult->status == 200) {
                *summaryParsed =
                    ParseAntigravityQuotaSummary(summaryResult->body, fresh, parseError);
            }
        }
        if (g_unloading) return false;

        *statusResult = HttpRequestLocal(
            server.port, server.secure,
            L"/exa.language_server_pb.LanguageServerService/GetUserStatus",
            server.csrfToken.c_str(), 5000);
        if (statusResult->ok && statusResult->status == 200) {
            std::wstring statusError;
            if (!ParseAntigravityUserStatus(statusResult->body, fresh, &statusError) &&
                parseError->empty()) {
                *parseError = std::move(statusError);
            }
        }

        bool parsed = fresh->win5h.pct >= 0 || fresh->winWeek.pct >= 0 ||
                      fresh->antigravityThirdParty5h.pct >= 0 ||
                      fresh->antigravityThirdPartyWeek.pct >= 0;
        if (!parsed && parseError->empty()) {
            *parseError = L"no usable Antigravity quota in response";
        }
        return parsed;
    };

    AccountData fresh;
    HttpResult summaryResult;
    HttpResult statusResult;
    bool summaryParsed = false;
    std::wstring parseError;
    bool fetched = fetchFromServer(info, &fresh, &summaryResult, &statusResult,
                                   &summaryParsed, &parseError);

    auto authenticationFailed = [](const HttpResult& result) {
        return result.ok && (result.status == 401 || result.status == 403);
    };
    auto endpointIsInvalid = [&](const AntigravityServerInfo& server) {
        return server.quotaSource == AntigravityQuotaSource::Summary ?
            ((!summaryResult.ok && !statusResult.ok) ||
             authenticationFailed(summaryResult) || authenticationFailed(statusResult)) :
            (!statusResult.ok || authenticationFailed(statusResult));
    };
    bool endpointInvalid = endpointIsInvalid(info);
    if (useCached && !fetched && endpointInvalid && !g_unloading) {
        g_antigravityCachedInfo = {};
        bool foundServer = false;
        if (DiscoverAntigravityServer(&info, &foundServer)) {
            info.discoveredMs = NowUnixMs();
            g_antigravityCachedInfo = info;
            fetched = fetchFromServer(info, &fresh, &summaryResult, &statusResult,
                                      &summaryParsed, &parseError);
            endpointInvalid = endpointIsInvalid(info);
        } else {
            if (g_unloading) return;
            d->stale = true;
            d->error = foundServer ? L"Antigravity quota unavailable" :
                                     L"Antigravity not running";
            return;
        }
    }
    if (g_unloading) return;

    if (!fetched) {
        d->stale = true;
        bool networkError = info.quotaSource == AntigravityQuotaSource::Summary ?
                                !summaryResult.ok && !statusResult.ok :
                                !statusResult.ok;
        if (networkError) {
            d->error = L"network error (language server)";
        } else if (info.quotaSource == AntigravityQuotaSource::Summary &&
                   summaryResult.ok && summaryResult.status != 200) {
            d->error = L"HTTP " + std::to_wstring(summaryResult.status) +
                       L" from language server";
        } else if (statusResult.ok && statusResult.status != 200) {
            d->error = L"HTTP " + std::to_wstring(statusResult.status) +
                       L" from language server";
        } else {
            d->error = parseError.empty() ? L"unexpected response" : parseError;
        }
        if (endpointInvalid) g_antigravityCachedInfo = {};
        return;
    }

    bool summaryUnsupported = summaryResult.ok &&
        ((summaryResult.status == 200 && !summaryParsed) ||
         summaryResult.status == 404 || summaryResult.status == 405 ||
         summaryResult.status == 501);
    if (info.quotaSource == AntigravityQuotaSource::Summary && summaryUnsupported) {
        info.quotaSource = AntigravityQuotaSource::UserStatus;
        info.discoveredMs = NowUnixMs();
    }
    g_antigravityCachedInfo = info;
    fresh.stale = false;
    fresh.lastSuccessMs = NowUnixMs();
    *d = std::move(fresh);
}
