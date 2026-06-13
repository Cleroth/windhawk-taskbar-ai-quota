// ==WindhawkMod==
// @id              taskbar-ai-quota
// @name            Taskbar AI Quota Bars
// @description     Shows LLM/agent quota usage for Anthropic and OpenAI as compact bars on the Windows 11 taskbar, left of the system tray
// @version         0.1
// @author          Cleroth
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lwindowsapp -lwinhttp -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar AI Quota Bars

Shows Anthropic Claude and OpenAI Codex LLM/agent subscription quota usage directly on
the Windows 11 taskbar, to the left of the system tray.

Each account gets one narrow column:
- top bar: 5-hour usage
- bottom bar: weekly usage

Bars use green/yellow/orange/red threshold colors, and gray when stale.
Enable colorblind mode to switch to a blue/orange palette.
Stale errors can mark labels/tooltips with `!`.
Hover for exact percentages/reset times. Click a column to refresh.

Credentials are read-only. The mod never refreshes or rewrites tokens.
Default source: `%USERPROFILE%\.local\share\opencode\auth.json`.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- accounts:
    - - provider: anthropic
        $name: Provider
        $description: 'Default accounts: anthropic and openai'
        $options:
          - anthropic: Anthropic (Claude OAuth)
          - openai: OpenAI (Codex OAuth)
      - label: A
        $name: Label
        $description: 'Default: A for Anthropic, O for OpenAI'
      - authFile: '%USERPROFILE%\.local\share\opencode\auth.json'
        $name: Auth file
        $description: 'Default: %USERPROFILE%\.local\share\opencode\auth.json'
      - authKey: ""
        $name: Auth JSON key
        $description: 'Default: empty. For opencode auth.json, empty means use the provider name.'
    - - provider: openai
      - label: O
      - authFile: '%USERPROFILE%\.local\share\opencode\auth.json'
      - authKey: ""
  $name: Accounts
  $description: 'Default: two accounts, Anthropic A and OpenAI O'
- pollIntervalMinutes: 10
  $name: Poll interval (minutes)
  $description: 'Default: 10'
- barWidth: 100
  $name: Bar width (px)
  $description: 'Default: 100'
- barHeight: 8
  $name: Bar height (px)
  $description: 'Default: 8'
- showLabels: true
  $name: Show labels
  $description: 'Default: true'
- labelOnLeft: true
  $name: Put labels on the left
  $description: 'Default: true'
- labelFontSize: 11
  $name: Label font size (px)
  $description: 'Default: 11'
- showPercentText: false
  $name: Show percent text
  $description: 'Default: false. Shows compact 5h/week percentages over the bars.'
- yellowThreshold: 50
  $name: Yellow threshold (%)
  $description: 'Default: 50. Usage below this stays green.'
- orangeThreshold: 75
  $name: Orange threshold (%)
  $description: 'Default: 75. Usage below this stays yellow.'
- redThreshold: 90
  $name: Red threshold (%)
  $description: 'Default: 90. Usage at or above this turns red.'
- colorblindMode: false
  $name: Colorblind mode
  $description: 'Default: false. Uses a blue-to-orange palette instead of green/red.'
- showStaleWarning: true
  $name: Show stale warning
  $description: 'Default: true. Marks stale error states with ! in labels and tooltips.'
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <windows.h>
#include <winhttp.h>
#include <unknwn.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;

namespace wuxcp = winrt::Windows::UI::Xaml::Controls::Primitives;
namespace wuxi = winrt::Windows::UI::Xaml::Input;

/**********************************************/
//  Settings and State
/**********************************************/

struct AccountConfig {
    std::wstring provider;
    std::wstring label;
    std::wstring authFile;
    std::wstring authKey;
};

struct Settings {
    std::vector<AccountConfig> accounts;
    int pollMinutes = 10;
    int barWidth = 100;
    int barHeight = 8;
    int labelFontSize = 11;
    int yellowThreshold = 50;
    int orangeThreshold = 75;
    int redThreshold = 90;
    bool showLabels = true;
    bool labelOnLeft = true;
    bool showPercentText = false;
    bool colorblindMode = false;
    bool showStaleWarning = true;
};

struct WindowUsage {
    double pct = -1;
    ULONGLONG resetUnixMs = 0;
};

struct AccountData {
    WindowUsage win5h;
    WindowUsage winWeek;
    std::wstring plan;
    std::wstring extraLines;
    std::wstring error;
    ULONGLONG lastSuccessMs = 0;
    bool stale = true;
};

struct AppliedState {
    int fillPx[2] = {-1, -1};
    uint32_t fillColor[2] = {0, 0};
    std::wstring tip;
    std::wstring percentText;
    std::wstring labelText;
    double labelOpacity = -1;
    double columnOpacity = -1;
};

struct TapHandler {
    UIElement element{nullptr};
    winrt::event_token token{};
};

static Settings g_settings;
static std::mutex g_settingsMutex;
static std::vector<AccountData> g_data;
static std::mutex g_dataMutex;
static std::vector<AppliedState> g_applied;

static std::atomic<bool> g_unloading{false};
static std::atomic<bool> g_refreshing{false};
static std::atomic<bool> g_uiInjected{false};
static HANDLE g_stopEvent = nullptr;
static HANDLE g_refreshEvent = nullptr;
static HANDLE g_fetchThread = nullptr;
static HANDLE g_retryThread = nullptr;
static HWND g_taskbarWnd = nullptr;

static Grid g_quotaGrid = nullptr;
static Grid g_injectionParent = nullptr;
static int g_quotaColumn = -1;
static std::vector<TapHandler> g_tapHandlers;

static const wchar_t* kRootName = L"AiQuota_Root";
static const wchar_t* kDefaultAuthFile = L"%USERPROFILE%\\.local\\share\\opencode\\auth.json";

using WindowThreadProc = void (*)(void*);
static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param);
static HWND FindCurrentProcessTaskbarWnd();

/**********************************************/
//  Helpers
/**********************************************/

static ULONGLONG NowUnixMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return t / 10000 - 11644473600000ULL;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::wstring ExpandEnv(PCWSTR s) {
    wchar_t buf[1024];
    DWORD n = ExpandEnvironmentStringsW(s, buf, ARRAYSIZE(buf));
    if (n == 0 || n > ARRAYSIZE(buf)) return s;
    return buf;
}

static bool ReadFileUtf8(const std::wstring& path, std::string* out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 1024 * 1024;
    if (ok) {
        out->resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = ReadFile(h, out->data(), (DWORD)out->size(), &read, nullptr) &&
             read == out->size();
    }
    CloseHandle(h);

    if (ok && out->size() >= 3 && (BYTE)(*out)[0] == 0xEF &&
        (BYTE)(*out)[1] == 0xBB && (BYTE)(*out)[2] == 0xBF) {
        out->erase(0, 3);
    }
    return ok;
}

