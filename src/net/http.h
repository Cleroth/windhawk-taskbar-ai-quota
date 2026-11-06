// WinHTTP requests with handle tracking so unload can cancel in-flight calls.

static std::mutex g_httpHandlesMutex;
static std::vector<HINTERNET> g_httpHandles;

struct HttpResult {
    bool ok = false;
    int status = 0;
    int retryAfterSec = 0;
    std::string body;
};

static bool TrackHttpHandle(HINTERNET h) {
    if (!h) return false;
    std::lock_guard<std::mutex> lk(g_httpHandlesMutex);
    if (g_unloading) {
        WinHttpCloseHandle(h);
        return false;
    }
    g_httpHandles.push_back(h);
    return true;
}

static bool UntrackHttpHandle(HINTERNET h) {
    std::lock_guard<std::mutex> lk(g_httpHandlesMutex);
    auto it = std::find(g_httpHandles.begin(), g_httpHandles.end(), h);
    if (it == g_httpHandles.end()) return false;
    g_httpHandles.erase(it);
    return true;
}

static void CloseActiveHttpHandles() {
    std::vector<HINTERNET> handles;
    {
        std::lock_guard<std::mutex> lk(g_httpHandlesMutex);
        handles.swap(g_httpHandles);
    }
    for (auto it = handles.rbegin(); it != handles.rend(); ++it) WinHttpCloseHandle(*it);
}

// Requests carry bearer tokens, refresh tokens or a local CSRF token, which WinHTTP would send
// unchanged to wherever a redirect points: never follow one. A request that can't turn redirects
// off isn't sent.
static bool DisableRedirects(HINTERNET req) {
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    return WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)) != FALSE;
}

// `timeoutMs` (0: the defaults) bounds each of resolving, connecting, sending and every receive,
// and gives up on a response still arriving twice that long after the request began. It can't
// interrupt a stage already waiting.
static HttpResult HttpRequest(PCWSTR method, PCWSTR host, PCWSTR path, PCWSTR userAgent,
                              const std::wstring& headers, const std::string& body = {},
                              DWORD timeoutMs = 0) {
    HttpResult res;
    const ULONGLONG deadline = timeoutMs ? GetTickCount64() + 2ULL * timeoutMs : 0;
    auto late = [&] { return deadline && GetTickCount64() > deadline; };
    HINTERNET ses = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                0);
    if (ses && !TrackHttpHandle(ses)) ses = nullptr;
    HINTERNET con = nullptr;
    HINTERNET req = nullptr;

    if (ses && !g_unloading) {
        if (timeoutMs) {
            WinHttpSetTimeouts(ses, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs);
        } else {
            WinHttpSetTimeouts(ses, 5000, 5000, 5000, 10000);
        }
        con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (con && !TrackHttpHandle(con)) con = nullptr;
        req = con ? WinHttpOpenRequest(con, method, path, nullptr,
                                        WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE)
                  : nullptr;
        if (req && !TrackHttpHandle(req)) req = nullptr;
    }

    if (!g_unloading && req && DisableRedirects(req) &&
        WinHttpSendRequest(req, headers.c_str(), (DWORD)headers.size(),
                           body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                           (DWORD)body.size(), (DWORD)body.size(), 0) &&
        !late() && WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                            WINHTTP_NO_HEADER_INDEX);
        res.status = (int)status;

        // An unfollowed redirect comes back as its own response, an HTTP error to the caller. Its
        // target, without query or fragment, shows where a moved endpoint went.
        if (res.status >= 300 && res.status < 400) {
            wchar_t location[1024]{};
            DWORD locationSz = sizeof(location);
            if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                     location, &locationSz, WINHTTP_NO_HEADER_INDEX)) {
                location[0] = L'\0';
            }
            location[wcscspn(location, L"?#")] = L'\0';
            Wh_Log(L"HTTP %d from %s%s: redirect to %s not followed", res.status, host, path,
                   location[0] ? location : L"an unreadable location");
        }

        // Retry-After: delay seconds (digits only), else an HTTP date; either waits at most a day.
        constexpr ULONGLONG kMaxRetryAfterSec = 24ULL * 60 * 60;
        wchar_t ra[128]{};
        DWORD raSz = sizeof(ra);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RETRY_AFTER,
                                WINHTTP_HEADER_NAME_BY_INDEX, ra, &raSz,
                                WINHTTP_NO_HEADER_INDEX)) {
            if (ra[0] && !ra[wcsspn(ra, L"0123456789")]) {
                ULONGLONG seconds = 0;
                for (const wchar_t* digit = ra; *digit; digit++) {
                    seconds = std::min(seconds * 10 + ULONGLONG(*digit - L'0'), kMaxRetryAfterSec);
                }
                res.retryAfterSec = (int)seconds;
            } else {
                SYSTEMTIME st{};
                FILETIME ft{};
                if (WinHttpTimeToSystemTime(ra, &st) && SystemTimeToFileTime(&st, &ft)) {
                    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                    ULONGLONG fileMs = t / 10000;
                    if (fileMs > kFileTimeUnixEpochOffsetMs) {
                        ULONGLONG retryUnixMs = fileMs - kFileTimeUnixEpochOffsetMs;
                        ULONGLONG now = NowUnixMs();
                        if (retryUnixMs > now) {
                            ULONGLONG deltaSec = (retryUnixMs - now + 999) / 1000;
                            res.retryAfterSec = (int)std::min(deltaSec, kMaxRetryAfterSec);
                        }
                    }
                }
            }
        }

        bool bodyOk = true;
        for (;;) {
            DWORD available = 0;
            if (late() || !WinHttpQueryDataAvailable(req, &available)) {
                bodyOk = false;
                break;
            }
            if (!available) break;

            size_t prev = res.body.size();
            constexpr size_t kMaxResponseBytes = 4 * 1024 * 1024;
            if (available > kMaxResponseBytes - prev) {
                bodyOk = false;
                break;
            }
            res.body.resize(prev + available);
            DWORD read = 0;
            if (late() || !WinHttpReadData(req, res.body.data() + prev, available, &read)) {
                res.body.resize(prev);
                bodyOk = false;
                break;
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
