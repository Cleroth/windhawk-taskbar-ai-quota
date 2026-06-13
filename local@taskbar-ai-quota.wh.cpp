// ==WindhawkMod==
// @id              taskbar-ai-quota
// @name            Taskbar AI Quota Bars
// @description     Shows compact 5-hour and weekly AI agent/LLM subscription quota bars for Anthropic and OpenAI on the Windows 11 taskbar
// @version         0.4
// @author          Cleroth
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lwindowsapp -lwinhttp -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar AI Quota Bars

Shows Anthropic Claude and OpenAI/Codex AI agent and LLM subscription quota usage as
compact bars on the Windows 11 taskbar, next to the system tray.
Can show on the primary taskbar only, all taskbars, or one specific monitor.

Each account gets one narrow column:
- top bar: 5-hour usage
- bottom bar: weekly usage

Hover for exact percentages and reset times. Click a column to refresh that account.
Bars use configurable green/yellow/orange/red thresholds, with a colorblind palette option.
Stale errors can mark labels/tooltips with `!`.

Supported credential sources:
- OpenCode: `%USERPROFILE%\.local\share\opencode\auth.json`
- Claude Code: `%USERPROFILE%\.claude\.credentials.json`
- Codex: `%USERPROFILE%\.codex\auth.json`

Credentials are read-only. The mod never refreshes or rewrites tokens, and never sends
refresh tokens as bearer tokens. Run OpenCode, Claude Code, or Codex to refresh their
own auth files if requests start returning `401`.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- accounts:
    - - provider: anthropic-opencode
        $name: Provider
        $description: 'Choose the API provider and credential source.'
        $options:
          - anthropic-opencode: Anthropic (OpenCode)
          - openai-opencode: OpenAI (OpenCode)
          - anthropic-claude-code: Anthropic (Claude Code)
          - openai-codex: OpenAI (Codex)
      - label: A
        $name: Label
        $description: 'Default: A for Anthropic, O for OpenAI'
      - authFile: ""
        $name: Auth file
        $description: 'Default: empty = provider default. OpenCode: %USERPROFILE%\.local\share\opencode\auth.json; Claude Code: %USERPROFILE%\.claude\.credentials.json; Codex: %USERPROFILE%\.codex\auth.json'
      - authKey: ""
        $name: Auth JSON key
        $description: 'Default: empty. OpenCode uses anthropic/openai; Claude Code and Codex ignore this.'
    - - provider: openai-opencode
      - label: O
      - authFile: ""
      - authKey: ""
  $name: Accounts
  $description: 'Default: two accounts, Anthropic A and OpenAI O'
- taskbarMonitorMode: primary
  $name: Taskbar monitors
  $description: 'Default: Primary only. Choose where quota bars are shown.'
  $options:
    - primary: Primary monitor only
    - all: All monitors
    - specific: Specific monitor number
- taskbarMonitorNumber: 1
  $name: Specific monitor number
  $description: 'Default: 1. Used when Taskbar monitors is Specific. 1 is the primary taskbar; 2+ are secondary taskbars in monitor order.'
- pollIntervalMinutes: 10
  $name: Poll interval (minutes)
  $description: 'Default: 10'
- barWidth: 100
  $name: Bar width (px)
  $description: 'Default: 100. Minimum: 10'
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
- accountMargin: 3
  $name: Account margin (px)
  $description: 'Default: 3. Horizontal margin around each account column.'
- labelGap: 3
  $name: Label gap (px)
  $description: 'Default: 3. Gap between label and bars.'
- barGap: 2
  $name: Bar gap (px)
  $description: 'Default: 2. Vertical gap between the two quota bars.'
- rightMargin: 4
  $name: Right tray gap (px)
  $description: 'Default: 4. Gap between quota bars and the system tray side.'
- showPercentText: false
  $name: Show percent text
  $description: 'Default: false. Shows compact 5h/week percentages over the bars.'
- showCodexSparkInTooltip: false
  $name: Show Codex Spark in tooltip
  $description: 'Default: false. Shows OpenAI/Codex Spark plan and rate-limit lines in tooltips.'
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
    std::wstring authSource;
    std::wstring label;
    std::wstring authFile;
    std::wstring authKey;
};

enum class TaskbarMonitorMode {
    Primary,
    All,
    Specific,
};

struct Settings {
    std::vector<AccountConfig> accounts;
    TaskbarMonitorMode taskbarMonitorMode = TaskbarMonitorMode::Primary;
    int taskbarMonitorNumber = 1;
    int pollMinutes = 10;
    int barWidth = 100;
    int barHeight = 8;
    int labelFontSize = 11;
    int accountMargin = 3;
    int labelGap = 3;
    int barGap = 2;
    int rightMargin = 4;
    int yellowThreshold = 50;
    int orangeThreshold = 75;
    int redThreshold = 90;
    bool showLabels = true;
    bool labelOnLeft = true;
    bool showPercentText = false;
    bool showCodexSparkInTooltip = false;
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
    std::wstring codexSparkLines;
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

struct QuotaUiInstance {
    HWND hWnd = nullptr;
    Grid quotaGrid{nullptr};
    Grid injectionParent{nullptr};
    int quotaColumn = -1;
    bool insertedColumn = false;
    std::vector<TapHandler> tapHandlers;
    std::vector<AppliedState> applied;
};

struct InjectTaskbarParam {
    HWND hWnd;
    bool* injected;
};

static Settings g_settings;
static std::mutex g_settingsMutex;
static ULONGLONG g_settingsGeneration = 0;
static std::vector<AccountData> g_data;
static std::mutex g_dataMutex;

static std::atomic<bool> g_unloading{false};
static std::atomic<bool> g_refreshing{false};
static std::atomic<int> g_refreshAccountIndex{-1};
static std::atomic<ULONGLONG> g_refreshGeneration{0};
static std::atomic<bool> g_uiInjected{false};
static std::atomic<bool> g_fetchThreadStarted{false};
static HANDLE g_stopEvent = nullptr;
static HANDLE g_refreshEvent = nullptr;
static HANDLE g_fetchThread = nullptr;
static HANDLE g_retryThread = nullptr;
static std::atomic<ULONGLONG> g_nextInjectFailureLogMs{0};
static std::mutex g_httpHandlesMutex;
static std::vector<HINTERNET> g_httpHandles;

static std::vector<std::unique_ptr<QuotaUiInstance>> g_uiInstances;

static const wchar_t* kRootName = L"AiQuota_Root";
static const wchar_t* kDefaultOpenCodeAuthFile = L"%USERPROFILE%\\.local\\share\\opencode\\auth.json";
static const wchar_t* kDefaultClaudeCodeAuthFile = L"%USERPROFILE%\\.claude\\.credentials.json";
static const wchar_t* kDefaultCodexAuthFile = L"%USERPROFILE%\\.codex\\auth.json";

using WindowThreadProc = void (*)(void*);
static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param);
static std::vector<HWND> FindCurrentProcessTaskbarWnds();
static QuotaUiInstance* FindUiState(HWND hWnd);
static void UpdateQuotaUi(QuotaUiInstance& state);
static void PostUiUpdate();

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
    if (n == 0) return s;
    if (n > ARRAYSIZE(buf)) {
        std::wstring expanded(n, L'\0');
        DWORD written = ExpandEnvironmentStringsW(s, expanded.data(), n);
        if (written == 0 || written > n) return s;
        expanded.resize(written - 1);
        return expanded;
    }
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
        PCWSTR p = s.c_str() + sign + 1;
        size_t end = s.find_first_not_of(L"0123456789:", sign + 1);
        size_t len = (end == std::wstring::npos ? s.size() : end) - sign - 1;
        auto digit = [](wchar_t c) { return c >= L'0' && c <= L'9'; };
        bool parsedOffset = len >= 2 && digit(p[0]) && digit(p[1]);
        if (parsedOffset) {
            oh = (p[0] - L'0') * 10 + (p[1] - L'0');
            if (len == 5 && p[2] == L':' && digit(p[3]) && digit(p[4])) {
                om = (p[3] - L'0') * 10 + (p[4] - L'0');
            } else if (len == 4 && digit(p[2]) && digit(p[3])) {
                om = (p[2] - L'0') * 10 + (p[3] - L'0');
            } else if (len != 2) {
                parsedOffset = false;
            }
        }
        if (parsedOffset && oh <= 23 && om <= 59) {
            LONGLONG off = ((LONGLONG)oh * 60 + om) * 60000;
            unixMs = s[sign] == L'+' ? unixMs - off : unixMs + off;
        }
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