static ULONGLONG ParseIso8601Ms(const std::wstring& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    double sec = 0;
    if (swscanf(s.c_str(), L"%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) != 6) {
        return 0;
    }

    SYSTEMTIME st{};
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    st.wSecond = (WORD)sec;
    st.wMilliseconds = (WORD)((sec - st.wSecond) * 1000);

    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    ULONGLONG unixMs = t / 10000 - 11644473600000ULL;

    size_t tpos = s.find(L'T');
    size_t sign = tpos == std::wstring::npos ? std::wstring::npos : s.find_first_of(L"+-", tpos);
    if (sign != std::wstring::npos) {
        int oh = 0, om = 0;
        swscanf(s.c_str() + sign + 1, L"%d:%d", &oh, &om);
        LONGLONG off = ((LONGLONG)oh * 60 + om) * 60000;
        unixMs = s[sign] == L'+' ? unixMs - off : unixMs + off;
    }
    return unixMs;
}

static std::wstring FormatReset(ULONGLONG unixMs) {
    if (!unixMs) return L"?";
    LONGLONG delta = (LONGLONG)(unixMs - NowUnixMs());
    if (delta <= 0) return L"now";

    ULONGLONG totalMin = ((ULONGLONG)delta + 59999) / 60000;
    ULONGLONG days = totalMin / (24 * 60);
    ULONGLONG hours = (totalMin / 60) % 24;
    ULONGLONG mins = totalMin % 60;

    wchar_t rel[64];
    if (days > 0) {
        if (hours > 0) swprintf(rel, ARRAYSIZE(rel), L"in %llud %lluh", days, hours);
        else swprintf(rel, ARRAYSIZE(rel), L"in %llud", days);
    } else if (hours > 0) {
        if (mins > 0) swprintf(rel, ARRAYSIZE(rel), L"in %lluh %llum", hours, mins);
        else swprintf(rel, ARRAYSIZE(rel), L"in %lluh", hours);
    } else {
        swprintf(rel, ARRAYSIZE(rel), L"in %llum", mins);
    }

    ULONGLONG t = (unixMs + 11644473600000ULL) * 10000;
    FILETIME ft{(DWORD)(t & 0xFFFFFFFF), (DWORD)(t >> 32)};
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&ft, &utc) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return rel;
    }

    wchar_t buf[64];
    if (delta < 24LL * 3600 * 1000) {
        swprintf(buf, ARRAYSIZE(buf), L"%s (%02u:%02u)", rel, local.wHour, local.wMinute);
    } else {
        wchar_t day[16] = L"";
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, L"ddd", day, ARRAYSIZE(day), nullptr);
        swprintf(buf, ARRAYSIZE(buf), L"%s (%s %02u:%02u)", rel, day, local.wHour, local.wMinute);
    }
    return buf;
}

static std::wstring FormatUpdated(ULONGLONG unixMs, bool stale) {
    if (!unixMs) return L"no data yet";
    ULONGLONG t = (unixMs + 11644473600000ULL) * 10000;
    FILETIME ft{(DWORD)(t & 0xFFFFFFFF), (DWORD)(t >> 32)};
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&ft, &utc) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return L"updated ?";
    }
    wchar_t buf[64];
    swprintf(buf, ARRAYSIZE(buf), L"updated %02u:%02u%s", local.wHour, local.wMinute,
             stale ? L" (stale)" : L"");
    return buf;
}

static winrt::Windows::UI::Color UsageColor(double pct, bool stale, int yellowThreshold,
                                            int orangeThreshold, int redThreshold,
                                            bool colorblindMode) {
    if (stale || pct < 0) return {255, 0x9E, 0x9E, 0x9E};

    if (colorblindMode) {
        if (pct >= redThreshold) return {255, 0xD5, 0x5E, 0x00};
        if (pct >= orangeThreshold) return {255, 0xE6, 0x9F, 0x00};
        if (pct >= yellowThreshold) return {255, 0x56, 0xB4, 0xE9};
        return {255, 0x00, 0x72, 0xB2};
    }

    if (pct >= redThreshold) return {255, 0xE5, 0x39, 0x35};
    if (pct >= orangeThreshold) return {255, 0xFB, 0x8C, 0x00};
    if (pct >= yellowThreshold) return {255, 0xFD, 0xD8, 0x35};
    return {255, 0x43, 0xA0, 0x47};
}

/**********************************************/
//  JSON Helpers
/**********************************************/

static JsonObject GetObj(JsonObject const& o, PCWSTR name) {
    if (!o || !o.HasKey(name)) return nullptr;
    auto v = o.GetNamedValue(name);
    return v.ValueType() == JsonValueType::Object ? v.GetObject() : nullptr;
}

static double GetNum(JsonObject const& o, PCWSTR name, double def = -1) {
    if (!o || !o.HasKey(name)) return def;
    auto v = o.GetNamedValue(name);
    return v.ValueType() == JsonValueType::Number ? v.GetNumber() : def;
}

static std::wstring GetStr(JsonObject const& o, PCWSTR name) {
    if (!o || !o.HasKey(name)) return {};
    auto v = o.GetNamedValue(name);
    if (v.ValueType() != JsonValueType::String) return {};
    auto s = v.GetString();
    return std::wstring(s.c_str(), s.size());
}

static bool GetBool(JsonObject const& o, PCWSTR name) {
    if (!o || !o.HasKey(name)) return false;
    auto v = o.GetNamedValue(name);
    return v.ValueType() == JsonValueType::Boolean && v.GetBoolean();
}

static std::wstring DescribeJsonBody(const std::string& body) {
    if (body.empty()) return L"empty body";

    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        std::wstring keys;
        int count = 0;
        for (auto const& kv : root) {
            if (count == 8) {
                keys += L", ...";
                break;
            }
            if (!keys.empty()) keys += L", ";
            auto key = kv.Key();
            keys += std::wstring(key.c_str(), key.size());
            count++;
        }
        return keys.empty() ? L"JSON object with no keys" : L"keys: " + keys;
    } catch (...) {
        return L"non-object or invalid JSON body";
    }
}

/**********************************************/
//  Credentials
/**********************************************/

struct AuthInfo {
    bool ok = false;
    std::wstring err;
    std::wstring accessToken;
    std::wstring accountId;
    ULONGLONG expiresMs = 0;
};

static AuthInfo LoadAuthInfo(const AccountConfig& acc) {
    AuthInfo a;
    std::string raw;
    if (!ReadFileUtf8(acc.authFile, &raw)) {
        a.err = L"can't read auth file";
        return a;
    }

    JsonObject root = nullptr;
    try {
        root = JsonObject::Parse(Utf8ToWide(raw));
    } catch (...) {
        a.err = L"auth file parse error";
        return a;
    }

    std::wstring key = acc.authKey.empty() ? acc.provider : acc.authKey;
    if (auto entry = GetObj(root, key.c_str()); entry && entry.HasKey(L"access")) {
        a.accessToken = GetStr(entry, L"access");
        a.accountId = GetStr(entry, L"accountId");
        a.expiresMs = (ULONGLONG)GetNum(entry, L"expires", 0);
        a.ok = !a.accessToken.empty();
        if (!a.ok) a.err = L"no access token";
        return a;
    }

    if (auto entry = GetObj(root, L"claudeAiOauth")) {
        a.accessToken = GetStr(entry, L"accessToken");
        a.expiresMs = (ULONGLONG)GetNum(entry, L"expiresAt", 0);
        a.ok = !a.accessToken.empty();
        if (!a.ok) a.err = L"no access token";
        return a;
    }

    if (auto entry = GetObj(root, L"tokens")) {
        a.accessToken = GetStr(entry, L"access_token");
        a.accountId = GetStr(entry, L"account_id");
        a.ok = !a.accessToken.empty();
        if (!a.ok) a.err = L"no access token";
        return a;
    }

    a.err = L"no credentials found";
    return a;
}

/**********************************************/
//  HTTP
/**********************************************/

struct HttpResult {
    bool ok = false;
    int status = 0;
    int retryAfterSec = 0;
    std::string body;
};

static HttpResult HttpGet(PCWSTR host, PCWSTR path, PCWSTR userAgent,
                          const std::wstring& headers) {
    HttpResult res;
    HINTERNET ses = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                0);
    HINTERNET con = nullptr;
    HINTERNET req = nullptr;

    if (ses) {
        WinHttpSetTimeouts(ses, 5000, 5000, 5000, 10000);
        con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        req = con ? WinHttpOpenRequest(con, L"GET", path, nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE)
                  : nullptr;
    }

    if (req && WinHttpSendRequest(req, headers.c_str(), (DWORD)headers.size(),
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                            WINHTTP_NO_HEADER_INDEX);
        res.status = (int)status;

        wchar_t ra[128]{};
        DWORD raSz = sizeof(ra);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RETRY_AFTER,
                                WINHTTP_HEADER_NAME_BY_INDEX, ra, &raSz,
                                WINHTTP_NO_HEADER_INDEX)) {
            res.retryAfterSec = _wtoi(ra);
            if (res.retryAfterSec <= 0) {
                SYSTEMTIME st{};
                FILETIME ft{};
                if (WinHttpTimeToSystemTime(ra, &st) && SystemTimeToFileTime(&st, &ft)) {
                    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                    ULONGLONG fileMs = t / 10000;
                    if (fileMs > 11644473600000ULL) {
                        ULONGLONG retryUnixMs = fileMs - 11644473600000ULL;
                        ULONGLONG now = NowUnixMs();
                        if (retryUnixMs > now) {
                            ULONGLONG deltaSec = (retryUnixMs - now + 999) / 1000;
                            res.retryAfterSec = (int)std::min(deltaSec, 24ULL * 60 * 60);
                        }
                    }
                }
            }
        }

        bool bodyOk = true;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(req, &available)) {
                bodyOk = false;
                break;
            }
            if (!available) break;

            size_t prev = res.body.size();
            res.body.resize(prev + available);
            DWORD read = 0;
            if (!WinHttpReadData(req, res.body.data() + prev, available, &read)) {
                res.body.resize(prev);
                bodyOk = false;
                break;
            }
            res.body.resize(prev + read);
            if (!read || res.body.size() > 4 * 1024 * 1024) break;
        }
        res.ok = bodyOk;
    }

    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return res;
}

/**********************************************/
//  Usage Parsers
/**********************************************/

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
        }
        if (auto sd = GetObj(usage, L"seven_day")) {
            d->winWeek.pct = GetNum(sd, L"utilization");
            d->winWeek.resetUnixMs = ParseIso8601Ms(GetStr(sd, L"resets_at"));
        }

        d->plan.clear();
        d->extraLines.clear();
        if (auto op = GetObj(usage, L"seven_day_opus"); op && GetNum(op, L"utilization") >= 0) {
            wchar_t line[64];
            swprintf(line, ARRAYSIZE(line), L"opus week: %.0f%%", GetNum(op, L"utilization"));
            d->extraLines += line;
        }
        if (auto eu = GetObj(usage, L"extra_usage"); eu && GetBool(eu, L"is_enabled")) {
            wchar_t line[96];
            swprintf(line, ARRAYSIZE(line), L"extra usage: %.1f%% monthly",
                     GetNum(eu, L"utilization", 0));
            if (!d->extraLines.empty()) d->extraLines += L"\n";
            d->extraLines += line;
        }
        bool parsed = d->win5h.pct >= 0 || d->winWeek.pct >= 0;
        if (!parsed && error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return parsed;
    } catch (...) {
        if (error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return false;
    }
}

static bool ParseOpenAiUsage(const std::string& body, AccountData* d, std::wstring* error) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        JsonObject usage = root;
        auto rl = GetObj(usage, L"rate_limit");
        if (!rl) {
            if (auto wrapped = GetObj(root, L"usage"); wrapped && GetObj(wrapped, L"rate_limit")) usage = wrapped;
            else if (auto wrapped = GetObj(root, L"data"); wrapped && GetObj(wrapped, L"rate_limit")) usage = wrapped;
            rl = GetObj(usage, L"rate_limit");
        }
        if (!rl && (GetObj(usage, L"primary_window") || GetObj(usage, L"secondary_window"))) rl = usage;

        auto resetUnixMs = [](JsonObject const& window) -> ULONGLONG {
            double resetAt = GetNum(window, L"reset_at", 0);
            if (resetAt > 100000000000.0) return (ULONGLONG)resetAt;
            if (resetAt > 0) return (ULONGLONG)(resetAt * 1000);

            double resetAfter = GetNum(window, L"reset_after_seconds", 0);
            return resetAfter > 0 ? NowUnixMs() + (ULONGLONG)(resetAfter * 1000) : 0ULL;
        };

        if (auto pw = GetObj(rl, L"primary_window")) {
            d->win5h.pct = GetNum(pw, L"used_percent");
            d->win5h.resetUnixMs = resetUnixMs(pw);
        }
        if (auto sw = GetObj(rl, L"secondary_window")) {
            d->winWeek.pct = GetNum(sw, L"used_percent");
            d->winWeek.resetUnixMs = resetUnixMs(sw);
        }

        d->plan = GetStr(usage, L"plan_type");
        d->extraLines.clear();
        auto addLimitLine = [&](JsonObject const& item) {
            auto itemRl = GetObj(item, L"rate_limit");
            auto pw = GetObj(itemRl, L"primary_window");
            auto sw = GetObj(itemRl, L"secondary_window");
            std::wstring name = GetStr(item, L"limit_name");
            if (name.empty() || (!pw && !sw)) return;
            wchar_t line[128];
            swprintf(line, ARRAYSIZE(line), L"%s: 5h %.0f%% | wk %.0f%%",
                     name.c_str(), GetNum(pw, L"used_percent", 0),
                     GetNum(sw, L"used_percent", 0));
            if (!d->extraLines.empty()) d->extraLines += L"\n";
            d->extraLines += line;
        };
        if (usage.HasKey(L"additional_rate_limits")) {
            auto limits = usage.GetNamedValue(L"additional_rate_limits");
            if (limits.ValueType() == JsonValueType::Array) {
                auto arr = limits.GetArray();
                for (uint32_t i = 0; i < arr.Size() && i < 3; i++) {
                    if (arr.GetAt(i).ValueType() == JsonValueType::Object) addLimitLine(arr.GetAt(i).GetObject());
                }
            } else if (limits.ValueType() == JsonValueType::Object) {
                addLimitLine(limits.GetObject());
            }
        }

        if (auto cr = GetObj(usage, L"credits"); cr && GetBool(cr, L"has_credits")) {
            double balance = GetNum(cr, L"balance", -1);
            if (balance < 0) {
                std::wstring s = GetStr(cr, L"balance");
                if (!s.empty()) balance = wcstod(s.c_str(), nullptr);
            }
            if (balance >= 0) {
                wchar_t line[64];
                swprintf(line, ARRAYSIZE(line), L"credits: %.2f", balance);
                if (!d->extraLines.empty()) d->extraLines += L"\n";
                d->extraLines += line;
            }
        }
        bool parsed = d->win5h.pct >= 0 || d->winWeek.pct >= 0;
        if (!parsed && error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return parsed;
    } catch (...) {
        if (error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return false;
    }
}