static ULONGLONG GetExpiryMs(JsonObject const& o) {
    double expiry = GetNum(o, L"expires", 0);
    if (expiry <= 0) expiry = GetNum(o, L"expiresAt", 0);
    if (expiry <= 0) expiry = GetNum(o, L"expires_at", 0);
    if (expiry > 0) {
        ULONGLONG expiryMs = (ULONGLONG)expiry;
        return expiryMs < 100000000000ULL ? expiryMs * 1000 : expiryMs;
    }

    std::wstring expiryText = GetStr(o, L"expires");
    if (expiryText.empty()) expiryText = GetStr(o, L"expiresAt");
    if (expiryText.empty()) expiryText = GetStr(o, L"expires_at");
    return expiryText.empty() ? 0 : ParseIso8601Ms(expiryText);
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

    bool legacyAutoSource = acc.authSource == L"auto";

    if (acc.authSource == L"opencode" || legacyAutoSource) {
        std::wstring key = acc.authKey.empty() ? acc.provider : acc.authKey;
        if (auto entry = GetObj(root, key.c_str())) {
            a.accessToken = GetStr(entry, L"access");
            a.accountId = GetStr(entry, L"accountId");
            a.expiresMs = GetExpiryMs(entry);
            a.ok = !a.accessToken.empty();
            if (!a.ok) a.err = L"no access token";
            if (a.ok || !legacyAutoSource) return a;
        }
        if (!legacyAutoSource) {
            a.err = L"no OpenCode credentials found";
            return a;
        }
    }

    if ((acc.authSource == L"claude-code" || legacyAutoSource) && acc.provider == L"anthropic") {
        if (auto entry = GetObj(root, L"claudeAiOauth")) {
            a.accessToken = GetStr(entry, L"accessToken");
            a.expiresMs = GetExpiryMs(entry);
            a.ok = !a.accessToken.empty();
            if (!a.ok) a.err = L"no access token";
            return a;
        }
        if (!legacyAutoSource) {
            a.err = L"no Claude Code credentials found";
            return a;
        }
    }

    if ((acc.authSource == L"codex" || legacyAutoSource) && acc.provider == L"openai") {
        if (auto entry = GetObj(root, L"tokens")) {
            a.accessToken = GetStr(entry, L"access_token");
            a.accountId = GetStr(entry, L"account_id");
            a.expiresMs = GetExpiryMs(entry);
            a.ok = !a.accessToken.empty();
            if (!a.ok) a.err = L"no access token";
            return a;
        }
        if (!legacyAutoSource) {
            a.err = L"no Codex credentials found";
            return a;
        }
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

static HttpResult HttpGet(PCWSTR host, PCWSTR path, PCWSTR userAgent,
                          const std::wstring& headers) {
    HttpResult res;
    HINTERNET ses = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                0);
    if (ses && !TrackHttpHandle(ses)) ses = nullptr;
    HINTERNET con = nullptr;
    HINTERNET req = nullptr;

    if (ses && !g_unloading) {
        WinHttpSetTimeouts(ses, 5000, 5000, 5000, 10000);
        con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (con && !TrackHttpHandle(con)) con = nullptr;
        req = con ? WinHttpOpenRequest(con, L"GET", path, nullptr,
                                        WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE)
                  : nullptr;
        if (req && !TrackHttpHandle(req)) req = nullptr;
    }

    if (!g_unloading && req && WinHttpSendRequest(req, headers.c_str(), (DWORD)headers.size(),
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

    if (req && UntrackHttpHandle(req)) WinHttpCloseHandle(req);
    if (con && UntrackHttpHandle(con)) WinHttpCloseHandle(con);
    if (ses && UntrackHttpHandle(ses)) WinHttpCloseHandle(ses);
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
        auto hasWindows = [](JsonObject const& o) -> bool {
            return GetObj(o, L"primary_window") || GetObj(o, L"secondary_window") ||
                   GetObj(o, L"primary") || GetObj(o, L"secondary") ||
                   GetObj(o, L"five_hour") || GetObj(o, L"weekly") ||
                   GetObj(o, L"five_hour_limit") || GetObj(o, L"weekly_limit");
        };

        JsonObject usage = root;
        if (!GetObj(root, L"rate_limit") && !hasWindows(root)) {
            if (auto wrapped = GetObj(root, L"usage");
                wrapped && (GetObj(wrapped, L"rate_limit") || hasWindows(wrapped))) {
                usage = wrapped;
            } else if (auto wrapped = GetObj(root, L"data");
                       wrapped && (GetObj(wrapped, L"rate_limit") || hasWindows(wrapped))) {
                usage = wrapped;
            }
        }

        auto rl = GetObj(usage, L"rate_limit");
        if (!rl && hasWindows(usage)) rl = usage;

        auto resetUnixMs = [](JsonObject const& window) -> ULONGLONG {
            double resetAt = GetNum(window, L"reset_time_ms", 0);
            if (resetAt > 0) return (ULONGLONG)resetAt;

            resetAt = GetNum(window, L"reset_at", 0);
            if (resetAt > 100000000000.0) return (ULONGLONG)resetAt;
            if (resetAt > 0) return (ULONGLONG)(resetAt * 1000);

            double resetAfter = GetNum(window, L"reset_after_seconds", 0);
            return resetAfter > 0 ? NowUnixMs() + (ULONGLONG)(resetAfter * 1000) : 0ULL;
        };

        auto applyWindow = [&](JsonObject const& window, WindowUsage* fallback) {
            if (!window) return;
            WindowUsage* target = fallback;
            double windowSeconds = GetNum(window, L"limit_window_seconds", 0);
            if (windowSeconds == 5 * 60 * 60) target = &d->win5h;
            else if (windowSeconds == 7 * 24 * 60 * 60) target = &d->winWeek;

            double pct = GetNum(window, L"used_percent");
            if (pct < 0) {
                pct = GetNum(window, L"percent_left");
                if (pct >= 0) pct = 100 - pct;
            }
            if (pct < 0) {
                pct = GetNum(window, L"remaining_percent");
                if (pct >= 0) pct = 100 - pct;
            }
            if (pct < 0) return;
            target->pct = pct;
            target->resetUnixMs = resetUnixMs(window);
        };

        applyWindow(GetObj(rl, L"primary_window"), &d->win5h);
        applyWindow(GetObj(rl, L"secondary_window"), &d->winWeek);
        applyWindow(GetObj(rl, L"primary"), &d->win5h);
        applyWindow(GetObj(rl, L"secondary"), &d->winWeek);
        applyWindow(GetObj(rl, L"five_hour"), &d->win5h);
        applyWindow(GetObj(rl, L"weekly"), &d->winWeek);
        applyWindow(GetObj(rl, L"five_hour_limit"), &d->win5h);
        applyWindow(GetObj(rl, L"weekly_limit"), &d->winWeek);

        d->plan = GetStr(usage, L"plan_type");
        d->codexSparkLines.clear();
        d->extraLines.clear();
        int extraLimitLineCount = 0;
        auto addLimitLine = [&](JsonObject const& item) {
            auto itemRl = GetObj(item, L"rate_limit");
            auto pw = GetObj(itemRl, L"primary_window");
            auto sw = GetObj(itemRl, L"secondary_window");
            std::wstring name = GetStr(item, L"limit_name");
            if (name.empty() || (!pw && !sw)) return;
            bool isCodexSpark = name.find(L"Codex-Spark") != std::wstring::npos ||
                                 name.find(L"Codex Spark") != std::wstring::npos ||
                                 name.find(L"codex-spark") != std::wstring::npos ||
                                 name.find(L"codex spark") != std::wstring::npos;
            if (!isCodexSpark && extraLimitLineCount >= 3) return;

            wchar_t line[128];
            swprintf(line, ARRAYSIZE(line), L"%s: 5h %.0f%% | wk %.0f%%",
                     name.c_str(), GetNum(pw, L"used_percent", 0),
                     GetNum(sw, L"used_percent", 0));
            std::wstring& target = isCodexSpark ? d->codexSparkLines : d->extraLines;
            if (!target.empty()) target += L"\n";
            target += line;
            if (!isCodexSpark) extraLimitLineCount++;
        };
        if (usage.HasKey(L"additional_rate_limits")) {
            auto limits = usage.GetNamedValue(L"additional_rate_limits");
            if (limits.ValueType() == JsonValueType::Array) {
                auto arr = limits.GetArray();
                for (uint32_t i = 0; i < arr.Size(); i++) {
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
                               L"\r\nOrigin: https://chatgpt.com"
                               L"\r\nReferer: https://chatgpt.com/"
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

static void PostUiUpdate() {
    // Never resolve XAML refs on the fetch thread; marshal first.
    if (g_unloading || !g_uiInjected.load(std::memory_order_acquire)) return;

    for (HWND hWnd : FindCurrentProcessTaskbarWnds()) {
        RunFromWindowThread(hWnd, [](void* param) {
            HWND hWnd = static_cast<HWND>(param);
            auto* state = FindUiState(hWnd);
            if (!g_unloading && state && state->quotaGrid) UpdateQuotaUi(*state);
        }, hWnd);
    }
}

static DWORD WINAPI FetchThreadProc(LPVOID) {
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartmentInitialized = true;
    } catch (...) {}

    std::vector<std::wstring> lastLoggedErrorStates;
    std::vector<ULONGLONG> retryDeadlineMs;
    ULONGLONG lastLoggedSettingsGeneration = 0;
    while (!g_unloading) {
        ULONGLONG refreshGeneration = g_refreshGeneration.load();
        int refreshAccountIndex = g_refreshAccountIndex.load();
        std::vector<AccountConfig> accounts;
        int intervalMin;
        ULONGLONG settingsGeneration;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            accounts = g_settings.accounts;
            intervalMin = g_settings.pollMinutes;
            settingsGeneration = g_settingsGeneration;
        }
        if (lastLoggedSettingsGeneration != settingsGeneration ||
            lastLoggedErrorStates.size() != accounts.size()) {
            lastLoggedErrorStates.assign(accounts.size(), {});
            retryDeadlineMs.assign(accounts.size(), 0);
            lastLoggedSettingsGeneration = settingsGeneration;
        }

        std::vector<AccountData> results(accounts.size());
        {
            std::lock_guard<std::mutex> lk(g_dataMutex);
            if (g_data.size() == results.size()) results = g_data;
        }

        bool refreshSingleAccount = g_refreshing.load() && refreshAccountIndex >= 0 &&
                                    refreshAccountIndex < (int)accounts.size();
        ULONGLONG nextRetryMs = 0;
        bool anyError = false;
        for (size_t i = 0; i < accounts.size() && !g_unloading; i++) {
            ULONGLONG nowMs = NowUnixMs();
            if (refreshSingleAccount && (int)i != refreshAccountIndex) {
                if (retryDeadlineMs[i] > nowMs &&
                    (nextRetryMs == 0 || retryDeadlineMs[i] < nextRetryMs)) {
                    nextRetryMs = retryDeadlineMs[i];
                }
                continue;
            }

            if (retryDeadlineMs[i] > nowMs) {
                if (nextRetryMs == 0 || retryDeadlineMs[i] < nextRetryMs) {
                    nextRetryMs = retryDeadlineMs[i];
                }
                continue;
            }

            int retryAfter = 0;
            FetchAccount(accounts[i], &results[i], &retryAfter);
            if (retryAfter > 0) {
                retryDeadlineMs[i] = NowUnixMs() + (ULONGLONG)retryAfter * 1000;
                if (nextRetryMs == 0 || retryDeadlineMs[i] < nextRetryMs) {
                    nextRetryMs = retryDeadlineMs[i];
                }
            } else {
                retryDeadlineMs[i] = 0;
            }

            if (!results[i].error.empty()) {
                if (retryAfter <= 0) anyError = true;
                std::wstring errorState = accounts[i].provider + L"\n" + accounts[i].authSource + L"\n" +
                                          accounts[i].authFile + L"\n" + accounts[i].authKey + L"\n" +
                                          results[i].error;
                if (errorState != lastLoggedErrorStates[i]) {
                    Wh_Log(L"Fetch [%d] %s/%s: %s", (int)i, accounts[i].provider.c_str(),
                           accounts[i].authSource.c_str(), results[i].error.c_str());
                    lastLoggedErrorStates[i] = std::move(errorState);
                }
            } else {
                lastLoggedErrorStates[i].clear();
            }
        }

        bool published = false;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            if (settingsGeneration == g_settingsGeneration) {
                std::lock_guard<std::mutex> lk2(g_dataMutex);
                if (g_data.size() == results.size()) {
                    g_data = std::move(results);
                    published = true;
                }
            }
        }
        if (refreshGeneration == g_refreshGeneration.load()) {
            g_refreshing = false;
            g_refreshAccountIndex = -1;
        }
        if (published) PostUiUpdate();

        DWORD waitMs = (DWORD)intervalMin * 60000;
        if (anyError) waitMs = std::min(waitMs, (DWORD)120000);
        ULONGLONG nowMs = NowUnixMs();
        if (nextRetryMs > nowMs) {
            waitMs = (DWORD)std::min<ULONGLONG>(waitMs, nextRetryMs - nowMs);
        }

        HANDLE handles[2] = {g_stopEvent, g_refreshEvent};
        if (WaitForMultipleObjects(2, handles, FALSE, waitMs) == WAIT_OBJECT_0) break;
    }
    if (apartmentInitialized) winrt::uninit_apartment();
    return 0;
}

/**********************************************/
//  Taskbar XAML Access
/**********************************************/

using CTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void*, void*);
using CSecondaryTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void*, void*);
using TaskbarHost_FrameHeight_t = int(WINAPI*)(void*);
using Std_Ref_Decref_t = void(WINAPI*)(void*);

static CTaskBand_GetTaskbarHost_t CTaskBand_GetTaskbarHost_Original = nullptr;
static CSecondaryTaskBand_GetTaskbarHost_t CSecondaryTaskBand_GetTaskbarHost_Original = nullptr;
static TaskbarHost_FrameHeight_t TaskbarHost_FrameHeight_Original = nullptr;
static Std_Ref_Decref_t Std_Ref_Decref_Original = nullptr;
static void* CTaskBand_ITaskListWndSite_vftable = nullptr;
static void* CSecondaryTaskBand_ITaskListWndSite_vftable = nullptr;

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
    DWORD_PTR ignored = 0;
    // Avoid worker/UI deadlocks during unload if the target thread stops pumping messages.
    if (!SendMessageTimeoutW(hWnd, kMsg, 0, reinterpret_cast<LPARAM>(&pay),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &ignored)) {
        UnhookWindowsHookEx(hook);
        return false;
    }
    UnhookWindowsHookEx(hook);
    return true;
}

static std::vector<HWND> FindCurrentProcessTaskbarWnds() {
    TaskbarMonitorMode mode;
    int targetMonitorNumber;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        mode = g_settings.taskbarMonitorMode;
        targetMonitorNumber = g_settings.taskbarMonitorNumber;
    }

    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lp) CALLBACK -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(hMonitor);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors));

    struct TaskbarWindowInfo {
        HWND hWnd;
        bool primary;
        int monitorNumber;
    };
    struct EnumContext {
        DWORD pid;
        TaskbarMonitorMode mode;
        std::vector<HMONITOR>* monitors;
        std::vector<TaskbarWindowInfo>* windows;
    } ctx{GetCurrentProcessId(), mode, &monitors, nullptr};

    std::vector<TaskbarWindowInfo> windows;
    ctx.windows = &windows;
    EnumWindows([](HWND hWnd, LPARAM lp) CALLBACK -> BOOL {
        auto* ctx = reinterpret_cast<EnumContext*>(lp);
        DWORD pid = 0;
        wchar_t cls[64] = {};
        if (!GetWindowThreadProcessId(hWnd, &pid) || pid != ctx->pid ||
            !GetClassNameW(hWnd, cls, ARRAYSIZE(cls))) {
            return TRUE;
        }

        bool primary = _wcsicmp(cls, L"Shell_TrayWnd") == 0;
        bool secondary = _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0;
        if (!primary && !secondary) return TRUE;
        if (ctx->mode == TaskbarMonitorMode::Primary && !primary) return TRUE;

        int monitorNumber = 0;
        if (HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST)) {
            for (size_t i = 0; i < ctx->monitors->size(); ++i) {
                if ((*ctx->monitors)[i] == hMonitor) {
                    monitorNumber = (int)i + 1;
                    break;
                }
            }
        }
        ctx->windows->push_back({hWnd, primary, monitorNumber});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    std::sort(windows.begin(), windows.end(), [](const auto& a, const auto& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.monitorNumber != b.monitorNumber) {
            if (a.monitorNumber == 0) return false;
            if (b.monitorNumber == 0) return true;
            return a.monitorNumber < b.monitorNumber;
        }
        return reinterpret_cast<UINT_PTR>(a.hWnd) < reinterpret_cast<UINT_PTR>(b.hWnd);
    });

    std::vector<HWND> result;
    result.reserve(windows.size());
    if (mode == TaskbarMonitorMode::Specific) {
        if (targetMonitorNumber >= 1 && targetMonitorNumber <= (int)windows.size()) {
            result.push_back(windows[targetMonitorNumber - 1].hWnd);
        }
        return result;
    }

    for (const auto& window : windows) result.push_back(window.hWnd);
    return result;
}