/**********************************************/
//  Fetch Thread
/**********************************************/

static void FetchAccount(const AccountConfig& acc, AccountData* d, int* retryAfterSec) {
    d->error.clear();
    AuthInfo auth = LoadAuthInfo(acc);
    if (!auth.ok) {
        d->stale = true;
        d->error = auth.err;
        return;
    }
    if (auth.expiresMs && auth.expiresMs < NowUnixMs()) {
        d->stale = true;
        d->error = L"token expired - use CLI to refresh";
        return;
    }

    HttpResult r;
    if (acc.provider == L"anthropic") {
        std::wstring headers = L"Authorization: Bearer " + auth.accessToken +
                               L"\r\nanthropic-beta: oauth-2025-04-20"
                               L"\r\nAccept: application/json\r\n";
        r = HttpGet(L"api.anthropic.com", L"/api/oauth/usage", L"claude-code/2.1.0", headers);
    } else {
        std::wstring headers = L"Authorization: Bearer " + auth.accessToken +
                               L"\r\nAccept: application/json\r\n";
        if (!auth.accountId.empty()) headers += L"ChatGPT-Account-Id: " + auth.accountId + L"\r\n";
        r = HttpGet(L"chatgpt.com", L"/backend-api/wham/usage", L"taskbar-ai-quota/0.1", headers);
    }

    if (!r.ok) {
        d->stale = true;
        d->error = L"network error";
        return;
    }
    if (r.status == 429) {
        d->stale = true;
        d->error = L"rate limited by API";
        *retryAfterSec = r.retryAfterSec > 0 ? r.retryAfterSec : 120;
        return;
    }
    if (r.status != 200) {
        d->stale = true;
        d->error = L"HTTP " + std::to_wstring(r.status);
        return;
    }

    AccountData fresh;
    std::wstring parseError;
    bool parsed = acc.provider == L"anthropic" ? ParseAnthropicUsage(r.body, &fresh, &parseError)
                                               : ParseOpenAiUsage(r.body, &fresh, &parseError);
    if (!parsed) {
        d->stale = true;
        d->error = parseError.empty() ? L"unexpected response format" : parseError;
        return;
    }

    fresh.stale = false;
    fresh.lastSuccessMs = NowUnixMs();
    *d = std::move(fresh);
}

static void UpdateQuotaUi();

static void PostUiUpdate() {
    // Never resolve XAML refs on the fetch thread; marshal first.
    if (g_unloading || !g_uiInjected.load(std::memory_order_acquire)) return;

    HWND hWnd = g_taskbarWnd ? g_taskbarWnd : FindCurrentProcessTaskbarWnd();
    if (!hWnd) return;
    RunFromWindowThread(hWnd, [](void*) {
        if (!g_unloading && g_uiInjected.load(std::memory_order_acquire) && g_quotaGrid) UpdateQuotaUi();
    }, nullptr);
}

static DWORD WINAPI FetchThreadProc(LPVOID) {
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}

    std::vector<std::wstring> lastLoggedErrorStates;
    while (!g_unloading) {
        std::vector<AccountConfig> accounts;
        int intervalMin;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            accounts = g_settings.accounts;
            intervalMin = g_settings.pollMinutes;
        }
        if (lastLoggedErrorStates.size() != accounts.size()) lastLoggedErrorStates.assign(accounts.size(), {});

        std::vector<AccountData> results(accounts.size());
        {
            std::lock_guard<std::mutex> lk(g_dataMutex);
            if (g_data.size() == results.size()) results = g_data;
        }

        int minRetryAfter = 0;
        bool anyError = false;
        for (size_t i = 0; i < accounts.size() && !g_unloading; i++) {
            int retryAfter = 0;
            FetchAccount(accounts[i], &results[i], &retryAfter);
            if (!results[i].error.empty()) {
                anyError = true;
                std::wstring errorState = accounts[i].provider + L"\n" + accounts[i].authFile + L"\n" +
                                          accounts[i].authKey + L"\n" + results[i].error;
                if (errorState != lastLoggedErrorStates[i]) {
                    Wh_Log(L"Fetch [%d] %s: %s", (int)i, accounts[i].provider.c_str(), results[i].error.c_str());
                    lastLoggedErrorStates[i] = std::move(errorState);
                }
            } else {
                lastLoggedErrorStates[i].clear();
            }
            if (retryAfter > 0 && (minRetryAfter == 0 || retryAfter < minRetryAfter)) {
                minRetryAfter = retryAfter;
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_dataMutex);
            if (g_data.size() == results.size()) g_data = std::move(results);
        }
        g_refreshing = false;
        PostUiUpdate();

        DWORD waitMs = (DWORD)intervalMin * 60000;
        if (minRetryAfter > 0) waitMs = std::clamp((DWORD)minRetryAfter * 1000, (DWORD)30000, waitMs);
        else if (anyError) waitMs = std::min(waitMs, (DWORD)120000);

        HANDLE handles[2] = {g_stopEvent, g_refreshEvent};
        if (WaitForMultipleObjects(2, handles, FALSE, waitMs) == WAIT_OBJECT_0) break;
    }
    return 0;
}

/**********************************************/
//  Taskbar XAML Access
/**********************************************/

using CTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void*, void*);
using TaskbarHost_FrameHeight_t = int(WINAPI*)(void*);
using Std_Ref_Decref_t = void(WINAPI*)(void*);

static CTaskBand_GetTaskbarHost_t CTaskBand_GetTaskbarHost_Original = nullptr;
static TaskbarHost_FrameHeight_t TaskbarHost_FrameHeight_Original = nullptr;
static Std_Ref_Decref_t Std_Ref_Decref_Original = nullptr;
static void* CTaskBand_ITaskListWndSite_vftable = nullptr;

static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param) {
    static const UINT kMsg = RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
    struct Payload { WindowThreadProc proc; void* param; };
    DWORD tid = GetWindowThreadProcessId(hWnd, nullptr);
    if (!tid) return false;
    if (tid == GetCurrentThreadId()) {
        proc(param);
        return true;
    }

    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM w, LPARAM l) CALLBACK -> LRESULT {
            if (code == HC_ACTION) {
                auto* cwp = reinterpret_cast<const CWPSTRUCT*>(l);
                static const UINT kM = RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
                if (cwp->message == kM) {
                    auto* p = reinterpret_cast<Payload*>(cwp->lParam);
                    p->proc(p->param);
                }
            }
            return CallNextHookEx(nullptr, code, w, l);
        }, nullptr, tid);
    if (!hook) return false;

    Payload pay{proc, param};
    SendMessageW(hWnd, kMsg, 0, reinterpret_cast<LPARAM>(&pay));
    UnhookWindowsHookEx(hook);
    return true;
}

static HWND FindCurrentProcessTaskbarWnd() {
    HWND result = nullptr;
    EnumWindows([](HWND hWnd, LPARAM lp) CALLBACK -> BOOL {
        DWORD pid = 0;
        wchar_t cls[32] = {};
        if (GetWindowThreadProcessId(hWnd, &pid) && pid == GetCurrentProcessId() &&
            GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) && _wcsicmp(cls, L"Shell_TrayWnd") == 0) {
            *reinterpret_cast<HWND*>(lp) = hWnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

static HRESULT TryGetTaskbarElementAbi(HWND hTaskbarWnd, void** result) {
    *result = nullptr;
    void* taskbarHostSharedPtr[2]{};

    auto cleanup = [&]() {
        if (taskbarHostSharedPtr[1] && Std_Ref_Decref_Original) Std_Ref_Decref_Original(taskbarHostSharedPtr[1]);
    };

    HWND hTaskSwWnd = (HWND)GetProp(hTaskbarWnd, L"TaskbandHWND");
    if (!hTaskSwWnd) return E_HANDLE;

    void* taskBand = (void*)GetWindowLongPtrW(hTaskSwWnd, 0);
    if (!taskBand) return E_POINTER;

    void* taskBandForTaskListWndSite = taskBand;
    for (int i = 0; *(void**)taskBandForTaskListWndSite != CTaskBand_ITaskListWndSite_vftable; i++) {
        if (i == 20) return E_NOINTERFACE;
        taskBandForTaskListWndSite = (void**)taskBandForTaskListWndSite + 1;
        if (!taskBandForTaskListWndSite) return E_POINTER;
    }

    CTaskBand_GetTaskbarHost_Original(taskBandForTaskListWndSite, taskbarHostSharedPtr);
    if (!taskbarHostSharedPtr[0]) {
        cleanup();
        return E_POINTER;
    }

    size_t taskbarElementIUnknownOffset = 0x10;
    const BYTE* b = (const BYTE*)TaskbarHost_FrameHeight_Original;
    if (b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xEC && b[4] == 0x48 &&
        b[5] == 0x83 && b[6] == 0xC1 && b[7] <= 0x7F) {
        taskbarElementIUnknownOffset = b[7];
    } else {
        Wh_Log(L"Unsupported TaskbarHost::FrameHeight pattern");
    }

    auto* taskbarElementIUnknown =
        *(IUnknown**)((BYTE*)taskbarHostSharedPtr[0] + taskbarElementIUnknownOffset);
    if (!taskbarElementIUnknown) {
        cleanup();
        return E_POINTER;
    }

    HRESULT hr = taskbarElementIUnknown->QueryInterface(winrt::guid_of<FrameworkElement>(), result);
    cleanup();
    if (FAILED(hr) && *result) {
        static_cast<IUnknown*>(*result)->Release();
        *result = nullptr;
    }

    return hr;
}

static XamlRoot GetTaskbarXamlRoot(HWND hTaskbarWnd) {
    if (!CTaskBand_ITaskListWndSite_vftable || !CTaskBand_GetTaskbarHost_Original ||
        !TaskbarHost_FrameHeight_Original) {
        return nullptr;
    }

    void* taskbarElementAbi = nullptr;
    if (FAILED(TryGetTaskbarElementAbi(hTaskbarWnd, &taskbarElementAbi)) || !taskbarElementAbi) return nullptr;

    FrameworkElement taskbarElement{nullptr};
    winrt::attach_abi(taskbarElement, taskbarElementAbi);
    auto result = taskbarElement ? taskbarElement.XamlRoot() : nullptr;
    return result;
}

static FrameworkElement FindChildByName(FrameworkElement const& root, std::wstring_view name, int depth = 32) {
    if (!root || depth == 0) return nullptr;
    int n = VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < n; ++i) {
        auto child = VisualTreeHelper::GetChild(root, i).try_as<FrameworkElement>();
        if (!child) continue;
        if (child.Name() == name) return child;
        if (auto found = FindChildByName(child, name, depth - 1)) return found;
    }
    return nullptr;
}

/**********************************************/
//  UI
/**********************************************/

static void ClearQuotaEventState() {
    // Routed event delegates point into this DLL, so revoke before XAML tears down the subtree.
    for (auto& handler : g_tapHandlers) {
        if (!handler.element) continue;

        try { handler.element.Tapped(handler.token); } catch (...) {}
        try {
            ToolTipService::SetToolTip(handler.element, winrt::Windows::Foundation::IInspectable{nullptr});
        } catch (...) {}
    }
    g_tapHandlers.clear();
}

static Grid BuildQuotaGrid() {
    try {
        std::vector<AccountConfig> accounts;
        int barWidth, barHeight, labelFontSize;
        bool showLabels, labelOnLeft, showPercentText;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            accounts = g_settings.accounts;
            barWidth = g_settings.barWidth;
            barHeight = g_settings.barHeight;
            labelFontSize = g_settings.labelFontSize;
            showLabels = g_settings.showLabels;
            labelOnLeft = g_settings.labelOnLeft;
            showPercentText = g_settings.showPercentText;
        }
        if (accounts.empty()) return nullptr;

        Grid root;
        root.Name(kRootName);
        root.VerticalAlignment(VerticalAlignment::Center);

        StackPanel panel;
        panel.Orientation(Orientation::Horizontal);
        panel.Margin({4, 0, 4, 0});

        wchar_t name[64];
        for (size_t i = 0; i < accounts.size(); i++) {
            StackPanel col;
            col.Orientation(labelOnLeft ? Orientation::Horizontal : Orientation::Vertical);
            col.VerticalAlignment(VerticalAlignment::Center);
            col.Margin({3, 0, 3, 0});
            col.Background(SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
            swprintf(name, ARRAYSIZE(name), L"AiQuota_Acc_%d", (int)i);
            col.Name(name);

            if (showLabels) {
                TextBlock label;
                label.Text(accounts[i].label);
                label.FontSize(labelFontSize);
                label.VerticalAlignment(VerticalAlignment::Center);
                label.HorizontalAlignment(labelOnLeft ? HorizontalAlignment::Left : HorizontalAlignment::Center);
                label.Margin(labelOnLeft ? Thickness{0, -2, 3, 0} : Thickness{0, 0, 0, 1});
                label.Opacity(0.8);
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Label_%d", (int)i);
                label.Name(name);
                col.Children().Append(label);
            }

            StackPanel bars;
            bars.Orientation(Orientation::Vertical);
            bars.VerticalAlignment(VerticalAlignment::Center);

            double radius = std::max(1.0, barHeight / 2.0);
            for (int w = 0; w < 2; w++) {
                Border track;
                track.Width(barWidth);
                track.Height(barHeight);
                track.CornerRadius({radius, radius, radius, radius});
                track.Margin({0, 1, 0, 1});
                track.HorizontalAlignment(HorizontalAlignment::Center);
                track.Background(SolidColorBrush(winrt::Windows::UI::Color{0x46, 0x80, 0x80, 0x80}));

                Border fill;
                fill.Height(barHeight);
                fill.Width(0);
                fill.CornerRadius({radius, radius, radius, radius});
                fill.HorizontalAlignment(HorizontalAlignment::Left);
                fill.Background(SolidColorBrush(winrt::Windows::UI::Color{255, 0x9E, 0x9E, 0x9E}));
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Fill_%d_%d", (int)i, w);
                fill.Name(name);

                track.Child(fill);
                bars.Children().Append(track);
            }

            if (showPercentText) {
                Grid overlay;
                overlay.Width(barWidth);
                overlay.VerticalAlignment(VerticalAlignment::Center);
                overlay.Children().Append(bars);

                TextBlock percent;
                percent.FontSize(std::max(8, labelFontSize - 2));
                percent.HorizontalAlignment(HorizontalAlignment::Center);
                percent.VerticalAlignment(VerticalAlignment::Center);
                percent.TextAlignment(TextAlignment::Center);
                percent.Foreground(SolidColorBrush(winrt::Windows::UI::Color{255, 255, 255, 255}));
                percent.Opacity(0.9);
                percent.IsHitTestVisible(false);
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Percent_%d", (int)i);
                percent.Name(name);
                overlay.Children().Append(percent);

                col.Children().Append(overlay);
            } else {
                col.Children().Append(bars);
            }

            ToolTipService::SetToolTip(col, winrt::box_value(winrt::hstring(L"loading...")));
            ToolTipService::SetPlacement(col, wuxcp::PlacementMode::Top);
            UIElement tappedElement = col.as<UIElement>();
            auto tappedToken = tappedElement.Tapped(
                [](winrt::Windows::Foundation::IInspectable const&, wuxi::TappedRoutedEventArgs const& e) {
                    if (g_unloading || !g_quotaGrid) {
                        e.Handled(true);
                        return;
                    }

                    g_refreshing = true;
                    UpdateQuotaUi();
                    if (g_refreshEvent) SetEvent(g_refreshEvent);
                    e.Handled(true);
                });
            g_tapHandlers.push_back({tappedElement, tappedToken});

            panel.Children().Append(col);
        }

        root.Children().Append(panel);
        return root;
    } catch (...) {
        ClearQuotaEventState();
        Wh_Log(L"BuildQuotaGrid: exception");
        return nullptr;
    }
}

static void UpdateQuotaUi() {
    if (!g_quotaGrid) return;

    std::vector<AccountConfig> accounts;
    int intervalMin, barWidth, yellowThreshold, orangeThreshold, redThreshold;
    bool showPercentText, colorblindMode, showStaleWarning;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        accounts = g_settings.accounts;
        intervalMin = g_settings.pollMinutes;
        barWidth = g_settings.barWidth;
        yellowThreshold = g_settings.yellowThreshold;
        orangeThreshold = g_settings.orangeThreshold;
        redThreshold = g_settings.redThreshold;
        showPercentText = g_settings.showPercentText;
        colorblindMode = g_settings.colorblindMode;
        showStaleWarning = g_settings.showStaleWarning;
    }

    std::vector<AccountData> data;
    {
        std::lock_guard<std::mutex> lk(g_dataMutex);
        data = g_data;
    }
    if (data.size() != accounts.size()) return;
    if (g_applied.size() != data.size()) g_applied.assign(data.size(), {});

    ULONGLONG now = NowUnixMs();
    bool refreshing = g_refreshing.load();
    wchar_t name[64];
    try {
        for (size_t i = 0; i < data.size(); i++) {
            const AccountData& d = data[i];
            AppliedState& ap = g_applied[i];
            bool stale = d.stale || d.lastSuccessMs == 0 ||
                         now - d.lastSuccessMs > (ULONGLONG)intervalMin * 2 * 60000;
            bool warn = showStaleWarning && stale && !d.error.empty();

            for (int w = 0; w < 2; w++) {
                const WindowUsage& wu = w == 0 ? d.win5h : d.winWeek;
                int px = wu.pct > 0 ? std::clamp((int)std::lround(barWidth * wu.pct / 100.0), 2, barWidth) : 0;
                auto c = UsageColor(wu.pct, stale, yellowThreshold, orangeThreshold, redThreshold,
                                    colorblindMode);
                uint32_t cv = ((uint32_t)c.A << 24) | ((uint32_t)c.R << 16) |
                              ((uint32_t)c.G << 8) | c.B;
                if (px != ap.fillPx[w] || cv != ap.fillColor[w]) {
                    swprintf(name, ARRAYSIZE(name), L"AiQuota_Fill_%d_%d", (int)i, w);
                    if (auto fe = FindChildByName(g_quotaGrid, name)) {
                        if (auto b = fe.try_as<Border>()) {
                            b.Width(px);
                            b.Background(SolidColorBrush(c));
                        }
                    }
                    ap.fillPx[w] = px;
                    ap.fillColor[w] = cv;
                }
            }

            std::wstring tip = (warn ? L"! " : L"") + accounts[i].label + L" - " +
                               (accounts[i].provider == L"anthropic" ? L"Anthropic" : L"OpenAI");
            if (!d.plan.empty()) tip += L" (" + d.plan + L")";
            wchar_t line[160];
            if (d.win5h.pct >= 0) {
                swprintf(line, ARRAYSIZE(line), L"\n5h: %.0f%% | resets %s", d.win5h.pct,
                         FormatReset(d.win5h.resetUnixMs).c_str());
                tip += line;
            } else {
                tip += L"\n5h: n/a";
            }
            if (d.winWeek.pct >= 0) {
                swprintf(line, ARRAYSIZE(line), L"\nweek: %.0f%% | resets %s", d.winWeek.pct,
                         FormatReset(d.winWeek.resetUnixMs).c_str());
                tip += line;
            } else {
                tip += L"\nweek: n/a";
            }
            if (!d.extraLines.empty()) tip += L"\n" + d.extraLines;
            if (!d.error.empty()) tip += L"\nerror: " + d.error;
            tip += L"\n" + FormatUpdated(d.lastSuccessMs, stale);
            tip += refreshing ? L"\nrefreshing..." : L"\nclick to refresh";

            if (showPercentText) {
                std::wstring percentText;
                if (d.win5h.pct >= 0 && d.winWeek.pct >= 0) {
                    wchar_t text[32];
                    swprintf(text, ARRAYSIZE(text), L"%.0f/%.0f", d.win5h.pct, d.winWeek.pct);
                    percentText = text;
                } else if (d.win5h.pct >= 0 || d.winWeek.pct >= 0) {
                    wchar_t text[32];
                    swprintf(text, ARRAYSIZE(text), L"%.0f%%", d.win5h.pct >= 0 ? d.win5h.pct : d.winWeek.pct);
                    percentText = text;
                }
                if (percentText != ap.percentText) {
                    swprintf(name, ARRAYSIZE(name), L"AiQuota_Percent_%d", (int)i);
                    if (auto fe = FindChildByName(g_quotaGrid, name)) {
                        if (auto tb = fe.try_as<TextBlock>()) tb.Text(percentText);
                    }
                    ap.percentText = percentText;
                }
            }

            if (tip != ap.tip) {
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Acc_%d", (int)i);
                if (auto fe = FindChildByName(g_quotaGrid, name)) {
                    ToolTipService::SetToolTip(fe, winrt::box_value(winrt::hstring(tip)));
                }
                ap.tip = tip;
            }

            double columnOpacity = refreshing ? 0.65 : 1.0;
            if (columnOpacity != ap.columnOpacity) {
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Acc_%d", (int)i);
                if (auto fe = FindChildByName(g_quotaGrid, name)) fe.Opacity(columnOpacity);
                ap.columnOpacity = columnOpacity;
            }

            double labelOpacity = stale ? 0.45 : 0.8;
            std::wstring labelText = warn ? accounts[i].label + L"!" : accounts[i].label;
            if (labelOpacity != ap.labelOpacity || labelText != ap.labelText) {
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Label_%d", (int)i);
                if (auto fe = FindChildByName(g_quotaGrid, name)) {
                    fe.Opacity(labelOpacity);
                    if (auto tb = fe.try_as<TextBlock>()) tb.Text(labelText);
                }
                ap.labelOpacity = labelOpacity;
                ap.labelText = std::move(labelText);
            }
        }
    } catch (...) {
        Wh_Log(L"UpdateQuotaUi: exception");
    }
}

static int RemoveQuotaChildren(Grid const& targetGrid) {
    if (!targetGrid) return -1;
    ClearQuotaEventState();

    int firstCol = -1;
    for (int i = (int)targetGrid.Children().Size() - 1; i >= 0; --i) {
        auto fe = targetGrid.Children().GetAt(i).try_as<FrameworkElement>();
        if (fe && fe.Name() == kRootName) {
            if (firstCol < 0) firstCol = Grid::GetColumn(fe);
            try { targetGrid.Children().RemoveAt(i); } catch (...) {}
        }
    }
    return firstCol;
}

static bool InjectQuotaGrid() {
    if (g_quotaGrid) return true;
    HWND hWnd = g_taskbarWnd ? g_taskbarWnd : FindCurrentProcessTaskbarWnd();
    if (!hWnd) return false;
    g_taskbarWnd = hWnd;

    try {
        auto xamlRoot = GetTaskbarXamlRoot(hWnd);
        if (!xamlRoot) return false;
        auto root = xamlRoot.Content().try_as<FrameworkElement>();
        if (!root) return false;
        auto trayFrame = FindChildByName(root, L"SystemTrayFrameGrid");
        auto trayGrid = trayFrame ? trayFrame.try_as<Grid>() : nullptr;
        if (!trayGrid) return false;

        int oldCol = RemoveQuotaChildren(trayGrid);
        if (oldCol >= 0 && oldCol < (int)trayGrid.ColumnDefinitions().Size()) {
            for (uint32_t i = 0; i < trayGrid.Children().Size(); ++i) {
                auto child = trayGrid.Children().GetAt(i).try_as<FrameworkElement>();
                if (child) {
                    int childCol = Grid::GetColumn(child);
                    if (childCol > oldCol) Grid::SetColumn(child, childCol - 1);
                }
            }
            trayGrid.ColumnDefinitions().RemoveAt(oldCol);
        }
        Grid quota = BuildQuotaGrid();
        if (!quota) return false;

        ColumnDefinition newCol;
        newCol.Width({1.0, GridUnitType::Auto});
        trayGrid.ColumnDefinitions().InsertAt(0, newCol);
        for (uint32_t i = 0; i < trayGrid.Children().Size(); ++i) {
            auto child = trayGrid.Children().GetAt(i).try_as<FrameworkElement>();
            if (child) Grid::SetColumn(child, Grid::GetColumn(child) + 1);
        }
        Grid::SetColumn(quota, 0);
        trayGrid.Children().Append(quota);

        g_quotaGrid = quota;
        g_injectionParent = trayGrid;
        g_quotaColumn = 0;
        g_uiInjected.store(true, std::memory_order_release);
        g_applied.clear();
        UpdateQuotaUi();
        Wh_Log(L"Injected quota bars");
        return true;
    } catch (...) {
        g_uiInjected.store(false, std::memory_order_release);
        ClearQuotaEventState();
        g_quotaGrid = nullptr;
        g_injectionParent = nullptr;
        g_quotaColumn = -1;
        g_applied.clear();
        Wh_Log(L"InjectQuotaGrid: exception");
        return false;
    }
}

static void RemoveQuotaGrid() {
    g_uiInjected.store(false, std::memory_order_release);
    ClearQuotaEventState();

    if (!g_injectionParent) {
        g_quotaGrid = nullptr;
        g_applied.clear();
        return;
    }

    try {
        auto targetGrid = g_injectionParent;
        int quotaCol = RemoveQuotaChildren(targetGrid);
        if (quotaCol >= 0 && quotaCol < (int)targetGrid.ColumnDefinitions().Size()) {
            for (uint32_t i = 0; i < targetGrid.Children().Size(); ++i) {
                auto child = targetGrid.Children().GetAt(i).try_as<FrameworkElement>();
                if (child) {
                    int childCol = Grid::GetColumn(child);
                    if (childCol > quotaCol) Grid::SetColumn(child, childCol - 1);
                }
            }
            targetGrid.ColumnDefinitions().RemoveAt(quotaCol);
        }
    } catch (...) {
        Wh_Log(L"RemoveQuotaGrid: exception");
    }

    g_quotaGrid = nullptr;
    g_injectionParent = nullptr;
    g_quotaColumn = -1;
    g_applied.clear();
}

static DWORD WINAPI RetryInjectThreadProc(LPVOID) {
    for (int attempt = 0; attempt < 600 && !g_unloading; attempt++) {
        HWND hWnd = g_taskbarWnd ? g_taskbarWnd : FindCurrentProcessTaskbarWnd();
        if (hWnd) {
            g_taskbarWnd = hWnd;
            bool injected = false;
            RunFromWindowThread(hWnd, [](void* param) {
                *static_cast<bool*>(param) = !g_unloading && InjectQuotaGrid();
            }, &injected);
            if (injected) break;
        }

        if (g_stopEvent) {
            if (WaitForSingleObject(g_stopEvent, 100) == WAIT_OBJECT_0) break;
        } else {
            Sleep(100);
        }
    }
    return 0;
}

static void StartRetryInject() {
    if (g_unloading) return;

    if (g_retryThread) {
        DWORD state = WaitForSingleObject(g_retryThread, 0);
        if (state == WAIT_TIMEOUT) return;
        CloseHandle(g_retryThread);
        g_retryThread = nullptr;
    }

    g_retryThread = CreateThread(nullptr, 0, RetryInjectThreadProc, nullptr, 0, nullptr);
}

/**********************************************/
//  Hooks
/**********************************************/

using TrayUI_StartTaskbar_t = void(WINAPI*)(void*);
static TrayUI_StartTaskbar_t TrayUI_StartTaskbar_Original = nullptr;

static void WINAPI TrayUI_StartTaskbar_Hook(void* pThis) {
    TrayUI_StartTaskbar_Original(pThis);
    if (g_unloading) return;

    g_uiInjected.store(false, std::memory_order_release);
    ClearQuotaEventState();
    g_quotaGrid = nullptr;
    g_injectionParent = nullptr;
    g_quotaColumn = -1;
    g_applied.clear();

    HWND hWnd = FindCurrentProcessTaskbarWnd();
    if (!hWnd) return;
    g_taskbarWnd = hWnd;

    StartRetryInject();
}

static bool HookTaskbarDllSymbols() {
    HMODULE h = LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) return false;

    WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
        {{LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"}, &CTaskBand_ITaskListWndSite_vftable},
        {{LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CTaskBand::GetTaskbarHost(void)const )"}, &CTaskBand_GetTaskbarHost_Original},
        {{LR"(public: int __cdecl TaskbarHost::FrameHeight(void)const )"}, &TaskbarHost_FrameHeight_Original},
        {{LR"(public: void __cdecl std::_Ref_count_base::_Decref(void))"}, &Std_Ref_Decref_Original},
        {{LR"(public: virtual void __cdecl TrayUI::StartTaskbar(void))"}, &TrayUI_StartTaskbar_Original, TrayUI_StartTaskbar_Hook},
    };
    return WindhawkUtils::HookSymbols(h, taskbarDllHooks, ARRAYSIZE(taskbarDllHooks));
}

/**********************************************/
//  Settings
/**********************************************/

static void LoadSettings() {
    Settings s;
    for (int i = 0;; i++) {
        PCWSTR provider = Wh_GetStringSetting(L"accounts[%d].provider", i);
        if (!*provider) {
            Wh_FreeStringSetting(provider);
            break;
        }

        AccountConfig a;
        a.provider = provider;
        Wh_FreeStringSetting(provider);

        PCWSTR label = Wh_GetStringSetting(L"accounts[%d].label", i);
        a.label = label;
        Wh_FreeStringSetting(label);

        PCWSTR authFile = Wh_GetStringSetting(L"accounts[%d].authFile", i);
        a.authFile = ExpandEnv(authFile);
        Wh_FreeStringSetting(authFile);
        if (a.authFile.empty()) a.authFile = ExpandEnv(kDefaultAuthFile);

        PCWSTR authKey = Wh_GetStringSetting(L"accounts[%d].authKey", i);
        a.authKey = authKey;
        Wh_FreeStringSetting(authKey);

        if (a.label.empty()) a.label = a.provider == L"anthropic" ? L"A" : L"O";
        s.accounts.push_back(std::move(a));
    }

    if (s.accounts.empty()) {
        std::wstring authFile = ExpandEnv(kDefaultAuthFile);
        s.accounts.push_back({L"anthropic", L"A", authFile, L""});
        s.accounts.push_back({L"openai", L"O", authFile, L""});
    }

    int pollMinutes = Wh_GetIntSetting(L"pollIntervalMinutes");
    int barWidth = Wh_GetIntSetting(L"barWidth");
    int barHeight = Wh_GetIntSetting(L"barHeight");
    int labelFontSize = Wh_GetIntSetting(L"labelFontSize");
    int yellowThreshold = Wh_GetIntSetting(L"yellowThreshold");
    int orangeThreshold = Wh_GetIntSetting(L"orangeThreshold");
    int redThreshold = Wh_GetIntSetting(L"redThreshold");
    s.pollMinutes = std::clamp(pollMinutes > 0 ? pollMinutes : 10, 2, 24 * 60);
    s.barWidth = std::clamp(barWidth > 0 ? barWidth : 100, 10, 100);
    s.barHeight = std::clamp(barHeight > 0 ? barHeight : 8, 2, 20);
    s.labelFontSize = std::clamp(labelFontSize > 0 ? labelFontSize : 11, 6, 24);
    s.yellowThreshold = std::clamp(yellowThreshold, 0, 100);
    s.orangeThreshold = std::clamp(orangeThreshold, s.yellowThreshold, 100);
    s.redThreshold = std::clamp(redThreshold, s.orangeThreshold, 100);
    s.showLabels = Wh_GetIntSetting(L"showLabels") != 0;
    s.labelOnLeft = Wh_GetIntSetting(L"labelOnLeft") != 0;
    s.showPercentText = Wh_GetIntSetting(L"showPercentText") != 0;
    s.colorblindMode = Wh_GetIntSetting(L"colorblindMode") != 0;
    s.showStaleWarning = Wh_GetIntSetting(L"showStaleWarning") != 0;

    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        g_settings = std::move(s);
    }
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        std::lock_guard<std::mutex> lk2(g_dataMutex);
        g_data.assign(g_settings.accounts.size(), {});
    }
}