static HRESULT TryGetTaskbarElementAbi(HWND hTaskbarWnd, void** result) {
    *result = nullptr;
    void* taskbarHostSharedPtr[2]{};

    auto cleanup = [&]() {
        if (taskbarHostSharedPtr[1] && Std_Ref_Decref_Original) Std_Ref_Decref_Original(taskbarHostSharedPtr[1]);
    };

    wchar_t cls[64] = {};
    bool isSecondary = GetClassNameW(hTaskbarWnd, cls, ARRAYSIZE(cls)) &&
                       _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0;
    HWND hTaskSwWnd = isSecondary ? FindWindowExW(hTaskbarWnd, nullptr, L"WorkerW", nullptr)
                                  : (HWND)GetProp(hTaskbarWnd, L"TaskbandHWND");
    if (!hTaskSwWnd) return E_HANDLE;

    void* taskBand = (void*)GetWindowLongPtrW(hTaskSwWnd, 0);
    if (!taskBand) return E_POINTER;

    void* taskBandVftable = isSecondary ? CSecondaryTaskBand_ITaskListWndSite_vftable
                                        : CTaskBand_ITaskListWndSite_vftable;
    auto getTaskbarHost = isSecondary ? CSecondaryTaskBand_GetTaskbarHost_Original
                                      : CTaskBand_GetTaskbarHost_Original;
    if (!taskBandVftable || !getTaskbarHost) return E_NOINTERFACE;

    void* taskBandForTaskListWndSite = taskBand;
    for (int i = 0; *(void**)taskBandForTaskListWndSite != taskBandVftable; i++) {
        if (i == 20) return E_NOINTERFACE;
        taskBandForTaskListWndSite = (void**)taskBandForTaskListWndSite + 1;
        if (!taskBandForTaskListWndSite) return E_POINTER;
    }

    getTaskbarHost(taskBandForTaskListWndSite, taskbarHostSharedPtr);
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

static QuotaUiInstance* FindUiState(HWND hWnd) {
    for (auto& state : g_uiInstances) {
        if (state->hWnd == hWnd) return state.get();
    }
    return nullptr;
}

static void EraseUiState(HWND hWnd) {
    g_uiInstances.erase(std::remove_if(g_uiInstances.begin(), g_uiInstances.end(),
        [hWnd](const auto& state) { return state->hWnd == hWnd; }), g_uiInstances.end());
    g_uiInjected.store(!g_uiInstances.empty(), std::memory_order_release);
}

static void ClearQuotaEventState(QuotaUiInstance& state) {
    // Routed event delegates point into this DLL, so revoke before XAML tears down the subtree.
    for (auto& handler : state.tapHandlers) {
        if (!handler.element) continue;

        try { handler.element.Tapped(handler.token); } catch (...) {}
        try {
            ToolTipService::SetToolTip(handler.element, winrt::Windows::Foundation::IInspectable{nullptr});
        } catch (...) {}
    }
    state.tapHandlers.clear();
}

static Grid BuildQuotaGrid(QuotaUiInstance& state) {
    try {
        std::vector<AccountConfig> accounts;
        int barWidth, barHeight, labelFontSize, accountMargin, labelGap, barGap, rightMargin;
        bool showLabels, labelOnLeft, showPercentText;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            accounts = g_settings.accounts;
            barWidth = g_settings.barWidth;
            barHeight = g_settings.barHeight;
            labelFontSize = g_settings.labelFontSize;
            accountMargin = g_settings.accountMargin;
            labelGap = g_settings.labelGap;
            barGap = g_settings.barGap;
            rightMargin = g_settings.rightMargin;
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
        panel.Margin({4, 0, (double)rightMargin, 0});

        wchar_t name[64];
        for (size_t i = 0; i < accounts.size(); i++) {
            StackPanel col;
            col.Orientation(labelOnLeft ? Orientation::Horizontal : Orientation::Vertical);
            col.VerticalAlignment(VerticalAlignment::Center);
            col.Margin({(double)accountMargin, 0, (double)accountMargin, 0});
            col.Background(SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
            swprintf(name, ARRAYSIZE(name), L"AiQuota_Acc_%d", (int)i);
            col.Name(name);

            if (showLabels) {
                TextBlock label;
                label.Text(accounts[i].label);
                label.FontSize(labelFontSize);
                label.VerticalAlignment(VerticalAlignment::Center);
                label.HorizontalAlignment(labelOnLeft ? HorizontalAlignment::Left : HorizontalAlignment::Center);
                label.Margin(labelOnLeft ? Thickness{0, -2, (double)labelGap, 0} :
                                           Thickness{0, 0, 0, (double)labelGap});
                label.Opacity(0.8);
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Label_%d", (int)i);
                label.Name(name);
                col.Children().Append(label);
            }

            StackPanel bars;
            bars.Orientation(Orientation::Vertical);
            bars.VerticalAlignment(VerticalAlignment::Center);

            double radius = std::max(1.0, barHeight / 2.0);
            double halfBarGap = barGap / 2.0;
            for (int w = 0; w < 2; w++) {
                Border track;
                track.Width(barWidth);
                track.Height(barHeight);
                track.CornerRadius({radius, radius, radius, radius});
                track.Margin(w == 0 ? Thickness{0, 1, 0, halfBarGap} : Thickness{0, halfBarGap, 0, 1});
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
            QuotaUiInstance* statePtr = &state;
            int accountIndex = (int)i;
            auto tappedToken = tappedElement.Tapped(
                [statePtr, accountIndex](winrt::Windows::Foundation::IInspectable const&,
                                         wuxi::TappedRoutedEventArgs const& e) {
                    if (g_unloading || !statePtr->quotaGrid) {
                        e.Handled(true);
                        return;
                    }

                    if (!g_fetchThreadStarted.load(std::memory_order_acquire)) {
                        g_refreshing = false;
                        g_refreshAccountIndex = -1;
                        PostUiUpdate();
                        e.Handled(true);
                        return;
                    }

                    g_refreshing = true;
                    g_refreshAccountIndex = accountIndex;
                    g_refreshGeneration++;
                    PostUiUpdate();
                    if (g_refreshEvent) SetEvent(g_refreshEvent);
                    e.Handled(true);
                });
            state.tapHandlers.push_back({tappedElement, tappedToken});

            panel.Children().Append(col);
        }

        root.Children().Append(panel);
        return root;
    } catch (...) {
        ClearQuotaEventState(state);
        Wh_Log(L"BuildQuotaGrid: exception");
        return nullptr;
    }
}

static void UpdateQuotaUi(QuotaUiInstance& state) {
    if (!state.quotaGrid) return;

    std::vector<AccountConfig> accounts;
    int intervalMin, barWidth, yellowThreshold, orangeThreshold, redThreshold;
    bool showPercentText, showCodexSparkInTooltip, colorblindMode, showStaleWarning;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        accounts = g_settings.accounts;
        intervalMin = g_settings.pollMinutes;
        barWidth = g_settings.barWidth;
        yellowThreshold = g_settings.yellowThreshold;
        orangeThreshold = g_settings.orangeThreshold;
        redThreshold = g_settings.redThreshold;
        showPercentText = g_settings.showPercentText;
        showCodexSparkInTooltip = g_settings.showCodexSparkInTooltip;
        colorblindMode = g_settings.colorblindMode;
        showStaleWarning = g_settings.showStaleWarning;
    }

    std::vector<AccountData> data;
    {
        std::lock_guard<std::mutex> lk(g_dataMutex);
        data = g_data;
    }
    if (data.size() != accounts.size()) return;
    if (state.applied.size() != data.size()) state.applied.assign(data.size(), {});

    ULONGLONG now = NowUnixMs();
    bool refreshing = g_refreshing.load();
    int refreshAccountIndex = g_refreshAccountIndex.load();
    wchar_t name[64];
    try {
        for (size_t i = 0; i < data.size(); i++) {
            const AccountData& d = data[i];
            AppliedState& ap = state.applied[i];
            bool stale = d.stale || d.lastSuccessMs == 0 ||
                         now - d.lastSuccessMs > (ULONGLONG)intervalMin * 2 * 60000;
            bool warn = showStaleWarning && stale && !d.error.empty();
            bool accountRefreshing = refreshing && (refreshAccountIndex < 0 || refreshAccountIndex == (int)i);

            for (int w = 0; w < 2; w++) {
                const WindowUsage& wu = w == 0 ? d.win5h : d.winWeek;
                int px = wu.pct > 0 ? std::clamp((int)std::lround(barWidth * wu.pct / 100.0), 2, barWidth) : 0;
                auto c = UsageColor(wu.pct, stale, yellowThreshold, orangeThreshold, redThreshold,
                                    colorblindMode);
                uint32_t cv = ((uint32_t)c.A << 24) | ((uint32_t)c.R << 16) |
                              ((uint32_t)c.G << 8) | c.B;
                if (px != ap.fillPx[w] || cv != ap.fillColor[w]) {
                    swprintf(name, ARRAYSIZE(name), L"AiQuota_Fill_%d_%d", (int)i, w);
                    if (auto fe = FindChildByName(state.quotaGrid, name)) {
                        if (auto b = fe.try_as<Border>()) {
                            b.Width(px);
                            b.Background(SolidColorBrush(c));
                        }
                    }
                    ap.fillPx[w] = px;
                    ap.fillColor[w] = cv;
                }
            }

            std::wstring sourceName = accounts[i].authSource == L"claude-code" ? L"Claude Code" :
                                      accounts[i].authSource == L"codex" ? L"Codex" : L"OpenCode";
            std::wstring tip = (warn ? L"! " : L"") + accounts[i].label + L" - " +
                               (accounts[i].provider == L"anthropic" ? L"Anthropic" : L"OpenAI") +
                               L" (" + sourceName + L")";
            bool planIsSpark = d.plan.find(L"Spark") != std::wstring::npos ||
                               d.plan.find(L"spark") != std::wstring::npos;
            if (!d.plan.empty() && (showCodexSparkInTooltip || !planIsSpark)) {
                tip += L" (" + d.plan + L")";
            }
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
            if (showCodexSparkInTooltip && accounts[i].provider == L"openai" && !d.codexSparkLines.empty()) {
                tip += L"\n" + d.codexSparkLines;
            }
            if (!d.extraLines.empty()) tip += L"\n" + d.extraLines;
            if (!d.error.empty()) tip += L"\nerror: " + d.error;
            tip += L"\n" + FormatUpdated(d.lastSuccessMs, stale);
            tip += accountRefreshing ? L" - refreshing..." : L" - click to refresh";

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
                    if (auto fe = FindChildByName(state.quotaGrid, name)) {
                        if (auto tb = fe.try_as<TextBlock>()) tb.Text(percentText);
                    }
                    ap.percentText = percentText;
                }
            }

            if (tip != ap.tip) {
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Acc_%d", (int)i);
                if (auto fe = FindChildByName(state.quotaGrid, name)) {
                    ToolTipService::SetToolTip(fe, winrt::box_value(winrt::hstring(tip)));
                }
                ap.tip = tip;
            }

            double columnOpacity = accountRefreshing ? 0.65 : 1.0;
            if (columnOpacity != ap.columnOpacity) {
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Acc_%d", (int)i);
                if (auto fe = FindChildByName(state.quotaGrid, name)) fe.Opacity(columnOpacity);
                ap.columnOpacity = columnOpacity;
            }

            double labelOpacity = stale ? 0.45 : 0.8;
            std::wstring labelText = warn ? accounts[i].label + L"!" : accounts[i].label;
            if (labelOpacity != ap.labelOpacity || labelText != ap.labelText) {
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Label_%d", (int)i);
                if (auto fe = FindChildByName(state.quotaGrid, name)) {
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

static int RemoveQuotaChildren(Grid const& targetGrid, QuotaUiInstance& state) {
    if (!targetGrid) return -1;
    ClearQuotaEventState(state);

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

static void RemoveQuotaGridFromState(QuotaUiInstance& state) {
    if (!state.injectionParent) {
        ClearQuotaEventState(state);
        state.quotaGrid = nullptr;
        state.quotaColumn = -1;
        state.insertedColumn = false;
        state.applied.clear();
        return;
    }

    try {
        auto targetGrid = state.injectionParent;
        int quotaCol = RemoveQuotaChildren(targetGrid, state);
        if (quotaCol < 0) quotaCol = state.quotaColumn;
        if (state.insertedColumn && quotaCol >= 0 && quotaCol < (int)targetGrid.ColumnDefinitions().Size()) {
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

    state.quotaGrid = nullptr;
    state.injectionParent = nullptr;
    state.quotaColumn = -1;
    state.insertedColumn = false;
    state.applied.clear();
}

static bool InjectQuotaGrid(HWND hWnd) {
    if (!hWnd) return false;
    if (auto* existingState = FindUiState(hWnd); existingState && existingState->quotaGrid) return true;

    auto fail = [&](PCWSTR reason) {
        ULONGLONG now = NowUnixMs();
        ULONGLONG nextLogMs = g_nextInjectFailureLogMs.load(std::memory_order_acquire);
        if (now >= nextLogMs &&
            g_nextInjectFailureLogMs.compare_exchange_strong(nextLogMs, now + 5000,
                                                            std::memory_order_acq_rel)) {
            wchar_t cls[64] = {};
            GetClassNameW(hWnd, cls, ARRAYSIZE(cls));
            Wh_Log(L"InjectQuotaGrid failed: hwnd=%p class=%s reason=%s", hWnd, cls, reason);
        }
        return false;
    };

    QuotaUiInstance* state = nullptr;
    try {
        auto xamlRoot = GetTaskbarXamlRoot(hWnd);
        if (!xamlRoot) return fail(L"no XamlRoot");
        auto root = xamlRoot.Content().try_as<FrameworkElement>();
        if (!root) return fail(L"no XamlRoot content");
        auto trayFrame = FindChildByName(root, L"SystemTrayFrameGrid");
        auto trayGrid = trayFrame ? trayFrame.try_as<Grid>() : nullptr;
        bool overlayFallback = false;
        if (!trayGrid) {
            auto rootGrid = FindChildByName(root, L"RootGrid");
            trayGrid = rootGrid ? rootGrid.try_as<Grid>() : nullptr;
            if (!trayGrid) trayGrid = root.try_as<Grid>();
            if (!trayGrid) return fail(L"no injection grid");
            overlayFallback = true;
        }

        state = FindUiState(hWnd);
        if (!state) {
            auto newState = std::make_unique<QuotaUiInstance>();
            newState->hWnd = hWnd;
            state = newState.get();
            g_uiInstances.push_back(std::move(newState));
        }
        state->injectionParent = trayGrid;

        int oldCol = RemoveQuotaChildren(trayGrid, *state);
        if (!overlayFallback && oldCol >= 0 && oldCol < (int)trayGrid.ColumnDefinitions().Size()) {
            for (uint32_t i = 0; i < trayGrid.Children().Size(); ++i) {
                auto child = trayGrid.Children().GetAt(i).try_as<FrameworkElement>();
                if (child) {
                    int childCol = Grid::GetColumn(child);
                    if (childCol > oldCol) Grid::SetColumn(child, childCol - 1);
                }
            }
            trayGrid.ColumnDefinitions().RemoveAt(oldCol);
        }
        Grid quota = BuildQuotaGrid(*state);
        if (!quota) {
            ClearQuotaEventState(*state);
            state->injectionParent = nullptr;
            state->quotaColumn = -1;
            state->insertedColumn = false;
            state->applied.clear();
            EraseUiState(hWnd);
            return fail(L"BuildQuotaGrid failed");
        }

        if (overlayFallback) {
            quota.HorizontalAlignment(HorizontalAlignment::Right);
            quota.VerticalAlignment(VerticalAlignment::Center);
            quota.Margin({0, 0, 4, 0});
            Grid::SetColumn(quota, 0);
            Grid::SetRow(quota, 0);
            Grid::SetColumnSpan(quota, std::max(1, (int)trayGrid.ColumnDefinitions().Size()));
            Grid::SetRowSpan(quota, std::max(1, (int)trayGrid.RowDefinitions().Size()));
            state->quotaColumn = -1;
            state->insertedColumn = false;
        } else {
            ColumnDefinition newCol;
            newCol.Width({1.0, GridUnitType::Auto});
            trayGrid.ColumnDefinitions().InsertAt(0, newCol);
            state->quotaColumn = 0;
            state->insertedColumn = true;
            for (uint32_t i = 0; i < trayGrid.Children().Size(); ++i) {
                auto child = trayGrid.Children().GetAt(i).try_as<FrameworkElement>();
                if (child) Grid::SetColumn(child, Grid::GetColumn(child) + 1);
            }
            Grid::SetColumn(quota, 0);
        }
        trayGrid.Children().Append(quota);

        state->quotaGrid = quota;
        g_uiInjected.store(true, std::memory_order_release);
        state->applied.clear();
        UpdateQuotaUi(*state);
        Wh_Log(L"Injected quota bars");
        return true;
    } catch (...) {
        if (state) {
            RemoveQuotaGridFromState(*state);
            EraseUiState(hWnd);
        }
        g_uiInjected.store(!g_uiInstances.empty(), std::memory_order_release);
        Wh_Log(L"InjectQuotaGrid: exception");
        return false;
    }
}

static void RemoveQuotaGrid(HWND hWnd) {
    auto* state = FindUiState(hWnd);
    if (!state) return;

    RemoveQuotaGridFromState(*state);
    EraseUiState(hWnd);
}

static void RemoveAllQuotaGrids() {
    std::vector<HWND> hWnds;
    hWnds.reserve(g_uiInstances.size());
    for (auto& state : g_uiInstances) hWnds.push_back(state->hWnd);

    for (HWND hWnd : hWnds) {
        if (!hWnd || !IsWindow(hWnd)) continue;
        if (!RunFromWindowThread(hWnd, [](void* param) {
                RemoveQuotaGrid(static_cast<HWND>(param));
            }, hWnd)) {
            Wh_Log(L"RemoveQuotaGrid marshal failed");
        }
    }

    for (auto& state : g_uiInstances) {
        if (state->hWnd && IsWindow(state->hWnd)) continue;

        state->tapHandlers.clear();
        state->quotaGrid = nullptr;
        state->injectionParent = nullptr;
        state->quotaColumn = -1;
        state->insertedColumn = false;
        state->applied.clear();
    }
    g_uiInstances.erase(std::remove_if(g_uiInstances.begin(), g_uiInstances.end(),
        [](const auto& state) { return !state->hWnd || !IsWindow(state->hWnd); }),
        g_uiInstances.end());
    g_uiInjected.store(!g_uiInstances.empty(), std::memory_order_release);
}

static DWORD WINAPI RetryInjectThreadProc(LPVOID) {
    for (int attempt = 0; attempt < 600 && !g_unloading; attempt++) {
        auto hWnds = FindCurrentProcessTaskbarWnds();
        TaskbarMonitorMode mode;
        int targetMonitorNumber;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            mode = g_settings.taskbarMonitorMode;
            targetMonitorNumber = g_settings.taskbarMonitorNumber;
        }
        if (hWnds.empty() || (mode == TaskbarMonitorMode::All && hWnds.size() < 2)) {
            ULONGLONG now = NowUnixMs();
            ULONGLONG nextLogMs = g_nextInjectFailureLogMs.load(std::memory_order_acquire);
            if (now >= nextLogMs &&
                g_nextInjectFailureLogMs.compare_exchange_strong(nextLogMs, now + 5000,
                                                                std::memory_order_acq_rel)) {
                Wh_Log(L"Taskbar discovery: mode=%d target=%d eligible=%zu",
                       (int)mode, targetMonitorNumber, hWnds.size());
            }
        }
        bool allInjected = !hWnds.empty();
        for (HWND hWnd : hWnds) {
            bool injected = false;
            InjectTaskbarParam param{hWnd, &injected};
            RunFromWindowThread(hWnd, [](void* param) {
                auto* p = static_cast<InjectTaskbarParam*>(param);
                *p->injected = !g_unloading && InjectQuotaGrid(p->hWnd);
            }, &param);
            allInjected = allInjected && injected;
        }
        if (allInjected) break;

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
    if (g_retryThread) return;

    DWORD err = GetLastError();
    Wh_Log(L"CreateThread RetryInjectThreadProc failed: %lu", err);

    bool anyInjected = false;
    for (HWND hWnd : FindCurrentProcessTaskbarWnds()) {
        bool injected = false;
        InjectTaskbarParam param{hWnd, &injected};
        if (RunFromWindowThread(hWnd, [](void* param) {
                auto* p = static_cast<InjectTaskbarParam*>(param);
                *p->injected = !g_unloading && InjectQuotaGrid(p->hWnd);
            }, &param) && injected) {
            anyInjected = true;
        }
    }
    if (!anyInjected) {
        Wh_Log(L"InjectQuotaGrid fallback failed");
    }
}

/**********************************************/
//  Hooks
/**********************************************/

using TrayUI_StartTaskbar_t = void(WINAPI*)(void*);
static TrayUI_StartTaskbar_t TrayUI_StartTaskbar_Original = nullptr;

static void WINAPI TrayUI_StartTaskbar_Hook(void* pThis) {
    TrayUI_StartTaskbar_Original(pThis);
    if (g_unloading) return;

    RemoveAllQuotaGrids();
    StartRetryInject();
}

static bool HookTaskbarDllSymbols() {
    HMODULE h = LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) return false;

    WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
        {{LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"}, &CTaskBand_ITaskListWndSite_vftable},
        {{LR"(const CSecondaryTaskBand::`vftable'{for `ITaskListWndSite'})"}, &CSecondaryTaskBand_ITaskListWndSite_vftable},
        {{LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CTaskBand::GetTaskbarHost(void)const )"}, &CTaskBand_GetTaskbarHost_Original},
        {{LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CSecondaryTaskBand::GetTaskbarHost(void)const )"}, &CSecondaryTaskBand_GetTaskbarHost_Original},
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
    auto defaultAuthFile = [](const std::wstring& authSource) {
        if (authSource == L"claude-code") return ExpandEnv(kDefaultClaudeCodeAuthFile);
        if (authSource == L"codex") return ExpandEnv(kDefaultCodexAuthFile);
        return ExpandEnv(kDefaultOpenCodeAuthFile);
    };

    for (int i = 0;; i++) {
        PCWSTR provider = Wh_GetStringSetting(L"accounts[%d].provider", i);
        if (!*provider) {
            Wh_FreeStringSetting(provider);
            break;
        }

        AccountConfig a;
        std::wstring providerSetting = provider;
        Wh_FreeStringSetting(provider);

        if (providerSetting == L"anthropic-claude-code") {
            a.provider = L"anthropic";
            a.authSource = L"claude-code";
        } else if (providerSetting == L"openai-codex") {
            a.provider = L"openai";
            a.authSource = L"codex";
        } else if (providerSetting == L"openai-opencode") {
            a.provider = L"openai";
            a.authSource = L"opencode";
        } else if (providerSetting == L"anthropic-opencode") {
            a.provider = L"anthropic";
            a.authSource = L"opencode";
        } else {
            // Existing configs used bare anthropic/openai and auto-detected the file shape.
            a.provider = providerSetting.find(L"openai") != std::wstring::npos ? L"openai" : L"anthropic";
            a.authSource = L"auto";
        }

        PCWSTR label = Wh_GetStringSetting(L"accounts[%d].label", i);
        a.label = label;
        Wh_FreeStringSetting(label);

        PCWSTR authFile = Wh_GetStringSetting(L"accounts[%d].authFile", i);
        a.authFile = ExpandEnv(authFile);
        Wh_FreeStringSetting(authFile);
        if (a.authFile.empty() ||
            ((a.authSource == L"claude-code" || a.authSource == L"codex") &&
             a.authFile == ExpandEnv(kDefaultOpenCodeAuthFile))) {
            a.authFile = defaultAuthFile(a.authSource);
        }

        PCWSTR authKey = Wh_GetStringSetting(L"accounts[%d].authKey", i);
        a.authKey = authKey;
        Wh_FreeStringSetting(authKey);

        if (a.label.empty()) a.label = a.provider == L"anthropic" ? L"A" : L"O";
        s.accounts.push_back(std::move(a));
    }

    if (s.accounts.empty()) {
        std::wstring authFile = ExpandEnv(kDefaultOpenCodeAuthFile);
        s.accounts.push_back({L"anthropic", L"opencode", L"A", authFile, L""});
        s.accounts.push_back({L"openai", L"opencode", L"O", authFile, L""});
    }

    auto hasSetting = [](PCWSTR name) {
        PCWSTR text = Wh_GetStringSetting(name);
        bool hasValue = *text != L'\0';
        Wh_FreeStringSetting(text);
        return hasValue;
    };
    auto getIntSetting = [&](PCWSTR name, int defaultValue) {
        return hasSetting(name) ? Wh_GetIntSetting(name) : defaultValue;
    };
    auto getBoolSetting = [&](PCWSTR name, bool defaultValue) {
        return hasSetting(name) ? Wh_GetIntSetting(name) != 0 : defaultValue;
    };

    int pollMinutes = getIntSetting(L"pollIntervalMinutes", 10);
    int barWidth = getIntSetting(L"barWidth", 100);
    int barHeight = getIntSetting(L"barHeight", 8);
    int labelFontSize = getIntSetting(L"labelFontSize", 11);
    int accountMargin = getIntSetting(L"accountMargin", 3);
    int labelGap = getIntSetting(L"labelGap", 3);
    int barGap = getIntSetting(L"barGap", 2);
    int rightMargin = getIntSetting(L"rightMargin", 4);
    int yellowThreshold = getIntSetting(L"yellowThreshold", 50);
    int orangeThreshold = getIntSetting(L"orangeThreshold", 75);
    int redThreshold = getIntSetting(L"redThreshold", 90);

    PCWSTR taskbarMonitorModeText = Wh_GetStringSetting(L"taskbarMonitorMode");
    std::wstring taskbarMonitorMode = taskbarMonitorModeText;
    Wh_FreeStringSetting(taskbarMonitorModeText);
    if (taskbarMonitorMode == L"all") {
        s.taskbarMonitorMode = TaskbarMonitorMode::All;
    } else if (taskbarMonitorMode == L"specific") {
        s.taskbarMonitorMode = TaskbarMonitorMode::Specific;
    } else {
        s.taskbarMonitorMode = TaskbarMonitorMode::Primary;
    }
    int taskbarMonitorNumber = getIntSetting(L"taskbarMonitorNumber", 1);

    s.pollMinutes = std::clamp(pollMinutes > 0 ? pollMinutes : 10, 2, 24 * 60);
    s.taskbarMonitorNumber = std::clamp(taskbarMonitorNumber > 0 ? taskbarMonitorNumber : 1, 1, 64);
    s.barWidth = std::max(barWidth > 0 ? barWidth : 100, 10);
    s.barHeight = std::clamp(barHeight > 0 ? barHeight : 8, 2, 20);
    s.labelFontSize = std::clamp(labelFontSize > 0 ? labelFontSize : 11, 6, 24);
    s.accountMargin = std::max(accountMargin, 0);
    s.labelGap = std::max(labelGap, 0);
    s.barGap = std::max(barGap, 0);
    s.rightMargin = std::max(rightMargin, 0);
    s.yellowThreshold = std::clamp(yellowThreshold, 0, 100);
    s.orangeThreshold = std::clamp(orangeThreshold, s.yellowThreshold, 100);
    s.redThreshold = std::clamp(redThreshold, s.orangeThreshold, 100);
    s.showLabels = getBoolSetting(L"showLabels", true);
    s.labelOnLeft = getBoolSetting(L"labelOnLeft", true);
    s.showPercentText = getBoolSetting(L"showPercentText", false);
    s.showCodexSparkInTooltip = getBoolSetting(L"showCodexSparkInTooltip", false);
    s.colorblindMode = getBoolSetting(L"colorblindMode", false);
    s.showStaleWarning = getBoolSetting(L"showStaleWarning", true);

    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        std::lock_guard<std::mutex> lk2(g_dataMutex);
        std::vector<AccountData> newData(s.accounts.size());
        std::vector<bool> oldDataUsed(g_data.size(), false);
        for (size_t i = 0; i < s.accounts.size(); i++) {
            for (size_t j = 0; j < g_settings.accounts.size() && j < g_data.size(); j++) {
                if (oldDataUsed[j]) continue;

                const AccountConfig& oldAccount = g_settings.accounts[j];
                const AccountConfig& newAccount = s.accounts[i];
                if (oldAccount.provider == newAccount.provider &&
                    oldAccount.authSource == newAccount.authSource &&
                    oldAccount.authFile == newAccount.authFile &&
                    oldAccount.authKey == newAccount.authKey) {
                    newData[i] = g_data[j];
                    oldDataUsed[j] = true;
                    break;
                }
            }
        }

        g_settings = std::move(s);
        g_settingsGeneration++;
        g_data = std::move(newData);
    }
}

/**********************************************/
//  Lifecycle
/**********************************************/

BOOL Wh_ModInit() {
    Wh_Log(L"Init");
    g_unloading = false;
    g_refreshing = false;
    g_refreshAccountIndex = -1;
    g_refreshGeneration = 0;
    g_uiInjected.store(false, std::memory_order_release);
    g_fetchThreadStarted.store(false, std::memory_order_release);
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
    g_fetchThread = CreateThread(nullptr, 0, FetchThreadProc, nullptr, 0, nullptr);
    if (g_fetchThread) {
        g_fetchThreadStarted.store(true, std::memory_order_release);
    } else {
        DWORD err = GetLastError();
        Wh_Log(L"CreateThread FetchThreadProc failed: %lu", err);
        g_refreshing = false;
        g_fetchThreadStarted.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(g_dataMutex);
            for (auto& data : g_data) {
                data.stale = true;
                data.error = L"fetch thread failed";
            }
        }
    }

    StartRetryInject();
    if (!g_fetchThread) PostUiUpdate();
}

void Wh_ModUninit() {
    Wh_Log(L"Uninit");
    g_unloading = true;
    g_uiInjected.store(false, std::memory_order_release);
    if (g_stopEvent) SetEvent(g_stopEvent);
    CloseActiveHttpHandles();

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
    g_fetchThreadStarted.store(false, std::memory_order_release);

    RemoveAllQuotaGrids();

    if (g_stopEvent) CloseHandle(g_stopEvent);
    if (g_refreshEvent) CloseHandle(g_refreshEvent);
    g_stopEvent = nullptr;
    g_refreshEvent = nullptr;
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"SettingsChanged");
    LoadSettings();
    g_refreshAccountIndex = -1;
    g_refreshGeneration++;

    RemoveAllQuotaGrids();
    StartRetryInject();
    if (g_refreshEvent) SetEvent(g_refreshEvent);
}