/**********************************************/
//  Lifecycle
/**********************************************/

BOOL Wh_ModInit() {
    Wh_Log(L"Init");
    g_unloading = false;
    g_refreshing = false;
    g_uiInjected.store(false, std::memory_order_release);
    LoadSettings();

    if (!HookTaskbarDllSymbols()) {
        Wh_Log(L"HookTaskbarDllSymbols failed");
        return FALSE;
    }

    g_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    g_refreshEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    return g_stopEvent && g_refreshEvent;
}

void Wh_ModAfterInit() {
    g_taskbarWnd = FindCurrentProcessTaskbarWnd();
    if (g_taskbarWnd) StartRetryInject();
    g_fetchThread = CreateThread(nullptr, 0, FetchThreadProc, nullptr, 0, nullptr);
}

void Wh_ModUninit() {
    Wh_Log(L"Uninit");
    g_unloading = true;
    g_uiInjected.store(false, std::memory_order_release);
    if (g_stopEvent) SetEvent(g_stopEvent);

    if (g_retryThread) {
        WaitForSingleObject(g_retryThread, INFINITE);
        CloseHandle(g_retryThread);
        g_retryThread = nullptr;
    }

    if (g_fetchThread) {
        WaitForSingleObject(g_fetchThread, INFINITE);
        CloseHandle(g_fetchThread);
        g_fetchThread = nullptr;
    }

    HWND hWnd = g_taskbarWnd ? g_taskbarWnd : FindCurrentProcessTaskbarWnd();
    if (hWnd && !RunFromWindowThread(hWnd, [](void*) { RemoveQuotaGrid(); }, nullptr)) {
        Wh_Log(L"RemoveQuotaGrid marshal failed");
    }

    if (g_stopEvent) CloseHandle(g_stopEvent);
    if (g_refreshEvent) CloseHandle(g_refreshEvent);
    g_stopEvent = nullptr;
    g_refreshEvent = nullptr;
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"SettingsChanged");
    LoadSettings();

    HWND hWnd = g_taskbarWnd ? g_taskbarWnd : FindCurrentProcessTaskbarWnd();
    if (hWnd) {
        RunFromWindowThread(hWnd, [](void*) {
            RemoveQuotaGrid();
            InjectQuotaGrid();
        }, nullptr);
    }
    if (g_refreshEvent) SetEvent(g_refreshEvent);
}
