// ==WindhawkMod==
// @id              taskbar-ai-quota
// @name            Taskbar AI Quota Bars
// @description     Shows configurable AI agent/LLM subscription quota bars for Anthropic, OpenAI, and Google Antigravity on the Windows 11 taskbar
// @version         2.0.0
// @author          Cleroth
// @github          https://github.com/Cleroth
// @include         explorer.exe
// @architecture    x86-64
// @license         MIT
// @compilerOptions -DWIN32_LEAN_AND_MEAN -lole32 -loleaut32 -lruntimeobject -lwindowsapp -lwinhttp -luser32 -lshell32 -lgdi32 -ladvapi32 -lws2_32 -liphlpapi -lcrypt32 -lbcrypt -lcomctl32 -lcomdlg32 -luuid
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar AI Quota Bars

A Windows 11 taskbar mod that shows subscription quota bars next to the system tray.

Supported providers and quotas:

- **Anthropic Claude:** 5-hour, weekly, Fable weekly, and monthly extra usage
- **OpenAI/Codex:** 5-hour, weekly, and prepaid credits against a max you set
- **Google Antigravity:** Gemini pool

Optional notifications warn when usage crosses the configured red threshold.

![Quota tooltip](https://raw.githubusercontent.com/Cleroth/windhawk-taskbar-ai-quota/master/ss1.png)

![Compact taskbar bars](https://raw.githubusercontent.com/Cleroth/windhawk-taskbar-ai-quota/master/ss2.png)

![Threshold notification](https://raw.githubusercontent.com/Cleroth/windhawk-taskbar-ai-quota/master/ss3.png)

## Setup

Open the native Settings window from the taskbar to add accounts. Anthropic and OpenAI
use browser sign-in; tokens are encrypted locally with Windows DPAPI. Antigravity uses
its signed-in local app or CLI session, which must remain running.

## Settings

- **Accounts and quota bars:** Add, order, or hide accounts and choose quota windows.
- **Layout and appearance:** Set orientation, size, labels, pace ticks, and colors.
- **Taskbar behavior:** Choose displays, click actions, polling, and alerts.
- **Usage history:** Opt in to local recording, set retention (90 days by default), and
  choose recording per account independently of taskbar visibility. Opt-in is per Windows user.
  Hidden accounts stop
  polling unless you disable **Do not poll hidden accounts** and enable their recording.

## Usage history

Open **Usage history...** from the taskbar context menu or Settings > Behavior. Compare raw
quota timelines on one graph with color-coded account lines, overlay the preceding week,
view extra-spending/credit balances, and export CSV. Refresh/F5 reads local samples;
history starts at opt-in, with no backfill.
All accounts defaults to Weekly over the past 24 hours; choose Short-term for short quotas.
Weekly follows each Anthropic account's taskbar weekly selections. All meters is available
for all accounts or a single account (its default), including weeklies hidden on the taskbar.
Percentages, USD, and credits use separate axes. Connected samples use lines only; dots keep
isolated readings visible.
Percentages from different accounts or overlapping windows are not added together.

History follows the configured entry: use a new entry for a different login. Data stays under
`%LOCALAPPDATA%\TaskbarAIQuota\<mod ID>` with your user-folder permissions, without tokens or
labels. It is not encrypted and survives mod removal. Retention and a roughly 60 MB size cap
prune old samples; **Clear all history...** removes recordings without changing sign-ins.
See the [usage history guide](https://github.com/Cleroth/windhawk-taskbar-ai-quota#usage-history)
for recording rules, export, and verification.

## Suggestions & bugs

Have a suggestion or found a bug?
[Open an issue](https://github.com/Cleroth/windhawk-taskbar-ai-quota/issues/new).
*/
// ==/WindhawkModReadme==

// Windhawk implicitly includes windhawk_api.h (and thus windows.h) before this file,
// so winsock2.h can't be ordered ahead of windows.h here. WIN32_LEAN_AND_MEAN (set in
// @compilerOptions) keeps that windows.h from pulling in the legacy winsock.h, so
// winsock2.h is included cleanly below without redefinition conflicts.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windhawk_utils.h>

#include <windows.h>
#include <winternl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <unknwn.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Documents.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tlhelp32.h>
#include <iphlpapi.h>

using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;

namespace wuxcp = winrt::Windows::UI::Xaml::Controls::Primitives;
namespace wuxd = winrt::Windows::UI::Xaml::Documents;
namespace wuxi = winrt::Windows::UI::Xaml::Input;
namespace wuxs = winrt::Windows::UI::Xaml::Shapes;

// Unity build: the fragments below form one translation unit, included in dependency
// order. generate-mod.ps1 inlines them into the published single-file mod.

// src/core/model.h
// Account configuration, settings, and fetched quota data shared by every domain.

enum QuotaBarIndex {
    kFiveHourBar,
    kWeeklyBar,
    kFableWeeklyBar,
    kExtraUsageBar,
    kQuotaBarCount,
};

struct AccountConfig {
    std::wstring provider;  // "anthropic", "openai", or "antigravity".
    std::wstring label;
    std::array<bool, kQuotaBarCount> showBars{true, true, false, false};
    // OpenAI only: user-chosen credits ceiling that turns the prepaid balance into a
    // used-percent bar in the extra-usage slot. 0 disables the bar.
    int creditsMax = 0;
    bool hidden = false;  // Runtime show/hide toggle (right-click menu), persisted in mod storage.
    bool recordHistory = true;  // Still requires the global history opt-in.
    uint64_t historyId = 0;  // Independent of the provider+label auth identity.

    bool operator==(const AccountConfig&) const = default;
};

enum class TaskbarMonitorMode {
    Primary,
    All,
    Specific,
};

enum class ClickAction {
    Refresh,
    OpenDashboard,
};

enum class BarLayout {
    Stacked,
    Vertical,
};

enum class BarMode {
    Used,
    Remaining,
};

enum class PercentTextAlignment {
    Adaptive,
    Left,
    Center,
    Right,
};

enum class PercentTextVisibility {
    Never,
    Hover,
    Always,
};

enum class LabelPosition {
    Hidden,
    Left,
    Top,
    Right,
    Bottom,
};

enum class PaceTickStyle {
    Caret,
    Line,
    Notch,
    Dot,
};

static constexpr COLORREF kDefaultPaceTickColor = RGB(222, 222, 222);

struct Settings {
    std::vector<AccountConfig> accounts;
    TaskbarMonitorMode taskbarMonitorMode = TaskbarMonitorMode::Primary;
    ClickAction clickAction = ClickAction::Refresh;
    BarLayout barLayout = BarLayout::Stacked;
    BarMode barMode = BarMode::Used;
    int taskbarMonitorNumber = 1;
    int pollMinutes = 10;
    bool historyEnabled = false;
    int historyRetentionDays = 90;
    bool skipHiddenPolling = true;
    int barLength = 100;
    int barThickness = 8;
    int labelFontSize = 11;
    int percentFontSize = 9;
    int accountMargin = 3;
    int labelGap = 3;
    int barGap = 2;
    int rightMargin = 4;
    int yellowThreshold = 50;
    int orangeThreshold = 75;
    int redThreshold = 90;
    LabelPosition labelPosition = LabelPosition::Left;
    bool showPaceTicks = true;
    PaceTickStyle paceTickStyle = PaceTickStyle::Caret;
    COLORREF paceTickColor = kDefaultPaceTickColor;
    bool showBarLabels = false;
    PercentTextVisibility percentTextVisibility = PercentTextVisibility::Hover;
    PercentTextAlignment percentTextAlignment = PercentTextAlignment::Adaptive;
    // Extra-usage/credits bars show the amount ($ or credits) instead of a percentage.
    bool showExtraBarAmounts = false;
    // additional_rate_limits lines (Codex Spark, gpt-reserve, ...) and the Spark plan name.
    bool showOpenAiExtraLimits = false;
    bool colorblindMode = false;
    bool showStaleWarning = true;
    bool enableNotifications = true;

    bool operator==(const Settings&) const = default;
};

static constexpr ULONGLONG kFiveHourWindowMs = 5ULL * 60 * 60 * 1000;
static constexpr ULONGLONG kWeeklyWindowMs = 7ULL * 24 * 60 * 60 * 1000;

struct WindowUsage {
    double pct = -1;
    ULONGLONG resetUnixMs = 0;
    ULONGLONG windowDurationMs = 0;
};

struct AccountData {
    WindowUsage win5h;
    WindowUsage winWeek;
    WindowUsage fableWeek;
    WindowUsage extraUsage;
    WindowUsage antigravityThirdParty5h;
    WindowUsage antigravityThirdPartyWeek;
    std::wstring plan;
    std::wstring openAiExtraLimitLines;
    std::wstring extraLines;
    // Amounts behind the extra-usage slot, in dollars (Anthropic) or credits (OpenAI);
    // -1 when unknown. Left = limit - used. Used may go negative if a credits balance
    // exceeds the configured max.
    double extraUsedAmount = -1;
    double extraLimitAmount = -1;
    // OpenAI prepaid credits; balance is -1 when the API reports none or hides it.
    bool hasCredits = false;
    bool creditsUnlimited = false;
    double creditsBalance = -1;
    std::wstring error;
    ULONGLONG lastSuccessMs = 0;
    ULONGLONG retryDeadlineMs = 0;
    bool stale = true;
    bool needsLogin = false;  // Sign-in required; left-click signs in instead of refreshing.
};

// Stable identity used to preserve g_data across settings reloads, persist the show/hide
// toggle, and key the encrypted token store. FNV-1a over provider+label; relabeling an
// account therefore points it at a fresh (unsigned-in) identity.
static uint64_t AccountIdentityHash(const AccountConfig& a) {
    std::wstring id = a.provider + L"\n" + a.label;
    uint64_t h = 1469598103934665603ull;
    for (const auto* p = reinterpret_cast<const unsigned char*>(id.data());
         p != reinterpret_cast<const unsigned char*>(id.data() + id.size()); ++p) {
        h = (h ^ *p) * 1099511628211ull;
    }
    return h;
}

// src/history/types.h
// Usage-history samples and the journal coordination state shared with fetch and settings.

// Loaded IDs are not recording identities until the shell owner persists them.
static std::atomic<bool> g_historyIdsPendingSave{false};

// History is a separate domain: sampled levels, never inferred token counts or spend.
// Fixed-width records contain no credentials, account labels, or server response text.
static constexpr size_t kHistoryMeterCount = 8;
static constexpr ULONGLONG kHistoryDayMs = 24ULL * 60 * 60 * 1000;
struct HistoryValue {
    double value = -1;
    ULONGLONG resetMs = 0;
    ULONGLONG durationMs = 0;
};
struct HistorySample {
    uint64_t accountId = 0;
    ULONGLONG timeMs = 0;
    ULONGLONG intervalMs = 0;
    uint64_t session = 0;
    uint32_t provider = 0;  // 1 Anthropic, 2 OpenAI, 3 Antigravity.
    uint32_t failed = 0;
    // Short, weekly, Fable weekly, extra %, 3p short, 3p weekly, extra $, credits.
    std::array<HistoryValue, kHistoryMeterCount> meters{};
    uint64_t checksum = 0;
};
static_assert(sizeof(HistorySample) == 240);
struct HistorySnapshot {
    std::vector<HistorySample> samples;
    std::wstring status;
};

static std::mutex g_historyMutex;  // Leaf: never acquire settings/UI/auth locks inside it.
static std::atomic<uint64_t> g_historyEpoch{1};  // Clear invalidates in-flight fetch batches.
static std::atomic<ULONGLONG> g_historySettingsGeneration{0};
static std::atomic<bool> g_historyStorageError{false};

static ULONGLONG g_historyContinuityGeneration = 0;  // Protected by g_settingsMutex.

// src/taskbar/ui-types.h
// Per-taskbar XAML UI instances and the registry that owns them.

struct AppliedState {
    std::array<int, kQuotaBarCount> fillPx{-1, -1, -1, -1};
    std::array<uint32_t, kQuotaBarCount> fillColor{0, 0, 0, 0};
    std::array<int, kQuotaBarCount> pacePx{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> paceVisible{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> percentAlignments{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> percentDark{-1, -1, -1, -1};
    std::wstring tip;
    std::array<std::wstring, kQuotaBarCount> percentTexts;
    std::wstring labelText;
    double labelOpacity = -1;
    double columnOpacity = -1;
    int barMask = -1;
    int visible = -1;  // -1 unset, 0 collapsed, 1 visible.
};

struct PointerHandlers {
    UIElement element{nullptr};
    winrt::event_token tappedToken{};
    winrt::event_token pointerEnteredToken{};
    winrt::event_token pointerMovedToken{};
    winrt::event_token pointerExitedToken{};
    winrt::event_token pointerCaptureLostToken{};
    winrt::event_token pointerCanceledToken{};
};

struct MenuItemClickHandler {
    MenuFlyoutItem item{nullptr};
    winrt::event_token token{};
};

struct AccountUiRefs {
    StackPanel column{nullptr};
    FrameworkElement barArea{nullptr};
    ToolTip toolTip{nullptr};
    winrt::event_token toolTipOpenedToken{};
    DispatcherTimer manualToolTipTimer{nullptr};
    winrt::event_token manualToolTipTimerToken{};
    std::array<Border, kQuotaBarCount> tracks{
        Border{nullptr}, Border{nullptr}, Border{nullptr}, Border{nullptr}};
    std::array<Grid, kQuotaBarCount> barItems{
        Grid{nullptr}, Grid{nullptr}, Grid{nullptr}, Grid{nullptr}};
    std::array<Border, kQuotaBarCount> fills{
        Border{nullptr}, Border{nullptr}, Border{nullptr}, Border{nullptr}};
    std::array<Border, kQuotaBarCount> paceTicks{
        Border{nullptr}, Border{nullptr}, Border{nullptr}, Border{nullptr}};
    std::array<TextBlock, kQuotaBarCount> percents{
        TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}};
    TextBlock label{nullptr};
    POINT toolTipOpenCursor{};
    bool hasToolTipOpenCursor = false;
    bool reopenToolTipOnMove = false;
    bool manualToolTipOpen = false;
};

struct QuotaUiInstance {
    HWND hWnd = nullptr;
    DWORD ownerThreadId = 0;
    double rasterizationScale = 1.0;
    bool windowSubclassed = false;
    ULONGLONG buildSettingsGeneration = 0;
    bool buildVisualTestMode = false;
    Grid quotaGrid{nullptr};
    Panel injectionParent{nullptr};
    ColumnDefinition quotaColumnDefinition{nullptr};
    std::vector<PointerHandlers> pointerHandlers;
    std::vector<MenuItemClickHandler> menuItemClickHandlers;
    std::vector<AccountUiRefs> accountRefs;
    std::vector<AppliedState> applied;
    DispatcherTimer paceTimer{nullptr};
    winrt::event_token paceTimerToken{};
    // Per-account show/hide toggle items, paired with their account index, for cross-instance
    // IsChecked sync in UpdateQuotaUi (Click is revoked via menuItemClickHandlers).
    std::vector<std::pair<int, ToggleMenuFlyoutItem>> accountToggleItems;
};

[[clang::no_destroy]] static std::optional<
    std::vector<std::unique_ptr<QuotaUiInstance>>> g_uiInstances{std::in_place};
static std::mutex g_uiInstancesMutex;

static const wchar_t* kRootName = L"AiQuota_Root";

using WindowThreadProc = bool (*)(void*);
struct TaskbarDisplayInfo {
    HWND hWnd = nullptr;
    bool primary = false;
    int monitorNumber = 0;
    RECT rect{};
};

// src/core/forward.h
// Forward declarations for calls that reach a later fragment.

static int ScaleForDpi(int value, UINT dpi);
static UINT WindowDpi(HWND hWnd);
static void UpdateQuotaUi(QuotaUiInstance& state);
static void PostUiUpdate();
static void OpenSettingsWindow();
static void OpenHistoryWindow();
static bool SaveOwnedSettings(const Settings& settings);
static void PublishSettings(Settings settings, uint64_t oldIdentity = 0,
                            uint64_t newIdentity = 0);

static void StartRetryInject(bool removeExisting = false);
static LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                  LPARAM lParam, DWORD_PTR refData);

// src/core/state.h
// Shared runtime state, lifecycle signals, and cross-thread requests to the fetch thread
// and settings window.

static Settings g_settings;
static std::mutex g_settingsMutex;
static std::mutex g_configEditMutex;
static ULONGLONG g_settingsGeneration = 0;
static std::vector<AccountData> g_data;
static std::mutex g_dataMutex;

static std::atomic<bool> g_unloading{false};
static std::atomic<bool> g_refreshing{false};
static std::atomic<uint64_t> g_refreshAccountIdentity{0};
static std::atomic<ULONGLONG> g_refreshGeneration{0};
static std::mutex g_refreshMutex;
static std::atomic<bool> g_uiInjected{false};
static std::atomic<bool> g_visualTestMode{false};
static std::atomic<bool> g_settingsLoadError{false};
static std::atomic<bool> g_fetchThreadStarted{false};
static HANDLE g_stopEvent = nullptr;
static HANDLE g_refreshEvent = nullptr;

static std::atomic<HWND> g_settingsWindow{nullptr};

static constexpr UINT kSettingsRefreshMessage = WM_APP + 20;
static constexpr UINT kExitVisualTestMessage = WM_APP + 21;
static constexpr UINT kHistoryConsentChangedMessage = WM_APP + 22;

static void NotifySettingsWindowChanged(bool historyConsentChanged = false) {
    if (HWND hWnd = g_settingsWindow.load()) {
        PostMessageW(hWnd, historyConsentChanged ? kHistoryConsentChangedMessage : kSettingsRefreshMessage, 0, 0);
    }
}

static void QueueRefresh(uint64_t identityHash) {
    if (g_unloading) return;

    if (!g_fetchThreadStarted.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            g_refreshing = false;
            g_refreshAccountIdentity = 0;
        }
        PostUiUpdate();
        return;
    }

    {
        std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
        g_refreshing = true;
        g_refreshAccountIdentity = identityHash;
        g_refreshGeneration++;
    }
    PostUiUpdate();
    if (g_refreshEvent) SetEvent(g_refreshEvent);
}

static void RefreshQuotaByIdentity(uint64_t identityHash) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        for (const auto& account : g_settings.accounts) {
            if (AccountIdentityHash(account) == identityHash) {
                found = true;
                break;
            }
        }
    }
    if (found) QueueRefresh(identityHash);
}

// src/core/helpers.h
// Time, text-conversion, display-name, and shell helpers.

static constexpr ULONGLONG kFileTimeUnixEpochOffsetMs = 11644473600000ULL;

static ULONGLONG NowUnixMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return t / 10000 - kFileTimeUnixEpochOffsetMs;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
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
    ULONGLONG unixMs = t / 10000 - kFileTimeUnixEpochOffsetMs;

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

static bool UnixMsToLocalSystemTime(ULONGLONG unixMs, SYSTEMTIME* local) {
    if (!unixMs || !local) return false;

    ULONGLONG t = (unixMs + kFileTimeUnixEpochOffsetMs) * 10000;
    FILETIME ft{(DWORD)(t & 0xFFFFFFFF), (DWORD)(t >> 32)};
    SYSTEMTIME utc;
    return FileTimeToSystemTime(&ft, &utc) &&
           SystemTimeToTzSpecificLocalTime(nullptr, &utc, local);
}

static std::wstring FormatLocalTime(SYSTEMTIME const& local) {
    wchar_t buf[64];
    if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, buf,
                        ARRAYSIZE(buf)) > 0) {
        return buf;
    }

    swprintf(buf, ARRAYSIZE(buf), L"%02u:%02u", local.wHour, local.wMinute);
    return buf;
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

    SYSTEMTIME local;
    if (!UnixMsToLocalSystemTime(unixMs, &local)) {
        return rel;
    }

    std::wstring localTime = FormatLocalTime(local);
    if (delta < 24LL * 3600 * 1000) {
        return std::wstring(rel) + L" (" + localTime + L")";
    }

    wchar_t day[16] = L"";
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, L"ddd", day, ARRAYSIZE(day), nullptr);
    return std::wstring(rel) + L" (" + day + L" " + localTime + L")";
}

static std::wstring FormatUpdated(ULONGLONG unixMs, bool stale) {
    if (!unixMs) return L"updated: no data yet";
    SYSTEMTIME local;
    if (!UnixMsToLocalSystemTime(unixMs, &local)) {
        return L"updated: ?";
    }
    return std::wstring(L"updated: ") + FormatLocalTime(local) + (stale ? L" (stale)" : L"");
}

static void OpenUrl(PCWSTR url) {
    if (g_unloading || !url || !*url) return;
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

static PCWSTR ProviderDisplayName(const std::wstring& provider) {
    if (provider == L"anthropic") return L"Anthropic";
    if (provider == L"openai") return L"OpenAI";
    return L"Google Antigravity";
}

// src/core/json.h
// Null-tolerant JSON accessors and error-body summaries.

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

// src/core/crypto.h
// Base64, SHA-256, JWT payload decoding, and DPAPI token encryption.

static std::string Base64Encode(const BYTE* data, size_t len) {
    if (!len) return {};
    DWORD outLen = 0;
    if (!CryptBinaryToStringA(data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              nullptr, &outLen) || !outLen) {
        return {};
    }
    std::string out(outLen, '\0');
    if (!CryptBinaryToStringA(data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              out.data(), &outLen)) {
        return {};
    }
    out.resize(outLen);
    return out;
}

static std::vector<BYTE> Base64Decode(const std::string& s) {
    if (s.empty()) return {};
    DWORD binLen = 0;
    if (!CryptStringToBinaryA(s.data(), (DWORD)s.size(), CRYPT_STRING_BASE64, nullptr, &binLen,
                              nullptr, nullptr) || !binLen) {
        return {};
    }
    std::vector<BYTE> out(binLen);
    if (!CryptStringToBinaryA(s.data(), (DWORD)s.size(), CRYPT_STRING_BASE64, out.data(), &binLen,
                              nullptr, nullptr)) {
        return {};
    }
    out.resize(binLen);
    return out;
}

// RFC 7636 base64url (no padding). Used for PKCE and to decode JWT segments.
static std::string ToBase64Url(const BYTE* data, size_t len) {
    std::string b64 = Base64Encode(data, len);
    std::string out;
    out.reserve(b64.size());
    for (char c : b64) {
        if (c == '+') out += '-';
        else if (c == '/') out += '_';
        else if (c == '=') continue;
        else out += c;
    }
    return out;
}

static std::vector<BYTE> FromBase64Url(std::string s) {
    for (char& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    s.append((4 - s.size() % 4) % 4, '=');
    return Base64Decode(s);
}

static std::string RandomBase64Url(size_t bytes) {
    std::vector<BYTE> buf(bytes);
    if (BCryptGenRandom(nullptr, buf.data(), (ULONG)buf.size(),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return {};
    }
    return ToBase64Url(buf.data(), buf.size());
}

// PKCE code challenge: base64url(SHA-256(verifier)).
static std::string Sha256Base64Url(const std::string& input) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    std::string out;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<BYTE, 32> digest{};
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        if (BCryptHashData(hash, (PUCHAR)input.data(), (ULONG)input.size(), 0) == 0 &&
            BCryptFinishHash(hash, digest.data(), (ULONG)digest.size(), 0) == 0) {
            out = ToBase64Url(digest.data(), digest.size());
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

// Decodes a JWT's payload segment into a JSON object (claims). Empty on any failure.
static JsonObject ParseJwtPayload(const std::wstring& jwt) {
    std::string narrow = WideToUtf8(jwt);
    size_t first = narrow.find('.');
    if (first == std::string::npos) return nullptr;
    size_t second = narrow.find('.', first + 1);
    std::string payload = narrow.substr(first + 1,
        second == std::string::npos ? std::string::npos : second - first - 1);
    std::vector<BYTE> bytes = FromBase64Url(payload);
    if (bytes.empty()) return nullptr;
    try {
        return JsonObject::Parse(Utf8ToWide(std::string((char*)bytes.data(), bytes.size())));
    } catch (...) {
        return nullptr;
    }
}

// DPAPI (current user) so stored tokens are not plaintext at rest. Returns base64 of the
// protected blob, or empty on failure.
static std::string DpapiProtect(const std::string& plain) {
    DATA_BLOB in{(DWORD)plain.size(), (BYTE*)plain.data()};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"taskbar-ai-quota", nullptr, nullptr, nullptr, 0, &out)) {
        return {};
    }
    std::string b64 = Base64Encode(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return b64;
}

static std::string DpapiUnprotect(const std::string& b64) {
    std::vector<BYTE> blob = Base64Decode(b64);
    if (blob.empty()) return {};
    DATA_BLOB in{(DWORD)blob.size(), blob.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        return {};
    }
    std::string plain((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}

// src/net/http.h
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

static HttpResult HttpRequest(PCWSTR method, PCWSTR host, PCWSTR path, PCWSTR userAgent,
                              const std::wstring& headers, const std::string& body = {}) {
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
        req = con ? WinHttpOpenRequest(con, method, path, nullptr,
                                        WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE)
                  : nullptr;
        if (req && !TrackHttpHandle(req)) req = nullptr;
    }

    if (!g_unloading && req &&
        WinHttpSendRequest(req, headers.c_str(), (DWORD)headers.size(),
                           body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                           (DWORD)body.size(), (DWORD)body.size(), 0) &&
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
                    if (fileMs > kFileTimeUnixEpochOffsetMs) {
                        ULONGLONG retryUnixMs = fileMs - kFileTimeUnixEpochOffsetMs;
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
            constexpr size_t kMaxResponseBytes = 4 * 1024 * 1024;
            if (available > kMaxResponseBytes - prev) {
                bodyOk = false;
                break;
            }
            res.body.resize(prev + available);
            DWORD read = 0;
            if (!WinHttpReadData(req, res.body.data() + prev, available, &read)) {
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

// src/auth/token-store.h
// DPAPI-encrypted per-account token storage, auth epochs, and rename redirects.

// Per-account OAuth credentials, owned entirely by the mod (the CLI credential files are
// never touched). Persisted DPAPI-encrypted in Windhawk mod storage, keyed by identity hash.
struct StoredToken {
    std::wstring accessToken;
    std::wstring refreshToken;
    std::wstring accountId;  // OpenAI ChatGPT-Account-Id; empty for Anthropic.
    ULONGLONG expiresMs = 0;  // 0 = unknown; refresh is then driven reactively by 401s.
};

static std::mutex g_authMutex;
static std::mutex g_authEpochMutex;
static std::vector<std::pair<uint64_t, ULONGLONG>> g_authEpochs;

struct AuthRenameRedirect {
    uint64_t sourceIdentity;
    ULONGLONG sourceEpoch;
    uint64_t destinationIdentity;
    ULONGLONG destinationEpoch;
};

static std::vector<AuthRenameRedirect> g_authRenameRedirects;

enum class TokenSaveResult {
    Saved,
    Stale,
    Failed,
};

static ULONGLONG AuthEpochLocked(uint64_t idHash) {
    for (const auto& [hash, epoch] : g_authEpochs) {
        if (hash == idHash) return epoch;
    }
    return 0;
}

static ULONGLONG CurrentAuthEpoch(uint64_t idHash) {
    std::lock_guard<std::mutex> lk(g_authEpochMutex);
    return AuthEpochLocked(idHash);
}

static std::wstring TokenStorageKey(uint64_t idHash) {
    wchar_t buf[32];
    swprintf(buf, ARRAYSIZE(buf), L"auth_%016llx", (unsigned long long)idHash);
    return buf;
}

static bool LoadStoredToken(uint64_t idHash, StoredToken* out) {
    std::lock_guard<std::mutex> lk(g_authMutex);
    std::vector<wchar_t> buf(16384);
    Wh_GetStringValue(TokenStorageKey(idHash).c_str(), buf.data(), buf.size());
    std::wstring stored = buf.data();
    if (stored.empty()) return false;

    std::string plain = DpapiUnprotect(WideToUtf8(stored));
    if (plain.empty()) return false;
    try {
        auto root = JsonObject::Parse(Utf8ToWide(plain));
        out->accessToken = GetStr(root, L"access");
        out->refreshToken = GetStr(root, L"refresh");
        out->accountId = GetStr(root, L"accountId");
        out->expiresMs = (ULONGLONG)GetNum(root, L"expiresMs", 0);
        return !out->accessToken.empty() || !out->refreshToken.empty();
    } catch (...) {
        return false;
    }
}

static bool SaveStoredToken(uint64_t idHash, const StoredToken& t) {
    std::lock_guard<std::mutex> lk(g_authMutex);
    std::wstring json;
    try {
        JsonObject root;
        root.SetNamedValue(L"access", JsonValue::CreateStringValue(winrt::hstring(t.accessToken)));
        root.SetNamedValue(L"refresh", JsonValue::CreateStringValue(winrt::hstring(t.refreshToken)));
        root.SetNamedValue(L"accountId", JsonValue::CreateStringValue(winrt::hstring(t.accountId)));
        root.SetNamedValue(L"expiresMs", JsonValue::CreateNumberValue((double)t.expiresMs));
        json = root.Stringify().c_str();
    } catch (...) {
        return false;
    }
    std::string b64 = DpapiProtect(WideToUtf8(json));
    if (b64.empty()) return false;
    return Wh_SetStringValue(TokenStorageKey(idHash).c_str(), Utf8ToWide(b64).c_str());
}

static bool ClearStoredToken(uint64_t idHash) {
    std::lock_guard<std::mutex> lk(g_authMutex);
    std::wstring key = TokenStorageKey(idHash);
    Wh_DeleteValue(key.c_str());
    std::vector<wchar_t> stored(16384);
    Wh_GetStringValue(key.c_str(), stored.data(), stored.size());
    return !stored[0];
}

static TokenSaveResult SaveStoredTokenIfCurrent(uint64_t idHash, ULONGLONG authEpoch,
                                                const StoredToken& t,
                                                uint64_t* savedIdentity = nullptr) {
    if (savedIdentity) *savedIdentity = 0;
    std::lock_guard<std::mutex> lk(g_authEpochMutex);
    uint64_t destinationIdentity = idHash;
    ULONGLONG destinationEpoch = authEpoch;
    // Exact epochs let saves follow renames but stop at sign-out or any other invalidating bump.
    for (int hop = 0; AuthEpochLocked(destinationIdentity) != destinationEpoch; hop++) {
        if (hop >= 32) return TokenSaveResult::Stale;
        auto redirect = std::find_if(
            g_authRenameRedirects.begin(), g_authRenameRedirects.end(),
            [&](const AuthRenameRedirect& candidate) {
                return candidate.sourceIdentity == destinationIdentity &&
                       candidate.sourceEpoch == destinationEpoch;
            });
        if (redirect == g_authRenameRedirects.end()) return TokenSaveResult::Stale;
        destinationIdentity = redirect->destinationIdentity;
        destinationEpoch = redirect->destinationEpoch;
    }
    if (!SaveStoredToken(destinationIdentity, t)) return TokenSaveResult::Failed;
    if (savedIdentity) *savedIdentity = destinationIdentity;
    return TokenSaveResult::Saved;
}

static void BumpAuthEpochLocked(uint64_t idHash) {
    for (auto& [hash, epoch] : g_authEpochs) {
        if (hash == idHash) {
            epoch++;
            return;
        }
    }
    g_authEpochs.push_back({idHash, 1});
}

static bool ClearStoredTokenAndBumpAuthEpoch(uint64_t idHash) {
    std::lock_guard<std::mutex> lk(g_authEpochMutex);
    bool cleared = ClearStoredToken(idHash);
    BumpAuthEpochLocked(idHash);
    return cleared;
}

enum class TokenCopyResult {
    SourceMissing,
    Copied,
    DestinationOccupied,
    Failed,
};

static TokenCopyResult CopyStoredTokenForRename(uint64_t oldHash, uint64_t newHash) {
    std::lock_guard<std::mutex> authLock(g_authMutex);

    std::vector<wchar_t> oldValue(16384);
    std::vector<wchar_t> newValue(16384);
    Wh_GetStringValue(TokenStorageKey(oldHash).c_str(), oldValue.data(), oldValue.size());
    if (!oldValue[0]) return TokenCopyResult::SourceMissing;
    Wh_GetStringValue(TokenStorageKey(newHash).c_str(), newValue.data(), newValue.size());
    if (newValue[0]) return TokenCopyResult::DestinationOccupied;
    if (!Wh_SetStringValue(TokenStorageKey(newHash).c_str(), oldValue.data())) {
        return TokenCopyResult::Failed;
    }
    return TokenCopyResult::Copied;
}

// src/auth/oauth.h
// OAuth client constants, authorize URLs, and token-endpoint exchange and refresh.

// Public OAuth clients used by the official CLIs; the mod runs the same flows so a sign-in
// here is independent of (and never touches) OpenCode/Claude Code/Codex credential files.
static constexpr PCWSTR kAnthropicClientId = L"9d1c250a-e61b-44d9-88ed-5944d1962f5e";
static constexpr PCWSTR kAnthropicTokenHost = L"console.anthropic.com";
static constexpr PCWSTR kAnthropicTokenPath = L"/v1/oauth/token";
static constexpr PCWSTR kAnthropicRedirect = L"https://console.anthropic.com/oauth/code/callback";
static constexpr PCWSTR kAnthropicScope = L"org:create_api_key user:profile user:inference";
static constexpr PCWSTR kOpenAiClientId = L"app_EMoamEEZ73f0CkXaXp7hrann";
static constexpr PCWSTR kOpenAiTokenHost = L"auth.openai.com";
static constexpr PCWSTR kOpenAiTokenPath = L"/oauth/token";
static constexpr PCWSTR kOpenAiScope =
    L"openid profile email offline_access api.connectors.read api.connectors.invoke";
static constexpr PCWSTR kOAuthUserAgent = L"taskbar-ai-quota/0.1";

static std::string UrlEncode(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

static std::string UrlDecode(const std::string& s) {
    auto hex = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '%' && i + 2 < s.size()) {
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += (c == '+') ? ' ' : c;
    }
    return out;
}

static std::wstring BuildAnthropicAuthorizeUrl(const std::string& challenge, const std::string& state) {
    std::string url = "https://claude.ai/oauth/authorize?code=true";
    url += "&client_id=" + UrlEncode(WideToUtf8(kAnthropicClientId));
    url += "&response_type=code";
    url += "&redirect_uri=" + UrlEncode(WideToUtf8(kAnthropicRedirect));
    url += "&scope=" + UrlEncode(WideToUtf8(kAnthropicScope));
    url += "&code_challenge=" + challenge;
    url += "&code_challenge_method=S256";
    url += "&state=" + state;
    return Utf8ToWide(url);
}

static std::wstring BuildOpenAiAuthorizeUrl(int port, const std::string& challenge,
                                            const std::string& state) {
    std::string redirect = "http://localhost:" + std::to_string(port) + "/auth/callback";
    std::string url = "https://auth.openai.com/oauth/authorize?response_type=code";
    url += "&client_id=" + UrlEncode(WideToUtf8(kOpenAiClientId));
    url += "&redirect_uri=" + UrlEncode(redirect);
    url += "&scope=" + UrlEncode(WideToUtf8(kOpenAiScope));
    url += "&code_challenge=" + challenge;
    url += "&code_challenge_method=S256";
    url += "&id_token_add_organizations=true";
    url += "&codex_cli_simplified_flow=true";
    url += "&originator=codex_cli_rs";
    url += "&state=" + state;
    return Utf8ToWide(url);
}

enum class TokenEndpointResult {
    Success,
    Rejected,
    TransientFailure,
};

static std::wstring ParseOAuthError(const std::string& body) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        std::wstring desc = GetStr(root, L"error_description");
        if (!desc.empty()) return desc;
        return GetStr(root, L"error");
    } catch (...) {
        return {};
    }
}

// Fills *tok from an OAuth token response (access/refresh/id_token), extracting the OpenAI
// ChatGPT-Account-Id and access-token expiry from JWT claims when present. Used for both the
// authorization-code exchange and refresh.
static bool ParseTokenResponse(const std::string& body, bool anthropic, StoredToken* tok,
                               std::wstring* err) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        std::wstring access = GetStr(root, L"access_token");
        std::wstring refresh = GetStr(root, L"refresh_token");
        if (access.empty()) {
            *err = L"no access token in response";
            return false;
        }
        tok->accessToken = access;
        if (!refresh.empty()) tok->refreshToken = refresh;

        tok->expiresMs = 0;
        double expiresIn = GetNum(root, L"expires_in", 0);
        if (expiresIn > 0) tok->expiresMs = NowUnixMs() + (ULONGLONG)(expiresIn * 1000);

        if (!anthropic) {
            std::wstring idToken = GetStr(root, L"id_token");
            if (auto claims = ParseJwtPayload(idToken.empty() ? access : idToken)) {
                if (auto authObj = GetObj(claims, L"https://api.openai.com/auth")) {
                    std::wstring acc = GetStr(authObj, L"chatgpt_account_id");
                    if (!acc.empty()) tok->accountId = acc;
                }
            }
            // OpenAI responses omit expires_in; derive expiry from the access-token JWT exp.
            if (tok->expiresMs == 0) {
                if (auto claims = ParseJwtPayload(access)) {
                    double exp = GetNum(claims, L"exp", 0);
                    if (exp > 0) tok->expiresMs = (ULONGLONG)(exp * 1000);
                }
            }
        }
        return true;
    } catch (...) {
        *err = L"invalid token response";
        return false;
    }
}

static TokenEndpointResult PostTokenEndpoint(bool anthropic, const std::wstring& contentType,
                                              const std::string& body, StoredToken* tok,
                                              std::wstring* err, int* retryAfterSec = nullptr) {
    if (retryAfterSec) *retryAfterSec = 0;
    std::wstring headers = L"Content-Type: " + contentType + L"\r\nAccept: application/json\r\n";
    PCWSTR host = anthropic ? kAnthropicTokenHost : kOpenAiTokenHost;
    PCWSTR path = anthropic ? kAnthropicTokenPath : kOpenAiTokenPath;
    HttpResult r = HttpRequest(L"POST", host, path, kOAuthUserAgent, headers, body);
    if (!r.ok) {
        *err = L"network error";
        return TokenEndpointResult::TransientFailure;
    }
    if (r.status < 200 || r.status >= 300) {
        if (retryAfterSec) *retryAfterSec = r.retryAfterSec;
        std::wstring detail = ParseOAuthError(r.body);
        *err = detail.empty() ? (L"HTTP " + std::to_wstring(r.status)) : detail;
        bool rejected = r.status >= 400 && r.status < 500 && r.status != 408 &&
                        r.status != 429;
        return rejected ? TokenEndpointResult::Rejected
                        : TokenEndpointResult::TransientFailure;
    }
    return ParseTokenResponse(r.body, anthropic, tok, err)
               ? TokenEndpointResult::Success
               : TokenEndpointResult::TransientFailure;
}

// grant_type=refresh_token. Anthropic sends JSON; OpenAI sends JSON too (its auth-code
// exchange is the form-encoded one). Rotated refresh tokens come back in the response.
static TokenEndpointResult RefreshToken(const std::wstring& provider, StoredToken* tok,
                                        std::wstring* err, int* retryAfterSec) {
    if (retryAfterSec) *retryAfterSec = 0;
    if (tok->refreshToken.empty()) {
        *err = L"no refresh token";
        return TokenEndpointResult::Rejected;
    }
    bool anthropic = provider == L"anthropic";
    std::string body;
    try {
        JsonObject obj;
        obj.SetNamedValue(L"grant_type", JsonValue::CreateStringValue(L"refresh_token"));
        obj.SetNamedValue(L"refresh_token",
                          JsonValue::CreateStringValue(winrt::hstring(tok->refreshToken)));
        obj.SetNamedValue(L"client_id", JsonValue::CreateStringValue(
                                            winrt::hstring(anthropic ? kAnthropicClientId : kOpenAiClientId)));
        body = WideToUtf8(std::wstring(obj.Stringify().c_str()));
    } catch (...) {
        *err = L"internal error";
        return TokenEndpointResult::TransientFailure;
    }
    return PostTokenEndpoint(anthropic, L"application/json", body, tok, err, retryAfterSec);
}

// src/auth/login.h
// Sign-in thread: the sign-in window, OpenAI loopback listener, and code exchange.

struct LoginRequest {
    std::wstring provider;
    std::wstring label;
    uint64_t idHash = 0;
    ULONGLONG authEpoch = 0;
};

static std::atomic<bool> g_loginInProgress{false};
static std::atomic<uint64_t> g_loginAccountIdentity{0};
static HANDLE g_loginThread = nullptr;
static std::mutex g_loginThreadMutex;  // Guards g_loginThread handoff vs. the unload join.
static std::atomic<HWND> g_loginWnd{nullptr};        // Anthropic paste dialog window.
static std::atomic<SOCKET> g_loginSocket{INVALID_SOCKET};  // OpenAI loopback listener.

// Small modal-style input window so the Anthropic flow can collect the pasted code#state.
// Runs its own message loop on the login thread; closed on cancel, submit, or unload.
struct LoginDialogState {
    std::wstring result;
    HWND edit = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    bool ok = false;
    bool done = false;
};

static void LayoutLoginDialog(HWND hWnd, LoginDialogState& state, UINT dpi) {
    auto sc = [dpi](int value) { return ScaleForDpi(value, dpi); };
    constexpr UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    SetWindowPos(GetDlgItem(hWnd, 100), nullptr, sc(12), sc(10), sc(424), sc(52), flags);
    SetWindowPos(GetDlgItem(hWnd, 101), nullptr, sc(12), sc(68), sc(424), sc(24), flags);
    SetWindowPos(GetDlgItem(hWnd, IDOK), nullptr, sc(262), sc(104), sc(84), sc(30), flags);
    SetWindowPos(GetDlgItem(hWnd, IDCANCEL), nullptr, sc(352), sc(104), sc(84), sc(30), flags);

    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    LOGFONTW fontDescription{};
    using SystemParametersInfoForDpi_t = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    auto systemParametersInfoForDpi = reinterpret_cast<SystemParametersInfoForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi"));
    if (systemParametersInfoForDpi &&
        systemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                                   &metrics, 0, dpi)) {
        fontDescription = metrics.lfMessageFont;
    } else {
        fontDescription.lfHeight = -MulDiv(9, (int)dpi, 72);
        fontDescription.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(fontDescription.lfFaceName, L"Segoe UI");
    }
    HFONT font = CreateFontIndirectW(&fontDescription);
    if (font) {
        for (HWND child : {GetDlgItem(hWnd, 100), GetDlgItem(hWnd, 101),
                           GetDlgItem(hWnd, IDOK), GetDlgItem(hWnd, IDCANCEL)}) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        if (state.font) DeleteObject(state.font);
        state.font = font;
    }
    state.dpi = dpi;
}

static LRESULT CALLBACK LoginDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* st = reinterpret_cast<LoginDialogState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            HINSTANCE hInst = GetModuleHandleW(nullptr);
            CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                            12, 10, 424, 52, hWnd, (HMENU)100, hInst, nullptr);
            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                        12, 68, 424, 24, hWnd, (HMENU)101, hInst, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Sign in",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            262, 104, 84, 30, hWnd, (HMENU)IDOK, hInst, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Cancel",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            352, 104, 84, 30, hWnd, (HMENU)IDCANCEL, hInst, nullptr);
            if (st) st->edit = edit;
            SetFocus(edit);
            return 0;
        }
        case WM_COMMAND: {
            auto* st = reinterpret_cast<LoginDialogState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            WORD id = LOWORD(wParam);
            if (id == IDOK && st && st->edit) {
                int len = GetWindowTextLengthW(st->edit);
                std::wstring buf(len + 1, L'\0');
                GetWindowTextW(st->edit, buf.data(), len + 1);
                buf.resize(len);
                st->result = std::move(buf);
                st->ok = true;
                DestroyWindow(hWnd);
                return 0;
            }
            if (id == IDCANCEL) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }
        case WM_DPICHANGED: {
            auto* st = reinterpret_cast<LoginDialogState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (st) LayoutLoginDialog(hWnd, *st, HIWORD(wParam));
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY: {
            auto* st = reinterpret_cast<LoginDialogState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            if (st) st->done = true;
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static std::wstring ShowLoginInputDialog(const std::wstring& title, const std::wstring& instructions) {
    static PCWSTR kClass = L"AiQuotaLoginDlg_" WH_MOD_ID;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = LoginDlgProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS ||
            !UnregisterClassW(kClass, hInst) || !RegisterClassExW(&wc)) {
            return {};
        }
    }

    LoginDialogState st;
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &monitorInfo.rcWork, 0);
    }
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    HWND wnd = CreateWindowExW(exStyle, kClass, title.c_str(),
                               style, monitorInfo.rcWork.left, monitorInfo.rcWork.top, 460, 184,
                               nullptr, nullptr, hInst, &st);
    if (!wnd) {
        UnregisterClassW(kClass, hInst);
        return {};
    }
    g_loginWnd.store(wnd);

    UINT dpi = WindowDpi(wnd);
    RECT windowRect{0, 0, ScaleForDpi(448, dpi), ScaleForDpi(144, dpi)};
    using AdjustWindowRectExForDpi_t = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjustWindowRectExForDpi = reinterpret_cast<AdjustWindowRectExForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (!adjustWindowRectExForDpi ||
        !adjustWindowRectExForDpi(&windowRect, style, FALSE, exStyle, dpi)) {
        AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);
    }
    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;
    int x = monitorInfo.rcWork.left +
            (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    int y = monitorInfo.rcWork.top +
            (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    SetWindowPos(wnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    LayoutLoginDialog(wnd, st, dpi);
    SetWindowTextW(GetDlgItem(wnd, 100), instructions.c_str());
    ShowWindow(wnd, SW_SHOW);
    SetForegroundWindow(wnd);
    SetFocus(st.edit);

    MSG msg;
    while (!g_unloading) {
        int getMessageResult = GetMessageW(&msg, nullptr, 0, 0);
        if (getMessageResult <= 0) break;
        if (!IsDialogMessageW(wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (st.done) break;
    }
    g_loginWnd.store(nullptr);
    if (IsWindow(wnd)) DestroyWindow(wnd);
    if (st.font) DeleteObject(st.font);
    // Unregister so a later mod reload can't reuse a class pointing at this now-unloaded WndProc.
    UnregisterClassW(kClass, hInst);
    return st.ok ? st.result : std::wstring();
}

static bool StartLoopback(SOCKET* outSock, int* outPort) {
    for (int port : {1455, 1457}) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) continue;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) == 0 && listen(s, 1) == 0) {
            *outSock = s;
            *outPort = port;
            return true;
        }
        closesocket(s);
    }
    return false;
}

// Accepts one localhost callback and returns the (URL-decoded) authorization code. Polls with
// a 1s select timeout so it stays responsive to g_unloading and an overall deadline.
static std::string WaitForLoopbackCode(SOCKET listener, const std::string& expectedState,
                                       DWORD totalTimeoutMs, std::wstring* terminalError) {
    if (terminalError) terminalError->clear();
    auto getParam = [](const std::string& query, const std::string& key) -> std::string {
        std::string pat = key + "=";
        for (size_t p = 0; (p = query.find(pat, p)) != std::string::npos; p += pat.size()) {
            if (p == 0 || query[p - 1] == '&' || query[p - 1] == '?') {
                size_t v = p + pat.size();
                size_t end = query.find('&', v);
                return UrlDecode(query.substr(v, end == std::string::npos ? std::string::npos : end - v));
            }
        }
        return {};
    };

    ULONGLONG deadline = GetTickCount64() + totalTimeoutMs;
    while (!g_unloading) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(listener, &rd);
        timeval tv{1, 0};
        int sel = select(0, &rd, nullptr, nullptr, &tv);
        if (g_unloading) break;
        if (sel <= 0) {
            if (GetTickCount64() > deadline) break;
            continue;
        }

        SOCKET c = accept(listener, nullptr, nullptr);
        if (c == INVALID_SOCKET) {
            if (g_unloading) break;
            continue;
        }

        DWORD rcvTimeout = 3000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (char*)&rcvTimeout, sizeof(rcvTimeout));
        std::string request;
        char buf[2048];
        for (int i = 0; i < 16; i++) {
            int n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0) break;
            request.append(buf, n);
            if (request.find("\r\n\r\n") != std::string::npos || request.size() > 16384) break;
        }

        std::string path;
        size_t sp1 = request.find(' ');
        size_t sp2 = sp1 == std::string::npos ? std::string::npos : request.find(' ', sp1 + 1);
        if (sp1 != std::string::npos && sp2 != std::string::npos) {
            path = request.substr(sp1 + 1, sp2 - sp1 - 1);
        }
        size_t q = path.find('?');
        std::string route = q == std::string::npos ? path : path.substr(0, q);
        std::string query = q == std::string::npos ? "" : path.substr(q + 1);
        std::string code = getParam(query, "code");
        std::string state = getParam(query, "state");
        std::string oauthError = getParam(query, "error");
        std::string oauthErrorDesc = getParam(query, "error_description");
        bool callback = route == "/auth/callback";
        bool stateOk = callback && (expectedState.empty() || state == expectedState);
        bool success = !code.empty() && stateOk;
        bool terminalFailure = stateOk && (code.empty() || !oauthError.empty());

        std::string html = success
            ? "<!doctype html><meta charset='utf-8'><body style='font-family:sans-serif;padding:2em'>"
              "<h3>Signed in.</h3><p>You can close this tab and return to the taskbar.</p></body>"
            : "<!doctype html><meta charset='utf-8'><body style='font-family:sans-serif;padding:2em'>"
              "<h3>Sign-in failed.</h3><p>You can close this tab and try again from the taskbar.</p></body>";
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                           "Connection: close\r\nContent-Length: " + std::to_string(html.size()) +
                           "\r\n\r\n" + html;
        send(c, resp.data(), (int)resp.size(), 0);
        closesocket(c);

        if (success) return code;
        if (terminalFailure) {
            if (terminalError) {
                if (!oauthErrorDesc.empty()) *terminalError = Utf8ToWide(oauthErrorDesc);
                else if (!oauthError.empty()) *terminalError = Utf8ToWide(oauthError);
                else *terminalError = L"missing authorization code";
            }
            return {};
        }
        // Ignore stray hits (favicon, etc.) and keep waiting until the deadline.
        if (GetTickCount64() > deadline) break;
    }
    return {};
}

static void DoAnthropicLogin(const LoginRequest& req) {
    std::string verifier = RandomBase64Url(32);
    std::string challenge = Sha256Base64Url(verifier);
    std::string state = RandomBase64Url(32);
    if (verifier.empty() || challenge.empty() || state.empty()) {
        Wh_Log(L"Sign-in: crypto init failed");
        return;
    }

    OpenUrl(BuildAnthropicAuthorizeUrl(challenge, state).c_str());
    std::wstring title = L"Sign in: " + req.label + L" (Anthropic)";
    std::wstring instructions =
        L"A browser window opened to claude.ai. Approve access, then copy the code shown "
        L"on the page and paste it below.";
    std::wstring pasted = ShowLoginInputDialog(title, instructions);
    if (g_unloading || pasted.empty()) return;

    size_t b = pasted.find_first_not_of(L" \t\r\n");
    size_t e = pasted.find_last_not_of(L" \t\r\n");
    std::wstring code = b == std::wstring::npos ? L"" : pasted.substr(b, e - b + 1);
    size_t hashPos = code.find(L'#');  // pasted value is code#state.
    if (hashPos != std::wstring::npos) code = code.substr(0, hashPos);
    if (code.empty()) {
        Wh_Log(L"Sign-in [%s]: no code entered", req.label.c_str());
        return;
    }

    std::string body;
    try {
        JsonObject obj;
        obj.SetNamedValue(L"grant_type", JsonValue::CreateStringValue(L"authorization_code"));
        obj.SetNamedValue(L"client_id", JsonValue::CreateStringValue(winrt::hstring(kAnthropicClientId)));
        obj.SetNamedValue(L"code", JsonValue::CreateStringValue(winrt::hstring(code)));
        obj.SetNamedValue(L"redirect_uri", JsonValue::CreateStringValue(winrt::hstring(kAnthropicRedirect)));
        obj.SetNamedValue(L"code_verifier", JsonValue::CreateStringValue(winrt::hstring(Utf8ToWide(verifier))));
        obj.SetNamedValue(L"state", JsonValue::CreateStringValue(winrt::hstring(Utf8ToWide(state))));
        body = WideToUtf8(std::wstring(obj.Stringify().c_str()));
    } catch (...) {
        Wh_Log(L"Sign-in [%s]: internal error", req.label.c_str());
        return;
    }

    StoredToken tok;
    std::wstring err;
    if (PostTokenEndpoint(/*anthropic*/ true, L"application/json", body, &tok, &err) ==
        TokenEndpointResult::Success) {
        TokenSaveResult saved = SaveStoredTokenIfCurrent(req.idHash, req.authEpoch, tok);
        if (saved == TokenSaveResult::Stale) {
            Wh_Log(L"Sign-in [%s]: cancelled before saving token", req.label.c_str());
            return;
        }
        if (saved != TokenSaveResult::Saved) {
            Wh_Log(L"Sign-in [%s] failed: could not save token", req.label.c_str());
            return;
        }
        RefreshQuotaByIdentity(req.idHash);
        Wh_Log(L"Sign-in [%s]: success", req.label.c_str());
    } else {
        Wh_Log(L"Sign-in [%s] failed: %s", req.label.c_str(), err.c_str());
    }
}

static void DoOpenAiLogin(const LoginRequest& req) {
    std::string verifier = RandomBase64Url(32);
    std::string challenge = Sha256Base64Url(verifier);
    std::string state = RandomBase64Url(32);
    if (verifier.empty() || challenge.empty() || state.empty()) {
        Wh_Log(L"Sign-in: crypto init failed");
        return;
    }

    SOCKET listener = INVALID_SOCKET;
    int port = 0;
    if (!StartLoopback(&listener, &port)) {
        Wh_Log(L"Sign-in [%s]: could not bind localhost:1455/1457 (Codex running?)",
               req.label.c_str());
        return;
    }
    g_loginSocket.store(listener);
    OpenUrl(BuildOpenAiAuthorizeUrl(port, challenge, state).c_str());
    std::wstring callbackErr;
    std::string code = WaitForLoopbackCode(listener, state, 180000, &callbackErr);
    SOCKET s = g_loginSocket.exchange(INVALID_SOCKET);
    if (s != INVALID_SOCKET) closesocket(s);
    if (g_unloading || code.empty()) {
        if (code.empty() && !g_unloading) {
            if (!callbackErr.empty()) {
                Wh_Log(L"Sign-in [%s] failed: %s", req.label.c_str(), callbackErr.c_str());
            } else {
                Wh_Log(L"Sign-in [%s]: cancelled or timed out", req.label.c_str());
            }
        }
        return;
    }

    std::string redirect = "http://localhost:" + std::to_string(port) + "/auth/callback";
    std::string formBody = "grant_type=authorization_code&code=" + UrlEncode(code) +
                           "&redirect_uri=" + UrlEncode(redirect) +
                           "&client_id=" + UrlEncode(WideToUtf8(kOpenAiClientId)) +
                           "&code_verifier=" + UrlEncode(verifier);

    StoredToken tok;
    std::wstring err;
    if (PostTokenEndpoint(/*anthropic*/ false, L"application/x-www-form-urlencoded", formBody,
                          &tok, &err) == TokenEndpointResult::Success) {
        TokenSaveResult saved = SaveStoredTokenIfCurrent(req.idHash, req.authEpoch, tok);
        if (saved == TokenSaveResult::Stale) {
            Wh_Log(L"Sign-in [%s]: cancelled before saving token", req.label.c_str());
            return;
        }
        if (saved != TokenSaveResult::Saved) {
            Wh_Log(L"Sign-in [%s] failed: could not save token", req.label.c_str());
            return;
        }
        RefreshQuotaByIdentity(req.idHash);
        Wh_Log(L"Sign-in [%s]: success", req.label.c_str());
    } else {
        Wh_Log(L"Sign-in [%s] failed: %s", req.label.c_str(), err.c_str());
    }
}

static DWORD WINAPI LoginThreadProc(LPVOID param) {
    std::unique_ptr<LoginRequest> req(reinterpret_cast<LoginRequest*>(param));
    using SetThreadDpiAwarenessContext_t = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto setDpi = reinterpret_cast<SetThreadDpiAwarenessContext_t>(
            GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartmentInitialized = true;
    } catch (...) {}
    try {
        if (!g_unloading) {
            if (req->provider == L"anthropic") DoAnthropicLogin(*req);
            else if (req->provider == L"openai") DoOpenAiLogin(*req);
        }
    } catch (...) {
        Wh_Log(L"Sign-in: exception");
    }
    if (apartmentInitialized) winrt::uninit_apartment();
    g_loginAccountIdentity.store(0);
    g_loginInProgress.store(false);
    NotifySettingsWindowChanged();
    return 0;
}

// Kicks off a sign-in on a dedicated thread (browser + paste dialog or loopback are blocking).
// One at a time; runs on a taskbar UI thread (menu click).
static void StartLoginByIdentity(uint64_t identityHash) {
    if (g_unloading) return;
    std::lock_guard<std::mutex> configLock(g_configEditMutex);
    AccountConfig account;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        auto it = std::find_if(g_settings.accounts.begin(), g_settings.accounts.end(),
            [&](const AccountConfig& candidate) {
                return AccountIdentityHash(candidate) == identityHash;
            });
        if (it == g_settings.accounts.end() ||
            (it->provider != L"anthropic" && it->provider != L"openai")) return;
        account = *it;
    }
    bool expected = false;
    if (!g_loginInProgress.compare_exchange_strong(expected, true)) return;

    auto* req = new LoginRequest();
    req->provider = account.provider;
    req->label = account.label;
    req->idHash = identityHash;
    req->authEpoch = CurrentAuthEpoch(req->idHash);
    g_loginAccountIdentity.store(identityHash);
    NotifySettingsWindowChanged();

    // Hand off g_loginThread under the lock and re-check g_unloading: Wh_ModUninit sets
    // g_unloading before joining under the same lock, so we never spawn a thread into an
    // unloading DLL or leave a handle the join would miss.
    std::lock_guard<std::mutex> lk(g_loginThreadMutex);
    if (g_unloading) {
        delete req;
        g_loginAccountIdentity.store(0);
        g_loginInProgress.store(false);
        NotifySettingsWindowChanged();
        return;
    }
    // A prior login thread has already cleared g_loginInProgress; release its handle before reuse.
    if (g_loginThread) {
        CloseHandle(g_loginThread);
        g_loginThread = nullptr;
    }
    g_loginThread = CreateThread(nullptr, 0, LoginThreadProc, req, 0, nullptr);
    if (!g_loginThread) {
        delete req;
        g_loginAccountIdentity.store(0);
        g_loginInProgress.store(false);
        NotifySettingsWindowChanged();
    }
}

static bool SignOutAccountByIdentity(uint64_t identityHash) {
    if (g_unloading) return false;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        auto it = std::find_if(g_settings.accounts.begin(), g_settings.accounts.end(),
            [&](const AccountConfig& account) {
                return AccountIdentityHash(account) == identityHash;
            });
        if (it == g_settings.accounts.end() || it->provider == L"antigravity") return false;
    }
    bool cleared = ClearStoredTokenAndBumpAuthEpoch(identityHash);
    RefreshQuotaByIdentity(identityHash);  // Re-fetch so the column flips to "not signed in".
    NotifySettingsWindowChanged();
    return cleared;
}

// src/providers/anthropic.h
// Anthropic usage response parsing.

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
            d->win5h.windowDurationMs = kFiveHourWindowMs;
        }
        if (auto sd = GetObj(usage, L"seven_day")) {
            d->winWeek.pct = GetNum(sd, L"utilization");
            d->winWeek.resetUnixMs = ParseIso8601Ms(GetStr(sd, L"resets_at"));
            d->winWeek.windowDurationMs = kWeeklyWindowMs;
        }
        if (auto fable = GetObj(usage, L"seven_day_fable")) {
            double utilization = GetNum(fable, L"utilization");
            if (std::isfinite(utilization) && utilization >= 0) {
                d->fableWeek.pct = utilization;
                d->fableWeek.resetUnixMs = ParseIso8601Ms(GetStr(fable, L"resets_at"));
                d->fableWeek.windowDurationMs = kWeeklyWindowMs;
            }
        }
        if (d->fableWeek.pct < 0 && usage.HasKey(L"limits")) {
            auto limits = usage.GetNamedValue(L"limits");
            if (limits.ValueType() == JsonValueType::Array) {
                bool selectedActive = false;
                for (const auto& value : limits.GetArray()) {
                    if (value.ValueType() != JsonValueType::Object) continue;
                    JsonObject limit = value.GetObject();
                    if (GetStr(limit, L"kind") != L"weekly_scoped" &&
                        GetStr(limit, L"group") != L"weekly") {
                        continue;
                    }

                    auto scope = GetObj(limit, L"scope");
                    auto model = GetObj(scope, L"model");
                    std::wstring modelName = GetStr(model, L"display_name");
                    // Match the model family token so versioned names such as Fable 5.1 keep working.
                    std::transform(modelName.begin(), modelName.end(), modelName.begin(),
                                   [](wchar_t ch) { return (wchar_t)towlower(ch); });
                    size_t fablePos = modelName.find(L"fable");
                    bool isFable = fablePos != std::wstring::npos &&
                                    (fablePos == 0 || !iswalpha(modelName[fablePos - 1])) &&
                                    (fablePos + 5 == modelName.size() ||
                                     !iswalpha(modelName[fablePos + 5]));
                    if (!isFable) continue;

                    double percent = GetNum(limit, L"percent");
                    if (!std::isfinite(percent) || percent < 0) continue;
                    bool active = GetBool(limit, L"is_active");
                    if (d->fableWeek.pct >= 0 && !active && selectedActive) continue;
                    if (d->fableWeek.pct >= 0 && active == selectedActive &&
                        percent <= d->fableWeek.pct) {
                        continue;
                    }
                    d->fableWeek.pct = percent;
                    d->fableWeek.resetUnixMs = ParseIso8601Ms(GetStr(limit, L"resets_at"));
                    d->fableWeek.windowDurationMs = kWeeklyWindowMs;
                    selectedActive = active;
                }
            }
        }

        d->plan.clear();
        d->extraLines.clear();
        if (auto op = GetObj(usage, L"seven_day_opus"); op && GetNum(op, L"utilization") >= 0) {
            wchar_t line[64];
            swprintf(line, ARRAYSIZE(line), L"opus week: %.0f%%", GetNum(op, L"utilization"));
            d->extraLines += line;
        }
        if (auto eu = GetObj(usage, L"extra_usage"); eu && GetBool(eu, L"is_enabled")) {
            // monthly_limit/used_credits are cents; an explicit null limit means unlimited (an
            // absent or malformed key must not claim that). utilization is null until the
            // first spend of the cycle, so gate the bar on the limit instead and treat the
            // missing value as 0% or the bar would vanish every month start. Either of
            // utilization/used_credits derives the other so the bar and amount text agree.
            double limitCents = GetNum(eu, L"monthly_limit");
            double usedCents = GetNum(eu, L"used_credits");
            double utilization = GetNum(eu, L"utilization");
            bool limitIsNull = eu.HasKey(L"monthly_limit") &&
                               eu.GetNamedValue(L"monthly_limit").ValueType() == JsonValueType::Null;
            d->extraUsage.resetUnixMs = ParseIso8601Ms(GetStr(eu, L"resets_at"));
            if (limitIsNull) {
                // Reported spending is usable history even without a percentage ceiling.
                if (std::isfinite(usedCents) && usedCents >= 0) {
                    d->extraUsedAmount = usedCents / 100.0;
                }
                if (!d->extraLines.empty()) d->extraLines += L"\n";
                d->extraLines += L"extra usage: unlimited";
            } else if (limitCents > 0) {
                if (!std::isfinite(utilization) || utilization < 0) {
                    utilization = usedCents > 0 ? usedCents * 100.0 / limitCents : 0;
                } else if (usedCents < 0) {
                    usedCents = utilization * limitCents / 100.0;
                }
                d->extraUsage.pct = utilization;
                d->extraUsedAmount = std::max(usedCents, 0.0) / 100.0;
                d->extraLimitAmount = limitCents / 100.0;
                if (d->extraUsage.resetUnixMs) {
                    // The API omits the cycle start. Derive the previous monthly billing
                    // boundary from its authoritative next-reset timestamp.
                    ULONGLONG resetFileTime =
                        (d->extraUsage.resetUnixMs + kFileTimeUnixEpochOffsetMs) * 10000;
                    FILETIME resetFt{(DWORD)resetFileTime, (DWORD)(resetFileTime >> 32)};
                    SYSTEMTIME start{};
                    if (FileTimeToSystemTime(&resetFt, &start)) {
                        if (start.wMonth == 1) {
                            start.wMonth = 12;
                            start.wYear--;
                        } else {
                            start.wMonth--;
                        }
                        static constexpr int kDaysPerMonth[] = {
                            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                        int days = kDaysPerMonth[start.wMonth - 1];
                        if (start.wMonth == 2 &&
                            (start.wYear % 4 == 0 &&
                             (start.wYear % 100 != 0 || start.wYear % 400 == 0))) {
                            days = 29;
                        }
                        start.wDay = (WORD)std::min<int>(start.wDay, days);
                        FILETIME startFt;
                        if (SystemTimeToFileTime(&start, &startFt)) {
                            ULONGLONG startFileTime =
                                ((ULONGLONG)startFt.dwHighDateTime << 32) |
                                startFt.dwLowDateTime;
                            ULONGLONG startUnixMs =
                                startFileTime / 10000 - kFileTimeUnixEpochOffsetMs;
                            if (startUnixMs < d->extraUsage.resetUnixMs) {
                                d->extraUsage.windowDurationMs =
                                    d->extraUsage.resetUnixMs - startUnixMs;
                            }
                        }
                    }
                }
            }
        }
        bool parsed = d->win5h.pct >= 0 || d->winWeek.pct >= 0 || d->fableWeek.pct >= 0 ||
                      d->extraUsage.pct >= 0 || d->extraUsedAmount >= 0;
        if (!parsed && error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return parsed;
    } catch (...) {
        if (error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return false;
    }
}

// src/providers/openai.h
// OpenAI usage response parsing and the user-set credits ceiling.

static constexpr ULONGLONG kUnixTimestampMsThreshold = 100000000000ULL;

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
            if (resetAt > (double)kUnixTimestampMsThreshold) return (ULONGLONG)resetAt;
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
            target->windowDurationMs = target == &d->winWeek ? kWeeklyWindowMs : kFiveHourWindowMs;
            if (std::isfinite(windowSeconds) && windowSeconds > 0 &&
                windowSeconds <= 366.0 * 24 * 60 * 60) {
                target->windowDurationMs = (ULONGLONG)(windowSeconds * 1000);
            }
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
        d->openAiExtraLimitLines.clear();
        d->extraLines.clear();
        // Every additional_rate_limits entry (Codex Spark, hidden model lanes such as
        // gpt-reserve, ...) is an opt-in tooltip line; only the primary windows drive bars.
        int extraLimitLineCount = 0;
        auto addLimitLine = [&](JsonObject const& item) {
            auto itemRl = GetObj(item, L"rate_limit");
            auto pw = GetObj(itemRl, L"primary_window");
            auto sw = GetObj(itemRl, L"secondary_window");
            std::wstring name = GetStr(item, L"limit_name");
            if (name.empty() || (!pw && !sw) || extraLimitLineCount >= 6) return;

            wchar_t line[128];
            swprintf(line, ARRAYSIZE(line), L"%s: 5h %.0f%% | wk %.0f%%",
                     name.c_str(), GetNum(pw, L"used_percent", 0),
                     GetNum(sw, L"used_percent", 0));
            if (!d->openAiExtraLimitLines.empty()) d->openAiExtraLimitLines += L"\n";
            d->openAiExtraLimitLines += line;
            extraLimitLineCount++;
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

        // credits: {has_credits, unlimited, balance: string|null}. The tooltip formats this
        // on the UI thread since the display depends on the account's credits max. The
        // balance is read even when has_credits is false so a depleted "0" still yields a
        // full bar instead of hiding it.
        if (auto cr = GetObj(usage, L"credits")) {
            d->hasCredits = GetBool(cr, L"has_credits");
            d->creditsUnlimited = GetBool(cr, L"unlimited");
            double balance = GetNum(cr, L"balance", -1);
            if (balance < 0) {
                // Only a fully numeric string counts; a formatted "$2.50" must not become 0.
                std::wstring s = GetStr(cr, L"balance");
                size_t first = s.find_first_not_of(L" \t");
                if (first != std::wstring::npos) {
                    wchar_t* end = nullptr;
                    balance = wcstod(s.c_str() + first, &end);
                    if (end == s.c_str() + first ||
                        s.find_first_not_of(L" \t", end - s.c_str()) != std::wstring::npos) {
                        balance = -1;
                    }
                }
            }
            if (std::isfinite(balance) && balance >= 0) d->creditsBalance = balance;
        }
        // A credits-only payload (no rate-limit windows) is still usable data, but say so in
        // the tooltip: the 5h/weekly bars auto-hide, and without this line a reshaped
        // rate_limit object would look identical to an account that simply has none.
        bool hasWindowUsage = d->win5h.pct >= 0 || d->winWeek.pct >= 0;
        bool parsed = hasWindowUsage || d->creditsBalance >= 0 || d->creditsUnlimited ||
                      d->hasCredits;
        if (parsed && !hasWindowUsage) d->extraLines = L"rate limits: none reported";
        if (!parsed && error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return parsed;
    } catch (...) {
        if (error) *error = L"unexpected response format (" + DescribeJsonBody(body) + L")";
        return false;
    }
}

// OpenAI has no server-side credits ceiling, so a user-set max turns the prepaid balance
// into a used-percent bar in the extra-usage slot; thresholds, percent text, and
// notifications then apply unchanged. No reset window, so pace ticks stay hidden. Runs on
// fresh fetch results and again from PublishSettings, because settings changes do not
// re-poll and the bar must follow a new max immediately.
static void ApplyCreditsMax(const AccountConfig& acc, AccountData* d) {
    if (acc.provider != L"openai") return;
    d->extraUsage = {};
    d->extraUsedAmount = -1;
    d->extraLimitAmount = -1;
    if (acc.creditsMax <= 0 || d->creditsUnlimited || d->creditsBalance < 0) return;
    d->extraUsage.pct =
        std::clamp(100.0 * (1.0 - d->creditsBalance / acc.creditsMax), 0.0, 100.0);
    d->extraUsedAmount = acc.creditsMax - d->creditsBalance;
    d->extraLimitAmount = acc.creditsMax;
}

// src/providers/antigravity.h
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

// src/history/journal.h
// Local usage-history journal and its recording consent. Takes no UI or auth locks.

struct HistoryFileHeader {
    uint64_t magic = 0x3154534948514941ULL;  // AIQHIST1
    uint32_t version = 1;
    uint32_t recordBytes = sizeof(HistorySample);
};
static constexpr size_t kHistoryMaxSamples = 250000;  // ~60 MB, independent of retention.
static constexpr ULONGLONG kHistoryTimestampLimitMs = 100000000000000ULL;
static constexpr ULONGLONG kHistoryMaxWindowMs = 3650 * kHistoryDayMs;
static constexpr ULONGLONG kHistoryMaintenanceIntervalMs = 3600000;
static std::deque<HistorySample> g_historySamples;
static std::wstring g_historyPath;
static std::wstring g_historyStatus;
static bool g_historyLoaded = false;
static bool g_historyWritable = true;
static bool g_historyDirty = false;
static bool g_historyCapped = false;
static ULONGLONG g_historyMaintenanceMs = 0;
static ULONGLONG g_historyWriteRetryMs = 0;
static winrt::file_handle g_historyOwnership;

static void SetHistoryStatus(std::wstring message) {
    g_historyStatus = std::move(message);
    bool error = !g_historyStatus.empty();
    if (g_historyStorageError.exchange(error) != error) NotifySettingsWindowChanged();
}

static bool IsHistoryOwnerProcess() {
    DWORD shellProcessId = 0;
    if (HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        GetWindowThreadProcessId(taskbar, &shellProcessId);
    }
    return shellProcessId && shellProcessId == GetCurrentProcessId();
}

static uint64_t HistoryChecksum(const HistorySample& sample) {
    uint64_t hash = 1469598103934665603ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&sample);
    for (size_t i = 0; i < offsetof(HistorySample, checksum); ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ULL;
    }
    return hash;
}

// Callers hold only g_historyMutex. An invalid format is preserved until explicit Clear.
static void LoadHistoryLocked(bool create = false) {
    if (g_historyLoaded) return;
    g_historyLoaded = true;
    g_historyWritable = true;
    SetHistoryStatus({});
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
        g_historyWritable = false;
        SetHistoryStatus(L"History storage is unavailable.");
        return;
    }
    std::wstring directory = localAppData;
    CoTaskMemFree(localAppData);
    // Windhawk's writable mod directory is shared across Windows users. Work-pattern
    // history belongs under the current user's profile, with its inherited private ACL.
    directory += L"\\TaskbarAIQuota";
    g_historyPath = directory + L"\\" WH_MOD_ID L"\\usage-history-v1.bin";
    if (!create && GetFileAttributesW(g_historyPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            g_historyLoaded = false;
            return;  // A read-only visit before opt-in must not create a directory or lock.
        }
    }
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        g_historyWritable = false;
        SetHistoryStatus(L"Cannot create the local history directory.");
        return;
    }
    directory += L"\\" WH_MOD_ID;
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        g_historyWritable = false;
        SetHistoryStatus(L"Cannot create the local history directory.");
        return;
    }
    g_historyPath = directory;
    g_historyPath += L"\\usage-history-v1.bin";
    // Separate Explorer processes, reloads, or sessions must never compact each other's
    // journal. The OS releases this exclusive ownership handle even after a shell crash.
    std::wstring lockPath = directory + L"\\history.lock";
    g_historyOwnership.attach(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!g_historyOwnership) {
        g_historyLoaded = false;  // Retry after an overlapping Explorer session exits.
        g_historyWritable = false;
        SetHistoryStatus(L"History storage is in use by another Explorer session, or is inaccessible.");
        return;
    }
    winrt::file_handle file{CreateFileW(g_historyPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            g_historyWritable = false;
            SetHistoryStatus(L"Cannot read history. Check storage permissions, then reopen the mod.");
        }
        return;
    }
    HistoryFileHeader header;
    LARGE_INTEGER size{};
    DWORD read = 0;
    if (!GetFileSizeEx(file.get(), &size)) {
        g_historyWritable = false;
        SetHistoryStatus(L"Cannot read the history file size.");
        return;
    }
    if (size.QuadPart == 0) {
        g_historyDirty = true;  // Crash between first file creation and header write.
        return;
    }
    if (size.QuadPart < (LONGLONG)sizeof(header) ||
        !ReadFile(file.get(), &header, sizeof(header), &read, nullptr) ||
        read != sizeof(header) || header.magic != HistoryFileHeader{}.magic ||
        header.version != 1 || header.recordBytes != sizeof(HistorySample)) {
        g_historyWritable = false;
        SetHistoryStatus(L"Unsupported or damaged history file. Clear history to start again.");
        return;
    }
    size_t count = (size_t)((size.QuadPart - sizeof(header)) / sizeof(HistorySample));
    // A crash may leave only a partial final record. Retain complete checksummed samples.
    g_historyDirty = (size.QuadPart - sizeof(header)) % sizeof(HistorySample) != 0;
    if (count > kHistoryMaxSamples) {
        LARGE_INTEGER offset;
        offset.QuadPart = sizeof(header) + (count - kHistoryMaxSamples) * sizeof(HistorySample);
        if (!SetFilePointerEx(file.get(), offset, nullptr, FILE_BEGIN)) {
            g_historyWritable = false;
            SetHistoryStatus(L"Cannot seek in history file.");
            return;
        }
        count = kHistoryMaxSamples;
        g_historyDirty = g_historyCapped = true;
    }
    std::array<HistorySample, 256> batch;
    while (count && !g_unloading) {
        size_t n = std::min(count, batch.size());
        DWORD bytes = (DWORD)(n * sizeof(HistorySample));
        if (!ReadFile(file.get(), batch.data(), bytes, &read, nullptr) || read != bytes) {
            g_historyWritable = false;
            SetHistoryStatus(L"History read failed. Loaded samples remain available for export.");
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            const auto& sample = batch[i];
            bool valid = sample.checksum == HistoryChecksum(sample) && sample.accountId &&
                         sample.timeMs && sample.timeMs < kHistoryTimestampLimitMs &&
                         sample.intervalMs > 0 && sample.intervalMs <= kHistoryDayMs &&
                         sample.provider >= 1 && sample.provider <= 3 && sample.failed <= 1;
            for (const auto& meter : sample.meters) {
                valid = valid && std::isfinite(meter.value) && meter.value >= -1 &&
                        meter.value <= 1e12 && meter.resetMs < kHistoryTimestampLimitMs &&
                        meter.durationMs <= kHistoryMaxWindowMs;
            }
            if (!valid) {
                // Stop at corruption rather than connecting graphs across unknown records.
                // A lone torn last record is recoverable; interior corruption needs consent.
                if (count == n && i + 1 == n) {
                    g_historyDirty = true;
                    return;
                }
                g_historyWritable = false;
                SetHistoryStatus(L"Damaged history record. Export readable samples or clear history.");
                return;
            }
            g_historySamples.push_back(sample);
        }
        count -= n;
    }
}

static bool RewriteHistoryLocked(const std::deque<HistorySample>& samples) {
    if (g_historyPath.empty() || !g_historyOwnership) return false;
    std::wstring temporary = g_historyPath + L".tmp";
    winrt::file_handle file{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) {
        SetHistoryStatus(L"Cannot write history. Check disk space and storage permissions.");
        g_historyWriteRetryMs = NowUnixMs() + 3600000;
        return false;
    }
    HistoryFileHeader header;
    DWORD written = 0;
    bool ok = WriteFile(file.get(), &header, sizeof(header), &written, nullptr) &&
              written == sizeof(header);
    std::array<HistorySample, 256> batch;
    auto it = samples.begin();
    while (ok && it != samples.end()) {
        size_t n = 0;
        while (n < batch.size() && it != samples.end()) batch[n++] = *it++;
        DWORD bytes = (DWORD)(n * sizeof(HistorySample));
        ok = WriteFile(file.get(), batch.data(), bytes, &written, nullptr) && written == bytes;
    }
    ok = ok && FlushFileBuffers(file.get());
    file.close();
    if (ok) {
        ok = MoveFileExW(temporary.c_str(), g_historyPath.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (!ok) {
        DeleteFileW(temporary.c_str());
        SetHistoryStatus(L"History write failed; the previous journal was kept.");
        g_historyWriteRetryMs = NowUnixMs() + 3600000;
        return false;
    }
    g_historyDirty = false;
    g_historyWriteRetryMs = 0;
    SetHistoryStatus({});
    return true;
}

static ULONGLONG MaintainHistory(int retentionDays, bool create = false) {
    try {
        std::lock_guard lock(g_historyMutex);
        LoadHistoryLocked(create);
        ULONGLONG now = NowUnixMs();
        // Retention edits use the regular hourly cadence too: autosave must not prune
        // immediately while someone is still adjusting a numeric field.
        if (g_historyMaintenanceMs && g_historyMaintenanceMs <= now &&
            now - g_historyMaintenanceMs < kHistoryMaintenanceIntervalMs) {
            return g_historyMaintenanceMs + kHistoryMaintenanceIntervalMs;
        }
        g_historyMaintenanceMs = now;
        ULONGLONG age = (ULONGLONG)std::clamp(retentionDays, 1, 3650) * kHistoryDayMs;
        ULONGLONG cutoff = now > age ? now - age : 0;
        auto previousSize = g_historySamples.size();
        std::erase_if(g_historySamples, [cutoff](const auto& sample) {
            return sample.timeMs < cutoff;
        });
        if (g_historySamples.size() != previousSize) g_historyDirty = true;
        if (g_historyDirty && g_historyWritable && !g_unloading && now >= g_historyWriteRetryMs) {
            RewriteHistoryLocked(g_historySamples);
        }
        return now + kHistoryMaintenanceIntervalMs;
    } catch (...) {
        Wh_Log(L"History maintenance failed");
        return NowUnixMs() + kHistoryMaintenanceIntervalMs;
    }
}

static void AppendHistory(const std::vector<HistorySample>& samples,
                          uint64_t epoch, ULONGLONG generation, int retentionDays) {
    if (samples.empty() || g_unloading) return;
    try {
        std::lock_guard lock(g_historyMutex);
        // This is the admission point. A later settings change doesn't retract samples
        // already admitted; Clear holds the same lock and invalidates pre-clear fetches.
        if (epoch != g_historyEpoch.load() || generation != g_historySettingsGeneration.load()) return;
        LoadHistoryLocked(true);
        if (!g_historyWritable || g_historyPath.empty()) return;
        ULONGLONG now = NowUnixMs();
        ULONGLONG age = (ULONGLONG)std::clamp(retentionDays, 1, 3650) * kHistoryDayMs;
        std::vector<HistorySample> admitted;
        for (auto sample : samples) {
            if ((now > age && sample.timeMs < now - age) ||
                (!g_historySamples.empty() && sample.timeMs < g_historySamples.back().timeMs)) {
                continue;  // Never turn a backwards system-clock adjustment into negative time.
            }
            if (sample.failed) {
                auto previous = std::find_if(g_historySamples.rbegin(), g_historySamples.rend(),
                    [&](const HistorySample& entry) { return entry.accountId == sample.accountId; });
                // One marker breaks continuity for an entire outage; failed retries add
                // no measurements and must not fill the journal with an unchanged error.
                if (previous != g_historySamples.rend() && previous->failed &&
                    previous->session == sample.session) continue;
            }
            for (auto& meter : sample.meters) {
                if (!std::isfinite(meter.value) || meter.value < 0 || meter.value > 1e12) {
                    meter.value = -1;
                }
                // Persist only metadata the reader accepts; bad provider fields mean
                // unknown timing, not a checksummed record that poisons the journal.
                if (meter.resetMs >= kHistoryTimestampLimitMs) meter.resetMs = 0;
                if (meter.durationMs > kHistoryMaxWindowMs) meter.durationMs = 0;
            }
            sample.checksum = HistoryChecksum(sample);
            g_historySamples.push_back(sample);
            admitted.push_back(sample);
        }
        if (admitted.empty()) return;
        if (g_historySamples.size() > kHistoryMaxSamples) {
            // Amortize compaction instead of rewriting a full journal every new sample.
            while (g_historySamples.size() > kHistoryMaxSamples * 9 / 10) {
                g_historySamples.pop_front();
            }
            g_historyDirty = g_historyCapped = true;
        }
        if (g_historyDirty) {
            if (now >= g_historyWriteRetryMs) RewriteHistoryLocked(g_historySamples);
            return;
        }
        winrt::file_handle file{CreateFileW(g_historyPath.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        LARGE_INTEGER size{};
        DWORD written = 0;
        bool ok = file && GetFileSizeEx(file.get(), &size);
        if (ok && size.QuadPart == 0) {
            HistoryFileHeader header;
            ok = WriteFile(file.get(), &header, sizeof(header), &written, nullptr) &&
                 written == sizeof(header);
        }
        DWORD bytes = (DWORD)(admitted.size() * sizeof(HistorySample));
        if (ok) ok = WriteFile(file.get(), admitted.data(), bytes, &written, nullptr) && written == bytes;
        if (!ok) {
            // Retain admitted samples in memory; next append repairs a possible torn tail.
            g_historyDirty = true;
            g_historyWriteRetryMs = now + 3600000;
            SetHistoryStatus(L"History is not being saved to disk. Check disk space and permissions.");
        } else {
            SetHistoryStatus({});
        }
    } catch (...) {
        Wh_Log(L"History append failed");
    }
}

static HistorySnapshot ReadHistorySnapshot(ULONGLONG fromMs) {
    std::lock_guard lock(g_historyMutex);
    LoadHistoryLocked();
    HistorySnapshot snapshot;
    snapshot.status = g_historyStatus;
    if (g_historyCapped) snapshot.status += L" Storage cap reached; oldest samples were pruned.";
    for (const auto& sample : g_historySamples) {
        if (sample.timeMs >= fromMs) snapshot.samples.push_back(sample);
    }
    return snapshot;
}

static bool ClearHistory(std::wstring* error) {
    std::lock_guard lock(g_historyMutex);
    LoadHistoryLocked();
    ++g_historyEpoch;
    if (!g_historyLoaded && g_historyWritable && !g_historyOwnership && !g_historyPath.empty()) {
        g_historySamples.clear();
        return true;  // Nothing has ever been recorded; Clear is an idempotent no-op.
    }
    if (!RewriteHistoryLocked({})) {
        if (error) *error = g_historyStatus.empty() ? L"History storage is unavailable." : g_historyStatus;
        return false;
    }
    g_historySamples.clear();
    g_historyWritable = true;
    g_historyCapped = false;
    return true;
}

static constexpr PCWSTR kHistoryConsentRegistryKey =
    L"Software\\Windhawk\\TaskbarAIQuota\\" WH_MOD_ID;

// Consent is per Windows user; Windhawk's settings_v1 storage is machine-shared.
static bool ReadHistoryConsent() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kHistoryConsentRegistryKey, L"RecordHistory",
                        RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           size == sizeof(value) && value == 1;
}

static bool WriteHistoryConsent(bool enabled) {
    HKEY key = nullptr;
    LSTATUS result = enabled ?
        RegCreateKeyExW(HKEY_CURRENT_USER, kHistoryConsentRegistryKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) :
        RegOpenKeyExW(HKEY_CURRENT_USER, kHistoryConsentRegistryKey, 0, KEY_SET_VALUE, &key);
    // Disabling an absent entry must not create a registry key.
    if (!enabled && (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)) return true;
    if (result != ERROR_SUCCESS) return false;
    DWORD value = enabled ? 1 : 0;
    result = RegSetValueExW(key, L"RecordHistory", 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

// src/fetch/notifications.h
// Fetch-thread tray icon and threshold notifications.

// Fetch-thread-owned: hidden message-only window that owns the mod's tray icon.
static HWND g_notifyWnd = nullptr;

static const UINT kNotifyIconId = 1;
static PCWSTR kNotifyClassName = L"AiQuotaNotify_" WH_MOD_ID;

// Fetch-thread only. Drops the tray icon, window, and class created on demand.
static void RemoveNotifyIcon() {
    if (!g_notifyWnd) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_notifyWnd;
    nid.uID = kNotifyIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(g_notifyWnd);
    g_notifyWnd = nullptr;
    UnregisterClassW(kNotifyClassName, GetModuleHandleW(nullptr));
}

// Fetch-thread only. Lazily creates a hidden message-only window owning one tray
// icon, then shows a balloon (rendered as a toast on Win11, kept in notify center).
static void FireThresholdNotification(const std::wstring& title, const std::wstring& body) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    auto addNotifyIcon = [](HWND hWnd) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hWnd;
        nid.uID = kNotifyIconId;
        nid.uFlags = NIF_ICON | NIF_STATE;
        nid.dwState = NIS_HIDDEN;
        nid.dwStateMask = NIS_HIDDEN;
        nid.hIcon = LoadIconW(nullptr, IDI_WARNING);
        return Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    };
    if (!g_notifyWnd) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = hInst;
        wc.lpszClassName = kNotifyClassName;
        RegisterClassExW(&wc);  // ERROR_CLASS_ALREADY_EXISTS is fine; CreateWindow reuses it.

        g_notifyWnd = CreateWindowExW(0, kNotifyClassName, L"", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, hInst, nullptr);
        if (!g_notifyWnd) {
            Wh_Log(L"Notify window creation failed: %lu", GetLastError());
            return;
        }

        if (!addNotifyIcon(g_notifyWnd)) {
            Wh_Log(L"Shell_NotifyIcon NIM_ADD failed");
            DestroyWindow(g_notifyWnd);
            g_notifyWnd = nullptr;
            return;
        }
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_notifyWnd;
    nid.uID = kNotifyIconId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, body.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        // Explorer can recreate the notification area without restarting this process.
        addNotifyIcon(g_notifyWnd);
        if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
            Wh_Log(L"Shell_NotifyIcon NIM_MODIFY failed");
        }
    }
}

// src/fetch/fetch-thread.h
// Per-account usage fetch and the polling loop that publishes results and history.

static void FetchAccount(const AccountConfig& acc, AccountData* d, int* retryAfterSec) {
    d->error.clear();
    d->retryDeadlineMs = 0;
    d->needsLogin = false;

    // Antigravity uses local language server discovery, not OAuth.
    if (acc.provider == L"antigravity") {
        FetchAntigravityAccount(d);
        return;
    }

    uint64_t idHash = AccountIdentityHash(acc);
    ULONGLONG authEpoch = CurrentAuthEpoch(idHash);
    StoredToken tok;
    if (!LoadStoredToken(idHash, &tok) || tok.accessToken.empty()) {
        d->stale = true;
        d->needsLogin = true;
        d->error = L"not signed in - click to sign in";
        return;
    }

    // Refresh just before expiry so a request rarely races the token going stale.
    if (tok.expiresMs && tok.expiresMs < NowUnixMs() + 60000) {
        std::wstring refreshErr;
        int refreshRetryAfter = 0;
        TokenEndpointResult refreshResult =
            RefreshToken(acc.provider, &tok, &refreshErr, &refreshRetryAfter);
        if (refreshResult == TokenEndpointResult::Success) {
            uint64_t savedIdentity = 0;
            TokenSaveResult saved =
                SaveStoredTokenIfCurrent(idHash, authEpoch, tok, &savedIdentity);
            if (saved == TokenSaveResult::Stale) {
                d->stale = true;
                d->needsLogin = true;
                d->error = L"not signed in - click to sign in";
                return;
            }
            if (saved != TokenSaveResult::Saved) {
                d->stale = true;
                d->error = L"could not save refreshed token";
                return;
            }
            if (savedIdentity != idHash) RefreshQuotaByIdentity(savedIdentity);
        } else {
            d->stale = true;
            if (refreshResult == TokenEndpointResult::Rejected) {
                d->needsLogin = true;
                d->error = L"session expired - click to sign in";
            } else {
                d->error = L"token refresh failed";
                if (!refreshErr.empty()) d->error += L": " + refreshErr;
                *retryAfterSec = refreshRetryAfter > 0 ? refreshRetryAfter : 120;
            }
            return;
        }
    }

    auto requestUsage = [&](const StoredToken& t) -> HttpResult {
        if (acc.provider == L"anthropic") {
            std::wstring headers = L"Authorization: Bearer " + t.accessToken +
                                   L"\r\nanthropic-beta: oauth-2025-04-20"
                                   L"\r\nAccept: application/json\r\n";
            return HttpRequest(L"GET", L"api.anthropic.com", L"/api/oauth/usage",
                               L"claude-code/2.1.0", headers);
        }
        std::wstring headers = L"Authorization: Bearer " + t.accessToken +
                               L"\r\nOrigin: https://chatgpt.com"
                               L"\r\nReferer: https://chatgpt.com/"
                               L"\r\nAccept: application/json\r\n";
        if (!t.accountId.empty()) headers += L"ChatGPT-Account-Id: " + t.accountId + L"\r\n";
        return HttpRequest(L"GET", L"chatgpt.com", L"/backend-api/wham/usage",
                           L"taskbar-ai-quota/0.1", headers);
    };

    HttpResult r = requestUsage(tok);
    // Reactive refresh: the access token may have been revoked or expired early.
    if (r.ok && r.status == 401 && !tok.refreshToken.empty()) {
        std::wstring refreshErr;
        int refreshRetryAfter = 0;
        TokenEndpointResult refreshResult =
            RefreshToken(acc.provider, &tok, &refreshErr, &refreshRetryAfter);
        if (refreshResult == TokenEndpointResult::Success) {
            uint64_t savedIdentity = 0;
            TokenSaveResult saved =
                SaveStoredTokenIfCurrent(idHash, authEpoch, tok, &savedIdentity);
            if (saved == TokenSaveResult::Saved) {
                if (savedIdentity != idHash) RefreshQuotaByIdentity(savedIdentity);
                r = requestUsage(tok);
            } else if (saved == TokenSaveResult::Stale) {
                d->stale = true;
                d->needsLogin = true;
                d->error = L"not signed in - click to sign in";
                return;
            } else {
                d->stale = true;
                d->error = L"could not save refreshed token";
                return;
            }
        } else if (refreshResult == TokenEndpointResult::TransientFailure) {
            d->stale = true;
            d->error = L"token refresh failed";
            if (!refreshErr.empty()) d->error += L": " + refreshErr;
            *retryAfterSec = refreshRetryAfter > 0 ? refreshRetryAfter : 120;
            return;
        }
    }

    if (!r.ok) {
        d->stale = true;
        d->error = L"network error";
        return;
    }
    if (r.status == 401) {
        d->stale = true;
        d->needsLogin = true;
        d->error = L"unauthorized - click to sign in";
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

    ApplyCreditsMax(acc, &fresh);
    fresh.stale = false;
    fresh.lastSuccessMs = NowUnixMs();
    *d = std::move(fresh);
}

static DWORD WINAPI FetchThreadProc(LPVOID) {
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartmentInitialized = true;
    } catch (...) {}

    std::vector<std::wstring> lastLoggedErrorStates;
    std::vector<uint64_t> retryIdentityHashes;
    std::vector<ULONGLONG> retryDeadlineMs;
    std::vector<ULONGLONG> nextPollDeadlineMs;
    // Per-account red-crossing arm state, indexed by QuotaBarIndex:
    // -1 unknown (primes without firing), 0 below/armed, 1 above/already notified.
    std::vector<std::array<int, kQuotaBarCount>> redState;
    ULONGLONG lastLoggedSettingsGeneration = 0;
    const uint64_t historySession = NowUnixMs();
    while (!g_unloading) {
        const bool historyOwner = IsHistoryOwnerProcess();
        if (historyOwner && g_historyIdsPendingSave) {
            // Init can run before the shell creates its taskbar. Persist the exact IDs
            // under the normal edit lock before any sample uses them, without history IO.
            std::lock_guard configLock(g_configEditMutex);
            Settings pending;
            {
                std::lock_guard settingsLock(g_settingsMutex);
                pending = g_settings;
            }
            if (SaveOwnedSettings(pending)) {
                g_historyIdsPendingSave = false;
                NotifySettingsWindowChanged();
            }
        }
        ULONGLONG refreshGeneration;
        uint64_t refreshAccountIdentity;
        bool refreshRequested;
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            refreshGeneration = g_refreshGeneration.load();
            refreshAccountIdentity = g_refreshAccountIdentity.load();
            refreshRequested = g_refreshing.load();
        }
        std::vector<AccountConfig> accounts;
        int intervalMin, redThreshold;
        bool enableNotifications, historyEnabled, skipHiddenPolling;
        int historyRetentionDays;
        ULONGLONG settingsGeneration, historyContinuityGeneration;
        const uint64_t historyEpoch = g_historyEpoch.load();
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            accounts = g_settings.accounts;
            intervalMin = g_settings.pollMinutes;
            redThreshold = g_settings.redThreshold;
            enableNotifications = g_settings.enableNotifications;
            historyEnabled = g_settings.historyEnabled && historyOwner && !g_historyIdsPendingSave;
            historyRetentionDays = g_settings.historyRetentionDays;
            skipHiddenPolling = g_settings.skipHiddenPolling;
            settingsGeneration = g_settingsGeneration;
            historyContinuityGeneration = g_historyContinuityGeneration;
        }
        ULONGLONG nextHistoryMaintenanceMs = historyOwner && historyEnabled
            ? MaintainHistory(historyRetentionDays, true) : 0;
        bool settingsChanged = lastLoggedSettingsGeneration != settingsGeneration ||
                               lastLoggedErrorStates.size() != accounts.size();
        if (settingsChanged) {
            lastLoggedErrorStates.assign(accounts.size(), {});

            // Remap provider-directed Retry-After and normal poll deadlines by stable account
            // identity so settings changes don't cause another request for unchanged accounts.
            auto oldIdentityHashes = std::move(retryIdentityHashes);
            auto oldRetryDeadlineMs = std::move(retryDeadlineMs);
            auto oldNextPollDeadlineMs = std::move(nextPollDeadlineMs);
            std::vector<bool> oldDeadlineUsed(oldIdentityHashes.size(), false);
            retryIdentityHashes.resize(accounts.size());
            retryDeadlineMs.assign(accounts.size(), 0);
            nextPollDeadlineMs.assign(accounts.size(), 0);
            for (size_t i = 0; i < accounts.size(); ++i) {
                retryIdentityHashes[i] = AccountIdentityHash(accounts[i]);
                for (size_t j = 0; j < oldIdentityHashes.size() &&
                                    j < oldRetryDeadlineMs.size(); ++j) {
                    if (!oldDeadlineUsed[j] &&
                        oldIdentityHashes[j] == retryIdentityHashes[i]) {
                        retryDeadlineMs[i] = oldRetryDeadlineMs[j];
                        if (j < oldNextPollDeadlineMs.size()) {
                            nextPollDeadlineMs[i] = oldNextPollDeadlineMs[j];
                            ULONGLONG latestDeadline = NowUnixMs() +
                                (accounts[i].provider == L"antigravity" ? 1ULL :
                                 (ULONGLONG)intervalMin) * 60000;
                            if (nextPollDeadlineMs[i] > latestDeadline) {
                                nextPollDeadlineMs[i] = latestDeadline;
                            }
                        }
                        oldDeadlineUsed[j] = true;
                        break;
                    }
                }
            }
            lastLoggedSettingsGeneration = settingsGeneration;
        }

        std::vector<AccountData> results(accounts.size());
        {
            std::lock_guard<std::mutex> lk(g_dataMutex);
            if (g_data.size() == results.size()) results = g_data;
        }
        if (settingsChanged || redState.size() != accounts.size()) {
            redState.assign(accounts.size(),
                            std::array<int, kQuotaBarCount>{-1, -1, -1, -1});
            for (size_t i = 0; i < accounts.size(); i++) {
                const std::array<const WindowUsage*, kQuotaBarCount> usage = {
                    &results[i].win5h, &results[i].winWeek, &results[i].fableWeek,
                    &results[i].extraUsage};
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (usage[w]->pct >= 0) {
                        redState[i][w] = usage[w]->pct >= redThreshold ? 1 : 0;
                    }
                }
            }
        }

        int refreshAccountIndex = -1;
        if (refreshRequested && refreshAccountIdentity) {
            for (size_t i = 0; i < accounts.size(); i++) {
                if (AccountIdentityHash(accounts[i]) == refreshAccountIdentity) {
                    refreshAccountIndex = (int)i;
                    break;
                }
            }
        }
        bool manualRefresh = refreshRequested &&
                             (!refreshAccountIdentity || refreshAccountIndex >= 0);
        bool refreshSingleAccount = manualRefresh && refreshAccountIndex >= 0 &&
                                    refreshAccountIndex < (int)accounts.size();
        std::vector<bool> fetchedOk(accounts.size(), false);
        std::vector<HistorySample> historyBatch;
        ULONGLONG nextRetryMs = 0;
        ULONGLONG nextPollMs = 0;
        bool anyError = false;
        for (size_t i = 0; i < accounts.size() && !g_unloading; i++) {
            ULONGLONG nowMs = NowUnixMs();
            // Hiding still stops polling by default. The explicit history exception never
            // polls an unrecorded hidden account, including during Refresh all.
            if (accounts[i].hidden && (skipHiddenPolling || !historyEnabled ||
                                        !accounts[i].recordHistory)) continue;
            if (refreshSingleAccount && (int)i != refreshAccountIndex) {
                if (retryDeadlineMs[i] > nowMs &&
                    (nextRetryMs == 0 || retryDeadlineMs[i] < nextRetryMs)) {
                    nextRetryMs = retryDeadlineMs[i];
                } else if (nextPollDeadlineMs[i] > nowMs &&
                           (nextPollMs == 0 || nextPollDeadlineMs[i] < nextPollMs)) {
                    nextPollMs = nextPollDeadlineMs[i];
                } else if (nextPollMs == 0 || nowMs < nextPollMs) {
                    nextPollMs = nowMs;
                }
                continue;
            }

            if (retryDeadlineMs[i] > nowMs) {
                if (nextRetryMs == 0 || retryDeadlineMs[i] < nextRetryMs) {
                    nextRetryMs = retryDeadlineMs[i];
                }
                continue;
            }
            if (!manualRefresh && nextPollDeadlineMs[i] > nowMs) {
                if (nextPollMs == 0 || nextPollDeadlineMs[i] < nextPollMs) {
                    nextPollMs = nextPollDeadlineMs[i];
                }
                continue;
            }

            int retryAfter = 0;
            FetchAccount(accounts[i], &results[i], &retryAfter);
            if (historyEnabled && accounts[i].recordHistory && accounts[i].historyId) {
                HistorySample sample;
                sample.accountId = accounts[i].historyId;
                sample.timeMs = NowUnixMs();
                sample.intervalMs = (accounts[i].provider == L"antigravity" ? 1ULL :
                                     (ULONGLONG)intervalMin) * 60000;
                // A settings pause/resume or poll-rate change must not bridge an interval
                // where recording was disabled, even if it was shorter than two polls.
                sample.session = historySession ^ (historyContinuityGeneration * 0x9e3779b97f4a7c15ULL);
                sample.provider = accounts[i].provider == L"anthropic" ? 1 :
                                  accounts[i].provider == L"openai" ? 2 : 3;
                sample.failed = !results[i].error.empty() || results[i].stale ||
                                results[i].needsLogin;
                if (!sample.failed) {
                    const std::array<const WindowUsage*, 6> windows = {
                        &results[i].win5h, &results[i].winWeek, &results[i].fableWeek,
                        &results[i].extraUsage, &results[i].antigravityThirdParty5h,
                        &results[i].antigravityThirdPartyWeek};
                    for (size_t m = 0; m < windows.size(); ++m) {
                        // OpenAI's user-chosen credit ceiling is not a historical quota.
                        if (m == 3 && sample.provider != 1) continue;
                        sample.meters[m] = {windows[m]->pct, windows[m]->resetUnixMs,
                                            windows[m]->windowDurationMs};
                    }
                    if (sample.provider == 1) {
                        sample.meters[6] = {results[i].extraUsedAmount,
                                           results[i].extraUsage.resetUnixMs, 0};
                    } else if (sample.provider == 2 && !results[i].creditsUnlimited) {
                        sample.meters[7].value = results[i].creditsBalance;
                    }
                }
                historyBatch.push_back(sample);
            }
            if (retryAfter > 0) {
                nextPollDeadlineMs[i] = 0;
                retryDeadlineMs[i] = NowUnixMs() + (ULONGLONG)retryAfter * 1000;
                results[i].retryDeadlineMs = retryDeadlineMs[i];
                if (nextRetryMs == 0 || retryDeadlineMs[i] < nextRetryMs) {
                    nextRetryMs = retryDeadlineMs[i];
                }
            } else {
                retryDeadlineMs[i] = 0;
                results[i].retryDeadlineMs = 0;
                ULONGLONG pollDelayMs = accounts[i].provider == L"antigravity"
                                            ? 60000
                                            : (ULONGLONG)intervalMin * 60000;
                if (!results[i].error.empty() && !results[i].needsLogin &&
                    accounts[i].provider != L"antigravity") {
                    pollDelayMs = std::min<ULONGLONG>(pollDelayMs, 120000);
                }
                nextPollDeadlineMs[i] = NowUnixMs() + pollDelayMs;
                if (nextPollMs == 0 || nextPollDeadlineMs[i] < nextPollMs) {
                    nextPollMs = nextPollDeadlineMs[i];
                }
            }

            if (!results[i].error.empty()) {
                if (retryAfter <= 0 && !results[i].needsLogin &&
                    accounts[i].provider != L"antigravity") {
                    anyError = true;
                }
                std::wstring errorState = accounts[i].provider + L"\n" + accounts[i].label +
                                          L"\n" + results[i].error;
                if (errorState != lastLoggedErrorStates[i]) {
                    Wh_Log(L"Fetch [%d] %s (%s): %s", (int)i, accounts[i].label.c_str(),
                           accounts[i].provider.c_str(), results[i].error.c_str());
                    lastLoggedErrorStates[i] = std::move(errorState);
                }
            } else {
                lastLoggedErrorStates[i].clear();
                fetchedOk[i] = true;
            }
        }
        bool published = false;
        bool generationRaced = false;
        std::vector<AccountConfig> currentAccounts;
        std::vector<AccountData> previousCurrentResults;
        std::vector<AccountData> remappedResults;
        std::vector<bool> remappedFetchedOk;
        int notificationRedThreshold = redThreshold;
        bool notificationsEnabled = enableNotifications;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            if (settingsGeneration == g_settingsGeneration) {
                std::lock_guard<std::mutex> lk2(g_dataMutex);
                if (g_data.size() == results.size()) {
                    g_data = results;
                    published = true;
                }
            } else {
                generationRaced = true;
                currentAccounts = g_settings.accounts;
                notificationRedThreshold = g_settings.redThreshold;
                notificationsEnabled = g_settings.enableNotifications;

                std::lock_guard<std::mutex> lk2(g_dataMutex);
                previousCurrentResults.resize(currentAccounts.size());
                for (size_t i = 0; i < currentAccounts.size() && i < g_data.size(); i++) {
                    previousCurrentResults[i] = g_data[i];
                }
                remappedResults = previousCurrentResults;
                remappedFetchedOk.assign(currentAccounts.size(), false);
                std::vector<bool> oldResultUsed(results.size(), false);
                for (size_t i = 0; i < currentAccounts.size(); i++) {
                    for (size_t j = 0; j < accounts.size() && j < results.size(); j++) {
                        if (!oldResultUsed[j] &&
                            AccountIdentityHash(accounts[j]) ==
                                AccountIdentityHash(currentAccounts[i])) {
                            remappedResults[i] = results[j];
                            // The fetch used the old settings; a changed credits max must
                            // not publish a stale percentage.
                            ApplyCreditsMax(currentAccounts[i], &remappedResults[i]);
                            remappedFetchedOk[i] = fetchedOk[j];
                            oldResultUsed[j] = true;
                            break;
                        }
                    }
                }
                g_data = remappedResults;
                published = true;
            }
        }
        // Admission is fenced against settings edits and Clear history. Never store a
        // carried-forward result or let a settings refresh manufacture a sample.
        AppendHistory(historyBatch, historyEpoch, settingsGeneration, historyRetentionDays);
        if (!notificationsEnabled && g_notifyWnd) RemoveNotifyIcon();
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            if (refreshGeneration == g_refreshGeneration.load()) {
                g_refreshing = false;
                g_refreshAccountIdentity = 0;
            }
        }
        if (published) {
            // Fire one toast per upward crossing of the red threshold; re-arm when
            // usage drops back below. A generation-raced result uses current settings and
            // current pre-publication data so settings edits alone cannot cause a toast.
            const auto& publishedAccounts = generationRaced ? currentAccounts : accounts;
            const auto& publishedResults = generationRaced ? remappedResults : results;
            const auto& publishedFetchedOk = generationRaced ? remappedFetchedOk : fetchedOk;
            for (size_t i = 0; i < publishedAccounts.size(); i++) {
                if (!publishedFetchedOk[i] || publishedAccounts[i].hidden) continue;
                const std::array<const WindowUsage*, kQuotaBarCount> usage = {
                    &publishedResults[i].win5h, &publishedResults[i].winWeek,
                    &publishedResults[i].fableWeek, &publishedResults[i].extraUsage};
                const std::array<const WindowUsage*, kQuotaBarCount> previousUsage = {
                    generationRaced ? &previousCurrentResults[i].win5h : nullptr,
                    generationRaced ? &previousCurrentResults[i].winWeek : nullptr,
                    generationRaced ? &previousCurrentResults[i].fableWeek : nullptr,
                    generationRaced ? &previousCurrentResults[i].extraUsage : nullptr};
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (!publishedAccounts[i].showBars[w]) continue;
                    const WindowUsage& wu = *usage[w];
                    if (wu.pct < 0) continue;
                    bool shouldNotify = false;
                    if (generationRaced) {
                        const WindowUsage& previous = *previousUsage[w];
                        shouldNotify = previous.pct >= 0 &&
                                       previous.pct < notificationRedThreshold &&
                                       wu.pct >= notificationRedThreshold &&
                                       notificationsEnabled;
                    } else {
                        int& st = redState[i][w];
                        if (wu.pct >= notificationRedThreshold) {
                            shouldNotify = st == 0 && notificationsEnabled;
                            st = 1;
                        } else {
                            st = 0;
                        }
                    }
                    if (shouldNotify) {
                        std::wstring providerName =
                            ProviderDisplayName(publishedAccounts[i].provider);
                        wchar_t title[96];
                        PCWSTR quotaName =
                            w == kFiveHourBar ? L"5h" :
                            w == kWeeklyBar ? L"weekly" :
                            w == kFableWeeklyBar ? L"Fable weekly" : L"monthly extra";
                        if (publishedAccounts[i].provider == L"antigravity") {
                            if (w == kFiveHourBar) {
                                ULONGLONG duration = publishedResults[i].win5h.windowDurationMs;
                                quotaName = duration == 3ULL * 60 * 60 * 1000 ? L"Gemini 3h" :
                                            duration == 60ULL * 60 * 1000 ? L"Gemini 1h" :
                                            duration == 0 ? L"Gemini current limit" :
                                                            L"Gemini 5h";
                            } else if (w == kWeeklyBar) {
                                quotaName = L"Gemini weekly";
                            }
                        } else if (publishedAccounts[i].provider == L"openai" &&
                                   w == kExtraUsageBar) {
                            quotaName = L"credits";
                        }
                        swprintf(title, ARRAYSIZE(title), L"%s usage at %.0f%%",
                                 quotaName, wu.pct);
                        std::wstring body = providerName;
                        if ((w != kExtraUsageBar && w != kFableWeeklyBar) || wu.resetUnixMs) {
                            body += L" - resets " + FormatReset(wu.resetUnixMs);
                        }
                        FireThresholdNotification(title, body);
                    }
                }
            }
            PostUiUpdate();
        }

        DWORD waitMs = (DWORD)intervalMin * 60000;
        if (anyError) waitMs = std::min(waitMs, (DWORD)120000);
        ULONGLONG nowMs = NowUnixMs();
        if (nextRetryMs > nowMs) {
            waitMs = (DWORD)std::min<ULONGLONG>(waitMs, nextRetryMs - nowMs);
        }
        if (nextPollMs <= nowMs && nextPollMs != 0) {
            waitMs = 0;
        } else if (nextPollMs > nowMs) {
            waitMs = (DWORD)std::min<ULONGLONG>(waitMs, nextPollMs - nowMs);
        }
        // Local retention must wake independently of slow provider polling. Existing
        // per-account deadlines still prevent this wake from issuing early requests.
        if (nextHistoryMaintenanceMs) {
            waitMs = (DWORD)std::min<ULONGLONG>(waitMs,
                nextHistoryMaintenanceMs > nowMs ? nextHistoryMaintenanceMs - nowMs : 0);
        }

        HANDLE handles[2] = {g_stopEvent, g_refreshEvent};
        if (WaitForMultipleObjects(2, handles, FALSE, waitMs) == WAIT_OBJECT_0) break;
    }
    RemoveNotifyIcon();
    if (apartmentInitialized) winrt::uninit_apartment();
    return 0;
}

// src/taskbar/marshal.h
// Taskbar window discovery, UI instance lookup, and marshaling onto taskbar UI threads.

static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param, DWORD timeoutMs = 2000);
static std::vector<HWND> FindCurrentProcessTaskbarWnds();
static QuotaUiInstance* FindUiState(HWND hWnd);

static void PostUiUpdate() {
    // Never resolve XAML refs on the fetch thread; marshal first.
    if (g_unloading || !g_uiInjected.load(std::memory_order_acquire)) return;

    for (HWND hWnd : FindCurrentProcessTaskbarWnds()) {
        RunFromWindowThread(hWnd, [](void* param) -> bool {
            HWND hWnd = static_cast<HWND>(param);
            auto* state = FindUiState(hWnd);
            if (!g_unloading && state && state->quotaGrid) UpdateQuotaUi(*state);
            return true;
        }, hWnd);
    }
}

static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param, DWORD timeoutMs) {
    static const UINT kMsg = RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
    struct Payload {
        WindowThreadProc proc;
        void* param;
        std::atomic<bool> ran{false};
        std::atomic<bool> result{false};

        Payload(WindowThreadProc proc, void* param) : proc(proc), param(param) {}
    };
    using PayloadRef = std::shared_ptr<Payload>;
    static std::mutex pendingPayloadsMutex;
    static std::vector<std::pair<UINT_PTR, PayloadRef>> pendingPayloads;
    static std::atomic<UINT_PTR> nextPayloadId{1};

    DWORD tid = GetWindowThreadProcessId(hWnd, nullptr);
    if (!tid) return false;
    if (tid == GetCurrentThreadId()) {
        return proc(param);
    }

    PayloadRef pay = std::make_shared<Payload>(proc, param);
    UINT_PTR payloadId;
    do {
        payloadId = nextPayloadId.fetch_add(1, std::memory_order_relaxed);
    } while (!payloadId);
    {
        std::lock_guard<std::mutex> lk(pendingPayloadsMutex);
        pendingPayloads.push_back({payloadId, pay});
    }

    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM w, LPARAM l) CALLBACK -> LRESULT {
            PayloadRef payload;
            if (code == HC_ACTION) {
                auto* cwp = reinterpret_cast<const CWPSTRUCT*>(l);
                static const UINT kM = RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
                if (cwp->message == kM) {
                    {
                        std::lock_guard<std::mutex> lk(pendingPayloadsMutex);
                        auto it = std::find_if(
                            pendingPayloads.begin(), pendingPayloads.end(),
                            [id = (UINT_PTR)cwp->wParam](const auto& entry) {
                                return entry.first == id;
                            });
                        if (it != pendingPayloads.end()) {
                            payload = std::move(it->second);
                            pendingPayloads.erase(it);
                        }
                    }
                    // Multiple concurrent marshals install hooks in the same chain. Only the
                    // first hook that claims this ID may execute and release its payload.
                    if (payload) {
                        payload->result.store(payload->proc(payload->param),
                                              std::memory_order_release);
                    }
                }
            }
            LRESULT result = CallNextHookEx(nullptr, code, w, l);
            if (payload) payload->ran.store(true, std::memory_order_release);
            return result;
        }, nullptr, tid);
    if (!hook) {
        std::lock_guard<std::mutex> lk(pendingPayloadsMutex);
        std::erase_if(pendingPayloads,
                      [payloadId](const auto& entry) { return entry.first == payloadId; });
        return false;
    }

    bool sent = true;
    if (timeoutMs == INFINITE) {
        SendMessageW(hWnd, kMsg, payloadId, 0);
    } else {
        DWORD_PTR ignored = 0;
        // Keep delivering while a responsive target pumps sent messages. A timeout can safely
        // cancel only a payload the target hasn't claimed yet.
        sent = SendMessageTimeoutW(hWnd, kMsg, payloadId, 0,
                                   SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_NOTIMEOUTIFNOTHUNG,
                                   timeoutMs, &ignored) != 0;
    }

    bool ran = pay->ran.load(std::memory_order_acquire);
    bool claimed = false;
    if (!ran) {
        std::lock_guard<std::mutex> lk(pendingPayloadsMutex);
        auto it = std::find_if(
            pendingPayloads.begin(), pendingPayloads.end(),
            [payloadId](const auto& entry) { return entry.first == payloadId; });
        if (it != pendingPayloads.end()) {
            pendingPayloads.erase(it);
        } else {
            // The target claimed the payload before the send timed out. It may still be
            // executing mod code, so don't let the caller proceed until it has returned.
            claimed = true;
        }
    }
    if (claimed) {
        if (!sent) Wh_Log(L"Window-thread marshal timed out after dispatch; waiting");
        while (!pay->ran.load(std::memory_order_acquire)) Sleep(1);
        // The hook tail can still be unwinding. Unload joins every finite-marshal worker,
        // then its synchronous per-taskbar cleanup sends serialize behind that tail.
        ran = true;
    }
    bool result = ran && pay->result.load(std::memory_order_acquire);
    UnhookWindowsHookEx(hook);
    return result;
}

static std::vector<TaskbarDisplayInfo> FindCurrentProcessTaskbarDisplays() {
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lp) CALLBACK -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(hMonitor);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors));

    struct EnumContext {
        DWORD pid;
        std::vector<HMONITOR>* monitors;
        std::vector<TaskbarDisplayInfo>* windows;
    } ctx{GetCurrentProcessId(), &monitors, nullptr};

    std::vector<TaskbarDisplayInfo> windows;
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

        int monitorNumber = 0;
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        if (hMonitor) {
            for (size_t i = 0; i < ctx->monitors->size(); ++i) {
                if ((*ctx->monitors)[i] == hMonitor) {
                    monitorNumber = (int)i + 1;
                    break;
                }
            }
        }
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        RECT rect{};
        if (hMonitor && GetMonitorInfoW(hMonitor, &monitorInfo)) {
            rect = monitorInfo.rcMonitor;
        }
        ctx->windows->push_back({hWnd, primary, monitorNumber, rect});
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
    return windows;
}

static std::vector<HWND> FindCurrentProcessTaskbarWnds() {
    TaskbarMonitorMode mode;
    int targetMonitorNumber;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        mode = g_settings.taskbarMonitorMode;
        targetMonitorNumber = g_settings.taskbarMonitorNumber;
    }
    std::vector<TaskbarDisplayInfo> windows = FindCurrentProcessTaskbarDisplays();

    std::vector<HWND> result;
    result.reserve(windows.size());
    if (mode == TaskbarMonitorMode::Specific) {
        if (targetMonitorNumber >= 1 && targetMonitorNumber <= (int)windows.size()) {
            result.push_back(windows[targetMonitorNumber - 1].hWnd);
        }
        return result;
    }

    for (const auto& window : windows) {
        if (mode != TaskbarMonitorMode::Primary || window.primary) {
            result.push_back(window.hWnd);
        }
    }
    return result;
}

// Registry access is locked, while state contents remain owner-UI-thread-only. Raw pointers stay
// valid after lookup because only that owner thread can erase its state.
static QuotaUiInstance* FindUiState(HWND hWnd) {
    std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
    if (!g_uiInstances) return nullptr;
    for (auto& state : *g_uiInstances) {
        if (state->hWnd == hWnd) return state.get();
    }
    return nullptr;
}

static void EraseUiState(HWND hWnd) {
    std::unique_ptr<QuotaUiInstance> removed;
    {
        std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
        if (!g_uiInstances) return;
        auto it = std::find_if(g_uiInstances->begin(), g_uiInstances->end(),
            [hWnd](const auto& state) { return state->hWnd == hWnd; });
        if (it == g_uiInstances->end()) return;
        if ((*it)->ownerThreadId != GetCurrentThreadId()) {
            Wh_Log(L"Refusing to destroy UI state from a non-owner thread");
            return;
        }

        removed = std::move(*it);
        g_uiInstances->erase(it);
        g_uiInjected.store(!g_unloading && !g_uiInstances->empty(),
                           std::memory_order_release);
    }

    if (removed->windowSubclassed && removed->hWnd && IsWindow(removed->hWnd)) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(
            removed->hWnd, TaskbarWindowSubclassProc);
        removed->windowSubclassed = false;
    }
    removed.reset();
}

static void ClearQuotaEventState(QuotaUiInstance& state) {
    // Routed event delegates point into this DLL, so revoke before XAML tears down the subtree.
    try {
        if (state.paceTimer) state.paceTimer.Stop();
    } catch (...) {}
    try {
        if (state.paceTimer) state.paceTimer.Tick(state.paceTimerToken);
    } catch (...) {}
    state.paceTimer = nullptr;

    for (auto& refs : state.accountRefs) {
        refs.hasToolTipOpenCursor = false;
        refs.reopenToolTipOnMove = false;
        refs.manualToolTipOpen = false;
        try {
            if (refs.manualToolTipTimer) refs.manualToolTipTimer.Stop();
        } catch (...) {}
        try {
            if (refs.manualToolTipTimer) {
                refs.manualToolTipTimer.Tick(refs.manualToolTipTimerToken);
            }
        } catch (...) {}
        try {
            if (refs.toolTip) refs.toolTip.Opened(refs.toolTipOpenedToken);
        } catch (...) {}
        try {
            if (refs.toolTip && refs.toolTip.IsOpen()) refs.toolTip.IsOpen(false);
        } catch (...) {}
    }

    for (auto& handler : state.pointerHandlers) {
        if (!handler.element) continue;

        try { handler.element.Tapped(handler.tappedToken); } catch (...) {}
        try { handler.element.PointerEntered(handler.pointerEnteredToken); } catch (...) {}
        try { handler.element.PointerMoved(handler.pointerMovedToken); } catch (...) {}
        try { handler.element.PointerExited(handler.pointerExitedToken); } catch (...) {}
        try { handler.element.PointerCaptureLost(handler.pointerCaptureLostToken); } catch (...) {}
        try { handler.element.PointerCanceled(handler.pointerCanceledToken); } catch (...) {}
        try { handler.element.ContextFlyout(nullptr); } catch (...) {}
        try {
            ToolTipService::SetToolTip(handler.element, winrt::Windows::Foundation::IInspectable{nullptr});
        } catch (...) {}
    }
    state.pointerHandlers.clear();

    for (auto& handler : state.menuItemClickHandlers) {
        if (!handler.item) continue;
        try { handler.item.Click(handler.token); } catch (...) {}
    }
    state.menuItemClickHandlers.clear();
    state.accountToggleItems.clear();
    state.accountRefs.clear();
}

// src/taskbar/xaml-access.h
// Private taskbar.dll symbols and access to the taskbar XAML tree. Version-fragile.

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

    size_t taskbarElementIUnknownOffset;

#if defined(_M_X64)
    {
        // 48:83EC 28 | sub rsp,28
        // 48:83C1 48 | add rcx,48
        const BYTE* b = (const BYTE*)TaskbarHost_FrameHeight_Original;
        if (b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xEC && b[4] == 0x48 &&
            b[5] == 0x83 && b[6] == 0xC1 && b[7] <= 0x7F) {
            taskbarElementIUnknownOffset = b[7];
        } else {
            Wh_Log(L"Unsupported TaskbarHost::FrameHeight");
            cleanup();
            return E_NOINTERFACE;
        }
    }
#elif defined(_M_ARM64)
    {
        // 7f2303d5 pacibsp
        // fd7bbfa9 stp     fp, lr, [sp, #-0x10]!
        // fd030091 mov     fp, sp
        // 080c41f8 ldr     x8, [x0, #0x10]!
        const DWORD* p = (const DWORD*)TaskbarHost_FrameHeight_Original;
        if (p[0] == 0xD503237F && (p[1] & 0xFFC07FFF) == 0xA9807BFD &&
            p[2] == 0x910003FD && (p[3] & 0xFFF00FE0) == 0xF8400C00) {
            taskbarElementIUnknownOffset = (p[3] >> 12) & 0xFF;
        } else {
            Wh_Log(L"Unsupported TaskbarHost::FrameHeight");
            cleanup();
            return E_NOINTERFACE;
        }
    }
#else
#error "Unsupported architecture"
#endif

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

// src/taskbar/tooltip.h
// Quota colors and the per-account tooltip.

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

static void UpdateQuotaToolTip(ToolTip const& toolTip, std::wstring const& tip, bool hasError) {
    constexpr double maxWidth = 460;
    auto muted = SolidColorBrush(winrt::Windows::UI::Color{255, 0xD6, 0xD6, 0xD6});
    auto quotaLabel = SolidColorBrush(winrt::Windows::UI::Color{255, 0x9A, 0xBE, 0xFF});
    auto infoLabel = SolidColorBrush(winrt::Windows::UI::Color{255, 0xC7, 0x9B, 0xFF});
    auto creditLabel = SolidColorBrush(winrt::Windows::UI::Color{255, 0xFF, 0xD7, 0x66});
    auto duration = SolidColorBrush(winrt::Windows::UI::Color{255, 0xB7, 0xE4, 0xA3});
    auto accent = SolidColorBrush(hasError ?
        winrt::Windows::UI::Color{255, 0xFF, 0xB4, 0xA9} :
        winrt::Windows::UI::Color{255, 0xC7, 0x9B, 0xFF});
    auto border = SolidColorBrush(hasError ?
        winrt::Windows::UI::Color{0xB8, 0xD1, 0x34, 0x38} :
        winrt::Windows::UI::Color{0x72, 0x8A, 0xD1, 0xFF});

    size_t firstBreak = tip.find(L'\n');
    std::wstring title = firstBreak == std::wstring::npos ? tip : tip.substr(0, firstBreak);
    std::wstring body = firstBreak == std::wstring::npos ? L"" : tip.substr(firstBreak + 1);

    TextBlock titleBlock;
    titleBlock.Text(winrt::hstring(title));
    titleBlock.FontSize(12.5);
    titleBlock.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    titleBlock.Foreground(accent);
    titleBlock.TextWrapping(TextWrapping::Wrap);
    titleBlock.MaxWidth(maxWidth);
    titleBlock.IsHitTestVisible(false);

    StackPanel content;
    content.Orientation(Orientation::Vertical);
    content.MaxWidth(maxWidth);
    content.IsHitTestVisible(false);
    content.Children().Append(titleBlock);

    if (!body.empty()) {
        auto appendRun = [](TextBlock const& textBlock, std::wstring const& text,
                            Brush const& brush, bool bold = false) {
            if (text.empty()) return;
            wuxd::Run run;
            run.Text(winrt::hstring(text));
            run.Foreground(brush);
            if (bold) run.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            textBlock.Inlines().Append(run);
        };

        bool firstLine = true;
        for (size_t pos = 0; pos <= body.size();) {
            size_t next = body.find(L'\n', pos);
            std::wstring line = next == std::wstring::npos ? body.substr(pos) : body.substr(pos, next - pos);

            TextBlock lineBlock;
            lineBlock.FontSize(12);
            lineBlock.LineHeight(16);
            lineBlock.TextWrapping(TextWrapping::Wrap);
            lineBlock.MaxWidth(maxWidth);
            lineBlock.Margin(firstLine ? Thickness{0, 4, 0, 0} : Thickness{0, 1, 0, 0});
            lineBlock.IsHitTestVisible(false);

            Brush labelBrush = muted;
            size_t labelEnd = std::wstring::npos;
            size_t highlightStart = std::wstring::npos;
            size_t highlightEnd = std::wstring::npos;
            bool labelBold = false;
            bool quotaLine = false;
            bool errorLine = false;
            if (line.rfind(L"5h:", 0) == 0) {
                labelBrush = quotaLabel;
                labelEnd = 3;
                labelBold = true;
                quotaLine = true;
            } else if (line.rfind(L"week:", 0) == 0) {
                labelBrush = quotaLabel;
                labelEnd = 5;
                labelBold = true;
                quotaLine = true;
            } else if (line.rfind(L"Fable week:", 0) == 0) {
                labelBrush = quotaLabel;
                labelEnd = 11;
                labelBold = true;
                quotaLine = true;
            } else if (line.rfind(L"error:", 0) == 0) {
                labelBrush = accent;
                labelEnd = 6;
                labelBold = true;
                errorLine = true;
            } else if (line.rfind(L"credits:", 0) == 0) {
                labelBrush = creditLabel;
                labelEnd = 8;
                highlightStart = line.find_first_not_of(L" ", labelEnd);
                if (highlightStart != std::wstring::npos) {
                    highlightEnd = highlightStart;
                    while (highlightEnd < line.size()) {
                        wchar_t ch = line[highlightEnd];
                        if ((ch < L'0' || ch > L'9') && ch != L'.') break;
                        highlightEnd++;
                    }
                }
                labelBold = true;
            } else if (line.rfind(L"extra usage:", 0) == 0) {
                labelBrush = creditLabel;
                labelEnd = 12;
                labelBold = true;
                quotaLine = true;
            } else if (line.rfind(L"updated:", 0) == 0) {
                labelBrush = infoLabel;
                labelEnd = 8;
                highlightStart = line.find(L"no data yet", labelEnd);
                if (highlightStart != std::wstring::npos) highlightEnd = highlightStart + 11;
                labelBold = true;
            }

            size_t textStart = 0;
            if (labelEnd != std::wstring::npos) {
                appendRun(lineBlock, line.substr(0, labelEnd), labelBrush, labelBold);
                textStart = labelEnd;
            }

            if (errorLine) {
                appendRun(lineBlock, line.substr(textStart), accent);
                content.Children().Append(lineBlock);
                firstLine = false;
                if (next == std::wstring::npos) break;
                pos = next + 1;
                continue;
            }

            size_t cursor = textStart;
            if (quotaLine) {
                size_t percentEnd = line.find(L"%", cursor);
                if (percentEnd != std::wstring::npos) {
                    size_t percentStart = percentEnd;
                    while (percentStart > cursor) {
                        wchar_t ch = line[percentStart - 1];
                        if ((ch < L'0' || ch > L'9') && ch != L'.') break;
                        percentStart--;
                    }
                    appendRun(lineBlock, line.substr(cursor, percentStart - cursor), muted);
                    appendRun(lineBlock, line.substr(percentStart, percentEnd + 1 - percentStart), duration, true);
                    cursor = percentEnd + 1;
                }
            }

            size_t inPos = line.find(L"in ", cursor);
            if (inPos != std::wstring::npos) {
                size_t durationStart = inPos + 3;
                size_t durationEnd = line.find(L" (", durationStart);
                size_t dashEnd = line.find(L" - ", durationStart);
                if (dashEnd != std::wstring::npos && (durationEnd == std::wstring::npos || dashEnd < durationEnd)) {
                    durationEnd = dashEnd;
                }
                if (durationEnd == std::wstring::npos) durationEnd = line.size();

                appendRun(lineBlock, line.substr(cursor, durationStart - cursor), muted);
                appendRun(lineBlock, line.substr(durationStart, durationEnd - durationStart), duration, true);
                appendRun(lineBlock, line.substr(durationEnd), muted);
            } else if (highlightStart != std::wstring::npos && highlightStart < highlightEnd) {
                appendRun(lineBlock, line.substr(cursor, highlightStart - cursor), muted);
                appendRun(lineBlock, line.substr(highlightStart, highlightEnd - highlightStart), duration, true);
                appendRun(lineBlock, line.substr(highlightEnd), muted);
            } else {
                appendRun(lineBlock, line.substr(cursor), muted);
            }

            content.Children().Append(lineBlock);
            firstLine = false;
            if (next == std::wstring::npos) break;
            pos = next + 1;
        }
    }

    toolTip.BorderBrush(border);
    toolTip.Content(content);
}

// src/taskbar/actions.h
// Click and context-menu actions on the taskbar bars.

static void OpenDashboardForIdentity(uint64_t identityHash) {
    std::wstring provider;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        for (const auto& account : g_settings.accounts) {
            if (AccountIdentityHash(account) == identityHash) {
                provider = account.provider;
                break;
            }
        }
    }
    if (provider.empty()) return;

    if (provider == L"antigravity") {
        RefreshQuotaByIdentity(identityHash);
    } else {
        OpenUrl(provider == L"anthropic" ? L"https://claude.ai/settings/usage"
                                         : L"https://chatgpt.com/codex/cloud/settings/analytics#usage");
    }
}

// Right-click menu: flip an account's show/hide state, keep at least one visible, persist the
// hidden-set to mod storage, then wake the fetch thread and refresh all taskbars. Runs on a
// taskbar UI thread (menu click); `sender` is the clicked ToggleMenuFlyoutItem (already flipped).
static void ToggleAccountVisibility(uint64_t identityHash,
                                    winrt::Windows::Foundation::IInspectable const& sender) {
    if (g_unloading) return;
    auto toggle = sender.try_as<ToggleMenuFlyoutItem>();
    bool clickedVisible = toggle && toggle.IsChecked();
    std::unique_lock<std::mutex> configLock(g_configEditMutex);

    std::wstring hashes;
    Settings settingsSnapshot;
    bool refreshNow = false;
    bool oldHidden = false;
    bool rejectedLastVisible = false;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        int accountIndex = -1;
        for (size_t i = 0; i < g_settings.accounts.size(); i++) {
            if (AccountIdentityHash(g_settings.accounts[i]) == identityHash) {
                accountIndex = (int)i;
                break;
            }
        }
        if (accountIndex < 0) return;
        oldHidden = g_settings.accounts[accountIndex].hidden;
        bool wantVisible = toggle ? clickedVisible : oldHidden;

        // Refuse to hide the last visible account: there'd be no bar left to right-click.
        if (!wantVisible && !g_settings.accounts[accountIndex].hidden) {
            int visibleCount = 0;
            for (const auto& a : g_settings.accounts) {
                if (!a.hidden) visibleCount++;
            }
            if (visibleCount <= 1) {
                rejectedLastVisible = true;
            }
        }
        if (!rejectedLastVisible) {
            bool newHidden = !wantVisible;
            settingsSnapshot = g_settings;
            settingsSnapshot.accounts[accountIndex].hidden = newHidden;
            if (newHidden != oldHidden) {
                if (!newHidden) {
                    // Showing: keep the existing (possibly stale) data and only re-query if it has
                    // already gone stale, matching the UI's grey-out threshold. This stops repeated
                    // hide/show from triggering fetches and hitting provider rate limits.
                    ULONGLONG staleIntervalMin =
                        settingsSnapshot.accounts[accountIndex].provider == L"antigravity"
                            ? 1
                            : (ULONGLONG)settingsSnapshot.pollMinutes;
                    ULONGLONG now = NowUnixMs();
                    std::lock_guard<std::mutex> lk2(g_dataMutex);
                    if (accountIndex >= (int)g_data.size()) {
                        refreshNow = true;
                    } else {
                        const AccountData& d = g_data[accountIndex];
                        refreshNow = d.stale || d.lastSuccessMs == 0 ||
                                     now - d.lastSuccessMs > staleIntervalMin * 2 * 60000;
                    }
                }
            }

            wchar_t buf[24];
            for (const auto& a : settingsSnapshot.accounts) {
                if (!a.hidden) continue;
                if (!hashes.empty()) hashes += L";";
                swprintf(buf, ARRAYSIZE(buf), L"%016llx",
                         (unsigned long long)AccountIdentityHash(a));
                hashes += buf;
            }
        }
    }
    if (rejectedLastVisible) {
        configLock.unlock();
        if (toggle) toggle.IsChecked(true);
        return;
    }

    if (!SaveOwnedSettings(settingsSnapshot)) {
        configLock.unlock();
        if (toggle) toggle.IsChecked(!oldHidden);
        Wh_Log(L"Could not persist account visibility");
        NotifySettingsWindowChanged();
        return;
    }
    PublishSettings(settingsSnapshot);
    Wh_SetStringValue(L"hiddenAccounts", hashes.c_str());
    // RefreshQuota re-queries only this account (and posts the UI); otherwise just repaint so the
    // column collapses/reappears with its existing data without any network request.
    configLock.unlock();
    NotifySettingsWindowChanged();
    if (refreshNow) RefreshQuotaByIdentity(identityHash);
    else PostUiUpdate();
}

// src/taskbar/quota-grid.h
// Builds the per-taskbar quota grid, including visual-test sample data.

static void BuildVisualTestSnapshot(int yellowThreshold, int orangeThreshold,
                                    int redThreshold, ULONGLONG now,
                                    std::vector<AccountConfig>* accounts,
                                    std::vector<AccountData>* data) {
    static constexpr std::array<const wchar_t*, 4> kLabels = {
        L"OAI", L"CC 1", L"CC 2", L"Go",
    };
    const std::array<double, 4> percentages = {
        yellowThreshold / 2.0,
        50.0,
        orangeThreshold + (redThreshold - orangeThreshold) / 2.0,
        redThreshold + (100 - redThreshold) / 2.0,
    };
    static constexpr std::array<ULONGLONG, kQuotaBarCount> kDurations = {
        kFiveHourWindowMs,
        kWeeklyWindowMs,
        kWeeklyWindowMs,
        30ULL * 24 * 60 * 60 * 1000,
    };
    static constexpr std::array<double, kQuotaBarCount> kRemainingFractions = {
        0.35, 0.5, 0.6, 0.8,
    };

    accounts->clear();
    accounts->reserve(percentages.size());
    if (data) {
        data->clear();
        data->reserve(percentages.size());
    }
    for (size_t i = 0; i < percentages.size(); i++) {
        AccountConfig account;
        account.provider = L"anthropic";
        account.label = kLabels[i];
        account.showBars.fill(false);
        for (size_t w = 0; w <= i; w++) account.showBars[w] = true;
        accounts->push_back(std::move(account));

        if (!data) continue;
        AccountData accountData;
        std::array<WindowUsage*, kQuotaBarCount> usage = {
            &accountData.win5h, &accountData.winWeek,
            &accountData.fableWeek, &accountData.extraUsage,
        };
        for (int w = 0; w < kQuotaBarCount; w++) {
            usage[w]->pct = percentages[i];
            usage[w]->windowDurationMs = kDurations[w];
            usage[w]->resetUnixMs = now +
                (ULONGLONG)std::lround(kDurations[w] * kRemainingFractions[w]);
        }
        accountData.plan = L"Visual test";
        accountData.extraUsedAmount = percentages[i] / 2.0;
        accountData.extraLimitAmount = 50.0;
        accountData.lastSuccessMs = now;
        accountData.stale = false;
        data->push_back(std::move(accountData));
    }
}

static Grid BuildQuotaGrid(QuotaUiInstance& state) {
    try {
        double physicalPixelDip = 1.0 / state.rasterizationScale;
        std::vector<AccountConfig> accounts;
        int barLength, barThickness, labelFontSize, percentFontSize;
        int accountMargin, labelGap, rightMargin;
        int yellowThreshold, orangeThreshold, redThreshold;
        bool showPaceTicks, showBarLabels;
        bool visualTestMode = g_visualTestMode.load(std::memory_order_acquire);
        COLORREF paceTickColor;
        BarLayout barLayout;
        PercentTextVisibility percentTextVisibility;
        PercentTextAlignment percentTextAlignment;
        PaceTickStyle paceTickStyle;
        LabelPosition labelPosition;
        ClickAction clickAction;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            state.buildSettingsGeneration = g_settingsGeneration;
            accounts = g_settings.accounts;
            clickAction = g_settings.clickAction;
            barLayout = g_settings.barLayout;
            barLength = g_settings.barLength;
            barThickness = g_settings.barThickness;
            labelFontSize = g_settings.labelFontSize;
            percentFontSize = g_settings.percentFontSize;
            accountMargin = g_settings.accountMargin;
            labelGap = g_settings.labelGap;
            rightMargin = g_settings.rightMargin;
            labelPosition = g_settings.labelPosition;
            showPaceTicks = g_settings.showPaceTicks;
            paceTickStyle = g_settings.paceTickStyle;
            paceTickColor = g_settings.paceTickColor;
            showBarLabels = g_settings.showBarLabels;
            percentTextVisibility = g_settings.percentTextVisibility;
            percentTextAlignment = g_settings.percentTextAlignment;
            yellowThreshold = g_settings.yellowThreshold;
            orangeThreshold = g_settings.orangeThreshold;
            redThreshold = g_settings.redThreshold;
        }
        state.buildVisualTestMode = visualTestMode;
        if (visualTestMode) {
            BuildVisualTestSnapshot(yellowThreshold, orangeThreshold, redThreshold,
                                    NowUnixMs(), &accounts, nullptr);
        }
        state.accountRefs.clear();
        Grid root;
        root.Name(kRootName);
        root.VerticalAlignment(VerticalAlignment::Center);

        StackPanel panel;
        panel.Orientation(Orientation::Horizontal);
        panel.Margin({4, 0, (double)rightMargin, 0});

        if (accounts.empty()) {
            Border setupTile;
            setupTile.CornerRadius({4, 4, 4, 4});
            setupTile.Padding({8, 3, 8, 3});
            setupTile.Margin({(double)accountMargin, 0, (double)accountMargin, 0});
            setupTile.Background(SolidColorBrush(winrt::Windows::UI::Color{0x30, 0x80, 0x80, 0x80}));

            TextBlock setupText;
            setupText.Text(L"AI +");
            setupText.FontSize(labelFontSize);
            setupText.Opacity(0.9);
            setupTile.Child(setupText);

            UIElement setupElement = setupTile.as<UIElement>();
            auto tappedToken = setupElement.Tapped(
                [](winrt::Windows::Foundation::IInspectable const&,
                   wuxi::TappedRoutedEventArgs const& e) {
                    OpenSettingsWindow();
                    e.Handled(true);
                });
            state.pointerHandlers.push_back({setupElement, tappedToken});
            ToolTipService::SetToolTip(
                setupElement,
                winrt::box_value(g_settingsLoadError.load() ?
                    L"Open AI quota settings. Stored settings could not be read; a backup was kept." :
                    L"Open AI quota settings"));

            MenuFlyout menu;
            MenuFlyoutItem settingsItem;
            settingsItem.Text(L"Settings...");
            auto settingsToken = settingsItem.Click(
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenSettingsWindow();
                });
            state.menuItemClickHandlers.push_back({settingsItem, settingsToken});

            MenuFlyoutItem historyItem;
            historyItem.Text(L"Usage history...");
            auto historyToken = historyItem.Click(
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenHistoryWindow();
                });
            state.menuItemClickHandlers.push_back({historyItem, historyToken});
            menu.Items().Append(historyItem);
            menu.Items().Append(settingsItem);
            setupElement.ContextFlyout(menu);

            panel.Children().Append(setupTile);
            root.Children().Append(panel);
            return root;
        }
        state.accountRefs.reserve(accounts.size());
        bool verticalBars = barLayout == BarLayout::Vertical;
        UINT toolTipDurationSeconds = 5;
        if (!SystemParametersInfoW(SPI_GETMESSAGEDURATION, 0, &toolTipDurationSeconds, 0)) {
            toolTipDurationSeconds = 5;
        }
        auto manualToolTipShowDuration =
            std::chrono::seconds(std::clamp(toolTipDurationSeconds, 1u, 60u));
        UINT toolTipHoverDelayMs = 400;
        if (!SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0, &toolTipHoverDelayMs, 0)) {
            toolTipHoverDelayMs = 400;
        }
        auto manualToolTipHoverDelay =
            std::chrono::milliseconds(std::clamp(toolTipHoverDelayMs, 100u, 2000u));
        UINT toolTipMoveThresholdX = 4;
        UINT toolTipMoveThresholdY = 4;
        SystemParametersInfoW(SPI_GETMOUSEHOVERWIDTH, 0, &toolTipMoveThresholdX, 0);
        SystemParametersInfoW(SPI_GETMOUSEHOVERHEIGHT, 0, &toolTipMoveThresholdY, 0);
        toolTipMoveThresholdX = std::clamp(toolTipMoveThresholdX, 2u, 32u);
        toolTipMoveThresholdY = std::clamp(toolTipMoveThresholdY, 2u, 32u);

        wchar_t name[64];
        for (size_t i = 0; i < accounts.size(); i++) {
            bool labelOnSide = labelPosition == LabelPosition::Left ||
                               labelPosition == LabelPosition::Right;
            bool labelBeforeBars = labelPosition == LabelPosition::Left ||
                                   labelPosition == LabelPosition::Top;
            StackPanel col;
            col.Orientation(labelOnSide ? Orientation::Horizontal : Orientation::Vertical);
            col.VerticalAlignment(VerticalAlignment::Center);
            col.Margin({(double)accountMargin, 0, (double)accountMargin, 0});
            col.Background(SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
            swprintf(name, ARRAYSIZE(name), L"AiQuota_Acc_%d", (int)i);
            col.Name(name);
            col.Visibility(accounts[i].hidden ? Visibility::Collapsed : Visibility::Visible);
            AccountUiRefs refs;
            refs.column = col;

            TextBlock label{nullptr};
            if (labelPosition != LabelPosition::Hidden) {
                label = TextBlock{};
                label.Text(accounts[i].label);
                label.FontSize(labelFontSize);
                label.VerticalAlignment(VerticalAlignment::Center);
                label.HorizontalAlignment(labelOnSide ? HorizontalAlignment::Left :
                                                         HorizontalAlignment::Center);
                if (labelPosition == LabelPosition::Left) {
                    label.Margin({0, -2, (double)labelGap, 0});
                } else if (labelPosition == LabelPosition::Right) {
                    label.Margin({(double)labelGap, -2, 0, 0});
                } else if (labelPosition == LabelPosition::Top) {
                    label.Margin({0, 0, 0, (double)labelGap});
                } else {
                    label.Margin({0, (double)labelGap, 0, 0});
                }
                label.Opacity(0.8);
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Label_%d", (int)i);
                label.Name(name);
                refs.label = label;
                if (labelBeforeBars) col.Children().Append(label);
            }

            StackPanel bars;
            bars.Orientation(verticalBars ? Orientation::Horizontal : Orientation::Vertical);
            bars.VerticalAlignment(VerticalAlignment::Center);

            double radius = std::max(1.0, barThickness / 2.0);
            for (int w = 0; w < kQuotaBarCount; w++) {
                Border track;
                track.Width(verticalBars ? barThickness : barLength);
                track.Height(verticalBars ? barLength : barThickness);
                track.CornerRadius({radius, radius, radius, radius});
                track.HorizontalAlignment(HorizontalAlignment::Center);
                track.Background(SolidColorBrush(winrt::Windows::UI::Color{0x46, 0x80, 0x80, 0x80}));
                refs.tracks[w] = track;

                Border fill;
                fill.Height(verticalBars ? 0 : barThickness);
                fill.Width(verticalBars ? barThickness : 0);
                fill.CornerRadius({radius, radius, radius, radius});
                fill.HorizontalAlignment(verticalBars ? HorizontalAlignment::Center : HorizontalAlignment::Left);
                fill.VerticalAlignment(verticalBars ? VerticalAlignment::Bottom : VerticalAlignment::Center);
                fill.Background(SolidColorBrush(winrt::Windows::UI::Color{255, 0x9E, 0x9E, 0x9E}));
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Fill_%d_%d", (int)i, w);
                fill.Name(name);
                refs.fills[w] = fill;

                Grid trackContent;
                trackContent.Children().Append(fill);

                if (showPaceTicks) {
                    double paceCrossSize = barThickness;
                    double paceLength = (paceTickStyle == PaceTickStyle::Caret ? 5 : 4) *
                                        physicalPixelDip;
                    if (paceTickStyle == PaceTickStyle::Notch) {
                        paceCrossSize = std::max(2.0, std::ceil(barThickness * 0.6));
                    } else if (paceTickStyle == PaceTickStyle::Dot) {
                        paceCrossSize = 4 * physicalPixelDip;
                    }

                    Border paceTick;
                    paceTick.Width(verticalBars ? paceCrossSize : paceLength);
                    paceTick.Height(verticalBars ? paceLength : paceCrossSize);
                    paceTick.HorizontalAlignment(
                        verticalBars ? (paceTickStyle == PaceTickStyle::Notch ?
                                            HorizontalAlignment::Left : HorizontalAlignment::Center) :
                                       HorizontalAlignment::Left);
                    paceTick.VerticalAlignment(
                        verticalBars ? VerticalAlignment::Bottom :
                                       (paceTickStyle == PaceTickStyle::Notch ?
                                            VerticalAlignment::Top : VerticalAlignment::Center));
                    paceTick.Visibility(Visibility::Collapsed);
                    paceTick.IsHitTestVisible(false);

                    auto tickColor = winrt::Windows::UI::Color{
                        0xFF, GetRValue(paceTickColor), GetGValue(paceTickColor),
                        GetBValue(paceTickColor)};
                    if (paceTickStyle == PaceTickStyle::Caret) {
                        Grid caretShape;
                        auto haloColor = winrt::Windows::UI::Color{0xC0, 0, 0, 0};
                        for (int layer = 0; layer < 2; layer++) {
                            bool core = layer == 1;
                            double capInset = (core ? 1 : 0) * physicalPixelDip;
                            double stemInset = (core ? 2 : 1) * physicalPixelDip;
                            double capDepth = std::min((core ? 1.0 : 2.0) * physicalPixelDip,
                                                       paceCrossSize / 2.0);
                            wuxs::Polygon part;
                            auto addPoint = [&](double along, double cross) {
                                part.Points().Append(verticalBars ?
                                    winrt::Windows::Foundation::Point{
                                        (float)cross, (float)along} :
                                    winrt::Windows::Foundation::Point{
                                        (float)along, (float)cross});
                            };
                            addPoint(capInset, 0);
                            addPoint(paceLength - capInset, 0);
                            addPoint(paceLength - capInset, capDepth);
                            addPoint(paceLength - stemInset, capDepth);
                            addPoint(paceLength - stemInset, paceCrossSize - capDepth);
                            addPoint(paceLength - capInset, paceCrossSize - capDepth);
                            addPoint(paceLength - capInset, paceCrossSize);
                            addPoint(capInset, paceCrossSize);
                            addPoint(capInset, paceCrossSize - capDepth);
                            addPoint(stemInset, paceCrossSize - capDepth);
                            addPoint(stemInset, capDepth);
                            addPoint(capInset, capDepth);
                            part.Fill(SolidColorBrush(core ? tickColor : haloColor));
                            caretShape.Children().Append(part);
                        }
                        paceTick.Child(caretShape);
                    } else {
                        paceTick.Background(
                            SolidColorBrush(winrt::Windows::UI::Color{0xC0, 0, 0, 0}));
                        if (paceTickStyle == PaceTickStyle::Dot) {
                            double radius = 2 * physicalPixelDip;
                            paceTick.CornerRadius({radius, radius, radius, radius});
                        }

                        Border paceCore;
                        double paceCoreCrossSize =
                            paceTickStyle == PaceTickStyle::Dot ?
                                3 * physicalPixelDip : paceCrossSize;
                        double paceCoreLength = (paceTickStyle == PaceTickStyle::Dot ? 3 : 2) *
                                                physicalPixelDip;
                        paceCore.Width(verticalBars ? paceCoreCrossSize : paceCoreLength);
                        paceCore.Height(verticalBars ? paceCoreLength : paceCoreCrossSize);
                        paceCore.HorizontalAlignment(HorizontalAlignment::Center);
                        paceCore.VerticalAlignment(VerticalAlignment::Center);
                        if (paceTickStyle == PaceTickStyle::Dot) {
                            double radius = 1.5 * physicalPixelDip;
                            paceCore.CornerRadius({radius, radius, radius, radius});
                        }
                        paceCore.Background(SolidColorBrush(tickColor));
                        paceTick.Child(paceCore);
                    }
                    refs.paceTicks[w] = paceTick;
                    trackContent.Children().Append(paceTick);
                }

                track.Child(trackContent);

                Grid barItem;
                double compactLabelFontSize = percentFontSize;
                barItem.Height(verticalBars ? barLength : barThickness);
                barItem.HorizontalAlignment(HorizontalAlignment::Center);

                if (showBarLabels) {
                    static constexpr PCWSTR kBarLabels[kQuotaBarCount] = {
                        L"5h", L"7d", L"Fa", L"Ex"};
                    ColumnDefinition labelColumn;
                    labelColumn.Width({std::ceil(compactLabelFontSize * 1.4) + 3.0,
                                       GridUnitType::Pixel});
                    ColumnDefinition trackColumn;
                    trackColumn.Width({(double)(verticalBars ? barThickness : barLength),
                                       GridUnitType::Pixel});
                    barItem.ColumnDefinitions().Append(labelColumn);
                    barItem.ColumnDefinitions().Append(trackColumn);

                    TextBlock barLabel;
                    barLabel.Text(w == kExtraUsageBar && accounts[i].provider == L"openai" ?
                                      L"Cr" : kBarLabels[w]);
                    barLabel.FontSize(compactLabelFontSize);
                    barLabel.Opacity(0.8);
                    barLabel.IsHitTestVisible(false);
                    barLabel.TextAlignment(TextAlignment::Right);
                    barLabel.VerticalAlignment(VerticalAlignment::Center);
                    barLabel.Margin({0, -1, 3, 0});
                    barItem.Children().Append(barLabel);
                    Grid::SetColumn(track, 1);
                } else {
                    barItem.Width(verticalBars ? barThickness : barLength);
                }

                barItem.Children().Append(track);
                refs.barItems[w] = barItem;

                if (percentTextVisibility != PercentTextVisibility::Never) {
                    TextBlock percent;
                    percent.FontSize(percentFontSize);
                    percent.Width(verticalBars ?
                        std::max((double)barThickness, percentFontSize * 4.0) :
                        (double)barLength);
                    percent.VerticalAlignment(VerticalAlignment::Center);
                    if (percentTextAlignment == PercentTextAlignment::Left) {
                        percent.HorizontalAlignment(HorizontalAlignment::Left);
                        percent.TextAlignment(TextAlignment::Left);
                    } else if (percentTextAlignment == PercentTextAlignment::Center) {
                        percent.HorizontalAlignment(HorizontalAlignment::Center);
                        percent.TextAlignment(TextAlignment::Center);
                    } else {
                        // Adaptive starts on the unfilled right side.
                        percent.HorizontalAlignment(HorizontalAlignment::Right);
                        percent.TextAlignment(TextAlignment::Right);
                    }
                    percent.Foreground(SolidColorBrush(
                        winrt::Windows::UI::Color{255, 255, 255, 255}));
                    percent.Opacity(percentTextVisibility == PercentTextVisibility::Always ?
                                        0.9 : 0.0);
                    percent.IsHitTestVisible(false);
                    TranslateTransform translation;
                    translation.X(percentTextAlignment == PercentTextAlignment::Left ?
                                      4 * physicalPixelDip :
                                  percentTextAlignment == PercentTextAlignment::Center ?
                                      0 : -4 * physicalPixelDip);
                    translation.Y(-1);
                    percent.RenderTransform(translation);
                    swprintf(name, ARRAYSIZE(name), L"AiQuota_Percent_%d_%d", (int)i, w);
                    percent.Name(name);
                    refs.percents[w] = percent;
                    if (showBarLabels) Grid::SetColumn(percent, 1);
                    barItem.Children().Append(percent);
                }

                bars.Children().Append(barItem);
            }

            refs.barArea = bars.as<FrameworkElement>();
            col.Children().Append(bars);
            if (label && !labelBeforeBars) col.Children().Append(label);

            ToolTip toolTip;
            toolTip.Placement(wuxcp::PlacementMode::Top);
            toolTip.VerticalOffset(20);
            toolTip.Padding(Thickness{10, 8, 10, 8});
            toolTip.Background(SolidColorBrush(winrt::Windows::UI::Color{0xF7, 0x1F, 0x1F, 0x1F}));
            toolTip.Foreground(SolidColorBrush(winrt::Windows::UI::Color{255, 0xF3, 0xF3, 0xF3}));
            toolTip.BorderThickness(Thickness{1, 1, 1, 1});
            toolTip.IsHitTestVisible(false);
            UpdateQuotaToolTip(toolTip, L"loading...", false);
            ToolTipService::SetToolTip(col, toolTip);
            refs.toolTip = toolTip;
            ToolTipService::SetPlacement(col, wuxcp::PlacementMode::Top);
            UIElement tappedElement = col.as<UIElement>();
            QuotaUiInstance* statePtr = &state;
            int accountIndex = (int)i;
            uint64_t accountIdentity = AccountIdentityHash(accounts[i]);
            refs.toolTipOpenedToken = toolTip.Opened(
                [hWnd = state.hWnd, accountIndex](winrt::Windows::Foundation::IInspectable const&,
                                                  RoutedEventArgs const&) {
                    if (g_unloading) return;

                    try {
                        auto* uiState = FindUiState(hWnd);
                        if (!uiState || accountIndex >= (int)uiState->accountRefs.size()) return;

                        auto& refs = uiState->accountRefs[accountIndex];
                        refs.hasToolTipOpenCursor = GetCursorPos(&refs.toolTipOpenCursor) != FALSE;
                    } catch (...) {}
                });
            DispatcherTimer manualToolTipTimer;
            manualToolTipTimer.Interval(manualToolTipHoverDelay);
            auto manualToolTipTimerToken = manualToolTipTimer.Tick(
                [hWnd = state.hWnd, accountIndex, manualToolTipShowDuration](
                    winrt::Windows::Foundation::IInspectable const& sender,
                    winrt::Windows::Foundation::IInspectable const&) {
                    try {
                        if (auto timer = sender.try_as<DispatcherTimer>()) timer.Stop();
                        if (g_unloading) return;

                        auto* uiState = FindUiState(hWnd);
                        if (!uiState || !uiState->quotaGrid ||
                            accountIndex >= (int)uiState->accountRefs.size()) return;

                        auto& refs = uiState->accountRefs[accountIndex];
                        if (!refs.toolTip) return;
                        if (refs.manualToolTipOpen) {
                            refs.manualToolTipOpen = false;
                            refs.reopenToolTipOnMove = true;
                            refs.toolTip.IsOpen(false);
                            return;
                        }
                        if (!refs.reopenToolTipOnMove) return;

                        refs.hasToolTipOpenCursor =
                            GetCursorPos(&refs.toolTipOpenCursor) != FALSE;
                        refs.toolTip.IsOpen(true);
                        refs.reopenToolTipOnMove = false;
                        refs.manualToolTipOpen = true;
                        if (refs.manualToolTipTimer) {
                            refs.manualToolTipTimer.Interval(manualToolTipShowDuration);
                            refs.manualToolTipTimer.Start();
                        }
                    } catch (...) {}
                });
            refs.manualToolTipTimer = manualToolTipTimer;
            refs.manualToolTipTimerToken = manualToolTipTimerToken;
            bool hasDashboard = !visualTestMode && accounts[i].provider != L"antigravity";
            auto tappedToken = tappedElement.Tapped(
                [statePtr, accountIndex, accountIdentity, clickAction, hasDashboard,
                  manualToolTipHoverDelay, visualTestMode](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::TappedRoutedEventArgs const& e) {
                    if (g_unloading || !statePtr->quotaGrid) {
                        e.Handled(true);
                        return;
                    }
                    if (visualTestMode) {
                        OpenSettingsWindow();
                        e.Handled(true);
                        return;
                    }

                    bool accountFound = false;
                    bool needsLogin = false;
                    {
                        std::lock_guard<std::mutex> lk(g_settingsMutex);
                        for (size_t i = 0; i < g_settings.accounts.size(); i++) {
                            if (AccountIdentityHash(g_settings.accounts[i]) != accountIdentity) continue;
                            accountFound = true;
                            std::lock_guard<std::mutex> lk2(g_dataMutex);
                            if (i < g_data.size()) needsLogin = g_data[i].needsLogin;
                            break;
                        }
                    }
                    if (!accountFound) {
                        e.Handled(true);
                        return;
                    }
                    if (needsLogin) StartLoginByIdentity(accountIdentity);
                    else if (clickAction == ClickAction::OpenDashboard && hasDashboard) {
                        OpenDashboardForIdentity(accountIdentity);
                    }
                    else {
                        RefreshQuotaByIdentity(accountIdentity);
                        try {
                            if (e.PointerDeviceType() ==
                                    winrt::Windows::Devices::Input::PointerDeviceType::Mouse &&
                                accountIndex < (int)statePtr->accountRefs.size()) {
                                auto& refs = statePtr->accountRefs[accountIndex];
                                if (refs.manualToolTipTimer) refs.manualToolTipTimer.Stop();
                                if (refs.manualToolTipOpen) {
                                    refs.manualToolTipOpen = false;
                                    if (refs.toolTip) refs.toolTip.IsOpen(false);
                                }
                                refs.hasToolTipOpenCursor =
                                    GetCursorPos(&refs.toolTipOpenCursor) != FALSE;
                                refs.reopenToolTipOnMove = true;
                                if (refs.manualToolTipTimer) {
                                    refs.manualToolTipTimer.Interval(manualToolTipHoverDelay);
                                    refs.manualToolTipTimer.Start();
                                }
                            }
                        } catch (...) {}
                    }
                    e.Handled(true);
                });

            auto pointerEnteredToken = tappedElement.PointerEntered(
                [statePtr, accountIndex, percentTextVisibility](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::PointerRoutedEventArgs const&) {
                    if (g_unloading || percentTextVisibility != PercentTextVisibility::Hover ||
                        accountIndex >= (int)statePtr->accountRefs.size()) return;

                    try {
                        for (auto& percent : statePtr->accountRefs[accountIndex].percents) {
                            if (percent) percent.Opacity(0.9);
                        }
                    } catch (...) {}
                });

            // System XAML suppresses the automatic tooltip after a click until pointer re-entry.
            // A short stationary hover reopens it; further movement dismisses it.
            auto pointerMovedToken = tappedElement.PointerMoved(
                [statePtr, accountIndex, manualToolTipHoverDelay,
                 toolTipMoveThresholdX, toolTipMoveThresholdY](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::PointerRoutedEventArgs const& e) {
                    if (g_unloading || !statePtr->quotaGrid ||
                        accountIndex >= (int)statePtr->accountRefs.size()) return;

                    try {
                        if (e.Pointer().PointerDeviceType() !=
                            winrt::Windows::Devices::Input::PointerDeviceType::Mouse) return;

                        auto& refs = statePtr->accountRefs[accountIndex];
                        if (!refs.toolTip) return;

                        bool wasOpen = refs.toolTip.IsOpen();
                        POINT cursor{};
                        bool haveCursor = GetCursorPos(&cursor) != FALSE;
                        bool trackingHover = wasOpen || refs.reopenToolTipOnMove ||
                                             refs.manualToolTipOpen;
                        if (trackingHover && refs.hasToolTipOpenCursor && haveCursor &&
                            std::abs(cursor.x - refs.toolTipOpenCursor.x) < toolTipMoveThresholdX &&
                            std::abs(cursor.y - refs.toolTipOpenCursor.y) < toolTipMoveThresholdY) {
                            return;
                        }
                        if (refs.manualToolTipTimer) refs.manualToolTipTimer.Stop();
                        bool rearmAfterMove = wasOpen || refs.reopenToolTipOnMove ||
                                              refs.manualToolTipOpen;
                        refs.manualToolTipOpen = false;
                        refs.hasToolTipOpenCursor = false;
                        if (wasOpen) {
                            // Automatic and manual tooltips both dismiss on local pointer movement.
                            refs.toolTip.IsOpen(false);
                        }
                        if (!rearmAfterMove) return;

                        refs.reopenToolTipOnMove = true;
                        refs.hasToolTipOpenCursor = haveCursor;
                        if (haveCursor) refs.toolTipOpenCursor = cursor;
                        if (refs.manualToolTipTimer) {
                            refs.manualToolTipTimer.Interval(manualToolTipHoverDelay);
                            refs.manualToolTipTimer.Start();
                        }
                    } catch (...) {}
                });

            auto closeToolTip =
                [statePtr, accountIndex](winrt::Windows::Foundation::IInspectable const&,
                                         wuxi::PointerRoutedEventArgs const&) {
                    if (g_unloading || accountIndex >= (int)statePtr->accountRefs.size()) return;

                    auto& refs = statePtr->accountRefs[accountIndex];
                    refs.hasToolTipOpenCursor = false;
                    refs.reopenToolTipOnMove = false;
                    try {
                        if (refs.manualToolTipTimer) refs.manualToolTipTimer.Stop();
                    } catch (...) {}

                    refs.manualToolTipOpen = false;
                    try {
                        if (refs.toolTip && refs.toolTip.IsOpen()) refs.toolTip.IsOpen(false);
                    } catch (...) {}
                };
            auto pointerExitedToken = tappedElement.PointerExited(
                [closeToolTip, statePtr, accountIndex, percentTextVisibility](
                    winrt::Windows::Foundation::IInspectable const& sender,
                    wuxi::PointerRoutedEventArgs const& e) {
                    closeToolTip(sender, e);
                    if (g_unloading || percentTextVisibility != PercentTextVisibility::Hover ||
                        accountIndex >= (int)statePtr->accountRefs.size()) return;

                    try {
                        for (auto& percent : statePtr->accountRefs[accountIndex].percents) {
                            if (percent) percent.Opacity(0.0);
                        }
                    } catch (...) {}
                });
            auto pointerCaptureLostToken = tappedElement.PointerCaptureLost(closeToolTip);
            auto pointerCanceledToken = tappedElement.PointerCanceled(closeToolTip);
            state.pointerHandlers.push_back({tappedElement, tappedToken, pointerEnteredToken,
                                             pointerMovedToken, pointerExitedToken,
                                             pointerCaptureLostToken, pointerCanceledToken});

            MenuFlyout menu;
            if (visualTestMode) {
                MenuFlyoutItem exitTestItem;
                exitTestItem.Text(L"Exit visual test");
                auto exitTestToken = exitTestItem.Click(
                    [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                        HWND settingsWindow = g_settingsWindow.load();
                        if (settingsWindow &&
                            PostMessageW(settingsWindow, kExitVisualTestMessage, 0, 0)) {
                            return;
                        }

                        // Avoid tearing down this grid while its XAML Click handler is running.
                        if (g_visualTestMode.exchange(false, std::memory_order_acq_rel)) {
                            StartRetryInject(true);
                        }
                    });
                state.menuItemClickHandlers.push_back({exitTestItem, exitTestToken});
                menu.Items().Append(exitTestItem);
            } else {
                MenuFlyoutItem refreshAllItem;
                refreshAllItem.Text(L"Refresh all");
                auto refreshAllToken = refreshAllItem.Click(
                    [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                        QueueRefresh(0);
                    });
                state.menuItemClickHandlers.push_back({refreshAllItem, refreshAllToken});
                menu.Items().Append(refreshAllItem);
            }

            if (hasDashboard) {
                MenuFlyoutItem dashboardItem;
                dashboardItem.Text(L"Open dashboard");
                auto dashboardToken = dashboardItem.Click(
                    [accountIdentity](winrt::Windows::Foundation::IInspectable const&,
                                   RoutedEventArgs const&) {
                        OpenDashboardForIdentity(accountIdentity);
                    });
                state.menuItemClickHandlers.push_back({dashboardItem, dashboardToken});
                menu.Items().Append(dashboardItem);
            }

            if (!visualTestMode) {
                // Per-account show/hide checkboxes (checked = visible). Every column carries the
                // same list; toggling flips global state, persists, and re-syncs all instances.
                menu.Items().Append(MenuFlyoutSeparator{});
                for (size_t k = 0; k < accounts.size(); k++) {
                    ToggleMenuFlyoutItem toggle;
                    toggle.Text(accounts[k].label + L" - " +
                                ProviderDisplayName(accounts[k].provider));
                    toggle.IsChecked(!accounts[k].hidden);
                    int toggleIndex = (int)k;
                    uint64_t toggleIdentity = AccountIdentityHash(accounts[k]);
                    auto toggleToken = toggle.Click(
                        [toggleIdentity](winrt::Windows::Foundation::IInspectable const& sender,
                                         RoutedEventArgs const&) {
                            ToggleAccountVisibility(toggleIdentity, sender);
                        });
                    state.menuItemClickHandlers.push_back({toggle, toggleToken});
                    state.accountToggleItems.push_back({toggleIndex, toggle});
                    menu.Items().Append(toggle);
                }
            }

            // Sign in / Sign out submenus drive the mod's own OAuth per account.
            if (!visualTestMode) {
                MenuFlyoutSubItem signInSub;
                signInSub.Text(L"Sign in");
                MenuFlyoutSubItem signOutSub;
                signOutSub.Text(L"Sign out");
                for (size_t k = 0; k < accounts.size(); k++) {
                    if (accounts[k].provider == L"antigravity") continue;
                    std::wstring name = accounts[k].label + L" - " +
                                        ProviderDisplayName(accounts[k].provider);
                    uint64_t authIdentity = AccountIdentityHash(accounts[k]);

                    MenuFlyoutItem signInItem;
                    signInItem.Text(name);
                    auto signInToken = signInItem.Click(
                        [authIdentity](winrt::Windows::Foundation::IInspectable const&,
                                       RoutedEventArgs const&) {
                            StartLoginByIdentity(authIdentity);
                        });
                    state.menuItemClickHandlers.push_back({signInItem, signInToken});
                    signInSub.Items().Append(signInItem);

                    MenuFlyoutItem signOutItem;
                    signOutItem.Text(name);
                    auto signOutToken = signOutItem.Click(
                        [authIdentity](winrt::Windows::Foundation::IInspectable const&,
                                       RoutedEventArgs const&) {
                            if (!SignOutAccountByIdentity(authIdentity)) OpenSettingsWindow();
                        });
                    state.menuItemClickHandlers.push_back({signOutItem, signOutToken});
                    signOutSub.Items().Append(signOutItem);
                }
                if (signInSub.Items().Size() > 0) {
                    menu.Items().Append(MenuFlyoutSeparator{});
                    menu.Items().Append(signInSub);
                    menu.Items().Append(signOutSub);
                }
            }

            menu.Items().Append(MenuFlyoutSeparator{});
            MenuFlyoutItem settingsItem;
            settingsItem.Text(L"Settings...");
            auto settingsToken = settingsItem.Click(
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenSettingsWindow();
                });
            state.menuItemClickHandlers.push_back({settingsItem, settingsToken});

            MenuFlyoutItem historyItem;
            historyItem.Text(L"Usage history...");
            auto historyToken = historyItem.Click(
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenHistoryWindow();
                });
            state.menuItemClickHandlers.push_back({historyItem, historyToken});
            menu.Items().Append(historyItem);
            menu.Items().Append(settingsItem);

            tappedElement.ContextFlyout(menu);

            panel.Children().Append(col);
            state.accountRefs.push_back(std::move(refs));
        }

        root.Children().Append(panel);
        if (showPaceTicks) {
            DispatcherTimer paceTimer;
            paceTimer.Interval(std::chrono::minutes(1));
            auto paceTimerToken = paceTimer.Tick(
                [hWnd = state.hWnd](winrt::Windows::Foundation::IInspectable const&,
                                     winrt::Windows::Foundation::IInspectable const&) {
                    if (g_unloading) return;
                    try {
                        auto* uiState = FindUiState(hWnd);
                        if (uiState && uiState->quotaGrid) UpdateQuotaUi(*uiState);
                    } catch (...) {}
                });
            state.paceTimer = paceTimer;
            state.paceTimerToken = paceTimerToken;
            paceTimer.Start();
        }
        return root;
    } catch (...) {
        ClearQuotaEventState(state);
        Wh_Log(L"BuildQuotaGrid: exception");
        return nullptr;
    }
}

// src/taskbar/quota-update.h
// Applies settings and fetched data to an existing quota grid.

static void UpdateQuotaUi(QuotaUiInstance& state) {
    if (!state.quotaGrid) return;

    bool visualTestMode = g_visualTestMode.load(std::memory_order_acquire);
    if (state.buildVisualTestMode != visualTestMode) return;

    std::vector<AccountConfig> accounts;
    std::vector<AccountData> data;
    int intervalMin, barLength, barThickness, barGap, yellowThreshold, orangeThreshold, redThreshold;
    bool showPaceTicks, showExtraBarAmounts, showOpenAiExtraLimits, colorblindMode,
         showStaleWarning;
    BarLayout barLayout;
    BarMode barMode;
    PercentTextVisibility percentTextVisibility;
    PercentTextAlignment percentTextAlignment;
    ClickAction clickAction;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        accounts = g_settings.accounts;
        intervalMin = g_settings.pollMinutes;
        clickAction = g_settings.clickAction;
        barLayout = g_settings.barLayout;
        barMode = g_settings.barMode;
        barLength = g_settings.barLength;
        barThickness = g_settings.barThickness;
        barGap = g_settings.barGap;
        yellowThreshold = g_settings.yellowThreshold;
        orangeThreshold = g_settings.orangeThreshold;
        redThreshold = g_settings.redThreshold;
        showPaceTicks = g_settings.showPaceTicks;
        percentTextVisibility = g_settings.percentTextVisibility;
        percentTextAlignment = g_settings.percentTextAlignment;
        showExtraBarAmounts = g_settings.showExtraBarAmounts;
        showOpenAiExtraLimits = g_settings.showOpenAiExtraLimits;
        colorblindMode = g_settings.colorblindMode;
        showStaleWarning = g_settings.showStaleWarning;
        if (!visualTestMode) {
            std::lock_guard<std::mutex> dataLock(g_dataMutex);
            data = g_data;
        }
    }

    ULONGLONG now = NowUnixMs();
    if (visualTestMode) {
        BuildVisualTestSnapshot(yellowThreshold, orangeThreshold, redThreshold,
                                now, &accounts, &data);
    }
    if (data.size() != accounts.size()) return;
    if (state.accountRefs.size() != data.size()) return;
    if (state.applied.size() != data.size()) state.applied.assign(data.size(), {});

    bool refreshing;
    uint64_t refreshAccountIdentity;
    {
        std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
        refreshing = g_refreshing.load();
        refreshAccountIdentity = g_refreshAccountIdentity.load();
    }
    bool verticalBars = barLayout == BarLayout::Vertical;
    double physicalPixelDip = 1.0 / state.rasterizationScale;
    // Remaining mode shows the quota left (100 - used); n/a (pct < 0) stays unchanged.
    auto displayPct = [&](double pct) {
        return barMode == BarMode::Remaining && pct >= 0 ? std::clamp(100.0 - pct, 0.0, 100.0) : pct;
    };
    const wchar_t* remainingSuffix = barMode == BarMode::Remaining ? L" remaining" : L"";
    try {
        for (size_t i = 0; i < data.size(); i++) {
            const AccountData& d = data[i];
            AppliedState& ap = state.applied[i];
            const AccountUiRefs& ui = state.accountRefs[i];

            // Hidden accounts collapse their column (no space, not right-clickable) and skip
            // visual work; data stays in g_data and repaints on un-hide (which posts an update).
            int visible = accounts[i].hidden ? 0 : 1;
            if (visible != ap.visible) {
                if (ui.column) {
                    ui.column.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
                }
                ap.visible = visible;
            }
            if (!visible) continue;

            ULONGLONG staleAfterMs = (accounts[i].provider == L"antigravity" ? 1ULL :
                                      (ULONGLONG)intervalMin) * 2 * 60000;
            bool stale = d.stale || d.lastSuccessMs == 0 || now - d.lastSuccessMs > staleAfterMs;
            bool warn = showStaleWarning && stale && !d.error.empty();
            bool accountRefreshing = !visualTestMode && refreshing &&
                (!refreshAccountIdentity ||
                 AccountIdentityHash(accounts[i]) == refreshAccountIdentity);

            const std::array<const WindowUsage*, kQuotaBarCount> usage = {
                &d.win5h, &d.winWeek, &d.fableWeek, &d.extraUsage};
            int barMask = 0;
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (accounts[i].showBars[w] && (d.lastSuccessMs == 0 || usage[w]->pct >= 0)) {
                    barMask |= 1 << w;
                }
            }
            if (barMask != ap.barMask) {
                int visibleCount = 0;
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (barMask & (1 << w)) visibleCount++;
                }

                int visiblePosition = 0;
                double halfBarGap = barGap / 2.0;
                for (int w = 0; w < kQuotaBarCount; w++) {
                    bool barVisible = (barMask & (1 << w)) != 0;
                    if (ui.barItems[w]) {
                        ui.barItems[w].Visibility(barVisible ? Visibility::Visible :
                                                              Visibility::Collapsed);
                        if (barVisible) {
                            bool first = visiblePosition == 0;
                            bool last = visiblePosition == visibleCount - 1;
                            ui.barItems[w].Margin(verticalBars ?
                                Thickness{first ? 0.0 : halfBarGap, 0,
                                          last ? 0.0 : halfBarGap, 0} :
                                Thickness{0, first ? 1.0 : halfBarGap, 0,
                                          last ? 1.0 : halfBarGap});
                        }
                    }
                    if (barVisible) visiblePosition++;
                }
                if (ui.barArea) {
                    ui.barArea.Visibility(visibleCount ? Visibility::Visible :
                                                         Visibility::Collapsed);
                }
                if (ui.column) {
                    // Preserve a small context-menu target if every selected quota is unavailable.
                    double minimumHitSize = visibleCount ? 0.0 : std::max(barThickness, 8);
                    ui.column.MinWidth(minimumHitSize);
                    ui.column.MinHeight(minimumHitSize);
                }
                ap.barMask = barMask;
            }

            for (int w = 0; w < kQuotaBarCount; w++) {
                const WindowUsage& wu = *usage[w];
                double dispPct = displayPct(wu.pct);
                int px = dispPct > 0 ? std::clamp((int)std::lround(barLength * dispPct / 100.0), 2, barLength) : 0;
                // Color stays keyed to actual usage so depleting quota still reds out.
                auto c = UsageColor(wu.pct, stale, yellowThreshold, orangeThreshold, redThreshold,
                                    colorblindMode);
                uint32_t cv = ((uint32_t)c.A << 24) | ((uint32_t)c.R << 16) |
                              ((uint32_t)c.G << 8) | c.B;
                if (px != ap.fillPx[w] || cv != ap.fillColor[w]) {
                    if (ui.fills[w]) {
                        if (verticalBars) ui.fills[w].Height(px);
                        else ui.fills[w].Width(px);
                        ui.fills[w].Background(SolidColorBrush(c));
                    }
                    ap.fillPx[w] = px;
                    ap.fillColor[w] = cv;
                }

                bool paceVisible = showPaceTicks && ui.paceTicks[w] &&
                                   !stale && wu.pct >= 0 &&
                                   wu.resetUnixMs > wu.windowDurationMs &&
                                   wu.windowDurationMs > 0 &&
                                   now >= wu.resetUnixMs - wu.windowDurationMs;
                int pacePx = 0;
                if (paceVisible) {
                    double remainingFraction =
                        ((double)wu.resetUnixMs - (double)now) / wu.windowDurationMs;
                    double pacePct = barMode == BarMode::Remaining ? remainingFraction * 100.0 :
                                                                     (1.0 - remainingFraction) * 100.0;
                    pacePct = std::clamp(pacePct, 0.0, 100.0);

                    double paceThickness = verticalBars ? ui.paceTicks[w].Height() :
                                                          ui.paceTicks[w].Width();
                    double radius = std::clamp(std::max(1.0, barThickness / 2.0),
                                               paceThickness / 2.0,
                                               (barLength - paceThickness) / 2.0);
                    double center = std::clamp(barLength * pacePct / 100.0,
                                               radius, barLength - radius);
                    pacePx = (int)std::lround(center - paceThickness / 2.0);
                }

                int paceVisibleInt = paceVisible ? 1 : 0;
                if (paceVisibleInt != ap.paceVisible[w]) {
                    if (ui.paceTicks[w]) {
                        ui.paceTicks[w].Visibility(paceVisible ? Visibility::Visible :
                                                                Visibility::Collapsed);
                    }
                    ap.paceVisible[w] = paceVisibleInt;
                }
                if (paceVisible && pacePx != ap.pacePx[w]) {
                    if (ui.paceTicks[w]) {
                        ui.paceTicks[w].Margin(verticalBars ? Thickness{0, 0, 0, (double)pacePx} :
                                                               Thickness{(double)pacePx, 0, 0, 0});
                    }
                    ap.pacePx[w] = pacePx;
                }

                if (percentTextVisibility != PercentTextVisibility::Never) {
                    PercentTextAlignment appliedAlignment =
                        percentTextAlignment == PercentTextAlignment::Adaptive ?
                            (dispPct >= yellowThreshold ? PercentTextAlignment::Left :
                                                         PercentTextAlignment::Right) :
                            percentTextAlignment;
                    if ((int)appliedAlignment != ap.percentAlignments[w]) {
                        if (ui.percents[w]) {
                            if (appliedAlignment == PercentTextAlignment::Left) {
                                ui.percents[w].HorizontalAlignment(HorizontalAlignment::Left);
                                ui.percents[w].TextAlignment(TextAlignment::Left);
                            } else if (appliedAlignment == PercentTextAlignment::Center) {
                                ui.percents[w].HorizontalAlignment(HorizontalAlignment::Center);
                                ui.percents[w].TextAlignment(TextAlignment::Center);
                            } else {
                                ui.percents[w].HorizontalAlignment(HorizontalAlignment::Right);
                                ui.percents[w].TextAlignment(TextAlignment::Right);
                            }
                            if (auto translation = ui.percents[w].RenderTransform()
                                                       .try_as<TranslateTransform>()) {
                                translation.X(appliedAlignment == PercentTextAlignment::Left ?
                                                  4 * physicalPixelDip :
                                              appliedAlignment == PercentTextAlignment::Center ?
                                                  0 : -4 * physicalPixelDip);
                            }
                        }
                        ap.percentAlignments[w] = (int)appliedAlignment;
                    }

                    bool yellowOrOrange = !stale && wu.pct >= 0 &&
                        ((wu.pct >= yellowThreshold && wu.pct < orangeThreshold) ||
                         (wu.pct >= orangeThreshold && wu.pct < redThreshold));
                    int darkText = appliedAlignment == PercentTextAlignment::Left &&
                                   yellowOrOrange ? 1 : 0;
                    if (darkText != ap.percentDark[w]) {
                        if (ui.percents[w]) {
                            ui.percents[w].Foreground(SolidColorBrush(darkText ?
                                winrt::Windows::UI::Color{255, 0, 0, 0} :
                                winrt::Windows::UI::Color{255, 255, 255, 255}));
                        }
                        ap.percentDark[w] = darkText;
                    }

                    std::wstring percentText;
                    if (wu.pct >= 0) {
                        wchar_t text[24];
                        if (w == kExtraUsageBar && showExtraBarAmounts &&
                            d.extraLimitAmount >= 0) {
                            // Amount follows the bar mode: spent in used mode, left in
                            // remaining mode. Dollars for Anthropic, credits for OpenAI.
                            double amount = barMode == BarMode::Remaining ?
                                d.extraLimitAmount - d.extraUsedAmount : d.extraUsedAmount;
                            amount = std::max(amount, 0.0);
                            swprintf(text, ARRAYSIZE(text),
                                     accounts[i].provider == L"openai" ? L"%.0f" : L"$%.2f",
                                     amount);
                        } else {
                            swprintf(text, ARRAYSIZE(text), L"%.0f%%", dispPct);
                        }
                        percentText = text;
                    }
                    if (percentText != ap.percentTexts[w]) {
                        if (ui.percents[w]) ui.percents[w].Text(percentText);
                        ap.percentTexts[w] = std::move(percentText);
                    }
                }
            }

            std::wstring tip = (warn ? L"! " : L"") + accounts[i].label + L" - " +
                               ProviderDisplayName(accounts[i].provider);
            bool planIsSpark = d.plan.find(L"Spark") != std::wstring::npos ||
                               d.plan.find(L"spark") != std::wstring::npos;
            bool hideSparkPlan = accounts[i].provider == L"openai" && planIsSpark &&
                                 !showOpenAiExtraLimits;
            if (!d.plan.empty() && !hideSparkPlan) {
                tip += L" (" + d.plan + L")";
            }
            wchar_t line[160];
            if (d.win5h.pct >= 0) {
                PCWSTR label = L"5h";
                if (accounts[i].provider == L"antigravity") {
                    label = d.win5h.windowDurationMs == 3ULL * 60 * 60 * 1000 ? L"Gemini 3h" :
                            d.win5h.windowDurationMs == 60ULL * 60 * 1000 ? L"Gemini 1h" :
                            d.win5h.windowDurationMs == 0 ?
                                L"Gemini current limit" : L"Gemini 5h";
                }
                swprintf(line, ARRAYSIZE(line), L"\n%s: %.0f%%%s | resets %s", label,
                         displayPct(d.win5h.pct),
                         remainingSuffix, FormatReset(d.win5h.resetUnixMs).c_str());
                tip += line;
            }
            if (d.winWeek.pct >= 0) {
                PCWSTR label = accounts[i].provider == L"antigravity" ? L"Gemini week" : L"week";
                swprintf(line, ARRAYSIZE(line), L"\n%s: %.0f%%%s | resets %s", label,
                         displayPct(d.winWeek.pct),
                         remainingSuffix, FormatReset(d.winWeek.resetUnixMs).c_str());
                tip += line;
            }
            if (accounts[i].provider == L"antigravity" &&
                d.antigravityThirdParty5h.pct >= 0) {
                PCWSTR label =
                    d.antigravityThirdParty5h.windowDurationMs == 3ULL * 60 * 60 * 1000 ?
                        L"Claude/GPT 3h" :
                    d.antigravityThirdParty5h.windowDurationMs == 60ULL * 60 * 1000 ?
                        L"Claude/GPT 1h" :
                    d.antigravityThirdParty5h.windowDurationMs == 0 ?
                        L"Claude/GPT current limit" : L"Claude/GPT 5h";
                swprintf(line, ARRAYSIZE(line), L"\n%s: %.0f%%%s | resets %s", label,
                         displayPct(d.antigravityThirdParty5h.pct), remainingSuffix,
                         FormatReset(d.antigravityThirdParty5h.resetUnixMs).c_str());
                tip += line;
            }
            if (accounts[i].provider == L"antigravity" &&
                d.antigravityThirdPartyWeek.pct >= 0) {
                swprintf(line, ARRAYSIZE(line),
                         L"\nClaude/GPT week: %.0f%%%s | resets %s",
                         displayPct(d.antigravityThirdPartyWeek.pct), remainingSuffix,
                         FormatReset(d.antigravityThirdPartyWeek.resetUnixMs).c_str());
                tip += line;
            }
            if (d.fableWeek.pct >= 0) {
                swprintf(line, ARRAYSIZE(line), L"\nFable week: %.0f%%%s",
                         displayPct(d.fableWeek.pct), remainingSuffix);
                tip += line;
                if (d.fableWeek.resetUnixMs) {
                    tip += L" | resets " + FormatReset(d.fableWeek.resetUnixMs);
                }
            }
            bool openAiAccount = accounts[i].provider == L"openai";
            // The credits percentage is only meaningful relative to the user's max, so with
            // the credits bar unchecked the tooltip falls back to the plain balance.
            if (d.extraUsage.pct >= 0 &&
                (!openAiAccount || accounts[i].showBars[kExtraUsageBar])) {
                // The slot holds Anthropic monthly extra usage or OpenAI credits vs. the
                // user's max.
                PCWSTR label = openAiAccount ? L"credits" : L"extra usage";
                PCWSTR suffix = barMode == BarMode::Remaining ?
                    (openAiAccount ? L" remaining" : L" remaining this month") :
                    (openAiAccount ? L" used" : L" monthly");
                swprintf(line, ARRAYSIZE(line), L"\n%s: %.1f%%%s", label,
                         displayPct(d.extraUsage.pct), suffix);
                tip += line;
                if (d.extraLimitAmount >= 0) {
                    // Credits state the balance explicitly so the number reads the same in
                    // used and remaining modes.
                    if (openAiAccount) {
                        swprintf(line, ARRAYSIZE(line), L" (%.0f left of %.0f)",
                                 d.extraLimitAmount - d.extraUsedAmount, d.extraLimitAmount);
                    } else {
                        swprintf(line, ARRAYSIZE(line), L" ($%.2f / $%.2f spent)",
                                 d.extraUsedAmount, d.extraLimitAmount);
                    }
                    tip += line;
                }
                if (d.extraUsage.resetUnixMs) {
                    tip += L" | resets " + FormatReset(d.extraUsage.resetUnixMs);
                }
            } else if (openAiAccount && d.creditsUnlimited) {
                tip += L"\ncredits: unlimited";
            } else if (openAiAccount && d.creditsBalance >= 0) {
                swprintf(line, ARRAYSIZE(line), L"\ncredits: %.0f", d.creditsBalance);
                tip += line;
            } else if (openAiAccount && d.hasCredits) {
                tip += L"\ncredits: available";
            }
            if (showOpenAiExtraLimits && accounts[i].provider == L"openai" && !d.openAiExtraLimitLines.empty()) {
                tip += L"\n" + d.openAiExtraLimitLines;
            }
            if (!d.extraLines.empty()) tip += L"\n" + d.extraLines;
            if (!d.error.empty()) {
                tip += L"\nerror: " + d.error;
                if (d.retryDeadlineMs > now) {
                    ULONGLONG retrySec = (d.retryDeadlineMs - now + 999) / 1000;
                    ULONGLONG days = retrySec / (24 * 60 * 60);
                    ULONGLONG hours = (retrySec / (60 * 60)) % 24;
                    ULONGLONG mins = (retrySec / 60) % 60;
                    ULONGLONG secs = retrySec % 60;
                    wchar_t retry[64];
                    if (days > 0) {
                        if (hours > 0) swprintf(retry, ARRAYSIZE(retry), L"%llud %lluh", days, hours);
                        else swprintf(retry, ARRAYSIZE(retry), L"%llud", days);
                    } else if (hours > 0) {
                        if (mins > 0) swprintf(retry, ARRAYSIZE(retry), L"%lluh %llum", hours, mins);
                        else swprintf(retry, ARRAYSIZE(retry), L"%lluh", hours);
                    } else if (mins > 0) {
                        if (secs > 0) swprintf(retry, ARRAYSIZE(retry), L"%llum %llus", mins, secs);
                        else swprintf(retry, ARRAYSIZE(retry), L"%llum", mins);
                    } else {
                        swprintf(retry, ARRAYSIZE(retry), L"%llus", secs);
                    }
                    tip += L" - retry in ";
                    tip += retry;
                }
            }
            tip += L"\n" + FormatUpdated(d.lastSuccessMs, stale);
            tip += visualTestMode ? L" - visual test; click to open settings" :
                   accountRefreshing ? L" - refreshing..." :
                   d.needsLogin ? L" - click to sign in" :
                   clickAction == ClickAction::OpenDashboard && accounts[i].provider != L"antigravity"
                       ? L" - click to open dashboard" :
                   L" - click to refresh";

            if (tip != ap.tip) {
                // Keep the attached ToolTip object alive so an in-place refresh doesn't reset
                // ToolTipService's pointer-over state and require a leave/re-enter cycle.
                if (ui.toolTip) UpdateQuotaToolTip(ui.toolTip, tip, !d.error.empty());
                ap.tip = tip;
            }

            double columnOpacity = accountRefreshing ? 0.65 : 1.0;
            if (columnOpacity != ap.columnOpacity) {
                if (ui.column) ui.column.Opacity(columnOpacity);
                ap.columnOpacity = columnOpacity;
            }

            double labelOpacity = stale ? 0.45 : 0.8;
            std::wstring labelText = warn ? accounts[i].label + L"!" : accounts[i].label;
            if (labelOpacity != ap.labelOpacity || labelText != ap.labelText) {
                if (ui.label) {
                    ui.label.Opacity(labelOpacity);
                    ui.label.Text(labelText);
                }
                ap.labelOpacity = labelOpacity;
                ap.labelText = std::move(labelText);
            }
        }

        // Keep every instance's menu checkboxes in sync with the shared hidden state
        // (programmatic IsChecked does not raise Click).
        for (auto& [idx, item] : state.accountToggleItems) {
            if (!item || idx < 0 || idx >= (int)accounts.size()) continue;
            bool checked = !accounts[idx].hidden;
            if (item.IsChecked() != checked) item.IsChecked(checked);
        }
    } catch (...) {
        Wh_Log(L"UpdateQuotaUi: exception");
    }
}

// src/taskbar/inject.h
// Injects and removes quota grids, retries injection, and hooks taskbar startup.

static HANDLE g_injectEvent = nullptr;
static HANDLE g_retryThread = nullptr;
static std::mutex g_retryThreadMutex;
static std::atomic<bool> g_rebuildQuotaUiBeforeInject{false};
static std::atomic<ULONGLONG> g_nextInjectFailureLogMs{0};

static UINT GetQuotaCleanupMessage() {
    static const UINT message = RegisterWindowMessageW(L"Windhawk_CleanupQuotaUi_" WH_MOD_ID);
    return message;
}

static void RemoveQuotaChildren(Panel const& targetPanel, QuotaUiInstance& state) {
    if (!targetPanel) return;
    ClearQuotaEventState(state);

    for (int i = (int)targetPanel.Children().Size() - 1; i >= 0; --i) {
        auto fe = targetPanel.Children().GetAt(i).try_as<FrameworkElement>();
        if (fe && fe.Name() == kRootName) {
            try { targetPanel.Children().RemoveAt(i); } catch (...) {}
        }
    }
}

static void RemoveQuotaGridFromState(QuotaUiInstance& state) {
    if (state.ownerThreadId && state.ownerThreadId != GetCurrentThreadId()) {
        Wh_Log(L"Refusing XAML cleanup from a non-owner thread");
        return;
    }

    if (!state.injectionParent) {
        ClearQuotaEventState(state);
        state.quotaGrid = nullptr;
        state.quotaColumnDefinition = nullptr;
        state.applied.clear();
        return;
    }

    try {
        RemoveQuotaChildren(state.injectionParent, state);
        auto targetGrid = state.injectionParent.try_as<Grid>();
        int ownedCol = -1;
        if (targetGrid && state.quotaColumnDefinition) {
            auto definitions = targetGrid.ColumnDefinitions();
            for (uint32_t i = 0; i < definitions.Size(); ++i) {
                auto definition = definitions.GetAt(i);
                if (winrt::get_abi(definition) == winrt::get_abi(state.quotaColumnDefinition)) {
                    ownedCol = (int)i;
                    break;
                }
            }
        }

        // If another component already removed our definition, it also owns the resulting
        // child shifts. Never substitute a numeric column belonging to somebody else.
        if (targetGrid && ownedCol >= 0 && ownedCol < (int)targetGrid.ColumnDefinitions().Size()) {
            targetGrid.ColumnDefinitions().RemoveAt(ownedCol);
            for (uint32_t i = 0; i < targetGrid.Children().Size(); ++i) {
                auto child = targetGrid.Children().GetAt(i).try_as<FrameworkElement>();
                if (child) {
                    int childCol = Grid::GetColumn(child);
                    int childSpan = Grid::GetColumnSpan(child);
                    if (childCol > ownedCol) {
                        Grid::SetColumn(child, childCol - 1);
                    } else if (childCol < ownedCol && childCol + childSpan > ownedCol) {
                        Grid::SetColumnSpan(child, childSpan - 1);
                    }
                }
            }
        }
    } catch (...) {
        Wh_Log(L"RemoveQuotaGrid: exception");
    }

    state.quotaGrid = nullptr;
    state.injectionParent = nullptr;
    state.quotaColumnDefinition = nullptr;
    state.applied.clear();
}

static bool InjectQuotaGrid(HWND hWnd) {
    if (!hWnd) return false;
    DWORD ownerThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (!ownerThreadId || ownerThreadId != GetCurrentThreadId()) return false;
    if (auto* existingState = FindUiState(hWnd); existingState) {
        if (existingState->ownerThreadId != ownerThreadId) {
            Wh_Log(L"Taskbar HWND was reused by a different UI thread");
            return false;
        }
        ULONGLONG settingsGeneration;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            settingsGeneration = g_settingsGeneration;
        }
        if (existingState->quotaGrid &&
            existingState->buildSettingsGeneration == settingsGeneration &&
            existingState->buildVisualTestMode ==
                g_visualTestMode.load(std::memory_order_acquire)) {
            return true;
        }
    }

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
        // On a cold start the XamlRoot is ready before the system tray contents are realized
        // in the visual tree, so SystemTrayFrameGrid may be missing for the first attempts.
        // Bail and let the retry loop poll until it appears; never inject elsewhere, which
        // would render the bars on top of the clock/tray.
        if (!trayFrame) return fail(L"no SystemTrayFrameGrid");
        auto trayPanel = trayFrame.try_as<Panel>();
        auto trayGrid = trayFrame.try_as<Grid>();
        // Newer Windows taskbars keep the name but use a StackPanel. Don't assume other
        // panel types share its child-order layout semantics.
        if (!trayPanel || (!trayGrid && !trayFrame.try_as<StackPanel>())) {
            std::wstring reason = L"unsupported SystemTrayFrameGrid type: ";
            reason += winrt::get_class_name(trayFrame);
            return fail(reason.c_str());
        }

        state = FindUiState(hWnd);
        if (!state) {
            auto newState = std::make_unique<QuotaUiInstance>();
            newState->hWnd = hWnd;
            newState->ownerThreadId = ownerThreadId;
            {
                std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
                if (g_uiInstances) {
                    g_uiInstances->push_back(std::move(newState));
                    state = g_uiInstances->back().get();
                }
            }
            if (!state) return fail(L"UI state registry unavailable");
            if (!WindhawkUtils::SetWindowSubclassFromAnyThread(
                    hWnd, TaskbarWindowSubclassProc, 0)) {
                EraseUiState(hWnd);
                state = nullptr;
                return fail(L"window subclass failed");
            }
            state->windowSubclassed = true;
            // Remove a visual left by an earlier failed teardown, but don't guess which column
            // definition it owned.
            RemoveQuotaChildren(trayPanel, *state);
        } else {
            RemoveQuotaGridFromState(*state);
        }
        state->injectionParent = trayPanel;
        double rasterizationScale = xamlRoot.RasterizationScale();
        state->rasterizationScale = rasterizationScale > 0 ? rasterizationScale : 1.0;
        Grid quota = BuildQuotaGrid(*state);
        if (!quota) {
            RemoveQuotaGridFromState(*state);
            EraseUiState(hWnd);
            return fail(L"BuildQuotaGrid failed");
        }

        if (trayGrid) {
            ColumnDefinition newCol;
            newCol.Width({1.0, GridUnitType::Auto});
            state->quotaColumnDefinition = newCol;
            trayGrid.ColumnDefinitions().InsertAt(0, newCol);
            // Inserting at the left edge means no existing child can span across the new column.
            for (uint32_t i = 0; i < trayGrid.Children().Size(); ++i) {
                auto child = trayGrid.Children().GetAt(i).try_as<FrameworkElement>();
                if (child) Grid::SetColumn(child, Grid::GetColumn(child) + 1);
            }
            Grid::SetColumn(quota, 0);
            trayGrid.Children().Append(quota);
        } else {
            // StackPanel layout follows child order; no column is created or owned.
            trayPanel.Children().InsertAt(0, quota);
        }

        state->quotaGrid = quota;
        g_uiInjected.store(true, std::memory_order_release);
        state->applied.clear();
        UpdateQuotaUi(*state);
        Wh_Log(L"Injected quota bars into %s", winrt::get_class_name(trayPanel).c_str());
        return true;
    } catch (...) {
        if (state) {
            RemoveQuotaGridFromState(*state);
            EraseUiState(hWnd);
        }
        Wh_Log(L"InjectQuotaGrid: exception");
        return false;
    }
}

static void RemoveQuotaGrid(HWND hWnd) {
    auto* state = FindUiState(hWnd);
    if (!state) return;
    if (state->ownerThreadId != GetCurrentThreadId()) {
        Wh_Log(L"Refusing UI removal from a non-owner thread");
        return;
    }

    RemoveQuotaGridFromState(*state);
    EraseUiState(hWnd);
}

static void ReleaseQuotaUiState(HWND hWnd) {
    auto* state = FindUiState(hWnd);
    if (!state) return;
    if (state->ownerThreadId != GetCurrentThreadId()) {
        Wh_Log(L"Refusing UI state release from a non-owner thread");
        return;
    }

    ClearQuotaEventState(*state);
    state->quotaGrid = nullptr;
    state->injectionParent = nullptr;
    state->quotaColumnDefinition = nullptr;
    state->applied.clear();
    EraseUiState(hWnd);
}

static LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                  LPARAM lParam, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
        // Windhawk's wrapper has already removed the subclass. The XAML tree is dying, so only
        // revoke callbacks and release references; don't mutate its columns.
        if (auto* state = FindUiState(hWnd)) state->windowSubclassed = false;
        ReleaseQuotaUiState(hWnd);
    } else if (message == GetQuotaCleanupMessage()) {
        // Fallback when installing a temporary marshal hook fails during unload.
        if (auto* state = FindUiState(hWnd)) {
            if (state->windowSubclassed) {
                WindhawkUtils::RemoveWindowSubclassFromAnyThread(
                    hWnd, TaskbarWindowSubclassProc);
                state->windowSubclassed = false;
            }
            RemoveQuotaGrid(hWnd);
        }
    } else if (message == WM_DISPLAYCHANGE && !g_unloading) {
        StartRetryInject(true);
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

static void RemoveAllQuotaGrids(bool waitForCompletion = false) {
    std::vector<std::pair<HWND, DWORD>> windows;
    {
        std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
        if (!g_uiInstances) return;
        windows.reserve(g_uiInstances->size());
        for (auto& state : *g_uiInstances) {
            windows.push_back({state->hWnd, state->ownerThreadId});
        }
    }

    for (auto [hWnd, ownerThreadId] : windows) {
        if (!hWnd) continue;
        if (ownerThreadId == GetCurrentThreadId()) {
            if (IsWindow(hWnd)) RemoveQuotaGrid(hWnd);
            else ReleaseQuotaUiState(hWnd);
            continue;
        }

        DWORD liveThreadId = GetWindowThreadProcessId(hWnd, nullptr);
        bool removed = false;
        if (liveThreadId == ownerThreadId) {
            removed = RunFromWindowThread(hWnd, [](void* param) -> bool {
                RemoveQuotaGrid(static_cast<HWND>(param));
                return true;
            }, hWnd, waitForCompletion ? INFINITE : 2000);
            if (!removed && IsWindow(hWnd)) {
                if (waitForCompletion) {
                    SendMessageW(hWnd, GetQuotaCleanupMessage(), 0, 0);
                } else {
                    DWORD_PTR ignored = 0;
                    SendMessageTimeoutW(
                        hWnd, GetQuotaCleanupMessage(), 0, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_NOTIMEOUTIFNOTHUNG,
                        2000, &ignored);
                }
                removed = !FindUiState(hWnd);
            }
        } else {
            HWND ownerWindow = nullptr;
            EnumThreadWindows(ownerThreadId,
                [](HWND candidate, LPARAM param) CALLBACK -> BOOL {
                    *reinterpret_cast<HWND*>(param) = candidate;
                    return FALSE;
                }, reinterpret_cast<LPARAM>(&ownerWindow));
            if (ownerWindow) {
                removed = RunFromWindowThread(ownerWindow, [](void* param) -> bool {
                    ReleaseQuotaUiState(static_cast<HWND>(param));
                    return true;
                }, hWnd, waitForCompletion ? INFINITE : 2000);
            }
        }
        if (!removed || FindUiState(hWnd)) {
            Wh_Log(L"RemoveQuotaGrid marshal failed");
        }
    }
}

static LRESULT CALLBACK TopologyWindowProc(HWND hWnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    if (message == WM_DISPLAYCHANGE && !g_unloading) {
        g_rebuildQuotaUiBeforeInject = true;
        if (g_injectEvent) SetEvent(g_injectEvent);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

static DWORD WINAPI RetryInjectThreadProc(LPVOID) {
    static PCWSTR kTopologyWindowClass = L"AiQuotaTopology_" WH_MOD_ID;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = TopologyWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kTopologyWindowClass;
    bool classRegistered = RegisterClassExW(&windowClass) != 0;
    if (!classRegistered && GetLastError() == ERROR_CLASS_ALREADY_EXISTS &&
        UnregisterClassW(kTopologyWindowClass, instance)) {
        classRegistered = RegisterClassExW(&windowClass) != 0;
    }
    HWND topologyWindow = classRegistered ?
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kTopologyWindowClass, L"",
                        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr) : nullptr;

    HANDLE startHandles[2] = {g_stopEvent, g_injectEvent};
    auto waitWithMessages = [&](DWORD timeout) {
        DWORD result = MsgWaitForMultipleObjects(2, startHandles, FALSE, timeout, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0 + 2) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return result;
    };
    bool stopping = false;
    while (!g_unloading) {
        DWORD startWait = waitWithMessages(INFINITE);
        if (startWait == WAIT_OBJECT_0 || g_unloading) break;
        if (startWait == WAIT_OBJECT_0 + 2) continue;
        if (startWait != WAIT_OBJECT_0 + 1) continue;

        ULONGLONG settleUntil = 0;
        for (int attempt = 0; attempt < 600 && !g_unloading; attempt++) {
            if (g_rebuildQuotaUiBeforeInject.exchange(false)) {
                settleUntil = GetTickCount64() + 2000;
            }
            if (settleUntil) {
                ULONGLONG now = GetTickCount64();
                if (now < settleUntil) {
                    DWORD settleWait = waitWithMessages(
                        (DWORD)std::min<ULONGLONG>(100, settleUntil - now));
                    if (settleWait == WAIT_OBJECT_0 || g_unloading) {
                        stopping = true;
                        break;
                    }
                    if (settleWait == WAIT_OBJECT_0 + 1) attempt = -1;
                    else attempt--;  // The absolute settle deadline bounds these wakes.
                    continue;
                }
                RemoveAllQuotaGrids();
                settleUntil = 0;
            }
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
                bool injected = RunFromWindowThread(hWnd, [](void* param) -> bool {
                    return !g_unloading && InjectQuotaGrid(static_cast<HWND>(param));
                }, hWnd);
                allInjected = allInjected && injected;
            }

            DWORD waitMs = allInjected ? 0 : 100;
            DWORD retryWait = waitWithMessages(waitMs);
            if (retryWait == WAIT_OBJECT_0 || g_unloading) {
                stopping = true;
                break;
            }
            if (retryWait == WAIT_OBJECT_0 + 1) {
                attempt = -1;  // A newer rebuild request supersedes this attempt series.
                continue;
            }
            if (allInjected) break;
        }
        if (stopping) break;
    }
    if (topologyWindow) DestroyWindow(topologyWindow);
    if (classRegistered) UnregisterClassW(kTopologyWindowClass, instance);
    return 0;
}

static void StartRetryInject(bool removeExisting) {
    if (removeExisting) g_rebuildQuotaUiBeforeInject = true;
    DWORD err;
    {
        // The long-lived worker consumes every rebuild event. Keeping it alive closes the race
        // where an autosave removes the UI just as an older one-shot injection thread exits.
        std::lock_guard<std::mutex> lk(g_retryThreadMutex);
        if (g_unloading) return;

        if (g_retryThread && WaitForSingleObject(g_retryThread, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_retryThread);
            g_retryThread = nullptr;
        }
        if (!g_retryThread) {
            g_retryThread = CreateThread(nullptr, 0, RetryInjectThreadProc, nullptr, 0, nullptr);
            if (!g_retryThread) {
                err = GetLastError();
            }
        }
        if (g_retryThread) {
            if (g_injectEvent) SetEvent(g_injectEvent);
            return;
        }
    }

    Wh_Log(L"CreateThread RetryInjectThreadProc failed: %lu", err);

    if (removeExisting) {
        g_rebuildQuotaUiBeforeInject = false;
        RemoveAllQuotaGrids();
    }
    bool anyInjected = false;
    for (HWND hWnd : FindCurrentProcessTaskbarWnds()) {
        if (RunFromWindowThread(hWnd, [](void* param) -> bool {
                return !g_unloading && InjectQuotaGrid(static_cast<HWND>(param));
            }, hWnd)) {
            anyInjected = true;
        }
    }
    if (!anyInjected) {
        Wh_Log(L"InjectQuotaGrid fallback failed");
    }
}

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

// src/settings/storage.h
// Settings persistence: normalize, serialize, load, legacy import, publish, and apply.

static constexpr PCWSTR kSettingsStorageKey = L"settings_v1";
static constexpr PCWSTR kSettingsLengthStorageKey = L"settings_v1_len";
static constexpr int kSettingsStorageVersion = 1;

// Returns whether history IDs need saving before recording can begin.
static bool NormalizeSettings(Settings* s) {
    std::vector<AccountConfig> accounts;
    for (auto& a : s->accounts) {
        if (a.provider != L"anthropic" && a.provider != L"openai" &&
            a.provider != L"antigravity") {
            continue;
        }
        // Labels are stable identities; only the native editor trims deliberate edits.
        if (a.label.empty()) {
            a.label = a.provider == L"anthropic" ? L"A" :
                      a.provider == L"openai" ? L"O" : L"G";
        }
        if (a.provider != L"anthropic") a.showBars[kFableWeeklyBar] = false;
        if (a.provider != L"openai") a.creditsMax = 0;
        a.creditsMax = std::max(a.creditsMax, 0);
        // The extra-usage slot is Anthropic monthly extra usage or OpenAI credits vs. max.
        if (a.provider == L"antigravity" || (a.provider == L"openai" && a.creditsMax == 0)) {
            a.showBars[kExtraUsageBar] = false;
        }
        if (!a.showBars[kFiveHourBar] && !a.showBars[kWeeklyBar] &&
            !a.showBars[kFableWeeklyBar] &&
            !a.showBars[kExtraUsageBar]) {
            a.showBars[kFiveHourBar] = true;
        }
        bool duplicate = std::any_of(accounts.begin(), accounts.end(),
            [&](const AccountConfig& existing) {
                return existing.provider == a.provider && existing.label == a.label;
            });
        if (!duplicate) accounts.push_back(std::move(a));
    }
    s->accounts = std::move(accounts);

    bool historyIdsChanged = false;
    for (size_t i = 0; i < s->accounts.size(); i++) {
        auto& account = s->accounts[i];
        bool duplicateId = account.historyId && std::any_of(
            s->accounts.begin(), s->accounts.begin() + i,
            [&](const AccountConfig& previous) { return previous.historyId == account.historyId; });
        if (account.historyId && !duplicateId) continue;
        historyIdsChanged = true;
        account.historyId = 0;
        // Check every existing ID, including later accounts, to keep their histories stable.
        for (int attempt = 0; attempt < 32; attempt++) {
            uint64_t id = 0;
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&id), sizeof(id),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) break;
            if (id && std::none_of(s->accounts.begin(), s->accounts.end(),
                                  [id](const AccountConfig& a) { return a.historyId == id; })) {
                account.historyId = id;
                break;
            }
        }
        if (!account.historyId) {
            Wh_Log(L"History disabled: could not generate a unique account ID");
            s->historyEnabled = false;
        }
    }

    if (!s->accounts.empty() && std::all_of(s->accounts.begin(), s->accounts.end(),
                                            [](const AccountConfig& a) { return a.hidden; })) {
        s->accounts[0].hidden = false;
    }
    s->pollMinutes = std::clamp(s->pollMinutes > 0 ? s->pollMinutes : 10, 2, 24 * 60);
    s->historyRetentionDays = std::clamp(s->historyRetentionDays, 1, 3650);
    s->taskbarMonitorNumber = std::clamp(s->taskbarMonitorNumber > 0 ?
                                             s->taskbarMonitorNumber : 1, 1, 64);
    s->barLength = std::clamp(s->barLength > 0 ? s->barLength : 100, 10, 500);
    s->barThickness = std::clamp(s->barThickness > 0 ? s->barThickness : 8, 2, 50);
    s->labelFontSize = std::clamp(s->labelFontSize > 0 ? s->labelFontSize : 11, 6, 24);
    s->percentFontSize = std::clamp(s->percentFontSize > 0 ? s->percentFontSize : 9, 6, 24);
    s->accountMargin = std::clamp(s->accountMargin, 0, 500);
    s->labelGap = std::clamp(s->labelGap, 0, 500);
    s->barGap = std::clamp(s->barGap, 0, 500);
    s->rightMargin = std::clamp(s->rightMargin, 0, 500);
    if (s->labelPosition < LabelPosition::Hidden ||
        s->labelPosition > LabelPosition::Bottom) {
        s->labelPosition = LabelPosition::Left;
    }
    if (s->paceTickStyle < PaceTickStyle::Caret ||
        s->paceTickStyle > PaceTickStyle::Dot) {
        s->paceTickStyle = PaceTickStyle::Caret;
    }
    if (s->percentTextAlignment < PercentTextAlignment::Adaptive ||
        s->percentTextAlignment > PercentTextAlignment::Right) {
        s->percentTextAlignment = PercentTextAlignment::Adaptive;
    }
    if (s->percentTextVisibility < PercentTextVisibility::Never ||
        s->percentTextVisibility > PercentTextVisibility::Always) {
        s->percentTextVisibility = PercentTextVisibility::Hover;
    }
    s->yellowThreshold = std::clamp(s->yellowThreshold, 0, 100);
    s->orangeThreshold = std::clamp(s->orangeThreshold, s->yellowThreshold, 100);
    s->redThreshold = std::clamp(s->redThreshold, s->orangeThreshold, 100);
    s->paceTickColor &= 0x00FFFFFF;
    return historyIdsChanged;
}

static std::wstring SerializeSettings(const Settings& s) {
    try {
        JsonObject root;
        root.SetNamedValue(L"version", JsonValue::CreateNumberValue(kSettingsStorageVersion));
        JsonArray accounts;
        for (const auto& a : s.accounts) {
            JsonObject account;
            account.SetNamedValue(L"provider", JsonValue::CreateStringValue(winrt::hstring(a.provider)));
            account.SetNamedValue(L"label", JsonValue::CreateStringValue(winrt::hstring(a.label)));
            account.SetNamedValue(L"fiveHour", JsonValue::CreateBooleanValue(a.showBars[kFiveHourBar]));
            account.SetNamedValue(L"weekly", JsonValue::CreateBooleanValue(a.showBars[kWeeklyBar]));
            account.SetNamedValue(L"fableWeekly", JsonValue::CreateBooleanValue(a.showBars[kFableWeeklyBar]));
            account.SetNamedValue(L"extraUsage", JsonValue::CreateBooleanValue(a.showBars[kExtraUsageBar]));
            account.SetNamedValue(L"creditsMax", JsonValue::CreateNumberValue(a.creditsMax));
            account.SetNamedValue(L"hidden", JsonValue::CreateBooleanValue(a.hidden));
            account.SetNamedValue(L"recordHistory", JsonValue::CreateBooleanValue(a.recordHistory));
            // JSON numbers cannot round-trip all 64 bits of a history identity.
            wchar_t historyId[17];
            swprintf(historyId, ARRAYSIZE(historyId), L"%016llx",
                     (unsigned long long)a.historyId);
            account.SetNamedValue(L"historyId", JsonValue::CreateStringValue(historyId));
            accounts.Append(account.as<IJsonValue>());
        }
        root.SetNamedValue(L"accounts", accounts.as<IJsonValue>());

        auto setString = [&](PCWSTR name, PCWSTR value) {
            root.SetNamedValue(name, JsonValue::CreateStringValue(value));
        };
        auto setNumber = [&](PCWSTR name, int value) {
            root.SetNamedValue(name, JsonValue::CreateNumberValue(value));
        };
        auto setBool = [&](PCWSTR name, bool value) {
            root.SetNamedValue(name, JsonValue::CreateBooleanValue(value));
        };
        setString(L"monitorMode", s.taskbarMonitorMode == TaskbarMonitorMode::All ? L"all" :
                                   s.taskbarMonitorMode == TaskbarMonitorMode::Specific ? L"specific" : L"primary");
        setNumber(L"monitorNumber", s.taskbarMonitorNumber);
        setString(L"clickAction", s.clickAction == ClickAction::OpenDashboard ? L"dashboard" : L"refresh");
        setNumber(L"pollMinutes", s.pollMinutes);
        setNumber(L"historyRetentionDays", s.historyRetentionDays);
        setBool(L"skipHiddenPolling", s.skipHiddenPolling);
        setNumber(L"barLength", s.barLength);
        setNumber(L"barThickness", s.barThickness);
        setString(L"barLayout", s.barLayout == BarLayout::Vertical ? L"vertical" : L"stacked");
        setString(L"barMode", s.barMode == BarMode::Remaining ? L"remaining" : L"used");
        setBool(L"showPaceTicks", s.showPaceTicks);
        setString(L"paceTickStyle", s.paceTickStyle == PaceTickStyle::Line ? L"line" :
                                    s.paceTickStyle == PaceTickStyle::Notch ? L"notch" :
                                    s.paceTickStyle == PaceTickStyle::Dot ? L"dot" : L"caret");
        setNumber(L"paceTickColor", (int)s.paceTickColor);
        setString(L"labelPosition",
                  s.labelPosition == LabelPosition::Hidden ? L"hidden" :
                  s.labelPosition == LabelPosition::Top ? L"top" :
                  s.labelPosition == LabelPosition::Right ? L"right" :
                  s.labelPosition == LabelPosition::Bottom ? L"bottom" : L"left");
        setNumber(L"labelFontSize", s.labelFontSize);
        setNumber(L"percentFontSize", s.percentFontSize);
        setNumber(L"accountMargin", s.accountMargin);
        setNumber(L"labelGap", s.labelGap);
        setNumber(L"barGap", s.barGap);
        setNumber(L"rightMargin", s.rightMargin);
        setString(L"percentTextVisibility",
                  s.percentTextVisibility == PercentTextVisibility::Never ? L"never" :
                  s.percentTextVisibility == PercentTextVisibility::Always ? L"always" : L"hover");
        setString(L"percentTextAlignment",
                  s.percentTextAlignment == PercentTextAlignment::Left ? L"left" :
                  s.percentTextAlignment == PercentTextAlignment::Center ? L"center" :
                  s.percentTextAlignment == PercentTextAlignment::Right ? L"right" : L"adaptive");
        setBool(L"showBarLabels", s.showBarLabels);
        setBool(L"extraBarAmounts", s.showExtraBarAmounts);
        setBool(L"openAiExtraLimits", s.showOpenAiExtraLimits);
        setNumber(L"yellowThreshold", s.yellowThreshold);
        setNumber(L"orangeThreshold", s.orangeThreshold);
        setNumber(L"redThreshold", s.redThreshold);
        setBool(L"notifications", s.enableNotifications);
        setBool(L"colorblind", s.colorblindMode);
        setBool(L"staleWarning", s.showStaleWarning);
        return root.Stringify().c_str();
    } catch (...) {
        return {};
    }
}

static bool DeserializeSettings(const std::wstring& json, Settings* out,
                                 bool* historyIdsChanged) {
    try {
        JsonObject root = JsonObject::Parse(json);
        if ((int)GetNum(root, L"version", 0) != kSettingsStorageVersion) return false;
        Settings s;
        // Shared JSON never grants or revokes the caller's per-user consent.
        s.historyEnabled = out->historyEnabled;
        if (root.HasKey(L"accounts") &&
            root.GetNamedValue(L"accounts").ValueType() == JsonValueType::Array) {
            for (const auto& value : root.GetNamedArray(L"accounts")) {
                if (value.ValueType() != JsonValueType::Object) continue;
                JsonObject obj = value.GetObject();
                AccountConfig a;
                a.provider = GetStr(obj, L"provider");
                a.label = GetStr(obj, L"label");
                auto getBoolDefault = [&](PCWSTR name, bool defaultValue) {
                    if (!obj.HasKey(name)) return defaultValue;
                    auto v = obj.GetNamedValue(name);
                    return v.ValueType() == JsonValueType::Boolean ? v.GetBoolean() : defaultValue;
                };
                a.showBars[kFiveHourBar] = getBoolDefault(L"fiveHour", true);
                a.showBars[kWeeklyBar] = getBoolDefault(L"weekly", true);
                a.showBars[kFableWeeklyBar] = getBoolDefault(L"fableWeekly", false);
                a.showBars[kExtraUsageBar] = getBoolDefault(L"extraUsage", false);
                a.creditsMax = (int)GetNum(obj, L"creditsMax", 0);
                a.hidden = getBoolDefault(L"hidden", false);
                a.recordHistory = getBoolDefault(L"recordHistory", true);
                std::wstring historyId = GetStr(obj, L"historyId");
                if (historyId.size() == 16 &&
                    historyId.find_first_not_of(L"0123456789abcdefABCDEF") == std::wstring::npos) {
                    a.historyId = wcstoull(historyId.c_str(), nullptr, 16);
                }
                s.accounts.push_back(std::move(a));
            }
        }

        auto getBoolDefault = [&](PCWSTR name, bool defaultValue) {
            if (!root.HasKey(name)) return defaultValue;
            auto value = root.GetNamedValue(name);
            return value.ValueType() == JsonValueType::Boolean ? value.GetBoolean() : defaultValue;
        };
        std::wstring monitorMode = GetStr(root, L"monitorMode");
        s.taskbarMonitorMode = monitorMode == L"all" ? TaskbarMonitorMode::All :
                               monitorMode == L"specific" ? TaskbarMonitorMode::Specific :
                                                            TaskbarMonitorMode::Primary;
        s.taskbarMonitorNumber = (int)GetNum(root, L"monitorNumber", 1);
        s.clickAction = GetStr(root, L"clickAction") == L"dashboard" ?
                            ClickAction::OpenDashboard : ClickAction::Refresh;
        s.pollMinutes = (int)GetNum(root, L"pollMinutes", 10);
        double retentionDays = GetNum(root, L"historyRetentionDays", 90);
        s.historyRetentionDays = std::isfinite(retentionDays) ?
                                     (int)std::clamp(retentionDays, 1.0, 3650.0) : 90;
        s.skipHiddenPolling = getBoolDefault(L"skipHiddenPolling", true);
        s.barLength = (int)GetNum(root, L"barLength", 100);
        s.barThickness = (int)GetNum(root, L"barThickness", 8);
        s.barLayout = GetStr(root, L"barLayout") == L"vertical" ?
                          BarLayout::Vertical : BarLayout::Stacked;
        s.barMode = GetStr(root, L"barMode") == L"remaining" ?
                        BarMode::Remaining : BarMode::Used;
        s.showPaceTicks = getBoolDefault(L"showPaceTicks", true);
        std::wstring paceTickStyle = GetStr(root, L"paceTickStyle");
        s.paceTickStyle = paceTickStyle == L"line" ? PaceTickStyle::Line :
                          paceTickStyle == L"notch" ? PaceTickStyle::Notch :
                          paceTickStyle == L"dot" ? PaceTickStyle::Dot : PaceTickStyle::Caret;
        double paceTickColor = GetNum(root, L"paceTickColor", kDefaultPaceTickColor);
        s.paceTickColor = std::isfinite(paceTickColor) && paceTickColor >= 0 &&
                                  paceTickColor <= 0x00FFFFFF ?
                              (COLORREF)paceTickColor : kDefaultPaceTickColor;
        std::wstring labelPosition = GetStr(root, L"labelPosition");
        if (labelPosition == L"hidden") s.labelPosition = LabelPosition::Hidden;
        else if (labelPosition == L"top") s.labelPosition = LabelPosition::Top;
        else if (labelPosition == L"right") s.labelPosition = LabelPosition::Right;
        else if (labelPosition == L"bottom") s.labelPosition = LabelPosition::Bottom;
        else if (labelPosition == L"left") s.labelPosition = LabelPosition::Left;
        else {
            bool showLabels = getBoolDefault(L"showLabels", true);
            bool labelOnLeft = getBoolDefault(L"labelOnLeft", true);
            s.labelPosition = !showLabels ? LabelPosition::Hidden :
                              labelOnLeft ? LabelPosition::Left : LabelPosition::Top;
        }
        s.labelFontSize = (int)GetNum(root, L"labelFontSize", 11);
        s.percentFontSize = (int)GetNum(
            root, L"percentFontSize",
            std::max(8, std::clamp(s.labelFontSize > 0 ? s.labelFontSize : 11, 6, 24) - 2));
        s.accountMargin = (int)GetNum(root, L"accountMargin", 3);
        s.labelGap = (int)GetNum(root, L"labelGap", 3);
        s.barGap = (int)GetNum(root, L"barGap", 2);
        s.rightMargin = (int)GetNum(root, L"rightMargin", 4);
        std::wstring percentTextVisibility = GetStr(root, L"percentTextVisibility");
        if (percentTextVisibility == L"never") {
            s.percentTextVisibility = PercentTextVisibility::Never;
        } else if (percentTextVisibility == L"always") {
            s.percentTextVisibility = PercentTextVisibility::Always;
        } else if (percentTextVisibility == L"hover") {
            s.percentTextVisibility = PercentTextVisibility::Hover;
        } else if (root.HasKey(L"showPercentText") &&
                   root.GetNamedValue(L"showPercentText").ValueType() == JsonValueType::Boolean) {
            s.percentTextVisibility = root.GetNamedBoolean(L"showPercentText") ?
                                          PercentTextVisibility::Always :
                                          PercentTextVisibility::Never;
        }
        std::wstring percentTextAlignment = GetStr(root, L"percentTextAlignment");
        s.percentTextAlignment = percentTextAlignment == L"left" ? PercentTextAlignment::Left :
                                 percentTextAlignment == L"center" ? PercentTextAlignment::Center :
                                 percentTextAlignment == L"right" ? PercentTextAlignment::Right :
                                                                    PercentTextAlignment::Adaptive;
        s.showBarLabels = getBoolDefault(L"showBarLabels", false);
        s.showExtraBarAmounts = getBoolDefault(L"extraBarAmounts", false);
        // "showCodexSpark" is the pre-1.6.2 key for the same toggle.
        s.showOpenAiExtraLimits = getBoolDefault(L"openAiExtraLimits",
                                                 getBoolDefault(L"showCodexSpark", false));
        s.yellowThreshold = (int)GetNum(root, L"yellowThreshold", 50);
        s.orangeThreshold = (int)GetNum(root, L"orangeThreshold", 75);
        s.redThreshold = (int)GetNum(root, L"redThreshold", 90);
        s.enableNotifications = getBoolDefault(L"notifications", true);
        s.colorblindMode = getBoolDefault(L"colorblind", false);
        s.showStaleWarning = getBoolDefault(L"staleWarning", true);
        *historyIdsChanged = NormalizeSettings(&s);
        *out = std::move(s);
        return true;
    } catch (...) {
        return false;
    }
}

static bool SaveOwnedSettings(const Settings& settings) {
    std::wstring json = SerializeSettings(settings);
    if (json.empty() || json.size() >= 65535) {
        Wh_Log(L"Settings save failed: serialization or size limit");
        return false;
    }
    if (!Wh_SetStringValue(kSettingsStorageKey, json.c_str())) return false;
    if (!Wh_SetIntValue(kSettingsLengthStorageKey, (int)json.size())) {
        Wh_Log(L"Settings length sentinel save failed");
    }
    return true;
}

enum class OwnedSettingsLoadResult {
    Missing,
    Loaded,
    Invalid,
    Unreadable,
};

static OwnedSettingsLoadResult LoadOwnedSettings(Settings* out, bool* historyIdsChanged) {
    std::vector<wchar_t> buf(65536);
    size_t copied = Wh_GetStringValue(kSettingsStorageKey, buf.data(), buf.size());
    if (!copied || !buf[0]) {
        int storedLength = Wh_GetIntValue(kSettingsLengthStorageKey, 0);
        if (storedLength <= 0) return OwnedSettingsLoadResult::Missing;
        if (storedLength >= 1024 * 1024) {
            Wh_Log(L"Settings load failed: stored length is unreasonable");
            return OwnedSettingsLoadResult::Unreadable;
        }
        constexpr size_t kMaximumReadChars = 1024 * 1024;
        size_t nextBufferChars = std::max(buf.size() * 2, (size_t)storedLength + 1);
        while (nextBufferChars <= kMaximumReadChars) {
            buf.assign(nextBufferChars, L'\0');
            copied = Wh_GetStringValue(kSettingsStorageKey, buf.data(), buf.size());
            if (copied && buf[0]) break;
            if (nextBufferChars == kMaximumReadChars) break;
            nextBufferChars = std::min(nextBufferChars * 2, kMaximumReadChars);
        }
        if (!copied || !buf[0]) {
            Wh_Log(L"Settings load failed: stored configuration is unreadable");
            return OwnedSettingsLoadResult::Unreadable;
        }
    }
    if (!DeserializeSettings(buf.data(), out, historyIdsChanged)) {
        Wh_Log(L"Settings load failed: invalid stored configuration");
        Wh_SetStringValue(L"settings_v1_invalid", buf.data());
        return OwnedSettingsLoadResult::Invalid;
    }
    Wh_SetIntValue(kSettingsLengthStorageKey, (int)copied);
    return OwnedSettingsLoadResult::Loaded;
}

static bool LoadLegacySettings(Settings* out) {
    auto getText = [](PCWSTR name) {
        PCWSTR text = Wh_GetStringSetting(name);
        std::wstring value = text;
        Wh_FreeStringSetting(text);
        return value;
    };
    auto getIndexedText = [](PCWSTR name, int index) {
        PCWSTR text = Wh_GetStringSetting(name, index);
        std::wstring value = text;
        Wh_FreeStringSetting(text);
        return value;
    };
    wchar_t hiddenBuf[4096] = {};
    Wh_GetStringValue(L"hiddenAccounts", hiddenBuf, ARRAYSIZE(hiddenBuf));
    std::vector<wchar_t> tokenBuf(16384);
    Wh_GetStringValue(TokenStorageKey(AccountIdentityHash({L"anthropic", L"A"})).c_str(),
                      tokenBuf.data(), tokenBuf.size());
    bool foundDefaultToken = tokenBuf[0] != L'\0';
    tokenBuf[0] = L'\0';
    Wh_GetStringValue(TokenStorageKey(AccountIdentityHash({L"openai", L"O"})).c_str(),
                      tokenBuf.data(), tokenBuf.size());
    foundDefaultToken = foundDefaultToken || tokenBuf[0] != L'\0';

    bool found = !getIndexedText(L"accounts[%d].provider", 0).empty() ||
                  !getText(L"taskbarMonitorMode").empty() ||
                  !getText(L"barLength").empty() || hiddenBuf[0] || foundDefaultToken;
    if (!found) return false;

    Settings s;
    bool reachedLegacyAccountLimit = true;
    for (int i = 0; i < 64; i++) {
        std::wstring providerSetting = getIndexedText(L"accounts[%d].provider", i);
        std::wstring label = getIndexedText(L"accounts[%d].label", i);
        if (providerSetting.empty() && label.empty()) {
            reachedLegacyAccountLimit = false;
            break;
        }
        if (providerSetting.empty()) {
            Wh_Log(L"Ignoring incomplete legacy account at index %d", i);
            continue;
        }

        AccountConfig a;
        if (providerSetting.find(L"antigravity") != std::wstring::npos) a.provider = L"antigravity";
        else if (providerSetting.find(L"openai") != std::wstring::npos) a.provider = L"openai";
        else a.provider = L"anthropic";
        a.label = std::move(label);
        if (a.label.empty()) {
            a.label = a.provider == L"anthropic" ? L"A" :
                      a.provider == L"openai" ? L"O" : L"G";
        }
        auto getBool = [&](PCWSTR name, bool defaultValue) {
            std::wstring value = getIndexedText(name, i);
            if (value.empty()) return defaultValue;
            if (_wcsicmp(value.c_str(), L"true") == 0) return true;
            if (_wcsicmp(value.c_str(), L"false") == 0) return false;
            return _wtoi(value.c_str()) != 0;
        };
        a.showBars[kFiveHourBar] = getBool(L"accounts[%d].showFiveHourBar", true);
        a.showBars[kWeeklyBar] = getBool(L"accounts[%d].showWeeklyBar", true);
        a.showBars[kExtraUsageBar] = getBool(L"accounts[%d].showExtraUsageBar", false);
        s.accounts.push_back(std::move(a));
    }
    if (reachedLegacyAccountLimit &&
        (!getIndexedText(L"accounts[%d].provider", 64).empty() ||
         !getIndexedText(L"accounts[%d].label", 64).empty())) {
        Wh_Log(L"Ignoring legacy accounts after the 64-account migration limit");
    }
    if (s.accounts.empty()) {
        s.accounts.push_back({L"anthropic", L"A"});
        s.accounts.push_back({L"openai", L"O"});
    }

    std::vector<uint64_t> hiddenHashes;
    for (std::wstring rest = hiddenBuf; !rest.empty();) {
        size_t end = rest.find(L';');
        std::wstring token = rest.substr(0, end);
        if (!token.empty()) hiddenHashes.push_back(wcstoull(token.c_str(), nullptr, 16));
        if (end == std::wstring::npos) break;
        rest.erase(0, end + 1);
    }
    for (auto& a : s.accounts) {
        a.hidden = std::find(hiddenHashes.begin(), hiddenHashes.end(),
                             AccountIdentityHash(a)) != hiddenHashes.end();
    }

    auto getInt = [&](PCWSTR name, int defaultValue) {
        std::wstring value = getText(name);
        return value.empty() ? defaultValue : _wtoi(value.c_str());
    };
    auto getBool = [&](PCWSTR name, bool defaultValue) {
        std::wstring value = getText(name);
        if (value.empty()) return defaultValue;
        if (_wcsicmp(value.c_str(), L"true") == 0) return true;
        if (_wcsicmp(value.c_str(), L"false") == 0) return false;
        return _wtoi(value.c_str()) != 0;
    };
    std::wstring monitorMode = getText(L"taskbarMonitorMode");
    s.taskbarMonitorMode = monitorMode == L"all" ? TaskbarMonitorMode::All :
                           monitorMode == L"specific" ? TaskbarMonitorMode::Specific :
                                                        TaskbarMonitorMode::Primary;
    s.taskbarMonitorNumber = getInt(L"taskbarMonitorNumber", 1);
    s.clickAction = getText(L"clickAction") == L"open-dashboard" ?
                        ClickAction::OpenDashboard : ClickAction::Refresh;
    s.pollMinutes = getInt(L"pollIntervalMinutes", 10);
    s.barLength = getInt(L"barLength", 100);
    s.barThickness = getInt(L"barThickness", 8);
    s.barLayout = getText(L"barLayout") == L"vertical" ? BarLayout::Vertical : BarLayout::Stacked;
    s.barMode = getText(L"barMode") == L"remaining" ? BarMode::Remaining : BarMode::Used;
    s.showPaceTicks = getBool(L"showPaceTicks", true);
    s.labelPosition = !getBool(L"showLabels", true) ? LabelPosition::Hidden :
                      getBool(L"labelOnLeft", true) ? LabelPosition::Left : LabelPosition::Top;
    s.labelFontSize = getInt(L"labelFontSize", 11);
    s.percentFontSize = std::max(
        8, std::clamp(s.labelFontSize > 0 ? s.labelFontSize : 11, 6, 24) - 2);
    s.accountMargin = getInt(L"accountMargin", 3);
    s.labelGap = getInt(L"labelGap", 3);
    s.barGap = getInt(L"barGap", 2);
    s.rightMargin = getInt(L"rightMargin", 4);
    s.percentTextVisibility = getBool(L"showPercentText", false) ?
                                  PercentTextVisibility::Always : PercentTextVisibility::Never;
    s.showOpenAiExtraLimits = getBool(L"showCodexSparkInTooltip", false);
    s.yellowThreshold = getInt(L"yellowThreshold", 50);
    s.orangeThreshold = getInt(L"orangeThreshold", 75);
    s.redThreshold = getInt(L"redThreshold", 90);
    s.enableNotifications = getBool(L"enableNotifications", true);
    s.colorblindMode = getBool(L"colorblindMode", false);
    s.showStaleWarning = getBool(L"showStaleWarning", true);
    int oldBarLength = s.barLength;
    int oldAccountMargin = s.accountMargin;
    int oldLabelGap = s.labelGap;
    int oldBarGap = s.barGap;
    int oldRightMargin = s.rightMargin;
    NormalizeSettings(&s);
    auto logCeilingClamp = [](PCWSTR name, int oldValue, int newValue) {
        if (oldValue > newValue) {
            Wh_Log(L"Clamped legacy %s from %d to %d", name, oldValue, newValue);
        }
    };
    logCeilingClamp(L"barLength", oldBarLength, s.barLength);
    logCeilingClamp(L"accountMargin", oldAccountMargin, s.accountMargin);
    logCeilingClamp(L"labelGap", oldLabelGap, s.labelGap);
    logCeilingClamp(L"barGap", oldBarGap, s.barGap);
    logCeilingClamp(L"rightMargin", oldRightMargin, s.rightMargin);
    *out = std::move(s);
    return true;
}

static void PublishSettings(Settings s, uint64_t oldIdentity, uint64_t newIdentity) {
    std::lock_guard<std::mutex> lk(g_settingsMutex);
    std::lock_guard<std::mutex> lk2(g_dataMutex);
    bool historyBoundary = s.historyEnabled != g_settings.historyEnabled ||
                           s.skipHiddenPolling != g_settings.skipHiddenPolling ||
                           s.pollMinutes != g_settings.pollMinutes;
    for (const auto& account : s.accounts) {
        auto previous = std::find_if(g_settings.accounts.begin(), g_settings.accounts.end(),
            [&](const AccountConfig& old) { return account.historyId && old.historyId == account.historyId; });
        if (previous != g_settings.accounts.end() &&
            (account.recordHistory != previous->recordHistory || account.hidden != previous->hidden)) {
            historyBoundary = true;
        }
    }
    // Appearance edits/renames don't break graphs; recording and polling changes do.
    if (historyBoundary) ++g_historyContinuityGeneration;
    std::vector<AccountData> newData(s.accounts.size());
    std::vector<bool> oldDataUsed(g_data.size(), false);
    for (size_t i = 0; i < s.accounts.size(); i++) {
        for (size_t j = 0; j < g_settings.accounts.size() && j < g_data.size(); j++) {
            if (oldDataUsed[j]) continue;
            uint64_t oldHash = AccountIdentityHash(g_settings.accounts[j]);
            uint64_t newHash = AccountIdentityHash(s.accounts[i]);
            bool renamedAccount = oldIdentity != newIdentity &&
                                  oldHash == oldIdentity && newHash == newIdentity;
            if (oldHash == newHash || renamedAccount) {
                newData[i] = g_data[j];
                if (renamedAccount) newData[i].retryDeadlineMs = 0;
                ApplyCreditsMax(s.accounts[i], &newData[i]);
                oldDataUsed[j] = true;
                break;
            }
        }
    }
    g_settings = std::move(s);
    g_settingsGeneration++;
    g_historySettingsGeneration.store(g_settingsGeneration);
    g_data = std::move(newData);
}

enum class SettingsApplyResult {
    Failed,
    Unchanged,
    Changed,
};

static SettingsApplyResult ApplyOwnedSettings(Settings s, uint64_t oldIdentity = 0,
                                              uint64_t newIdentity = 0) {
    Settings previous;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        previous = g_settings;
    }
    const bool consentChanged = s.historyEnabled != previous.historyEnabled;
    const bool requestedHistoryEnabled = s.historyEnabled;
    NormalizeSettings(&s);
    // An ID-generation failure may disable runtime recording, but is not a consent edit.
    if (consentChanged && s.historyEnabled != requestedHistoryEnabled) return SettingsApplyResult::Failed;
    if (s == previous) return SettingsApplyResult::Unchanged;
    Settings sharedPrevious = previous;
    sharedPrevious.historyEnabled = s.historyEnabled;
    const bool saveShared = s != sharedPrevious || g_historyIdsPendingSave || g_settingsLoadError;
    if (saveShared && !SaveOwnedSettings(s)) return SettingsApplyResult::Failed;
    // Save shared changes first: a failed shared save must never grant consent. Consent-only
    // edits leave shared JSON untouched. Registry calls are short and take no history locks.
    if (consentChanged && !WriteHistoryConsent(s.historyEnabled)) {
        if (saveShared && !SaveOwnedSettings(previous)) {
            Wh_Log(L"Consent save failed and shared settings rollback failed; runtime settings unchanged");
        }
        return SettingsApplyResult::Failed;
    }
    if (saveShared) {
        g_historyIdsPendingSave = false;
        g_settingsLoadError = false;
    }
    PublishSettings(std::move(s), oldIdentity, newIdentity);
    return SettingsApplyResult::Changed;
}

static void FinishSettingsApply(bool wakeFetch = true) {
    RemoveAllQuotaGrids();
    StartRetryInject();
    if (wakeFetch && g_refreshEvent) SetEvent(g_refreshEvent);
}

static void SetVisualTestMode(bool enabled) {
    bool previous = g_visualTestMode.exchange(enabled, std::memory_order_acq_rel);
    if (previous == enabled || g_unloading) return;

    FinishSettingsApply(false);
    NotifySettingsWindowChanged();
}

static void LoadSettings() {
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartmentInitialized = true;
    } catch (...) {}

    Settings s;
    const bool historyConsent = ReadHistoryConsent();
    s.historyEnabled = historyConsent;
    g_settingsLoadError = false;
    g_historyIdsPendingSave = false;
    bool historyIdsChanged = false;
    OwnedSettingsLoadResult loadResult = LoadOwnedSettings(&s, &historyIdsChanged);
    if (loadResult == OwnedSettingsLoadResult::Missing) {
        bool imported = LoadLegacySettings(&s);
        if (!imported) s = Settings{};
        s.historyEnabled = historyConsent;
        NormalizeSettings(&s);
        if (SaveOwnedSettings(s)) {
            if (imported) Wh_Log(L"Imported legacy Windhawk settings");
            else Wh_Log(L"Initialized empty mod-owned settings");
        } else {
            s.historyEnabled = false;
            g_historyIdsPendingSave = !s.accounts.empty();
            Wh_Log(L"Settings initialization save failed; history disabled");
        }
    } else if (loadResult == OwnedSettingsLoadResult::Loaded && historyIdsChanged) {
        // Other Explorer processes must not race to persist independently generated IDs.
        // Startup can precede Shell_TrayWnd: retain IDs and opt-in, but fence recording until
        // the shell fetch thread saves them. Failed saves leave the same fence in place.
        g_historyIdsPendingSave = true;
        if (IsHistoryOwnerProcess()) {
            if (SaveOwnedSettings(s)) {
                g_historyIdsPendingSave = false;
            } else {
                Wh_Log(L"History ID migration save failed; recording deferred");
            }
        }
    } else if (loadResult == OwnedSettingsLoadResult::Invalid ||
               loadResult == OwnedSettingsLoadResult::Unreadable) {
        // Keep the invalid/future/unreadable blob intact. The setup tile opens with safe
        // defaults; readable invalid blobs were also backed up above.
        s = Settings{};
        s.historyEnabled = historyConsent;
        g_settingsLoadError = true;
    }
    PublishSettings(std::move(s));
    if (apartmentInitialized) winrt::uninit_apartment();
}

// src/settings/window-state.h
// Settings window thread state, control IDs, and window/editor state.

static HANDLE g_settingsWindowThread = nullptr;
static std::mutex g_settingsWindowMutex;
static std::atomic<bool> g_settingsWindowCancelRequested{false};

static constexpr UINT_PTR kSettingsAutosaveTimer = 1;

enum SettingsControlId {
    kSettingsPageAccounts = 1996,
    kSettingsPageLayout,
    kSettingsPageDisplay,
    kSettingsPageBehavior,
    kAccountList = 2001,
    kAccountAdd,
    kAccountEdit,
    kAccountRemove,
    kAccountMoveUp,
    kAccountMoveDown,
    kAccountToggleVisible,
    kAccountSignIn,
    kAccountSignOut,
    kResetPage,

    kMonitorMode = 2100,
    kMonitorNumber,
    kBarLayout,
    kBarMode,
    kBarLength,
    kBarThickness,
    kLabelFontSize,
    kPercentFontSize,
    kAccountMargin,
    kLabelGap,
    kBarGap,
    kRightMargin,

    kLabelPosition = 2200,
    kShowPaceTicks,
    kPaceTickStyle,
    kPaceTickColor,
    kShowBarLabels,
    kPercentTextVisibility,
    kPercentTextAlignment,
    kShowOpenAiExtraLimits,
    kColorblindMode,
    kShowStaleWarning,
    kYellowThreshold,
    kOrangeThreshold,
    kRedThreshold,
    kShowExtraBarAmounts,

    kClickAction = 2300,
    kPollPreset,
    kPollMinutes,
    kEnableNotifications,
    kHistoryEnabled,
    kHistoryRetentionDays,
    kSkipHiddenPolling,
    kOpenHistoryWindow,

    kVisualTestModeLayout = 2350,
    kVisualTestModeDisplay,

    kSettingsRowsPrevious = 2360,
    kSettingsRowsNext,

    kAccountProvider = 2400,
    kAccountLabel,
    kAccountFiveHour,
    kAccountWeekly,
    kAccountFableWeekly,
    kAccountExtraUsage,
    kAccountProviderLabel,
    kAccountLabelLabel,
    kAccountCreditsMax,
    kAccountRecordHistory,
    kAccountHistoryIdentityNote,
};

struct SettingsRow {
    HWND label = nullptr;
    HWND control = nullptr;
    HWND slider = nullptr;
    HWND spin = nullptr;
    HWND preview = nullptr;
    COLORREF previewColor = RGB(128, 128, 128);
    int minimum = 0;
    int maximum = 0;
};

struct SettingsWindowState {
    HWND hWnd = nullptr;
    std::array<HWND, 4> pageButtons{};
    HWND resetPageButton = nullptr;
    HWND previousRowsButton = nullptr;
    HWND rowsPageLabel = nullptr;
    HWND nextRowsButton = nullptr;
    HWND accountList = nullptr;
    HWND toolTip = nullptr;
    HWND colorDialog = nullptr;
    std::array<std::vector<HWND>, 4> pageControls;
    std::array<std::vector<SettingsRow>, 4> rows;
    HFONT font = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH inputBrush = nullptr;
    int currentPage = 0;
    std::array<int, 4> rowSubpages{};
    int lockedWindowHeight = 0;
    UINT dpi = 96;
    bool dark = false;
    bool updating = false;
    bool inSizeMove = false;
    std::array<COLORREF, 16> customColors{};
};

struct AccountEditorState {
    AccountConfig account;
    uint64_t originalIdentity = 0;
    HWND edit = nullptr;
    HWND toolTip = nullptr;
    HFONT font = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH inputBrush = nullptr;
    UINT dpi = 96;
    bool dark = false;
    bool accepted = false;
    bool done = false;
};

static constexpr PCWSTR kSettingsWindowClass = L"AiQuotaSettings_" WH_MOD_ID;
static constexpr PCWSTR kAccountEditorClass = L"AiQuotaAccountEditor_" WH_MOD_ID;

// src/settings/native.h
// Native window helpers shared with the history window: themed message boxes and color
// dialogs, DPI scaling, and dark mode.

struct SettingsMessageBoxContext {
    SettingsMessageBoxContext* previous = nullptr;
    HWND hWnd = nullptr;
    int forcedResult = IDCANCEL;
    bool forced = false;
};

static thread_local SettingsMessageBoxContext* g_settingsMessageBoxContext = nullptr;

static LRESULT CALLBACK SettingsMessageBoxCbtProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE && g_settingsMessageBoxContext &&
        !g_settingsMessageBoxContext->hWnd) {
        HWND hWnd = reinterpret_cast<HWND>(wParam);
        wchar_t className[16] = {};
        if (GetClassNameW(hWnd, className, ARRAYSIZE(className)) &&
            wcscmp(className, L"#32770") == 0) {
            g_settingsMessageBoxContext->hWnd = hWnd;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// Only settings-thread callers participate in owner-close handling.
static int SettingsMessageBoxW(HWND hWnd, LPCWSTR text, LPCWSTR caption, UINT type) {
    SettingsMessageBoxContext context;
    context.previous = g_settingsMessageBoxContext;
    switch (type & MB_TYPEMASK) {
        case MB_OK:
            context.forcedResult = IDOK;
            break;
        case MB_ABORTRETRYIGNORE:
            context.forcedResult = IDABORT;
            break;
        case MB_YESNO:
            context.forcedResult = IDNO;
            break;
        case MB_OKCANCEL:
        case MB_YESNOCANCEL:
        case MB_RETRYCANCEL:
        case MB_CANCELTRYCONTINUE:
            context.forcedResult = IDCANCEL;
            break;
    }
    if (g_unloading || g_settingsWindowCancelRequested || (hWnd && !IsWindow(hWnd))) {
        return context.forcedResult;
    }

    g_settingsMessageBoxContext = &context;
    HHOOK hook = SetWindowsHookExW(WH_CBT, SettingsMessageBoxCbtProc, nullptr,
                                   GetCurrentThreadId());
    if (!hook) {
        Wh_Log(L"Could not install settings MessageBox hook: %lu", GetLastError());
        g_settingsMessageBoxContext = context.previous;
        return context.forcedResult;
    }

    int result = MessageBoxW(hWnd, text, caption, type);
    if (hook) {
        if (!UnhookWindowsHookEx(hook)) {
            Wh_Log(L"Could not remove settings MessageBox hook: %lu", GetLastError());
        }
    }
    g_settingsMessageBoxContext = context.previous;
    return context.forced ? context.forcedResult : result;
}

static UINT_PTR CALLBACK SettingsColorHookProc(HWND hWnd, UINT message,
                                               WPARAM, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* chooser = reinterpret_cast<CHOOSECOLORW*>(lParam);
        auto* state = reinterpret_cast<SettingsWindowState*>(chooser->lCustData);
        if (state) state->colorDialog = hWnd;
    }
    return FALSE;
}

static int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, (int)dpi, 96);
}

static UINT WindowDpi(HWND hWnd) {
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using GetDpiForWindow_t = UINT(WINAPI*)(HWND);
        auto getDpi = reinterpret_cast<GetDpiForWindow_t>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpi && hWnd) return getDpi(hWnd);
    }
    HDC dc = GetDC(hWnd);
    UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hWnd, dc);
    return dpi ? dpi : 96;
}

static bool IsWindowsDarkMode() {
    HIGHCONTRASTW highContrast{sizeof(highContrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast),
                              &highContrast, 0) &&
        (highContrast.dwFlags & HCF_HIGHCONTRASTON)) {
        return false;
    }

    DWORD light = 1;
    DWORD size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

static void ApplyNativeWindowTheme(HWND hWnd, bool dark) {
    HMODULE uxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (!uxTheme) {
        uxTheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (uxTheme) {
        using SetWindowTheme_t = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
        auto setWindowTheme = reinterpret_cast<SetWindowTheme_t>(
            GetProcAddress(uxTheme, "SetWindowTheme"));
        if (setWindowTheme) {
            struct ThemeContext {
                SetWindowTheme_t setWindowTheme;
                bool dark;
            } context{setWindowTheme, dark};
            EnumChildWindows(hWnd, [](HWND child, LPARAM param) -> BOOL {
                auto* context = reinterpret_cast<ThemeContext*>(param);
                wchar_t className[32] = {};
                GetClassNameW(child, className, ARRAYSIZE(className));
                PCWSTR theme = context->dark ? L"DarkMode_Explorer" : L"Explorer";
                if (context->dark && _wcsicmp(className, L"ComboBox") == 0) {
                    theme = L"DarkMode_CFD";
                } else if (context->dark && _wcsicmp(className, WC_HEADERW) == 0) {
                    theme = L"DarkMode_ItemsView";
                }
                context->setWindowTheme(child, theme, nullptr);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&context));
        }
    }

    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm) {
        dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (dwm) {
        using DwmSetWindowAttribute_t = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto setAttribute = reinterpret_cast<DwmSetWindowAttribute_t>(
            GetProcAddress(dwm, "DwmSetWindowAttribute"));
        if (setAttribute) {
            BOOL enabled = dark;
            setAttribute(hWnd, 20, &enabled, sizeof(enabled));
        }
    }
}

// src/settings/controls.h
// Settings window control creation, pages, and layout.

static void RecreateSettingsVisuals(SettingsWindowState& state) {
    state.dark = IsWindowsDarkMode();
    if (state.font) DeleteObject(state.font);
    if (state.backgroundBrush) DeleteObject(state.backgroundBrush);
    if (state.inputBrush) DeleteObject(state.inputBrush);

    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(9, (int)state.dpi, 72);
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    state.font = CreateFontIndirectW(&lf);
    state.backgroundBrush = CreateSolidBrush(state.dark ? RGB(32, 32, 32) :
                                                          GetSysColor(COLOR_BTNFACE));
    state.inputBrush = CreateSolidBrush(state.dark ? RGB(43, 43, 43) :
                                                     GetSysColor(COLOR_WINDOW));
    EnumChildWindows(state.hWnd, [](HWND child, LPARAM param) -> BOOL {
        SendMessageW(child, WM_SETFONT, param, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(state.font));
    if (state.accountList) {
        COLORREF background = state.dark ? RGB(43, 43, 43) : GetSysColor(COLOR_WINDOW);
        ListView_SetBkColor(state.accountList, background);
        ListView_SetTextBkColor(state.accountList, background);
        ListView_SetTextColor(state.accountList,
                              state.dark ? RGB(235, 235, 235) : GetSysColor(COLOR_WINDOWTEXT));
    }
    ApplyNativeWindowTheme(state.hWnd, state.dark);
    RedrawWindow(state.hWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

static LRESULT CALLBACK AccountListSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                LPARAM lParam, UINT_PTR subclassId,
                                                DWORD_PTR referenceData) {
    auto* state = reinterpret_cast<SettingsWindowState*>(referenceData);
    if (message == WM_NOTIFY && state && state->dark) {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->hwndFrom == ListView_GetHeader(hWnd) &&
            header->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                RECT client{};
                GetClientRect(header->hwndFrom, &client);
                FillRect(draw->hdc, &client, state->inputBrush);
                return CDRF_NOTIFYITEMDRAW;
            }
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                int savedDc = SaveDC(draw->hdc);
                if (draw->uItemState & (CDIS_HOT | CDIS_SELECTED)) {
                    SetDCBrushColor(draw->hdc, RGB(62, 62, 62));
                    FillRect(draw->hdc, &draw->rc,
                             reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                } else {
                    FillRect(draw->hdc, &draw->rc, state->inputBrush);
                }

                wchar_t text[256] = {};
                HDITEMW item{};
                item.mask = HDI_TEXT | HDI_FORMAT;
                item.pszText = text;
                item.cchTextMax = ARRAYSIZE(text);
                Header_GetItem(header->hwndFrom, (int)draw->dwItemSpec, &item);

                SetBkMode(draw->hdc, TRANSPARENT);
                SetTextColor(draw->hdc, RGB(235, 235, 235));
                if (HFONT font = reinterpret_cast<HFONT>(
                        SendMessageW(header->hwndFrom, WM_GETFONT, 0, 0))) {
                    SelectObject(draw->hdc, font);
                }
                RECT textRect = draw->rc;
                int padding = ScaleForDpi(6, state->dpi);
                textRect.left += padding;
                textRect.right -= padding;
                UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
                format |= (item.fmt & HDF_RIGHT) ? DT_RIGHT :
                          (item.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT;
                DrawTextW(draw->hdc, text, -1, &textRect, format);

                SetDCPenColor(draw->hdc, RGB(75, 75, 75));
                SelectObject(draw->hdc, GetStockObject(DC_PEN));
                MoveToEx(draw->hdc, draw->rc.right - 1, draw->rc.top, nullptr);
                LineTo(draw->hdc, draw->rc.right - 1, draw->rc.bottom);
                MoveToEx(draw->hdc, draw->rc.left, draw->rc.bottom - 1, nullptr);
                LineTo(draw->hdc, draw->rc.right, draw->rc.bottom - 1);
                if (savedDc) RestoreDC(draw->hdc, savedDc);
                return CDRF_SKIPDEFAULT;
            }
        }
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, AccountListSubclassProc, subclassId);
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK SettingsNoEraseSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                    LPARAM lParam, UINT_PTR subclassId,
                                                    DWORD_PTR referenceData) {
    if (message == WM_ERASEBKGND) {
        auto* state = reinterpret_cast<SettingsWindowState*>(referenceData);
        if (state && state->dark) {
            // Preserve the previous frame until the native dark paint pass replaces it.
            return 1;
        }
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, SettingsNoEraseSubclassProc, subclassId);
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

static HWND CreateSettingsControl(SettingsWindowState& state, int page, PCWSTR className,
                                  PCWSTR text, DWORD style, DWORD exStyle, int id) {
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | style,
                                   0, 0, 1, 1, state.hWnd,
                                   reinterpret_cast<HMENU>((INT_PTR)id),
                                   GetModuleHandleW(nullptr), nullptr);
    if (control) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        state.pageControls[page].push_back(control);
    }
    return control;
}

static HWND AddSettingsRow(SettingsWindowState& state, int page, PCWSTR labelText,
                           PCWSTR className, DWORD style, DWORD exStyle, int id) {
    HWND label = CreateSettingsControl(state, page, L"STATIC", labelText,
                                       WS_VISIBLE | SS_LEFT | SS_NOTIFY, 0, -1);
    HWND control = CreateSettingsControl(state, page, className, L"",
                                         WS_VISIBLE | WS_TABSTOP | style, exStyle, id);
    state.rows[page].push_back({label, control});
    return control;
}

static HWND AddNumericRow(SettingsWindowState& state, int page, PCWSTR labelText, int id,
                          int minimum, int maximum, bool addSlider = false,
                          int sliderMaximum = -1) {
    HWND edit = AddSettingsRow(state, page, labelText, L"EDIT", ES_NUMBER,
                               WS_EX_CLIENTEDGE, id);
    SettingsRow& row = state.rows[page].back();
    row.minimum = minimum;
    row.maximum = maximum;
    row.spin = CreateSettingsControl(
        state, page, UPDOWN_CLASSW, L"",
        WS_VISIBLE | UDS_ARROWKEYS | UDS_SETBUDDYINT | UDS_NOTHOUSANDS, 0, -1);
    if (row.spin &&
        !SetWindowSubclass(row.spin, SettingsNoEraseSubclassProc, 0,
                           reinterpret_cast<DWORD_PTR>(&state))) {
        Wh_Log(L"Could not subclass settings spin control");
    }
    SendMessageW(row.spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(edit), 0);
    SendMessageW(row.spin, UDM_SETRANGE32, minimum, maximum);
    if (addSlider) {
        row.slider = CreateSettingsControl(
            state, page, TRACKBAR_CLASSW, L"",
            WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 0, -1);
        if (row.slider &&
            !SetWindowSubclass(row.slider, SettingsNoEraseSubclassProc, 0,
                               reinterpret_cast<DWORD_PTR>(&state))) {
            Wh_Log(L"Could not subclass settings slider");
        }
        int trackMaximum = sliderMaximum >= minimum ? sliderMaximum : maximum;
        SendMessageW(row.slider, TBM_SETRANGE, TRUE, MAKELPARAM(minimum, trackMaximum));
        SendMessageW(row.slider, TBM_SETPAGESIZE, 0,
                     std::max(1, (trackMaximum - minimum) / 10));
    }
    return edit;
}

static HWND AddThresholdRow(SettingsWindowState& state, PCWSTR labelText, int id) {
    HWND edit = AddNumericRow(state, 2, labelText, id, 0, 100);
    SettingsRow& row = state.rows[2].back();
    row.preview = CreateSettingsControl(state, 2, L"STATIC", L"",
                                        WS_VISIBLE | SS_OWNERDRAW, 0, -1);
    return edit;
}

static HWND AddSettingsCheck(SettingsWindowState& state, int page, PCWSTR text, int id) {
    HWND control = CreateSettingsControl(state, page, L"BUTTON", text,
                                         WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, id);
    state.rows[page].push_back({nullptr, control});
    return control;
}

static void AddComboItems(HWND combo, std::initializer_list<PCWSTR> items) {
    for (PCWSTR item : items) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
}

static void AddSettingsToolTip(SettingsWindowState& state, int id, PCWSTR text) {
    if (!state.toolTip || !text) return;
    HWND control = GetDlgItem(state.hWnd, id);
    SettingsRow* row = nullptr;
    for (auto& pageRows : state.rows) {
        auto it = std::find_if(pageRows.begin(), pageRows.end(),
                               [control](const SettingsRow& candidate) {
                                   return candidate.control == control;
                               });
        if (it != pageRows.end()) {
            row = &*it;
            break;
        }
    }
    const HWND targets[] = {row ? row->label : nullptr, control,
                            row ? row->slider : nullptr, row ? row->spin : nullptr};
    for (HWND target : targets) {
        if (!target) continue;
        TOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = state.hWnd;
        info.uId = reinterpret_cast<UINT_PTR>(target);
        info.lpszText = const_cast<PWSTR>(text);
        SendMessageW(state.toolTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
}

static void ShowSettingsPage(SettingsWindowState& state, int page) {
    state.currentPage = std::clamp(page, 0, 3);
    state.rowSubpages[state.currentPage] = 0;
    const PCWSTR resetLabels[] = {L"", L"Reset Layout to defaults...",
                                  L"Reset Display to defaults...",
                                  L"Reset Behavior to defaults..."};
    SetWindowTextW(state.resetPageButton, resetLabels[state.currentPage]);
    for (int i = 0; i < 4; i++) {
        SendMessageW(state.pageButtons[i], BM_SETCHECK,
                     i == state.currentPage ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    for (int i = 0; i < 4; i++) {
        for (HWND control : state.pageControls[i]) {
            SetWindowPos(control, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_NOREDRAW |
                             (i == state.currentPage ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        }
    }
}

static void FitSettingsWindowToContent(SettingsWindowState& state,
                                       bool reposition = true) {
    if (!state.hWnd || IsIconic(state.hWnd) || IsZoomed(state.hWnd)) return;

    // Grow from live row counts; constrained work areas paginate in the layout pass.
    size_t maximumRows = 0;
    for (int page = 1; page < 4; page++) {
        maximumRows = std::max(maximumRows, state.rows[page].size());
    }
    int desiredClientHeight = ScaleForDpi(62, state.dpi) +
                              (int)maximumRows * ScaleForDpi(37, state.dpi) +
                              ScaleForDpi(28, state.dpi) + ScaleForDpi(12, state.dpi);

    RECT windowRect{};
    GetWindowRect(state.hWnd, &windowRect);
    RECT adjustedRect{0, 0, ScaleForDpi(700, state.dpi), desiredClientHeight};
    DWORD style = (DWORD)GetWindowLongPtrW(state.hWnd, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongPtrW(state.hWnd, GWL_EXSTYLE);
    using AdjustWindowRectExForDpi_t = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjustWindowRectExForDpi = reinterpret_cast<AdjustWindowRectExForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    int desiredWindowHeight;
    if (adjustWindowRectExForDpi &&
        adjustWindowRectExForDpi(&adjustedRect, style, FALSE, exStyle, state.dpi)) {
        desiredWindowHeight = adjustedRect.bottom - adjustedRect.top;
    } else {
        RECT client{};
        GetClientRect(state.hWnd, &client);
        desiredWindowHeight = desiredClientHeight +
                              (windowRect.bottom - windowRect.top) - client.bottom;
    }

    HMONITOR monitor = MonitorFromWindow(state.hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    int width = std::min((int)(windowRect.right - windowRect.left), workWidth);
    int height = std::min(desiredWindowHeight, workHeight);
    int centerX = windowRect.left + (windowRect.right - windowRect.left) / 2;
    int centerY = windowRect.top + (windowRect.bottom - windowRect.top) / 2;
    int x = std::clamp(centerX - width / 2, (int)monitorInfo.rcWork.left,
                       (int)monitorInfo.rcWork.right - width);
    int y = std::clamp(centerY - height / 2, (int)monitorInfo.rcWork.top,
                       (int)monitorInfo.rcWork.bottom - height);
    state.lockedWindowHeight = height;
    if (!reposition && width == windowRect.right - windowRect.left &&
        height == windowRect.bottom - windowRect.top) {
        return;
    }
    SetWindowPos(state.hWnd, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void LayoutSettingsWindow(SettingsWindowState& state) {
    RECT client{};
    GetClientRect(state.hWnd, &client);
    int width = client.right;
    int height = client.bottom;
    int margin = ScaleForDpi(12, state.dpi);
    constexpr UINT positionFlags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW;
    auto setVisible = [&](HWND control, bool visible) {
        SetWindowPos(control, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | positionFlags |
                         (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    };
    int pageButtonWidth = std::max(1, (width - margin * 2) / 4);
    for (int i = 0; i < 4; i++) {
        int x = margin + i * pageButtonWidth;
        int buttonWidth = i == 3 ? std::max(1, width - margin - x) : pageButtonWidth;
        SetWindowPos(state.pageButtons[i], nullptr, x, margin,
                     buttonWidth, ScaleForDpi(32, state.dpi),
                     positionFlags);
    }

    int viewportTop = ScaleForDpi(52, state.dpi);
    int viewportBottom = height - margin;
    int settingsRowHeight = ScaleForDpi(37, state.dpi);
    int resetButtonHeight = ScaleForDpi(28, state.dpi);

    if (state.accountList) {
        int x = ScaleForDpi(26, state.dpi);
        int y = ScaleForDpi(54, state.dpi);
        int buttonHeight = ScaleForDpi(28, state.dpi);
        int gap = ScaleForDpi(6, state.dpi);
        bool compactButtons = width < ScaleForDpi(600, state.dpi);
        int buttonRows = compactButtons ? 2 : 1;
        int buttonY = viewportBottom - buttonHeight * buttonRows - gap * (buttonRows - 1);
        int listTop = std::max(y, viewportTop);
        int listBottom = std::min(buttonY - ScaleForDpi(10, state.dpi), viewportBottom);
        SetWindowPos(state.accountList, nullptr, x, listTop, width - x * 2,
                     std::max(1, listBottom - listTop), positionFlags);
        setVisible(state.accountList,
                   state.currentPage == 0 &&
                       listBottom - listTop >= ScaleForDpi(30, state.dpi));
        const int columnWidths[] = {80, 135, 110, 55, 125, 100};
        int desiredColumnsWidth = ScaleForDpi(605, state.dpi);
        int availableColumnsWidth = width - x * 2 - ScaleForDpi(4, state.dpi);
        double columnScale = desiredColumnsWidth > availableColumnsWidth ?
                                 (double)availableColumnsWidth / desiredColumnsWidth : 1.0;
        for (int i = 0; i < (int)ARRAYSIZE(columnWidths); i++) {
            ListView_SetColumnWidth(state.accountList, i,
                                    std::max(ScaleForDpi(48, state.dpi),
                                             (int)(ScaleForDpi(columnWidths[i], state.dpi) *
                                                   columnScale)));
        }
        const int ids[] = {kAccountAdd, kAccountEdit, kAccountRemove,
                           kAccountMoveUp, kAccountMoveDown, kAccountToggleVisible,
                           kAccountSignIn, kAccountSignOut};
        const int widths[] = {60, 60, 70, 42, 50, 72, 68, 68};
        int availableButtonsWidth = width - x * 2 - gap * 7;
        int desiredButtonsWidth = ScaleForDpi(490, state.dpi);
        double buttonScale = desiredButtonsWidth > availableButtonsWidth ?
                                 (double)availableButtonsWidth / desiredButtonsWidth : 1.0;
        int normalButtonX = x;
        for (int i = 0; i < 8; i++) {
            int buttonRow = compactButtons ? i / 4 : 0;
            int buttonColumn = compactButtons ? i % 4 : i;
            int buttonWidth = compactButtons ?
                std::max(1, (width - x * 2 - gap * 3) / 4) :
                std::max(ScaleForDpi(36, state.dpi),
                         (int)(ScaleForDpi(widths[i], state.dpi) * buttonScale));
            int buttonX = compactButtons ? x + buttonColumn * (buttonWidth + gap) :
                                           normalButtonX;
            HWND button = GetDlgItem(state.hWnd, ids[i]);
            int rowY = buttonY + buttonRow * (buttonHeight + gap);
            SetWindowPos(button, nullptr, buttonX, rowY,
                         buttonWidth, buttonHeight, positionFlags);
            setVisible(button, state.currentPage == 0 && rowY >= viewportTop &&
                                   rowY + buttonHeight <= viewportBottom);
            if (!compactButtons) normalButtonX += buttonWidth + gap;
        }
    }

    setVisible(state.resetPageButton, false);
    setVisible(state.previousRowsButton, false);
    setVisible(state.rowsPageLabel, false);
    setVisible(state.nextRowsButton, false);
    for (int page = 1; page < 4; page++) {
        int firstRow = 0;
        int lastRow = (int)state.rows[page].size();
        int subpageCount = 1;
        int rowsPerSubpage = std::max(
            1, (viewportBottom - ScaleForDpi(62, state.dpi) - resetButtonHeight) /
                   settingsRowHeight);
        if (page == state.currentPage) {
            subpageCount = std::max(
                1, ((int)state.rows[page].size() + rowsPerSubpage - 1) / rowsPerSubpage);
            state.rowSubpages[page] = std::clamp(state.rowSubpages[page], 0,
                                                 subpageCount - 1);
            firstRow = state.rowSubpages[page] * rowsPerSubpage;
            lastRow = std::min((int)state.rows[page].size(), firstRow + rowsPerSubpage);
        }
        int y = ScaleForDpi(62, state.dpi);
        int labelX = ScaleForDpi(28, state.dpi);
        int controlX = width < ScaleForDpi(620, state.dpi) ?
                           std::max(labelX + ScaleForDpi(120, state.dpi), width * 44 / 100) :
                           ScaleForDpi(300, state.dpi);
        int controlWidth = std::max(ScaleForDpi(100, state.dpi),
                                    width - controlX - ScaleForDpi(24, state.dpi));
        for (int rowIndex = 0; rowIndex < (int)state.rows[page].size(); rowIndex++) {
            const auto& row = state.rows[page][rowIndex];
            bool rowOnSubpage = page == state.currentPage && rowIndex >= firstRow &&
                                rowIndex < lastRow;
            if (!rowOnSubpage) {
                if (row.label) setVisible(row.label, false);
                setVisible(row.control, false);
                if (row.slider) setVisible(row.slider, false);
                if (row.spin) setVisible(row.spin, false);
                if (row.preview) setVisible(row.preview, false);
                continue;
            }
            bool sliderFits = row.slider && controlWidth >= ScaleForDpi(200, state.dpi);
            bool rowVisible = y >= viewportTop &&
                               y + ScaleForDpi(26, state.dpi) <= viewportBottom;
            if (row.label) {
                int labelRight = controlX - ScaleForDpi(12, state.dpi);
                if (row.preview) labelRight -= ScaleForDpi(28, state.dpi);
                SetWindowPos(row.label, nullptr, labelX, y + ScaleForDpi(4, state.dpi),
                             std::max(1, labelRight - labelX),
                             ScaleForDpi(22, state.dpi), positionFlags);
                if (row.preview) {
                    SetWindowPos(row.preview, nullptr,
                                 controlX - ScaleForDpi(24, state.dpi),
                                 y + ScaleForDpi(4, state.dpi),
                                 ScaleForDpi(18, state.dpi), ScaleForDpi(18, state.dpi),
                                 positionFlags);
                }
                if (row.spin) {
                    int spinWidth = ScaleForDpi(18, state.dpi);
                    int editWidth = ScaleForDpi(72, state.dpi);
                    int editX = controlX;
                    if (sliderFits) {
                        int sliderWidth = std::max(ScaleForDpi(80, state.dpi),
                                                   controlWidth - editWidth - spinWidth -
                                                       ScaleForDpi(12, state.dpi));
                        SetWindowPos(row.slider, nullptr, controlX, y, sliderWidth,
                                     ScaleForDpi(28, state.dpi), positionFlags);
                        editX += sliderWidth + ScaleForDpi(8, state.dpi);
                    }
                    SetWindowPos(row.control, nullptr, editX, y, editWidth,
                                 ScaleForDpi(26, state.dpi), positionFlags);
                    SetWindowPos(row.spin, nullptr, editX + editWidth, y, spinWidth,
                                 ScaleForDpi(26, state.dpi), positionFlags);
                } else {
                    wchar_t className[32] = {};
                    GetClassNameW(row.control, className, ARRAYSIZE(className));
                    int controlHeight = _wcsicmp(className, L"ComboBox") == 0 ?
                                            ScaleForDpi(220, state.dpi) :
                                            ScaleForDpi(26, state.dpi);
                    SetWindowPos(row.control, nullptr, controlX, y, controlWidth, controlHeight,
                                 positionFlags);
                }
                setVisible(row.label, rowVisible);
            } else {
                SetWindowPos(row.control, nullptr, labelX, y, width - labelX * 2,
                             ScaleForDpi(26, state.dpi), positionFlags);
            }
            setVisible(row.control, rowVisible);
            if (row.slider) setVisible(row.slider, rowVisible && sliderFits);
            if (row.spin) setVisible(row.spin, rowVisible);
            if (row.preview) setVisible(row.preview, rowVisible);
            y += settingsRowHeight;
        }
        if (page == state.currentPage) {
            int buttonWidth = std::max(
                1, std::min(width - labelX * 2, ScaleForDpi(200, state.dpi)));
            int buttonY = y;
            bool buttonVisible = buttonY >= viewportTop &&
                                 buttonY + resetButtonHeight <= viewportBottom;
            SetWindowPos(state.resetPageButton, nullptr,
                         width - labelX - buttonWidth, buttonY,
                         buttonWidth, resetButtonHeight, positionFlags);
            setVisible(state.resetPageButton, buttonVisible);
            if (subpageCount > 1) {
                int gap = ScaleForDpi(6, state.dpi);
                int navigationButtonWidth = ScaleForDpi(72, state.dpi);
                int pageLabelWidth = ScaleForDpi(48, state.dpi);
                int previousX = labelX;
                int labelPositionX = previousX + navigationButtonWidth + gap;
                int nextX = labelPositionX + pageLabelWidth + gap;
                SetWindowPos(state.previousRowsButton, nullptr,
                             previousX, buttonY, navigationButtonWidth,
                             resetButtonHeight, positionFlags);
                SetWindowPos(state.rowsPageLabel, nullptr,
                             labelPositionX, buttonY, pageLabelWidth,
                             resetButtonHeight, positionFlags);
                SetWindowPos(state.nextRowsButton, nullptr,
                             nextX, buttonY, navigationButtonWidth,
                             resetButtonHeight, positionFlags);
                wchar_t pageText[32];
                swprintf(pageText, ARRAYSIZE(pageText), L"%d / %d",
                         state.rowSubpages[page] + 1, subpageCount);
                SetWindowTextW(state.rowsPageLabel, pageText);
                bool previousEnabled = state.rowSubpages[page] > 0;
                bool nextEnabled = state.rowSubpages[page] + 1 < subpageCount;
                if (!previousEnabled && GetFocus() == state.previousRowsButton) {
                    SetFocus(state.nextRowsButton);
                } else if (!nextEnabled && GetFocus() == state.nextRowsButton) {
                    SetFocus(state.previousRowsButton);
                }
                EnableWindow(state.previousRowsButton, previousEnabled);
                EnableWindow(state.nextRowsButton, nextEnabled);
                setVisible(state.previousRowsButton, buttonVisible);
                setVisible(state.rowsPageLabel, buttonVisible);
                setVisible(state.nextRowsButton, buttonVisible);
            }
        }
    }
    for (HWND pageButton : state.pageButtons) {
        SetWindowPos(pageButton, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
    RedrawWindow(state.hWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// src/settings/bindings.h
// Two-way sync between the settings controls and the edited settings.

static int SelectedAccountIndex(const SettingsWindowState& state) {
    if (!state.accountList) return -1;
    return ListView_GetNextItem(state.accountList, -1, LVNI_SELECTED);
}

static uint64_t SelectedAccountIdentity(const SettingsWindowState& state) {
    int index = SelectedAccountIndex(state);
    std::lock_guard<std::mutex> lk(g_settingsMutex);
    return index >= 0 && index < (int)g_settings.accounts.size() ?
               AccountIdentityHash(g_settings.accounts[index]) : 0;
}

static void UpdateAccountButtons(SettingsWindowState& state) {
    int index = SelectedAccountIndex(state);
    bool selected = index >= 0;
    AccountConfig account;
    int accountCount = 0;
    int visibleCount = 0;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        accountCount = (int)g_settings.accounts.size();
        for (const auto& candidate : g_settings.accounts) {
            if (!candidate.hidden) visibleCount++;
        }
        if (selected && index < accountCount) account = g_settings.accounts[index];
        else selected = false;
    }
    bool loginInProgress = g_loginInProgress.load();
    EnableWindow(GetDlgItem(state.hWnd, kAccountEdit), selected && !loginInProgress);
    EnableWindow(GetDlgItem(state.hWnd, kAccountRemove), selected && !loginInProgress);
    EnableWindow(GetDlgItem(state.hWnd, kAccountMoveUp), selected && index > 0);
    EnableWindow(GetDlgItem(state.hWnd, kAccountMoveDown),
                 selected && index + 1 < accountCount);
    HWND visibilityButton = GetDlgItem(state.hWnd, kAccountToggleVisible);
    EnableWindow(visibilityButton, selected && (account.hidden || visibleCount > 1));
    SetWindowTextW(visibilityButton, selected && account.hidden ? L"Show" : L"Hide");

    bool oauth = selected && account.provider != L"antigravity";
    bool hasToken = false;
    if (oauth) {
        StoredToken token;
        hasToken = LoadStoredToken(AccountIdentityHash(account), &token);
    }
    bool selectedSigningIn = selected && loginInProgress &&
                             AccountIdentityHash(account) == g_loginAccountIdentity.load();
    SetWindowTextW(GetDlgItem(state.hWnd, kAccountSignIn),
                   selectedSigningIn ? L"Signing in..." :
                   hasToken ? L"Re-sign" : L"Sign in");
    EnableWindow(GetDlgItem(state.hWnd, kAccountSignIn),
                 oauth && !loginInProgress);
    EnableWindow(GetDlgItem(state.hWnd, kAccountSignOut), oauth && hasToken);
}

static void RefreshAccountList(SettingsWindowState& state) {
    std::vector<AccountConfig> accounts;
    bool historyEnabled;
    bool skipHiddenPolling;
    bool historyIdsPendingSave;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        accounts = g_settings.accounts;
        historyEnabled = g_settings.historyEnabled;
        skipHiddenPolling = g_settings.skipHiddenPolling;
        historyIdsPendingSave = g_historyIdsPendingSave.load();
    }
    uint64_t selectedIdentity = 0;
    int selected = SelectedAccountIndex(state);
    if (selected >= 0) {
        LVITEMW selectedItem{};
        selectedItem.mask = LVIF_PARAM;
        selectedItem.iItem = selected;
        if (ListView_GetItem(state.accountList, &selectedItem)) {
            selectedIdentity = (uint64_t)selectedItem.lParam;
        }
    }
    ListView_DeleteAllItems(state.accountList);
    int restoredSelection = -1;
    bool loginInProgress = g_loginInProgress.load();
    uint64_t loginIdentity = g_loginAccountIdentity.load();
    for (size_t i = 0; i < accounts.size(); i++) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = (int)i;
        item.pszText = const_cast<PWSTR>(accounts[i].label.c_str());
        item.lParam = (LPARAM)AccountIdentityHash(accounts[i]);
        ListView_InsertItem(state.accountList, &item);
        if ((uint64_t)item.lParam == selectedIdentity) restoredSelection = (int)i;
        ListView_SetItemText(state.accountList, (int)i, 1,
                             const_cast<PWSTR>(ProviderDisplayName(accounts[i].provider)));
        std::wstring bars;
        if (accounts[i].showBars[kFiveHourBar]) bars += L"5h";
        if (accounts[i].showBars[kWeeklyBar]) bars += bars.empty() ? L"Week" : L", Week";
        if (accounts[i].showBars[kFableWeeklyBar]) {
            bars += bars.empty() ? L"Fable" : L", Fable";
        }
        if (accounts[i].showBars[kExtraUsageBar]) {
            PCWSTR name = accounts[i].provider == L"openai" ? L"Credits" : L"Extra";
            if (!bars.empty()) bars += L", ";
            bars += name;
        }
        ListView_SetItemText(state.accountList, (int)i, 2, const_cast<PWSTR>(bars.c_str()));
        std::wstring visible = accounts[i].hidden ? L"No" : L"Yes";
        ListView_SetItemText(state.accountList, (int)i, 3, visible.data());
        std::wstring status;
        if (loginInProgress && AccountIdentityHash(accounts[i]) == loginIdentity) {
            status = L"Signing in...";
        } else if (accounts[i].provider == L"antigravity") {
            status = L"Uses local session";
        } else {
            StoredToken token;
            status = LoadStoredToken(AccountIdentityHash(accounts[i]), &token) ?
                         L"Signed in" : L"Not signed in";
        }
        ListView_SetItemText(state.accountList, (int)i, 4, const_cast<PWSTR>(status.c_str()));
        PCWSTR recorded = !accounts[i].recordHistory ? L"No" :
                          !historyEnabled ? L"User off" :
                          historyIdsPendingSave ? L"Pending save" :
                          !accounts[i].historyId ? L"Unavailable" :
                          accounts[i].hidden && skipHiddenPolling ? L"Paused" :
                          g_historyStorageError ? L"Storage error" : L"Yes";
        ListView_SetItemText(state.accountList, (int)i, 5, const_cast<PWSTR>(recorded));
    }
    if (restoredSelection >= 0) {
        ListView_SetItemState(state.accountList, restoredSelection, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    UpdateAccountButtons(state);
}

static void SetControlInt(SettingsWindowState& state, int id, int value) {
    wchar_t text[32];
    swprintf(text, ARRAYSIZE(text), L"%d", value);
    HWND control = GetDlgItem(state.hWnd, id);
    SetWindowTextW(control, text);
    for (const auto& pageRows : state.rows) {
        for (const auto& row : pageRows) {
            if (row.control != control) continue;
            if (row.spin) SendMessageW(row.spin, UDM_SETPOS32, 0, value);
            if (row.slider) {
                int maximum = (int)SendMessageW(row.slider, TBM_GETRANGEMAX, 0, 0);
                SendMessageW(row.slider, TBM_SETPOS, TRUE,
                             std::clamp(value, row.minimum, maximum));
            }
            return;
        }
    }
}

static int GetControlInt(HWND hWnd, int id, int fallback) {
    wchar_t text[64] = {};
    GetWindowTextW(GetDlgItem(hWnd, id), text, ARRAYSIZE(text));
    wchar_t* end = nullptr;
    long value = wcstol(text, &end, 10);
    return end && end != text ? (int)value : fallback;
}

static SettingsRow* FindSettingsRow(SettingsWindowState& state, HWND control) {
    for (auto& pageRows : state.rows) {
        for (auto& row : pageRows) {
            if (row.control == control || row.slider == control || row.spin == control) {
                return &row;
            }
        }
    }
    return nullptr;
}

static void RefreshColorPreviews(SettingsWindowState& state, const Settings& settings) {
    if (SettingsRow* row = FindSettingsRow(
            state, GetDlgItem(state.hWnd, kPaceTickColor))) {
        row->previewColor = settings.paceTickColor;
        if (row->preview) InvalidateRect(row->preview, nullptr, TRUE);
    }
    const int ids[] = {kYellowThreshold, kOrangeThreshold, kRedThreshold};
    const COLORREF normalColors[] = {RGB(0xFD, 0xD8, 0x35), RGB(0xFB, 0x8C, 0x00),
                                     RGB(0xE5, 0x39, 0x35)};
    const COLORREF colorblindColors[] = {RGB(0x56, 0xB4, 0xE9), RGB(0xE6, 0x9F, 0x00),
                                         RGB(0xD5, 0x5E, 0x00)};
    for (int i = 0; i < 3; i++) {
        SettingsRow* row = FindSettingsRow(state, GetDlgItem(state.hWnd, ids[i]));
        if (!row || !row->preview) continue;
        row->previewColor = settings.colorblindMode ? colorblindColors[i] : normalColors[i];
        InvalidateRect(row->preview, nullptr, TRUE);
    }
}

static void EnableSettingsRow(SettingsWindowState& state, int id, bool enabled) {
    HWND control = GetDlgItem(state.hWnd, id);
    for (const auto& pageRows : state.rows) {
        for (const auto& row : pageRows) {
            if (row.control != control) continue;
            if (row.label) EnableWindow(row.label, enabled);
            EnableWindow(row.control, enabled);
            if (row.slider) EnableWindow(row.slider, enabled);
            if (row.spin) EnableWindow(row.spin, enabled);
            return;
        }
    }
}

static void UpdateDependentSettingsControls(SettingsWindowState& state) {
    EnableSettingsRow(state, kMonitorNumber,
                      SendDlgItemMessageW(state.hWnd, kMonitorMode,
                                          CB_GETCURSEL, 0, 0) == 2);
    bool labelsVisible = SendDlgItemMessageW(state.hWnd, kLabelPosition,
                                             CB_GETCURSEL, 0, 0) !=
                         (LRESULT)LabelPosition::Hidden;
    EnableSettingsRow(state, kLabelFontSize, labelsVisible);
    EnableSettingsRow(state, kLabelGap, labelsVisible);
    LRESULT percentTextVisibility = SendDlgItemMessageW(
        state.hWnd, kPercentTextVisibility, CB_GETCURSEL, 0, 0);
    bool barTextVisible =
        SendDlgItemMessageW(state.hWnd, kShowBarLabels, BM_GETCHECK, 0, 0) == BST_CHECKED ||
        percentTextVisibility > (LRESULT)PercentTextVisibility::Never;
    EnableSettingsRow(state, kPercentFontSize, barTextVisible);
    EnableSettingsRow(state, kPercentTextAlignment,
                      percentTextVisibility > (LRESULT)PercentTextVisibility::Never);
    EnableSettingsRow(state, kShowExtraBarAmounts,
                      percentTextVisibility > (LRESULT)PercentTextVisibility::Never);
    bool paceTicksVisible = SendDlgItemMessageW(state.hWnd, kShowPaceTicks,
                                                BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableSettingsRow(state, kPaceTickStyle, paceTicksVisible);
    EnableSettingsRow(state, kPaceTickColor, paceTicksVisible);
    EnableSettingsRow(state, kPollMinutes,
                      SendDlgItemMessageW(state.hWnd, kPollPreset,
                                          CB_GETCURSEL, 0, 0) == 6);
    EnableSettingsRow(state, kHistoryRetentionDays,
                      SendDlgItemMessageW(state.hWnd, kHistoryEnabled,
                                          BM_GETCHECK, 0, 0) == BST_CHECKED);
}

static void PopulateMonitorCombo(SettingsWindowState& state, int selectedMonitorNumber) {
    HWND combo = GetDlgItem(state.hWnd, kMonitorNumber);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    auto displays = FindCurrentProcessTaskbarDisplays();
    int selectedIndex = -1;
    for (size_t i = 0; i < displays.size(); i++) {
        const auto& display = displays[i];
        int width = std::abs(display.rect.right - display.rect.left);
        int height = std::abs(display.rect.bottom - display.rect.top);
        wchar_t text[128];
        swprintf(text, ARRAYSIZE(text), L"Display %d - %dx%d%s", (int)i + 1,
                 width, height, display.primary ? L" (Primary)" : L"");
        int item = (int)SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(combo, CB_SETITEMDATA, item, (LPARAM)(i + 1));
        if ((int)i + 1 == selectedMonitorNumber) selectedIndex = item;
    }
    if (selectedIndex < 0) {
        wchar_t text[96];
        swprintf(text, ARRAYSIZE(text), L"Display %d - unavailable", selectedMonitorNumber);
        selectedIndex = (int)SendMessageW(combo, CB_ADDSTRING, 0,
                                          reinterpret_cast<LPARAM>(text));
        SendMessageW(combo, CB_SETITEMDATA, selectedIndex, selectedMonitorNumber);
    }
    SendMessageW(combo, CB_SETCURSEL, selectedIndex, 0);
}

static void RefreshSettingsControls(SettingsWindowState& state) {
    Settings s;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        s = g_settings;
    }
    state.updating = true;
    SendDlgItemMessageW(state.hWnd, kMonitorMode, CB_SETCURSEL,
                        s.taskbarMonitorMode == TaskbarMonitorMode::All ? 1 :
                        s.taskbarMonitorMode == TaskbarMonitorMode::Specific ? 2 : 0, 0);
    PopulateMonitorCombo(state, s.taskbarMonitorNumber);
    SendDlgItemMessageW(state.hWnd, kBarLayout, CB_SETCURSEL,
                        s.barLayout == BarLayout::Vertical ? 1 : 0, 0);
    SendDlgItemMessageW(state.hWnd, kBarMode, CB_SETCURSEL,
                        s.barMode == BarMode::Remaining ? 1 : 0, 0);
    SetControlInt(state, kBarLength, s.barLength);
    SetControlInt(state, kBarThickness, s.barThickness);
    SetControlInt(state, kLabelFontSize, s.labelFontSize);
    SetControlInt(state, kPercentFontSize, s.percentFontSize);
    SetControlInt(state, kAccountMargin, s.accountMargin);
    SetControlInt(state, kLabelGap, s.labelGap);
    SetControlInt(state, kBarGap, s.barGap);
    SetControlInt(state, kRightMargin, s.rightMargin);

    auto setCheck = [&](int id, bool checked) {
        SendDlgItemMessageW(state.hWnd, id, BM_SETCHECK,
                            checked ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    bool visualTestMode = g_visualTestMode.load(std::memory_order_acquire);
    setCheck(kVisualTestModeLayout, visualTestMode);
    setCheck(kVisualTestModeDisplay, visualTestMode);
    SendDlgItemMessageW(state.hWnd, kLabelPosition, CB_SETCURSEL,
                        (int)s.labelPosition, 0);
    setCheck(kShowPaceTicks, s.showPaceTicks);
    SendDlgItemMessageW(state.hWnd, kPaceTickStyle, CB_SETCURSEL,
                        (int)s.paceTickStyle, 0);
    setCheck(kShowBarLabels, s.showBarLabels);
    SendDlgItemMessageW(state.hWnd, kPercentTextVisibility, CB_SETCURSEL,
                        (int)s.percentTextVisibility, 0);
    SendDlgItemMessageW(state.hWnd, kPercentTextAlignment, CB_SETCURSEL,
                        (int)s.percentTextAlignment, 0);
    setCheck(kShowExtraBarAmounts, s.showExtraBarAmounts);
    setCheck(kShowOpenAiExtraLimits, s.showOpenAiExtraLimits);
    setCheck(kColorblindMode, s.colorblindMode);
    setCheck(kShowStaleWarning, s.showStaleWarning);
    SetControlInt(state, kYellowThreshold, s.yellowThreshold);
    SetControlInt(state, kOrangeThreshold, s.orangeThreshold);
    SetControlInt(state, kRedThreshold, s.redThreshold);

    SendDlgItemMessageW(state.hWnd, kClickAction, CB_SETCURSEL,
                        s.clickAction == ClickAction::OpenDashboard ? 1 : 0, 0);
    const int pollPresets[] = {2, 5, 10, 15, 30, 60};
    int pollPresetIndex = 6;
    for (int i = 0; i < (int)ARRAYSIZE(pollPresets); i++) {
        if (s.pollMinutes == pollPresets[i]) {
            pollPresetIndex = i;
            break;
        }
    }
    SendDlgItemMessageW(state.hWnd, kPollPreset, CB_SETCURSEL, pollPresetIndex, 0);
    SetControlInt(state, kPollMinutes, s.pollMinutes);
    setCheck(kEnableNotifications, s.enableNotifications);
    setCheck(kHistoryEnabled, s.historyEnabled);
    SetControlInt(state, kHistoryRetentionDays, s.historyRetentionDays);
    setCheck(kSkipHiddenPolling, s.skipHiddenPolling);
    RefreshColorPreviews(state, s);
    state.updating = false;
    UpdateDependentSettingsControls(state);
    RefreshAccountList(state);
}

static void CommitScalarSettings(SettingsWindowState& state, bool refreshControls = true) {
    if (state.updating || g_unloading) return;
    if (!GetDlgItem(state.hWnd, kMonitorMode)) return;
    KillTimer(state.hWnd, kSettingsAutosaveTimer);
    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings s;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        s = g_settings;
    }
    auto getBoundedInt = [&](int id, int fallback) {
        int value = GetControlInt(state.hWnd, id, fallback);
        if (SettingsRow* row = FindSettingsRow(state, GetDlgItem(state.hWnd, id))) {
            value = std::clamp(value, row->minimum, row->maximum);
        }
        return value;
    };
    int selection = (int)SendDlgItemMessageW(state.hWnd, kMonitorMode, CB_GETCURSEL, 0, 0);
    s.taskbarMonitorMode = selection == 1 ? TaskbarMonitorMode::All :
                           selection == 2 ? TaskbarMonitorMode::Specific :
                                            TaskbarMonitorMode::Primary;
    int monitorSelection = (int)SendDlgItemMessageW(state.hWnd, kMonitorNumber,
                                                     CB_GETCURSEL, 0, 0);
    LRESULT monitorNumber = monitorSelection >= 0 ?
        SendDlgItemMessageW(state.hWnd, kMonitorNumber, CB_GETITEMDATA,
                            monitorSelection, 0) : CB_ERR;
    if (monitorNumber != CB_ERR) s.taskbarMonitorNumber = (int)monitorNumber;
    s.barLayout = SendDlgItemMessageW(state.hWnd, kBarLayout, CB_GETCURSEL, 0, 0) == 1 ?
                          BarLayout::Vertical : BarLayout::Stacked;
    s.barMode = SendDlgItemMessageW(state.hWnd, kBarMode, CB_GETCURSEL, 0, 0) == 1 ?
                        BarMode::Remaining : BarMode::Used;
    s.barLength = getBoundedInt(kBarLength, s.barLength);
    s.barThickness = getBoundedInt(kBarThickness, s.barThickness);
    s.labelFontSize = getBoundedInt(kLabelFontSize, s.labelFontSize);
    s.percentFontSize = getBoundedInt(kPercentFontSize, s.percentFontSize);
    s.accountMargin = getBoundedInt(kAccountMargin, s.accountMargin);
    s.labelGap = getBoundedInt(kLabelGap, s.labelGap);
    s.barGap = getBoundedInt(kBarGap, s.barGap);
    s.rightMargin = getBoundedInt(kRightMargin, s.rightMargin);
    auto isChecked = [&](int id) {
        return SendDlgItemMessageW(state.hWnd, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
    };
    int labelPosition = (int)SendDlgItemMessageW(state.hWnd, kLabelPosition,
                                                 CB_GETCURSEL, 0, 0);
    s.labelPosition = labelPosition >= 0 && labelPosition <= (int)LabelPosition::Bottom ?
                          (LabelPosition)labelPosition : LabelPosition::Left;
    s.showPaceTicks = isChecked(kShowPaceTicks);
    int paceTickStyle = (int)SendDlgItemMessageW(state.hWnd, kPaceTickStyle,
                                                 CB_GETCURSEL, 0, 0);
    s.paceTickStyle = paceTickStyle >= 0 && paceTickStyle <= (int)PaceTickStyle::Dot ?
                          (PaceTickStyle)paceTickStyle : PaceTickStyle::Caret;
    if (SettingsRow* row = FindSettingsRow(
            state, GetDlgItem(state.hWnd, kPaceTickColor))) {
        s.paceTickColor = row->previewColor;
    }
    s.showBarLabels = isChecked(kShowBarLabels);
    int percentTextVisibility = (int)SendDlgItemMessageW(
        state.hWnd, kPercentTextVisibility, CB_GETCURSEL, 0, 0);
    s.percentTextVisibility =
        percentTextVisibility >= 0 &&
                percentTextVisibility <= (int)PercentTextVisibility::Always ?
            (PercentTextVisibility)percentTextVisibility : PercentTextVisibility::Hover;
    int percentTextAlignment = (int)SendDlgItemMessageW(
        state.hWnd, kPercentTextAlignment, CB_GETCURSEL, 0, 0);
    s.percentTextAlignment =
        percentTextAlignment >= 0 && percentTextAlignment <= (int)PercentTextAlignment::Right ?
            (PercentTextAlignment)percentTextAlignment : PercentTextAlignment::Adaptive;
    s.showExtraBarAmounts = isChecked(kShowExtraBarAmounts);
    s.showOpenAiExtraLimits = isChecked(kShowOpenAiExtraLimits);
    s.colorblindMode = isChecked(kColorblindMode);
    s.showStaleWarning = isChecked(kShowStaleWarning);
    s.yellowThreshold = getBoundedInt(kYellowThreshold, s.yellowThreshold);
    s.orangeThreshold = getBoundedInt(kOrangeThreshold, s.orangeThreshold);
    s.redThreshold = getBoundedInt(kRedThreshold, s.redThreshold);
    s.clickAction = SendDlgItemMessageW(state.hWnd, kClickAction, CB_GETCURSEL, 0, 0) == 1 ?
                            ClickAction::OpenDashboard : ClickAction::Refresh;
    const int pollPresets[] = {2, 5, 10, 15, 30, 60};
    int pollPresetIndex = (int)SendDlgItemMessageW(state.hWnd, kPollPreset,
                                                   CB_GETCURSEL, 0, 0);
    s.pollMinutes = pollPresetIndex >= 0 && pollPresetIndex < (int)ARRAYSIZE(pollPresets) ?
                        pollPresets[pollPresetIndex] :
                        getBoundedInt(kPollMinutes, s.pollMinutes);
    s.enableNotifications = isChecked(kEnableNotifications);
    s.historyEnabled = isChecked(kHistoryEnabled);
    s.historyRetentionDays = getBoundedInt(kHistoryRetentionDays, s.historyRetentionDays);
    s.skipHiddenPolling = isChecked(kSkipHiddenPolling);
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(s));
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not save settings.", L"Taskbar AI Quota Bars",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    if (refreshControls) RefreshSettingsControls(state);
}

// src/settings/account-editor.h
// Account editor dialog and the add, edit, remove, reorder, and hide account commands.

static void LayoutAccountEditor(HWND hWnd, const AccountEditorState& state) {
    auto sc = [&](int value) { return ScaleForDpi(value, state.dpi); };
    RECT client{};
    GetClientRect(hWnd, &client);
    int width = client.right;
    int height = client.bottom;
    int controlX = width < sc(390) ? sc(100) : sc(130);
    int controlWidth = std::max(sc(120), width - controlX - sc(16));
    SetWindowPos(GetDlgItem(hWnd, kAccountProviderLabel), nullptr,
                 sc(16), sc(18), controlX - sc(28), sc(22), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountProvider), nullptr,
                 controlX, sc(14), controlWidth, sc(220), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountLabelLabel), nullptr,
                 sc(16), sc(58), controlX - sc(28), sc(22), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountLabel), nullptr,
                 controlX, sc(54), controlWidth, sc(26), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountFiveHour), nullptr,
                 sc(16), sc(98), sc(180), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountWeekly), nullptr,
                 sc(16), sc(128), sc(180), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountFableWeekly), nullptr,
                 sc(16), sc(158), width - sc(32), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    // For OpenAI the 4th checkbox reads "Show credits bar, max:" and the max edit follows it.
    bool openai = SendDlgItemMessageW(hWnd, kAccountProvider, CB_GETCURSEL, 0, 0) == 1;
    SetWindowPos(GetDlgItem(hWnd, kAccountExtraUsage), nullptr,
                 sc(16), sc(188), openai ? sc(164) : width - sc(32), sc(24),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountCreditsMax), nullptr,
                 sc(180), sc(188), sc(80), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountRecordHistory), nullptr,
                 sc(16), sc(218), width - sc(32), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountHistoryIdentityNote), nullptr,
                 sc(16), sc(246), width - sc(32), sc(44), SWP_NOZORDER | SWP_NOACTIVATE);
    int buttonY = std::max(sc(306), height - sc(46));
    SetWindowPos(GetDlgItem(hWnd, IDOK), nullptr,
                 width - sc(182), buttonY, sc(80), sc(30), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, IDCANCEL), nullptr,
                 width - sc(96), buttonY, sc(80), sc(30), SWP_NOZORDER | SWP_NOACTIVATE);
}

static void RecreateAccountEditorVisuals(HWND hWnd, AccountEditorState& state) {
    state.dark = IsWindowsDarkMode();
    if (state.font) DeleteObject(state.font);
    if (state.backgroundBrush) DeleteObject(state.backgroundBrush);
    if (state.inputBrush) DeleteObject(state.inputBrush);
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(9, (int)state.dpi, 72);
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    state.font = CreateFontIndirectW(&lf);
    state.backgroundBrush = CreateSolidBrush(state.dark ? RGB(32, 32, 32) :
                                                          GetSysColor(COLOR_BTNFACE));
    state.inputBrush = CreateSolidBrush(state.dark ? RGB(43, 43, 43) :
                                                     GetSysColor(COLOR_WINDOW));
    EnumChildWindows(hWnd, [](HWND child, LPARAM param) -> BOOL {
        SendMessageW(child, WM_SETFONT, param, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(state.font));
    ApplyNativeWindowTheme(hWnd, state.dark);
    RedrawWindow(hWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void UpdateAccountEditorProvider(HWND hWnd) {
    int providerIndex = (int)SendDlgItemMessageW(hWnd, kAccountProvider, CB_GETCURSEL, 0, 0);
    bool anthropic = providerIndex == 0;
    bool openai = providerIndex == 1;
    HWND fableWeekly = GetDlgItem(hWnd, kAccountFableWeekly);
    HWND extraUsage = GetDlgItem(hWnd, kAccountExtraUsage);
    EnableWindow(fableWeekly, anthropic);
    EnableWindow(extraUsage, anthropic || openai);
    SetWindowTextW(extraUsage, openai ? L"Show credits bar, max:" :
                                        L"Show monthly extra-usage bar");
    ShowWindow(GetDlgItem(hWnd, kAccountCreditsMax), openai ? SW_SHOW : SW_HIDE);
}

static bool HasDuplicateAccount(const Settings& settings, uint64_t ignoredIdentity,
                                const AccountConfig& account) {
    for (const auto& existing : settings.accounts) {
        if (ignoredIdentity && AccountIdentityHash(existing) == ignoredIdentity) continue;
        if (existing.provider == account.provider && existing.label == account.label) return true;
    }
    return false;
}

static LRESULT CALLBACK AccountEditorWndProc(HWND hWnd, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AccountEditorState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<AccountEditorState*>(create->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            state->dpi = WindowDpi(hWnd);
            auto sc = [&](int value) { return ScaleForDpi(value, state->dpi); };
            HWND providerLabel = CreateWindowExW(0, L"STATIC", L"Provider", WS_CHILD | WS_VISIBLE,
                                                  sc(16), sc(18), sc(100), sc(22), hWnd,
                                                  reinterpret_cast<HMENU>(kAccountProviderLabel),
                                                  GetModuleHandleW(nullptr), nullptr);
            HWND provider = CreateWindowExW(0, L"COMBOBOX", L"",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                             sc(130), sc(14), sc(240), sc(220), hWnd,
                                             reinterpret_cast<HMENU>(kAccountProvider),
                                             GetModuleHandleW(nullptr), nullptr);
            AddComboItems(provider, {L"Anthropic (Claude)", L"OpenAI (ChatGPT/Codex)",
                                     L"Google Antigravity"});
            int providerIndex = state->account.provider == L"openai" ? 1 :
                                state->account.provider == L"antigravity" ? 2 : 0;
            SendMessageW(provider, CB_SETCURSEL, providerIndex, 0);

            HWND labelLabel = CreateWindowExW(0, L"STATIC", L"Label", WS_CHILD | WS_VISIBLE,
                                               sc(16), sc(58), sc(100), sc(22), hWnd,
                                               reinterpret_cast<HMENU>(kAccountLabelLabel),
                                               GetModuleHandleW(nullptr), nullptr);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->account.label.c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                           sc(130), sc(54), sc(240), sc(26), hWnd,
                                           reinterpret_cast<HMENU>(kAccountLabel),
                                           GetModuleHandleW(nullptr), nullptr);
            SendMessageW(state->edit, EM_SETLIMITTEXT, 64, 0);

            HWND fiveHour = CreateWindowExW(0, L"BUTTON", L"Show 5-hour bar",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                             sc(16), sc(98), sc(180), sc(24), hWnd,
                                             reinterpret_cast<HMENU>(kAccountFiveHour),
                                             GetModuleHandleW(nullptr), nullptr);
            HWND weekly = CreateWindowExW(0, L"BUTTON", L"Show weekly bar",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                           sc(16), sc(128), sc(180), sc(24), hWnd,
                                           reinterpret_cast<HMENU>(kAccountWeekly),
                                           GetModuleHandleW(nullptr), nullptr);
            HWND fable = CreateWindowExW(0, L"BUTTON", L"Show Fable weekly bar",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          sc(16), sc(158), sc(220), sc(24), hWnd,
                                          reinterpret_cast<HMENU>(kAccountFableWeekly),
                                          GetModuleHandleW(nullptr), nullptr);
            HWND extra = CreateWindowExW(0, L"BUTTON", L"Show monthly extra-usage bar",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          sc(16), sc(188), sc(260), sc(24), hWnd,
                                          reinterpret_cast<HMENU>(kAccountExtraUsage),
                                          GetModuleHandleW(nullptr), nullptr);
            SendMessageW(fiveHour, BM_SETCHECK,
                         state->account.showBars[kFiveHourBar] ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(weekly, BM_SETCHECK,
                         state->account.showBars[kWeeklyBar] ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(fable, BM_SETCHECK,
                         state->account.showBars[kFableWeeklyBar] ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(extra, BM_SETCHECK,
                         state->account.showBars[kExtraUsageBar] ? BST_CHECKED : BST_UNCHECKED, 0);
            // Shown only for OpenAI; UpdateAccountEditorProvider toggles visibility.
            std::wstring creditsMaxText = state->account.creditsMax > 0 ?
                std::to_wstring(state->account.creditsMax) : L"";
            HWND creditsMax = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", creditsMaxText.c_str(),
                                               WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                               sc(180), sc(188), sc(80), sc(24), hWnd,
                                               reinterpret_cast<HMENU>(kAccountCreditsMax),
                                               GetModuleHandleW(nullptr), nullptr);
            SendMessageW(creditsMax, EM_SETLIMITTEXT, 9, 0);

            HWND recordHistory = CreateWindowExW(
                0, L"BUTTON", L"Record history", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                sc(16), sc(218), sc(260), sc(24), hWnd,
                reinterpret_cast<HMENU>(kAccountRecordHistory), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(recordHistory, BM_SETCHECK,
                         state->account.recordHistory ? BST_CHECKED : BST_UNCHECKED, 0);
            CreateWindowExW(0, L"STATIC",
                L"History follows this entry, even after signing in again. "
                L"Use a new entry for a different login.",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hWnd,
                reinterpret_cast<HMENU>(kAccountHistoryIdentityNote),
                GetModuleHandleW(nullptr), nullptr);
            state->toolTip = CreateWindowExW(
                WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (state->toolTip && recordHistory) {
                SendMessageW(state->toolTip, TTM_SETMAXTIPWIDTH, 0, sc(360));
                TOOLINFOW info{};
                info.cbSize = sizeof(info);
                info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                info.hwnd = hWnd;
                info.uId = reinterpret_cast<UINT_PTR>(recordHistory);
                info.lpszText = const_cast<PWSTR>(
                    L"This account preference is shared across Windows users. Records only when "
                    L"Enable local history is on for the current Windows user in Behavior. "
                    L"Do not poll hidden accounts overrides this checkbox: hidden accounts are not "
                    L"polled or recorded while that option is on. History follows this configured "
                    L"account, not the signed-in login. Use a new account entry for a different login.");
                SendMessageW(state->toolTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
            }

            HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                      sc(204), sc(232), sc(80), sc(30), hWnd,
                                      reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          sc(290), sc(232), sc(80), sc(30), hWnd,
                                          reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
            (void)providerLabel;
            (void)labelLabel;
            (void)ok;
            (void)cancel;
            RecreateAccountEditorVisuals(hWnd, *state);
            LayoutAccountEditor(hWnd, *state);
            UpdateAccountEditorProvider(hWnd);
            SetFocus(state->edit);
            return 0;
        }
        case WM_COMMAND:
            if (!state) break;
            if (LOWORD(wParam) == kAccountProvider && HIWORD(wParam) == CBN_SELCHANGE) {
                UpdateAccountEditorProvider(hWnd);
                LayoutAccountEditor(hWnd, *state);
                return 0;
            }
            if (LOWORD(wParam) == IDOK) {
                int length = GetWindowTextLengthW(state->edit);
                std::wstring label(length + 1, L'\0');
                GetWindowTextW(state->edit, label.data(), length + 1);
                label.resize(length);
                size_t first = label.find_first_not_of(L" \t\r\n");
                size_t last = label.find_last_not_of(L" \t\r\n");
                label = first == std::wstring::npos ? L"" : label.substr(first, last - first + 1);
                if (label.empty()) {
                    SettingsMessageBoxW(hWnd, L"Enter an account label.", L"Account",
                                MB_OK | MB_ICONWARNING);
                    SetFocus(state->edit);
                    return 0;
                }
                int providerIndex = (int)SendDlgItemMessageW(hWnd, kAccountProvider,
                                                              CB_GETCURSEL, 0, 0);
                state->account.provider = providerIndex == 1 ? L"openai" :
                                          providerIndex == 2 ? L"antigravity" : L"anthropic";
                state->account.label = std::move(label);
                state->account.recordHistory =
                    IsDlgButtonChecked(hWnd, kAccountRecordHistory) == BST_CHECKED;
                state->account.showBars[kFiveHourBar] =
                    IsDlgButtonChecked(hWnd, kAccountFiveHour) == BST_CHECKED;
                state->account.showBars[kWeeklyBar] =
                    IsDlgButtonChecked(hWnd, kAccountWeekly) == BST_CHECKED;
                state->account.showBars[kFableWeeklyBar] =
                    state->account.provider == L"anthropic" &&
                    IsDlgButtonChecked(hWnd, kAccountFableWeekly) == BST_CHECKED;
                bool openai = state->account.provider == L"openai";
                state->account.showBars[kExtraUsageBar] =
                    (state->account.provider == L"anthropic" || openai) &&
                    IsDlgButtonChecked(hWnd, kAccountExtraUsage) == BST_CHECKED;
                state->account.creditsMax =
                    openai ? (int)GetDlgItemInt(hWnd, kAccountCreditsMax, nullptr, FALSE) : 0;
                if (openai && state->account.showBars[kExtraUsageBar] &&
                    state->account.creditsMax <= 0) {
                    SettingsMessageBoxW(hWnd, L"Enter the credits max for the credits bar.",
                                        L"Account", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hWnd, kAccountCreditsMax));
                    return 0;
                }
                if (!state->account.showBars[kFiveHourBar] &&
                    !state->account.showBars[kWeeklyBar] &&
                    !state->account.showBars[kFableWeeklyBar] &&
                    !state->account.showBars[kExtraUsageBar]) {
                    SettingsMessageBoxW(hWnd, L"Select at least one quota bar.", L"Account",
                                MB_OK | MB_ICONWARNING);
                    return 0;
                }
                bool duplicate;
                {
                    std::lock_guard<std::mutex> lk(g_settingsMutex);
                    duplicate = HasDuplicateAccount(g_settings, state->originalIdentity,
                                                    state->account);
                }
                if (duplicate) {
                    SettingsMessageBoxW(hWnd,
                                        L"That provider and label are already configured.",
                                        L"Account", MB_OK | MB_ICONWARNING);
                    SetFocus(state->edit);
                    return 0;
                }
                state->accepted = true;
                DestroyWindow(hWnd);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            if (state && state->dark) {
                SetTextColor(reinterpret_cast<HDC>(wParam), RGB(235, 235, 235));
                SetBkColor(reinterpret_cast<HDC>(wParam), RGB(32, 32, 32));
                return reinterpret_cast<LRESULT>(state->backgroundBrush);
            }
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            if (state && state->dark) {
                SetTextColor(reinterpret_cast<HDC>(wParam), RGB(235, 235, 235));
                SetBkColor(reinterpret_cast<HDC>(wParam), RGB(43, 43, 43));
                return reinterpret_cast<LRESULT>(state->inputBrush);
            }
            break;
        case WM_DPICHANGED:
            if (state) {
                state->dpi = HIWORD(wParam);
                auto* suggested = reinterpret_cast<RECT*>(lParam);
                MONITORINFO monitorInfo{sizeof(monitorInfo)};
                GetMonitorInfoW(MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST),
                                &monitorInfo);
                int width = std::min((int)(suggested->right - suggested->left),
                                     (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left));
                int height = std::min((int)(suggested->bottom - suggested->top),
                                      (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top));
                int x = std::clamp((int)suggested->left, (int)monitorInfo.rcWork.left,
                                   (int)monitorInfo.rcWork.right - width);
                int y = std::clamp((int)suggested->top, (int)monitorInfo.rcWork.top,
                                   (int)monitorInfo.rcWork.bottom - height);
                SetWindowPos(hWnd, nullptr, x, y, width, height,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                RecreateAccountEditorVisuals(hWnd, *state);
                LayoutAccountEditor(hWnd, *state);
            }
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            if (state) RecreateAccountEditorVisuals(hWnd, *state);
            return 0;
        case WM_ERASEBKGND:
            if (state && state->backgroundBrush) {
                RECT client{};
                GetClientRect(hWnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                return 1;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            if (state) {
                if (state->font) DeleteObject(state->font);
                if (state->backgroundBrush) DeleteObject(state->backgroundBrush);
                if (state->inputBrush) DeleteObject(state->inputBrush);
                state->font = nullptr;
                state->backgroundBrush = nullptr;
                state->inputBrush = nullptr;
                state->done = true;
            }
            return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

static bool ShowAccountEditor(HWND owner, AccountConfig* account, bool adding) {
    AccountEditorState state;
    state.account = *account;
    state.originalIdentity = adding ? 0 : AccountIdentityHash(*account);
    UINT dpi = WindowDpi(owner);
    int width = ScaleForDpi(404, dpi);
    int height = ScaleForDpi(388, dpi);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitorInfo);
    width = std::min(width, (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left));
    height = std::min(height, (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top));
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    x = std::clamp(x, (int)monitorInfo.rcWork.left, (int)monitorInfo.rcWork.right - width);
    y = std::clamp(y, (int)monitorInfo.rcWork.top, (int)monitorInfo.rcWork.bottom - height);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kAccountEditorClass,
                                  adding ? L"Add account" : L"Edit account",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, owner, nullptr,
                                  GetModuleHandleW(nullptr), &state);
    if (!window) return false;

    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    MSG message;
    while (!g_unloading && !g_settingsWindowCancelRequested && !state.done) {
        int result = GetMessageW(&message, nullptr, 0, 0);
        if (result == 0) {
            PostQuitMessage((int)message.wParam);
            break;
        }
        if (result < 0) break;
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(window)) DestroyWindow(window);
    if (IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (state.accepted) *account = std::move(state.account);
    return state.accepted;
}

static void AddAccountFromSettingsWindow(SettingsWindowState& state) {
    AccountConfig account{L"anthropic", L"A"};
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        for (int suffix = 1;; suffix++) {
            account.label = suffix == 1 ? L"A" : L"A" + std::to_wstring(suffix);
            if (!HasDuplicateAccount(g_settings, 0, account)) break;
        }
    }
    if (!ShowAccountEditor(state.hWnd, &account, true)) return;
    if (g_unloading || g_settingsWindowCancelRequested || !IsWindow(state.hWnd)) return;
    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    if (HasDuplicateAccount(settings, 0, account)) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"That provider and label are already configured.",
                    L"Account", MB_OK | MB_ICONWARNING);
        return;
    }
    settings.accounts.push_back(std::move(account));
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(settings));
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not save the account.", L"Account",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    RefreshSettingsControls(state);
}

static void EditAccountFromSettingsWindow(SettingsWindowState& state) {
    int index = SelectedAccountIndex(state);
    AccountConfig oldAccount;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        if (index < 0 || index >= (int)g_settings.accounts.size()) return;
        oldAccount = g_settings.accounts[index];
    }
    AccountConfig newAccount = oldAccount;
    if (!ShowAccountEditor(state.hWnd, &newAccount, false)) return;
    if (g_unloading || g_settingsWindowCancelRequested || !IsWindow(state.hWnd)) return;

    bool identityChanged = AccountIdentityHash(oldAccount) != AccountIdentityHash(newAccount);
    bool onlyBarSelectionChanged = oldAccount.provider == newAccount.provider &&
                                   oldAccount.label == newAccount.label &&
                                   oldAccount.creditsMax == newAccount.creditsMax &&
                                   oldAccount.recordHistory == newAccount.recordHistory &&
                                   oldAccount.historyId == newAccount.historyId &&
                                   oldAccount.showBars != newAccount.showBars;
    if (identityChanged && g_loginInProgress.load()) {
        SettingsMessageBoxW(state.hWnd, L"Wait for the current sign-in to finish before changing identity.",
                    L"Account", MB_OK | MB_ICONINFORMATION);
        return;
    }
    bool clearOldToken = false;
    if (identityChanged && oldAccount.provider != newAccount.provider &&
        oldAccount.provider != L"antigravity") {
        StoredToken token;
        if (LoadStoredToken(AccountIdentityHash(oldAccount), &token)) {
            int result = SettingsMessageBoxW(
                state.hWnd,
                L"Changing provider creates a new account identity.\n\n"
                L"Yes: delete the old stored sign-in\nNo: keep it for later\nCancel: discard this edit",
                L"Change account provider", MB_YESNOCANCEL | MB_ICONQUESTION);
            if ((result != IDYES && result != IDNO) || g_unloading ||
                !IsWindow(state.hWnd)) return;
            clearOldToken = result == IDYES;
        }
    }

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    if (identityChanged && g_loginInProgress.load()) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"Wait for the current sign-in to finish before changing identity.",
                    L"Account", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    uint64_t oldIdentity = AccountIdentityHash(oldAccount);
    index = -1;
    for (size_t i = 0; i < settings.accounts.size(); i++) {
        if (AccountIdentityHash(settings.accounts[i]) == oldIdentity) {
            index = (int)i;
            break;
        }
    }
    if (index < 0) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"The account was changed from another taskbar window.",
                    L"Account", MB_OK | MB_ICONWARNING);
        RefreshSettingsControls(state);
        return;
    }
    if (HasDuplicateAccount(settings, oldIdentity, newAccount)) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"That provider and label are already configured.",
                    L"Account", MB_OK | MB_ICONWARNING);
        return;
    }

    uint64_t newIdentity = AccountIdentityHash(newAccount);
    bool sameProviderIdentityChange = identityChanged &&
                                      oldAccount.provider == newAccount.provider;
    bool sameProviderRename = sameProviderIdentityChange &&
                              oldAccount.provider != L"antigravity";
    TokenCopyResult tokenCopyResult = TokenCopyResult::SourceMissing;
    bool oldTokenCleared = true;
    ULONGLONG oldAuthEpoch = 0;
    ULONGLONG newAuthEpoch = 0;
    std::unique_lock<std::mutex> authEpochLock(g_authEpochMutex, std::defer_lock);
    if (sameProviderRename) {
        // Block epoch-gated saves only across the local copy/settings/delete transaction.
        authEpochLock.lock();
        oldAuthEpoch = AuthEpochLocked(oldIdentity);
        newAuthEpoch = AuthEpochLocked(newIdentity);
        tokenCopyResult = CopyStoredTokenForRename(oldIdentity, newIdentity);
        if (tokenCopyResult == TokenCopyResult::DestinationOccupied ||
            tokenCopyResult == TokenCopyResult::Failed) {
            authEpochLock.unlock();
            configLock.unlock();
            SettingsMessageBoxW(
                state.hWnd,
                tokenCopyResult == TokenCopyResult::DestinationOccupied ?
                    L"The renamed account identity already has a retained sign-in. To replace it, "
                    L"add an account with that provider and label, then remove it and delete its "
                    L"stored sign-in. The account was not changed." :
                    L"The stored sign-in could not be copied. The account was not changed.",
                L"Account", MB_OK | MB_ICONERROR);
            return;
        }
    }

    // Visibility can be toggled from a taskbar menu while the account editor is open.
    newAccount.hidden = settings.accounts[index].hidden;
    // Renames retain history; switching providers starts a new series during normalization.
    newAccount.historyId = oldAccount.provider == newAccount.provider ?
                               settings.accounts[index].historyId : 0;
    settings.accounts[index] = newAccount;
    SettingsApplyResult applyResult = ApplyOwnedSettings(
        std::move(settings), sameProviderIdentityChange ? oldIdentity : 0,
        sameProviderIdentityChange ? newIdentity : 0);
    if (applyResult == SettingsApplyResult::Failed) {
        bool copiedTokenCleared = tokenCopyResult != TokenCopyResult::Copied ||
                                  ClearStoredToken(newIdentity);
        if (authEpochLock.owns_lock()) authEpochLock.unlock();
        configLock.unlock();
        SettingsMessageBoxW(
            state.hWnd,
            copiedTokenCleared ?
                L"Could not save the account." :
                L"Could not save the account. The copied sign-in remains under the attempted "
                L"new identity. To delete it, add an account with the attempted provider and "
                L"label, then remove it and choose to delete its stored sign-in.",
            L"Account", MB_OK | MB_ICONERROR);
        return;
    }
    if (sameProviderRename) {
        oldTokenCleared = ClearStoredToken(oldIdentity);
        BumpAuthEpochLocked(oldIdentity);
        g_authRenameRedirects.push_back(
            {oldIdentity, oldAuthEpoch, newIdentity, newAuthEpoch});
        authEpochLock.unlock();
    }
    configLock.unlock();
    bool providerTokenCleared = true;
    if (clearOldToken) {
        providerTokenCleared = ClearStoredTokenAndBumpAuthEpoch(oldIdentity);
    }
    if (applyResult == SettingsApplyResult::Changed) {
        // Every bar already exists in the XAML tree; visibility and spacing update in place.
        if (onlyBarSelectionChanged) {
            PostUiUpdate();
            if (!g_uiInjected.load(std::memory_order_acquire)) StartRetryInject();
        } else {
            FinishSettingsApply();
        }
    }
    if (!oldTokenCleared) {
        SettingsMessageBoxW(
            state.hWnd,
            L"The account was renamed, but a duplicate stored sign-in remains under the old "
            L"identity. To delete it, re-add an account with the old provider and label, then "
            L"remove it and choose to delete its stored sign-in.",
            L"Account", MB_OK | MB_ICONWARNING);
    } else if (!providerTokenCleared) {
        SettingsMessageBoxW(
            state.hWnd,
            L"The provider was changed, but the old sign-in remains retained. To recover or "
            L"delete it, re-add an account with the old provider and label.",
            L"Account", MB_OK | MB_ICONWARNING);
    }
    NotifySettingsWindowChanged();
    RefreshSettingsControls(state);
}

static void RemoveAccountFromSettingsWindow(SettingsWindowState& state) {
    int index = SelectedAccountIndex(state);
    AccountConfig account;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        if (index < 0 || index >= (int)g_settings.accounts.size()) return;
        account = g_settings.accounts[index];
    }
    if (g_loginInProgress.load()) {
        SettingsMessageBoxW(state.hWnd, L"Wait for the current sign-in to finish before removing accounts.",
                    L"Account", MB_OK | MB_ICONINFORMATION);
        return;
    }
    bool deleteToken = false;
    if (account.provider == L"antigravity") {
        int result = SettingsMessageBoxW(state.hWnd, L"Remove this account?", L"Remove account",
                                 MB_YESNO | MB_ICONQUESTION);
        if (result != IDYES || g_unloading || !IsWindow(state.hWnd)) return;
    } else {
        int result = SettingsMessageBoxW(
            state.hWnd,
            L"Remove this account?\n\n"
            L"Yes: remove it and delete its stored sign-in\n"
            L"No: remove it but retain the sign-in for later\n"
            L"Cancel: do nothing",
            L"Remove account", MB_YESNOCANCEL | MB_ICONQUESTION);
        if ((result != IDYES && result != IDNO) || g_unloading ||
            !IsWindow(state.hWnd)) return;
        deleteToken = result == IDYES;
    }

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    if (g_loginInProgress.load()) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"Wait for the current sign-in to finish before removing accounts.",
                    L"Account", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    uint64_t identity = AccountIdentityHash(account);
    index = -1;
    for (size_t i = 0; i < settings.accounts.size(); i++) {
        if (AccountIdentityHash(settings.accounts[i]) == identity) {
            index = (int)i;
            break;
        }
    }
    if (index < 0) {
        configLock.unlock();
        RefreshSettingsControls(state);
        return;
    }
    if (deleteToken && !ClearStoredTokenAndBumpAuthEpoch(identity)) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd,
                    L"The stored sign-in could not be deleted, so the account was not removed.",
                    L"Account", MB_OK | MB_ICONERROR);
        return;
    }
    settings.accounts.erase(settings.accounts.begin() + index);
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(settings));
    if (applyResult == SettingsApplyResult::Failed) {
        configLock.unlock();
        if (deleteToken) {
            RefreshQuotaByIdentity(identity);
            NotifySettingsWindowChanged();
        }
        SettingsMessageBoxW(state.hWnd,
                    deleteToken ?
                        L"The stored sign-in was deleted, but settings could not be saved. The "
                        L"account remains configured but signed out." :
                        L"Could not remove the account.",
                    L"Account",
                    MB_OK | MB_ICONERROR);
        return;
    }
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Changed) FinishSettingsApply();
    RefreshSettingsControls(state);
}

static void MoveAccountFromSettingsWindow(SettingsWindowState& state, int direction) {
    uint64_t identity = SelectedAccountIdentity(state);
    if (!identity || (direction != -1 && direction != 1)) return;

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    int index = -1;
    for (size_t i = 0; i < settings.accounts.size(); i++) {
        if (AccountIdentityHash(settings.accounts[i]) == identity) {
            index = (int)i;
            break;
        }
    }
    int target = index + direction;
    if (index < 0 || target < 0 || target >= (int)settings.accounts.size()) return;
    std::swap(settings.accounts[index], settings.accounts[target]);
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(settings));
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not reorder the account.", L"Account",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    RefreshSettingsControls(state);
}

static void ToggleAccountFromSettingsWindow(SettingsWindowState& state) {
    uint64_t identity = SelectedAccountIdentity(state);
    if (!identity) return;

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    int index = -1;
    int visibleCount = 0;
    for (size_t i = 0; i < settings.accounts.size(); i++) {
        if (!settings.accounts[i].hidden) visibleCount++;
        if (AccountIdentityHash(settings.accounts[i]) == identity) index = (int)i;
    }
    if (index < 0 || (!settings.accounts[index].hidden && visibleCount <= 1)) return;
    settings.accounts[index].hidden = !settings.accounts[index].hidden;
    SettingsApplyResult applyResult = ApplyOwnedSettings(settings);
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not update account visibility.", L"Account",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        std::wstring hashes;
        wchar_t buffer[24];
        for (const auto& account : settings.accounts) {
            if (!account.hidden) continue;
            if (!hashes.empty()) hashes += L";";
            swprintf(buffer, ARRAYSIZE(buffer), L"%016llx",
                     (unsigned long long)AccountIdentityHash(account));
            hashes += buffer;
        }
        Wh_SetStringValue(L"hiddenAccounts", hashes.c_str());
        FinishSettingsApply();
    }
    RefreshSettingsControls(state);
}

// src/settings/window.h
// Settings window procedure, thread, and activation.

static UINT GetSettingsActivateMessage() {
    static const UINT message =
        RegisterWindowMessageW(L"Windhawk_ActivateSettings_" WH_MOD_ID);
    return message;
}

static void ResetCurrentSettingsPage(SettingsWindowState& state) {
    int page = state.currentPage;
    if (page <= 0 || page >= 4) return;
    if (SettingsMessageBoxW(state.hWnd,
                    L"Reset every setting on this page to its default value?",
                    L"Reset settings", MB_YESNO | MB_ICONQUESTION) != IDYES ||
        g_unloading || !IsWindow(state.hWnd)) {
        return;
    }

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    Settings defaults;
    if (page == 1) {
        settings.taskbarMonitorMode = defaults.taskbarMonitorMode;
        settings.taskbarMonitorNumber = defaults.taskbarMonitorNumber;
        settings.barLayout = defaults.barLayout;
        settings.barMode = defaults.barMode;
        settings.barLength = defaults.barLength;
        settings.barThickness = defaults.barThickness;
        settings.labelFontSize = defaults.labelFontSize;
        settings.percentFontSize = defaults.percentFontSize;
        settings.accountMargin = defaults.accountMargin;
        settings.labelGap = defaults.labelGap;
        settings.barGap = defaults.barGap;
        settings.rightMargin = defaults.rightMargin;
    } else if (page == 2) {
        settings.labelPosition = defaults.labelPosition;
        settings.showPaceTicks = defaults.showPaceTicks;
        settings.paceTickStyle = defaults.paceTickStyle;
        settings.paceTickColor = defaults.paceTickColor;
        settings.showBarLabels = defaults.showBarLabels;
        settings.percentTextVisibility = defaults.percentTextVisibility;
        settings.percentTextAlignment = defaults.percentTextAlignment;
        settings.showExtraBarAmounts = defaults.showExtraBarAmounts;
        settings.showOpenAiExtraLimits = defaults.showOpenAiExtraLimits;
        settings.colorblindMode = defaults.colorblindMode;
        settings.showStaleWarning = defaults.showStaleWarning;
        settings.yellowThreshold = defaults.yellowThreshold;
        settings.orangeThreshold = defaults.orangeThreshold;
        settings.redThreshold = defaults.redThreshold;
    } else {
        settings.clickAction = defaults.clickAction;
        settings.pollMinutes = defaults.pollMinutes;
        settings.enableNotifications = defaults.enableNotifications;
        settings.historyEnabled = defaults.historyEnabled;
        settings.historyRetentionDays = defaults.historyRetentionDays;
        settings.skipHiddenPolling = defaults.skipHiddenPolling;
    }
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(settings));
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not reset settings.", L"Reset settings",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    RefreshSettingsControls(state);
}

static LRESULT CALLBACK SettingsWindowProc(HWND hWnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SettingsWindowState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (UINT activateMessage = GetSettingsActivateMessage();
        activateMessage && message == activateMessage) {
        if (!g_unloading) {
            if (IsIconic(hWnd)) ShowWindow(hWnd, SW_RESTORE);
            HWND target = GetLastActivePopup(hWnd);
            SetForegroundWindow(IsWindow(target) ? target : hWnd);
        }
        return 0;
    }
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<SettingsWindowState*>(create->lpCreateParams);
            state->hWnd = hWnd;
            state->dpi = WindowDpi(hWnd);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            const PCWSTR pageNames[] = {L"Accounts", L"Layout", L"Display", L"Behavior"};
            const int pageIds[] = {kSettingsPageAccounts, kSettingsPageLayout,
                                   kSettingsPageDisplay, kSettingsPageBehavior};
            for (int i = 0; i < 4; i++) {
                DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                              BS_AUTORADIOBUTTON | BS_PUSHLIKE;
                if (i == 0) style |= WS_GROUP;
                state->pageButtons[i] = CreateWindowExW(
                    0, L"BUTTON", pageNames[i], style, 0, 0, 1, 1, hWnd,
                    reinterpret_cast<HMENU>((INT_PTR)pageIds[i]),
                    GetModuleHandleW(nullptr), nullptr);
            }
            state->accountList = CreateSettingsControl(
                *state, 0, WC_LISTVIEWW, L"",
                WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                WS_EX_CLIENTEDGE, kAccountList);
            ListView_SetExtendedListViewStyle(state->accountList,
                                              LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            if (!SetWindowSubclass(state->accountList, AccountListSubclassProc, 0,
                                   reinterpret_cast<DWORD_PTR>(state))) {
                Wh_Log(L"Could not subclass settings account list");
            }
            const PCWSTR headers[] = {L"Label", L"Provider", L"Bars", L"Visible", L"Sign-in", L"Recorded"};
            const int widths[] = {80, 135, 110, 55, 125, 100};
            for (int i = 0; i < (int)ARRAYSIZE(headers); i++) {
                LVCOLUMNW column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH;
                column.pszText = const_cast<PWSTR>(headers[i]);
                column.cx = ScaleForDpi(widths[i], state->dpi);
                ListView_InsertColumn(state->accountList, i, &column);
            }
            CreateSettingsControl(*state, 0, L"BUTTON", L"Add...",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountAdd);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Edit...",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountEdit);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Remove",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountRemove);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Up",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountMoveUp);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Down",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountMoveDown);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Hide",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountToggleVisible);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Sign in",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountSignIn);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Sign out",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountSignOut);

            AddSettingsCheck(*state, 1, L"Preview test data", kVisualTestModeLayout);
            HWND monitorMode = AddSettingsRow(*state, 1, L"Taskbar monitors",
                                               L"COMBOBOX", CBS_DROPDOWNLIST, 0, kMonitorMode);
            AddComboItems(monitorMode, {L"Primary monitor only", L"All monitors",
                                        L"Specific display"});
            AddSettingsRow(*state, 1, L"Specific display", L"COMBOBOX",
                           CBS_DROPDOWNLIST, 0, kMonitorNumber);
            HWND layout = AddSettingsRow(*state, 1, L"Bar layout", L"COMBOBOX",
                                         CBS_DROPDOWNLIST, 0, kBarLayout);
            AddComboItems(layout, {L"Stacked horizontal", L"Vertical"});
            HWND mode = AddSettingsRow(*state, 1, L"Bar mode", L"COMBOBOX",
                                       CBS_DROPDOWNLIST, 0, kBarMode);
            AddComboItems(mode, {L"Used", L"Remaining"});
            AddNumericRow(*state, 1, L"Bar length (px)", kBarLength,
                          10, 500, true);
            AddNumericRow(*state, 1, L"Bar thickness (px)", kBarThickness,
                          2, 50, true);
            AddNumericRow(*state, 1, L"Label font size (px)", kLabelFontSize,
                          6, 24, true);
            AddNumericRow(*state, 1, L"Bar text size (px)", kPercentFontSize,
                          6, 24, true);
            AddNumericRow(*state, 1, L"Account margin (px)", kAccountMargin, 0, 500);
            AddNumericRow(*state, 1, L"Label gap (px)", kLabelGap, 0, 500);
            AddNumericRow(*state, 1, L"Bar gap (px)", kBarGap, 0, 500);
            AddNumericRow(*state, 1, L"Right tray gap (px)", kRightMargin, 0, 500);

            AddSettingsCheck(*state, 2, L"Preview test data", kVisualTestModeDisplay);
            HWND labelPosition = AddSettingsRow(*state, 2, L"Label position", L"COMBOBOX",
                                                CBS_DROPDOWNLIST, 0, kLabelPosition);
            AddComboItems(labelPosition, {L"Hidden", L"Left", L"Top", L"Right", L"Bottom"});
            AddSettingsCheck(*state, 2, L"Show quota pace ticks", kShowPaceTicks);
            HWND paceTickStyle = AddSettingsRow(*state, 2, L"Pace tick style", L"COMBOBOX",
                                                CBS_DROPDOWNLIST, 0, kPaceTickStyle);
            AddComboItems(paceTickStyle,
                          {L"Caret", L"Full line", L"Edge notch", L"Dot"});
            HWND paceTickColor = AddSettingsRow(*state, 2, L"Pace tick color", L"BUTTON",
                                                BS_PUSHBUTTON, 0, kPaceTickColor);
            SetWindowTextW(paceTickColor, L"Choose...");
            state->rows[2].back().preview = CreateSettingsControl(
                *state, 2, L"STATIC", L"", WS_VISIBLE | SS_OWNERDRAW, 0, -1);
            AddSettingsCheck(*state, 2, L"Show bar labels (5h, 7d, Fa, Ex)",
                             kShowBarLabels);
            HWND percentTextVisibility = AddSettingsRow(
                *state, 2, L"Percentage text", L"COMBOBOX",
                CBS_DROPDOWNLIST, 0, kPercentTextVisibility);
            AddComboItems(percentTextVisibility,
                          {L"Never show", L"Show on hover", L"Always show"});
            HWND percentTextAlignment = AddSettingsRow(
                *state, 2, L"Percentage text alignment", L"COMBOBOX",
                CBS_DROPDOWNLIST, 0, kPercentTextAlignment);
            AddComboItems(percentTextAlignment, {L"Adaptive", L"Left", L"Center", L"Right"});
            AddSettingsCheck(*state, 2, L"Show amounts on extra/credits bars",
                             kShowExtraBarAmounts);
            AddSettingsCheck(*state, 2, L"Show additional OpenAI rate limits",
                             kShowOpenAiExtraLimits);
            AddSettingsCheck(*state, 2, L"Use colorblind palette", kColorblindMode);
            AddSettingsCheck(*state, 2, L"Mark stale data with !", kShowStaleWarning);
            AddThresholdRow(*state, L"Yellow threshold (%)", kYellowThreshold);
            AddThresholdRow(*state, L"Orange threshold (%)", kOrangeThreshold);
            AddThresholdRow(*state, L"Red threshold (%)", kRedThreshold);

            HWND clickAction = AddSettingsRow(*state, 3, L"Left-click action",
                                               L"COMBOBOX", CBS_DROPDOWNLIST, 0, kClickAction);
            AddComboItems(clickAction, {L"Refresh account", L"Open provider dashboard"});
            HWND pollPreset = AddSettingsRow(*state, 3, L"Cloud poll interval",
                                              L"COMBOBOX", CBS_DROPDOWNLIST, 0, kPollPreset);
            AddComboItems(pollPreset, {L"2 minutes", L"5 minutes", L"10 minutes",
                                      L"15 minutes", L"30 minutes", L"60 minutes", L"Custom"});
            AddNumericRow(*state, 3, L"Custom interval (minutes)",
                          kPollMinutes, 2, 1440);
            AddSettingsCheck(*state, 3, L"Show threshold notifications", kEnableNotifications);
            AddSettingsCheck(*state, 3, L"Enable local history", kHistoryEnabled);
            AddNumericRow(*state, 3, L"Retention (days)", kHistoryRetentionDays, 1, 3650);
            AddSettingsCheck(*state, 3, L"Do not poll hidden accounts", kSkipHiddenPolling);
            HWND historyButton = AddSettingsRow(*state, 3, L"Local usage history", L"BUTTON",
                                                 BS_PUSHBUTTON, 0, kOpenHistoryWindow);
            SetWindowTextW(historyButton, L"Usage history...");
            state->resetPageButton = CreateWindowExW(
                0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, hWnd,
                reinterpret_cast<HMENU>((INT_PTR)kResetPage),
                GetModuleHandleW(nullptr), nullptr);
            state->previousRowsButton = CreateWindowExW(
                0, L"BUTTON", L"Previous", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, hWnd,
                reinterpret_cast<HMENU>((INT_PTR)kSettingsRowsPrevious),
                GetModuleHandleW(nullptr), nullptr);
            state->rowsPageLabel = CreateWindowExW(
                0, L"STATIC", L"", WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
                0, 0, 1, 1, hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            state->nextRowsButton = CreateWindowExW(
                0, L"BUTTON", L"Next", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, hWnd,
                reinterpret_cast<HMENU>((INT_PTR)kSettingsRowsNext),
                GetModuleHandleW(nullptr), nullptr);

            state->toolTip = CreateWindowExW(
                WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (state->toolTip) {
                SendMessageW(state->toolTip, TTM_SETMAXTIPWIDTH, 0,
                             ScaleForDpi(360, state->dpi));
                AddSettingsToolTip(
                    *state, kVisualTestModeLayout,
                    L"Temporarily replaces taskbar accounts with synthetic percentages while this settings window is open.");
                AddSettingsToolTip(
                    *state, kVisualTestModeDisplay,
                    L"Temporarily replaces taskbar accounts with synthetic percentages while this settings window is open.");
                AddSettingsToolTip(
                    *state, kShowPaceTicks,
                    L"Marks elapsed time in each quota reset window so usage can be compared with its expected pace.");
                AddSettingsToolTip(
                    *state, kPaceTickStyle,
                    L"Draws each pace marker as a thin caret, a full-width line, an edge notch, or a centered dot.");
                AddSettingsToolTip(
                    *state, kShowExtraBarAmounts,
                    L"Replaces the percentage on Anthropic extra-usage and OpenAI credits bars with the dollar or credit amount: spent in used mode, left in remaining mode.");
                AddSettingsToolTip(
                    *state, kShowOpenAiExtraLimits,
                    L"Adds every extra OpenAI rate limit (Codex Spark, hidden model lanes such as gpt-reserve) and the Spark plan name to OpenAI account tooltips.");
                AddSettingsToolTip(
                    *state, kShowStaleWarning,
                    L"Adds ! when quota data is stale because a refresh failed or is overdue.");
                AddSettingsToolTip(
                    *state, kPollPreset,
                    L"How often Anthropic and OpenAI are polled. Antigravity always polls its local session once per minute.");
                AddSettingsToolTip(
                    *state, kPollMinutes,
                    L"Custom cloud polling interval from 2 to 1440 minutes.");
                AddSettingsToolTip(
                    *state, kHistoryEnabled,
                    L"Opt in for this Windows user only; off by default. Disabling recording keeps saved history. "
                    L"Account recording choices, retention, and hidden-account polling settings are shared "
                    L"across Windows users. Do not poll hidden accounts takes priority for hidden accounts.");
                AddSettingsToolTip(
                    *state, kHistoryRetentionDays,
                    L"Shared across Windows users. Keep local usage history for 1 to 3650 days. Reducing this permanently prunes older "
                    L"samples at hourly maintenance during recording or viewer refresh. The roughly 60 MB cap can shorten retention.");
                AddSettingsToolTip(
                    *state, kSkipHiddenPolling,
                    L"Shared across Windows users. Skip polling hidden accounts, even if Record history is checked. Turn this off "
                    L"to allow hidden accounts to keep recording when local history is enabled.");
                AddSettingsToolTip(
                    *state, kOpenHistoryWindow,
                    L"View saved local usage history, including while recording is disabled.");
                AddSettingsToolTip(
                    *state, kAccountList,
                    L"Recorded shows whether history recording is enabled, not whether a poll succeeded. "
                    L"Storage error means recording is not being saved; open Usage history for details. "
                    L"User off requires this Windows user's Enable local history in Behavior; Paused means the hidden "
                    L"account is skipped by Do not poll hidden accounts. Pending save blocks recording "
                    L"until stable history IDs are saved by the taskbar's Explorer process.");
            }

            RecreateSettingsVisuals(*state);
            RefreshSettingsControls(*state);
            ShowSettingsPage(*state, 0);
            LayoutSettingsWindow(*state);
            return 0;
        }
        case WM_NOTIFY:
            if (!state) break;
            if (reinterpret_cast<NMHDR*>(lParam)->code == UDN_DELTAPOS) {
                auto* delta = reinterpret_cast<NMUPDOWN*>(lParam);
                if (SettingsRow* row = FindSettingsRow(*state, delta->hdr.hwndFrom)) {
                    int id = GetDlgCtrlID(row->control);
                    int64_t proposed = (int64_t)GetControlInt(hWnd, id, delta->iPos) +
                                       delta->iDelta;
                    int value = (int)std::clamp<int64_t>(proposed, row->minimum, row->maximum);
                    state->updating = true;
                    SetControlInt(*state, id, value);
                    state->updating = false;
                    SetTimer(hWnd, kSettingsAutosaveTimer, 250, nullptr);
                    return TRUE;
                }
            }
            if (reinterpret_cast<NMHDR*>(lParam)->hwndFrom == state->accountList) {
                if (reinterpret_cast<NMHDR*>(lParam)->code == LVN_ITEMCHANGED) {
                    UpdateAccountButtons(*state);
                } else if (reinterpret_cast<NMHDR*>(lParam)->code == NM_DBLCLK) {
                    PostMessageW(hWnd, WM_COMMAND,
                                 MAKEWPARAM(kAccountEdit, BN_CLICKED), 0);
                }
            }
            break;
        case WM_DRAWITEM:
            if (state) {
                auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                for (const auto& pageRows : state->rows) {
                    for (const auto& row : pageRows) {
                        if (!row.preview || draw->hwndItem != row.preview) continue;
                        HBRUSH brush = (HBRUSH)GetStockObject(DC_BRUSH);
                        SetDCBrushColor(draw->hDC,
                                        state->dark ? RGB(105, 105, 105) : RGB(120, 120, 120));
                        FillRect(draw->hDC, &draw->rcItem, brush);
                        RECT inner = draw->rcItem;
                        InflateRect(&inner, -1, -1);
                        SetDCBrushColor(draw->hDC, row.previewColor);
                        FillRect(draw->hDC, &inner, brush);
                        return TRUE;
                    }
                }
            }
            break;
        case WM_HSCROLL:
            if (state && lParam) {
                HWND slider = reinterpret_cast<HWND>(lParam);
                if (SettingsRow* row = FindSettingsRow(*state, slider);
                    row && row->slider == slider) {
                    int id = GetDlgCtrlID(row->control);
                    int value = (int)SendMessageW(slider, TBM_GETPOS, 0, 0);
                    state->updating = true;
                    SetControlInt(*state, id, value);
                    state->updating = false;
                    int action = LOWORD(wParam);
                    if (action == TB_ENDTRACK) {
                        CommitScalarSettings(*state);
                    } else if (action == TB_LINEUP || action == TB_LINEDOWN ||
                               action == TB_PAGEUP || action == TB_PAGEDOWN ||
                               action == TB_TOP || action == TB_BOTTOM ||
                               action == TB_THUMBPOSITION) {
                        SetTimer(hWnd, kSettingsAutosaveTimer, 250, nullptr);
                    }
                    return 0;
                }
            }
            break;
        case WM_COMMAND:
            if (!state) break;
            switch (LOWORD(wParam)) {
                case kSettingsPageAccounts:
                case kSettingsPageLayout:
                case kSettingsPageDisplay:
                case kSettingsPageBehavior:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        ShowSettingsPage(*state, LOWORD(wParam) - kSettingsPageAccounts);
                        LayoutSettingsWindow(*state);
                    }
                    return 0;
                case kAccountAdd:
                    if (HIWORD(wParam) == BN_CLICKED) AddAccountFromSettingsWindow(*state);
                    return 0;
                case kAccountEdit:
                    if (HIWORD(wParam) == BN_CLICKED && IsWindowEnabled(hWnd) &&
                        IsWindowEnabled(GetDlgItem(hWnd, kAccountEdit)) &&
                        !g_loginInProgress.load()) {
                        EditAccountFromSettingsWindow(*state);
                    }
                    return 0;
                case kAccountRemove:
                    if (HIWORD(wParam) == BN_CLICKED) RemoveAccountFromSettingsWindow(*state);
                    return 0;
                case kAccountMoveUp:
                    if (HIWORD(wParam) == BN_CLICKED) MoveAccountFromSettingsWindow(*state, -1);
                    return 0;
                case kAccountMoveDown:
                    if (HIWORD(wParam) == BN_CLICKED) MoveAccountFromSettingsWindow(*state, 1);
                    return 0;
                case kAccountToggleVisible:
                    if (HIWORD(wParam) == BN_CLICKED) ToggleAccountFromSettingsWindow(*state);
                    return 0;
                case kAccountSignIn:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        if (uint64_t identity = SelectedAccountIdentity(*state)) {
                            StartLoginByIdentity(identity);
                        }
                        UpdateAccountButtons(*state);
                    }
                    return 0;
                case kAccountSignOut:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        if (uint64_t identity = SelectedAccountIdentity(*state)) {
                            if (!SignOutAccountByIdentity(identity)) {
                                SettingsMessageBoxW(
                                    state->hWnd,
                                    L"The stored sign-in could not be deleted and remains "
                                    L"retained for this account.",
                                    L"Account", MB_OK | MB_ICONERROR);
                            }
                        }
                    }
                    return 0;
                case kResetPage:
                    if (HIWORD(wParam) == BN_CLICKED) ResetCurrentSettingsPage(*state);
                    return 0;
                case kOpenHistoryWindow:
                    if (HIWORD(wParam) == BN_CLICKED && !g_unloading) {
                        CommitScalarSettings(*state);
                        if (!g_unloading && !g_settingsWindowCancelRequested) OpenHistoryWindow();
                    }
                    return 0;
                case kSettingsRowsPrevious:
                    if (HIWORD(wParam) == BN_CLICKED && state->currentPage > 0) {
                        state->rowSubpages[state->currentPage]--;
                        LayoutSettingsWindow(*state);
                    }
                    return 0;
                case kSettingsRowsNext:
                    if (HIWORD(wParam) == BN_CLICKED && state->currentPage > 0) {
                        state->rowSubpages[state->currentPage]++;
                        LayoutSettingsWindow(*state);
                    }
                    return 0;
                case kVisualTestModeLayout:
                case kVisualTestModeDisplay:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        SetVisualTestMode(
                            SendMessageW(reinterpret_cast<HWND>(lParam), BM_GETCHECK,
                                         0, 0) == BST_CHECKED);
                    }
                    return 0;
                case kPaceTickColor:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        SettingsRow* row = FindSettingsRow(
                            *state, GetDlgItem(hWnd, kPaceTickColor));
                        if (!row) return 0;
                        CHOOSECOLORW chooser{};
                        chooser.lStructSize = sizeof(chooser);
                        chooser.hwndOwner = hWnd;
                        chooser.rgbResult = row->previewColor;
                        chooser.lpCustColors = state->customColors.data();
                        chooser.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ENABLEHOOK;
                        chooser.lCustData = reinterpret_cast<LPARAM>(state);
                        chooser.lpfnHook = SettingsColorHookProc;
                        bool accepted = ChooseColorW(&chooser);
                        state->colorDialog = nullptr;
                        if (accepted) {
                            row->previewColor = chooser.rgbResult;
                            if (row->preview) InvalidateRect(row->preview, nullptr, TRUE);
                            CommitScalarSettings(*state);
                        }
                    }
                    return 0;
                case kPollPreset:
                    if (HIWORD(wParam) == CBN_SELCHANGE &&
                        SendDlgItemMessageW(hWnd, kPollPreset, CB_GETCURSEL, 0, 0) == 6) {
                        UpdateDependentSettingsControls(*state);
                        SetFocus(GetDlgItem(hWnd, kPollMinutes));
                    } else if (HIWORD(wParam) == CBN_SELCHANGE) {
                        CommitScalarSettings(*state);
                    }
                    return 0;
            }
            if ((HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == BN_CLICKED ||
                 HIWORD(wParam) == EN_KILLFOCUS) && LOWORD(wParam) >= kMonitorMode &&
                LOWORD(wParam) <= kSkipHiddenPolling) {
                CommitScalarSettings(*state);
                return 0;
            }
            break;
        case WM_SIZE:
            if (state) LayoutSettingsWindow(*state);
            return 0;
        case WM_TIMER:
            if (state && wParam == kSettingsAutosaveTimer) {
                KillTimer(hWnd, kSettingsAutosaveTimer);
                CommitScalarSettings(*state);
                return 0;
            }
            break;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            UINT dpi = state ? state->dpi : 96;
            MONITORINFO monitorInfo{sizeof(monitorInfo)};
            GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
            int workWidth = (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left);
            int workHeight = (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top);
            info->ptMinTrackSize.x = std::min(ScaleForDpi(500, dpi), workWidth);
            if (state && state->lockedWindowHeight > 0) {
                int lockedHeight = std::min(state->lockedWindowHeight, workHeight);
                info->ptMinTrackSize.y = lockedHeight;
                info->ptMaxTrackSize.y = lockedHeight;
            } else {
                info->ptMinTrackSize.y = std::min(ScaleForDpi(400, dpi), workHeight);
            }
            return 0;
        }
        case WM_ENTERSIZEMOVE:
            if (state) state->inSizeMove = true;
            return 0;
        case WM_DPICHANGED:
            if (state) {
                state->dpi = HIWORD(wParam);
                state->lockedWindowHeight = 0;
                auto* suggested = reinterpret_cast<RECT*>(lParam);
                SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                RecreateSettingsVisuals(*state);
                if (state->toolTip) {
                    SendMessageW(state->toolTip, TTM_SETMAXTIPWIDTH, 0,
                                 ScaleForDpi(360, state->dpi));
                }
                if (!state->inSizeMove) FitSettingsWindowToContent(*state);
                LayoutSettingsWindow(*state);
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (state) {
                RecreateSettingsVisuals(*state);
                if (wParam == SPI_SETWORKAREA) {
                    FitSettingsWindowToContent(*state);
                    LayoutSettingsWindow(*state);
                }
            }
            return 0;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            if (state) RecreateSettingsVisuals(*state);
            return 0;
        case WM_DISPLAYCHANGE:
            if (state) {
                int monitorNumber;
                {
                    std::lock_guard<std::mutex> lk(g_settingsMutex);
                    monitorNumber = g_settings.taskbarMonitorNumber;
                }
                state->updating = true;
                PopulateMonitorCombo(*state, monitorNumber);
                state->updating = false;
                UpdateDependentSettingsControls(*state);
                StartRetryInject(true);
                FitSettingsWindowToContent(*state);
                LayoutSettingsWindow(*state);
            }
            return 0;
        case WM_EXITSIZEMOVE:
            if (state) {
                state->inSizeMove = false;
                FitSettingsWindowToContent(*state, false);
                LayoutSettingsWindow(*state);
            }
            return 0;
        case kExitVisualTestMessage:
            SetVisualTestMode(false);
            return 0;
        case kHistoryConsentChangedMessage:
        case kSettingsRefreshMessage:
            if (state) {
                if (message == kHistoryConsentChangedMessage) {
                    bool historyEnabled;
                    {
                        std::lock_guard lock(g_settingsMutex);
                        historyEnabled = g_settings.historyEnabled;
                    }
                    // Only synchronize the externally changed toggle; don't overwrite
                    // unrelated edits in an already-open settings window.
                    bool updating = state->updating;
                    state->updating = true;
                    SendDlgItemMessageW(hWnd, kHistoryEnabled, BM_SETCHECK,
                                        historyEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
                    UpdateDependentSettingsControls(*state);
                    state->updating = updating;
                }
                bool enabled = g_visualTestMode.load(std::memory_order_acquire);
                SendDlgItemMessageW(hWnd, kVisualTestModeLayout, BM_SETCHECK,
                                    enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                SendDlgItemMessageW(hWnd, kVisualTestModeDisplay, BM_SETCHECK,
                                    enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                RefreshAccountList(*state);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            if (state && state->dark) {
                SetTextColor(reinterpret_cast<HDC>(wParam), RGB(235, 235, 235));
                SetBkColor(reinterpret_cast<HDC>(wParam), RGB(32, 32, 32));
                return reinterpret_cast<LRESULT>(state->backgroundBrush);
            }
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            if (state && state->dark) {
                SetTextColor(reinterpret_cast<HDC>(wParam), RGB(235, 235, 235));
                SetBkColor(reinterpret_cast<HDC>(wParam), RGB(43, 43, 43));
                return reinterpret_cast<LRESULT>(state->inputBrush);
            }
            break;
        case WM_ERASEBKGND:
            if (state && state->backgroundBrush) {
                RECT client{};
                GetClientRect(hWnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                return 1;
            }
            break;
        case WM_CLOSE:
            if (state && state->colorDialog) {
                PostMessageW(state->colorDialog, WM_COMMAND, IDCANCEL, 0);
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (g_settingsMessageBoxContext &&
                IsWindow(g_settingsMessageBoxContext->hWnd)) {
                g_settingsMessageBoxContext->forced = true;
                PostMessageW(g_settingsMessageBoxContext->hWnd, WM_COMMAND,
                             g_settingsMessageBoxContext->forcedResult, 0);
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (state && !g_unloading) CommitScalarSettings(*state, false);
            SetVisualTestMode(false);
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            if (state) {
                if (state->font) DeleteObject(state->font);
                if (state->backgroundBrush) DeleteObject(state->backgroundBrush);
                if (state->inputBrush) DeleteObject(state->inputBrush);
                state->font = nullptr;
                state->backgroundBrush = nullptr;
                state->inputBrush = nullptr;
                state->hWnd = nullptr;
                state->accountList = nullptr;
            }
            g_settingsWindow.store(nullptr);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

static DWORD WINAPI SettingsWindowThreadProc(LPVOID) {
    using SetThreadDpiAwarenessContext_t = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto setDpi = reinterpret_cast<SetThreadDpiAwarenessContext_t>(
            GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        apartmentInitialized = true;
    } catch (...) {}

    INITCOMMONCONTROLSEX controls{sizeof(controls),
                                  ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES |
                                      ICC_BAR_CLASSES | ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&controls);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW settingsClass{};
    settingsClass.cbSize = sizeof(settingsClass);
    settingsClass.lpfnWndProc = SettingsWindowProc;
    settingsClass.hInstance = instance;
    settingsClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    settingsClass.lpszClassName = kSettingsWindowClass;
    settingsClass.hbrBackground = nullptr;
    WNDCLASSEXW accountClass = settingsClass;
    accountClass.lpfnWndProc = AccountEditorWndProc;
    accountClass.lpszClassName = kAccountEditorClass;
    if (!RegisterClassExW(&settingsClass)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS ||
            !UnregisterClassW(kSettingsWindowClass, instance) ||
            !RegisterClassExW(&settingsClass)) {
            if (apartmentInitialized) winrt::uninit_apartment();
            return 0;
        }
    }
    if (!RegisterClassExW(&accountClass)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS ||
            !UnregisterClassW(kAccountEditorClass, instance) ||
            !RegisterClassExW(&accountClass)) {
            UnregisterClassW(kSettingsWindowClass, instance);
            if (apartmentInitialized) winrt::uninit_apartment();
            return 0;
        }
    }
    if (g_unloading || g_settingsWindowCancelRequested) {
        UnregisterClassW(kAccountEditorClass, instance);
        UnregisterClassW(kSettingsWindowClass, instance);
        if (apartmentInitialized) winrt::uninit_apartment();
        return 0;
    }

    SettingsWindowState state;
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    UINT dpi = WindowDpi(nullptr);
    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    if (!shcore) {
        shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (shcore) {
        using GetDpiForMonitor_t = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        auto getDpi = reinterpret_cast<GetDpiForMonitor_t>(
            GetProcAddress(shcore, "GetDpiForMonitor"));
        UINT monitorDpiX = dpi;
        UINT monitorDpiY = dpi;
        if (getDpi && SUCCEEDED(getDpi(monitor, 0, &monitorDpiX, &monitorDpiY))) {
            dpi = monitorDpiX;
        }
    }
    int width = ScaleForDpi(700, dpi);
    int height = ScaleForDpi(400, dpi);
    width = std::min(width, (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left));
    height = std::min(height, (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top));
    int x = monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    int y = monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    HWND window = CreateWindowExW(WS_EX_APPWINDOW, kSettingsWindowClass,
                                  L"Taskbar AI Quota Bars - Settings",
                                  (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) |
                                      WS_CLIPCHILDREN,
                                  x, y, width, height, nullptr, nullptr, instance, &state);
    if (window) {
        FitSettingsWindowToContent(state);
        LayoutSettingsWindow(state);
        g_settingsWindow.store(window);
        if (g_unloading || g_settingsWindowCancelRequested) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        } else {
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
        }
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (IsWindow(window)) DestroyWindow(window);
    }
    SetVisualTestMode(false);
    g_settingsWindow.store(nullptr);
    UnregisterClassW(kAccountEditorClass, instance);
    UnregisterClassW(kSettingsWindowClass, instance);
    if (apartmentInitialized) winrt::uninit_apartment();
    return 0;
}

static void OpenSettingsWindow() {
    if (g_unloading) return;
    std::lock_guard<std::mutex> lock(g_settingsWindowMutex);
    if (HWND window = g_settingsWindow.load(); window && IsWindow(window)) {
        if (UINT message = GetSettingsActivateMessage()) {
            PostMessageW(window, message, 0, 0);
        }
        return;
    }
    if (g_settingsWindowThread) {
        if (WaitForSingleObject(g_settingsWindowThread, 0) != WAIT_OBJECT_0) return;
        CloseHandle(g_settingsWindowThread);
        g_settingsWindowThread = nullptr;
    }
    if (g_unloading) return;
    g_settingsWindowCancelRequested = false;
    g_settingsWindowThread = CreateThread(nullptr, 0, SettingsWindowThreadProc,
                                          nullptr, 0, nullptr);
    if (!g_settingsWindowThread) Wh_Log(L"Could not create settings window thread");
}

// src/history/window-state.h
// History window thread state, control IDs, meter and view tables, and view-model types.

static HANDLE g_historyWindowThread = nullptr;
static std::mutex g_historyWindowMutex;
static std::atomic<HWND> g_historyWindow{nullptr};
static std::atomic<HWND> g_historyDialog{nullptr};
static std::atomic<bool> g_historyWindowCancelRequested{false};

static constexpr PCWSTR kHistoryWindowClass = L"AiQuotaHistory_" WH_MOD_ID;
static constexpr PCWSTR kHistoryGraphClass = L"AiQuotaHistoryGraph_" WH_MOD_ID;
static constexpr UINT kHistoryActivateMessage = WM_APP + 181;
static constexpr UINT kHistoryLoadMessage = WM_APP + 182;
static constexpr int kHistorySeriesHeight = 90;
static constexpr ULONGLONG kHistoryWeekMs = 7 * kHistoryDayMs;

enum HistoryControlId {
    kHistoryAccountCombo = 2800, kHistoryMeterCombo, kHistoryRangeCombo,
    kHistoryOverlayCheck, kHistoryRefreshButton, kHistoryExportButton,
    kHistoryClearButton, kHistorySettingsButton, kHistoryGraphControl, kHistoryInfoButton,
    kHistoryEnableButton,
};

static constexpr PCWSTR kHistoryMeterNames[kHistoryMeterCount] = {
    L"Short-term used (%)", L"Weekly used (%)", L"Fable weekly used (%)", L"Extra budget used (%)",
    L"Claude/GPT short-term used (%)", L"Claude/GPT weekly used (%)",
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
static constexpr unsigned kHistoryProviderMeters[] = {
    0, (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3) | (1U << 6),
    (1U << 0) | (1U << 1) | (1U << 7), (1U << 0) | (1U << 1) | (1U << 4) | (1U << 5),
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
    size_t sourceRow = 0;  // Raw provenance for structural tests; paint never dereferences it.
    bool connect = false;
};

struct HistoryRenderSeries {
    std::vector<HistoryRenderPoint> points;
};

struct HistoryUiAccount {
    uint64_t id = 0;
    uint32_t provider = 0;
    std::wstring title, label;
    bool configured = false;
    bool recording = false;
    bool hidden = false;
    bool showWeekly = true, showFableWeekly = true;
    std::vector<size_t> rows;
};

struct HistoryUiSeries {
    size_t account = 0;
    int meter = 0;
    std::wstring title;
    std::wstring legendTitle, hoverTitle;
    RECT legendRect{};  // Unscrolled graph coordinates, shared by paint and hit testing.
    COLORREF color = 0;
    int stroke = 3;  // Logical pixels; scoped quotas use 2, never thin 1px dashes.
    bool plotted = true;
    HistoryStatistics current, previous;
    std::array<HistoryRenderSeries, 2> render;
};

struct HistoryAxis {
    double low = 0, high = 1;
    bool visible = false;
};

struct HistoryWindowState {
    HWND window = nullptr, graph = nullptr;
    HWND accountLabel = nullptr, meterLabel = nullptr, rangeLabel = nullptr;
    HWND accountCombo = nullptr, meterCombo = nullptr, rangeCombo = nullptr;
    HWND overlayCheck = nullptr, refreshButton = nullptr, exportButton = nullptr;
    HWND clearButton = nullptr, settingsButton = nullptr, legend = nullptr, status = nullptr;
    HWND infoButton = nullptr;
    HWND recordingBanner = nullptr, enableButton = nullptr;
    HWND exportTip = nullptr, graphTip = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    HistoryView allAccountsView = HistoryView::Weekly, accountView = HistoryView::AllMeters;
    int range = 0, selected = -1, scroll = 0, wheelRemainder = 0;
    int renderWidth = -1, layoutWidth = -1, graphHeight = 0;
    RECT plot{};  // Unscrolled; layout, reduction, hit tests and statistics share this geometry.
    POINT hoverPoint{-1, -1};  // Ignore mouse moves synthesized by positioning the tooltip.
    std::vector<size_t> hoverSeries;
    std::wstring graphTipText;
    bool graphTipActive = false, trackingMouse = false;
    bool overlay = false, closing = false, modal = false, loading = false;
    bool enabled = false, skipHidden = true, idsPendingSave = false;
    int retention = 90, redThreshold = 90;
    ULONGLONG from = 0, to = 0, readFrom = 0;
    std::array<HistoryAxis, 3> axes;  // Percent, USD, credits; never share unlike units.
    HistorySnapshot snapshot;
    std::vector<HistoryUiAccount> accounts;
    std::vector<HistoryUiSeries> visible;
    std::vector<std::wstring> noReadings;
    std::wstring snapshotStatus;
};

static void HistoryLayout(HistoryWindowState& state, bool newFont);

// src/history/view-model.h
// History series reduction, render cache, hover hit-testing, statistics, and loading.

// Formatting stays independent of locale for exact values/CSV. Dates on-screen are local.
static std::wstring HistoryNumber(double value) {
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) return L"?";
    return std::wstring(buffer, result.ptr);
}

static std::wstring HistoryLocalDate(ULONGLONG time) {
    SYSTEMTIME local{};
    if (!UnixMsToLocalSystemTime(time, &local)) return L"unknown local time";
    wchar_t text[48];
    swprintf(text, ARRAYSIZE(text), L"%04u-%02u-%02u %02u:%02u:%02u",
             local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
    return text;
}

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

static bool HistoryHasValue(const HistorySample& sample, int meter) {
    return !sample.failed && sample.meters[meter].value != -1 &&
           std::isfinite(sample.meters[meter].value);
}

enum class HistoryBreak { None, Gap, Reset, Unavailable };

// Shared by rendering and integration: no extrapolation, including at range boundaries.
static HistoryBreak HistoryConnection(const HistorySample& a, const HistorySample& b,
                                      int meter) {
    if (!HistoryHasValue(a, meter) || !HistoryHasValue(b, meter))
        return HistoryBreak::Unavailable;
    if (a.accountId != b.accountId || a.provider != b.provider || a.session != b.session ||
        b.timeMs <= a.timeMs || !a.intervalMs || !b.intervalMs)
        return HistoryBreak::Gap;
    ULONGLONG gap = b.timeMs - a.timeMs;
    ULONGLONG polling = std::max(a.intervalMs, b.intervalMs);
    // ceil(gap / 2) > polling is overflow-safe gap > 2 * polling.
    if (gap / 2 + gap % 2 > polling) return HistoryBreak::Gap;
    const auto& av = a.meters[meter];
    const auto& bv = b.meters[meter];
    if (av.durationMs != bv.durationMs) return HistoryBreak::Reset;
    if (!!av.resetMs != !!bv.resetMs) return HistoryBreak::Reset;
    if (av.resetMs) {
        // Reset endpoints can jitter. Never require exact equality; reject a boundary
        // within two minutes of this interval or a material shift of the reset epoch.
        constexpr ULONGLONG tolerance = 120000;
        ULONGLONG shift = av.resetMs > bv.resetMs ? av.resetMs - bv.resetMs : bv.resetMs - av.resetMs;
        ULONGLONG lower = a.timeMs > tolerance ? a.timeMs - tolerance : 0;
        ULONGLONG upper = b.timeMs > ULLONG_MAX - tolerance ? ULLONG_MAX : b.timeMs + tolerance;
        if ((av.resetMs >= lower && av.resetMs <= upper) ||
            (bv.resetMs >= lower && bv.resetMs <= upper)) return HistoryBreak::Reset;
        if (shift > tolerance) {
            // OpenAI's unused window rolls a full duration ahead until first use anchors it.
            // This is not a reset: the old deadline is still beyond this sampled interval.
            // Keep real boundaries, other providers, backward shifts, and partial windows conservative.
            if (a.provider == 2 && av.value == 0 && bv.value >= 0 && av.durationMs &&
                av.resetMs > upper && bv.resetMs > upper && bv.resetMs > av.resetMs &&
                (shift <= gap || shift - gap <= tolerance)) {
                ULONGLONG remaining = av.resetMs - a.timeMs;
                ULONGLONG error = remaining > av.durationMs ? remaining - av.durationMs : av.durationMs - remaining;
                if (error <= tolerance) return HistoryBreak::None;
            }
            return HistoryBreak::Reset;
        }
    }
    return HistoryBreak::None;
}

// Reduction boundary, also callable by isolated structural tests. Each continuous
// segment/pixel keeps its first, minimum, maximum and last raw samples in temporal
// order. A break flushes the bucket even inside the same pixel, so no extrema or
// segment endpoints are lost and no fabricated bridge crosses a reset/failure/gap.
static HistoryRenderSeries HistoryReducePeriod(const HistorySnapshot& snapshot,
                                               const std::vector<size_t>& rows,
                                               int meter, ULONGLONG from,
                                               ULONGLONG to, int width) {
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
            result.points.push_back({bucketX, snapshot.samples[row].meters[meter].value, row, connect});
            connect = true;
        }
        bucketX = -1;
    };
    // rows follow timestamp order, as established by HistoryLoad. The number of
    // occupied segment/pixel buckets is bounded by plot width + structural breaks.
    for (size_t position = 0; position < rows.size(); ++position) {
        if (g_unloading || g_historyWindowCancelRequested) return {};
        const auto& sample = snapshot.samples[rows[position]];
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
                if (value < snapshot.samples[rows[minimum]].meters[meter].value) minimum = position;
                if (value > snapshot.samples[rows[maximum]].meters[meter].value) maximum = position;
                last = position;
            }
        }
        previous = &sample;
    }
    flush();
    return result;
}

// Cache reduction and painting reserve identical space for additional unit axes.
static int HistoryPlotRightPadding(const HistoryWindowState& state) {
    int axes = 0;
    for (const auto& axis : state.axes) axes += axis.visible;
    return ScaleForDpi(26 + std::max(0, axes - 1) * 110, state.dpi);
}

static void HistoryClearHover(HistoryWindowState& state) {
    if (state.graphTip && state.graphTipActive) {
        TOOLINFOW tool{};
        tool.cbSize = TTTOOLINFOW_V2_SIZE;
        tool.hwnd = state.graph;
        tool.uId = 1;
        SendMessageW(state.graphTip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    }
    state.graphTipActive = false;
    state.hoverPoint = {-1, -1};
    // The native tool can retain lpszText even while hidden; do not discard its backing string.
    if (!state.hoverSeries.empty()) {
        state.hoverSeries.clear();
        if (state.graph) InvalidateRect(state.graph, nullptr, FALSE);
    }
    if (state.trackingMouse && state.graph) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_CANCEL | TME_LEAVE, state.graph, 0};
        TrackMouseEvent(&tracking);
    }
    state.trackingMouse = false;
}

// One projection for drawing and picking, including large finite values and unlike units.
static POINT HistoryPlotPoint(const HistoryWindowState& state, const HistoryUiSeries& series,
                              const HistoryRenderPoint& point) {
    const auto& axis = state.axes[series.meter < 6 ? 0 : series.meter - 5];
    double magnitude = std::max({1.0, std::abs(axis.low), std::abs(axis.high)});
    double low = axis.low / magnitude, span = axis.high / magnitude - low;
    double fraction = span > 0 ? (point.value / magnitude - low) / span : 0;
    return {state.plot.left + point.x,
            state.plot.bottom - int(std::clamp(fraction, 0.0, 1.0) * (state.plot.bottom - state.plot.top))};
}

static COLORREF HistoryLineColor(const HistoryWindowState& state, size_t index, bool highContrast) {
    COLORREF color = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : state.visible[index].color;
    if (state.hoverSeries.empty() ||
        std::find(state.hoverSeries.begin(), state.hoverSeries.end(), index) != state.hoverSeries.end()) return color;
    if (highContrast) return GetSysColor(COLOR_GRAYTEXT);
    COLORREF background = GetSysColor(COLOR_WINDOW);
    return RGB((GetRValue(color) + 3 * GetRValue(background)) / 4,
               (GetGValue(color) + 3 * GetGValue(background)) / 4,
               (GetBValue(color) + 3 * GetBValue(background)) / 4);
}

// Only selection/snapshot/size handlers build caches; WM_PAINT never scans raw rows.
// Selection rebuilds invalidate renderWidth even if the physical plot width is unchanged.
static void HistoryRebuildRenderCache(HistoryWindowState& state) {
    RECT client{};
    if (!state.graph || !GetClientRect(state.graph, &client)) return;
    int width = std::max(0, int(client.right) - ScaleForDpi(110, state.dpi) - HistoryPlotRightPadding(state));
    if (width == state.renderWidth && state.layoutWidth == client.right) return;
    HistoryClearHover(state);
    state.layoutWidth = -1;
    HDC dc = GetDC(state.graph);
    if (!dc) return;
    HGDIOBJ oldFont = SelectObject(dc, state.font ? state.font : GetStockObject(DEFAULT_GUI_FONT));
    int pad = ScaleForDpi(12, state.dpi), swatch = ScaleForDpi(34, state.dpi);
    int right = std::max(pad, int(client.right) - pad);
    int x = pad, y = ScaleForDpi(36, state.dpi), rowHeight = 0;
    for (auto& series : state.visible) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        series.legendRect = {};
        if (!series.plotted || right <= pad) continue;
        RECT text{0, 0, std::max(1, right - pad - swatch), 0};
        DrawTextW(dc, series.legendTitle.c_str(), int(series.legendTitle.size()), &text,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        int itemWidth = std::min(right - pad, int(text.right) + swatch);
        int itemHeight = std::max(ScaleForDpi(22, state.dpi), int(text.bottom));
        if (x > pad && x + itemWidth > right) {
            x = pad;
            y += rowHeight + ScaleForDpi(2, state.dpi);
            rowHeight = 0;
        }
        series.legendRect = {x, y, x + itemWidth, y + itemHeight};
        x += itemWidth + pad;
        rowHeight = std::max(rowHeight, itemHeight);
    }
    SelectObject(dc, oldFont);
    ReleaseDC(state.graph, dc);
    if (g_unloading || g_historyWindowCancelRequested) return;
    int plotTop = rowHeight ? y + rowHeight + ScaleForDpi(10, state.dpi) : ScaleForDpi(42, state.dpi);
    int left = std::min(int(client.right), ScaleForDpi(110, state.dpi));
    state.plot = {left, plotTop, left + width, plotTop + ScaleForDpi(166, state.dpi)};
    state.graphHeight = state.plot.bottom + ScaleForDpi(44, state.dpi);
    for (auto& series : state.visible) {
        if (g_unloading || g_historyWindowCancelRequested) return;
        if (!series.plotted) continue;
        const auto& account = state.accounts[series.account];
        series.render[0] = HistoryReducePeriod(state.snapshot, account.rows, series.meter, state.from, state.to, width);
        series.render[1] = state.overlay
            ? HistoryReducePeriod(state.snapshot, account.rows, series.meter, state.readFrom, state.from, width)
            : HistoryRenderSeries{};
    }
    state.renderWidth = width;
    state.layoutWidth = client.right;
}

struct HistoryHoverHit {
    size_t series = 0;
    int period = 0;
    size_t point = 0;
};

// Picking boundary: return every nearby series/period, not just the last painted line.
// Only real connected segments participate; the reported endpoint is always a recorded sample.
static std::vector<HistoryHoverHit> HistoryHitTest(const HistoryWindowState& state, POINT cursor) {
    std::vector<HistoryHoverHit> hits;
    if (state.renderWidth <= 0 || state.renderWidth != state.plot.right - state.plot.left ||
        cursor.x < state.plot.left || cursor.x > state.plot.right ||
        cursor.y < state.plot.top || cursor.y > state.plot.bottom) return hits;
    for (size_t index = 0; index < state.visible.size(); ++index) {
        if (g_unloading || g_historyWindowCancelRequested) return {};
        const auto& line = state.visible[index];
        if (!line.plotted) continue;
        int tolerance = ScaleForDpi(5, state.dpi) + std::max(1, ScaleForDpi(line.stroke, state.dpi)) / 2;
        for (int period = 0; period < (state.overlay ? 2 : 1); ++period) {
            const auto& points = line.render[period].points;
            int minX = cursor.x - state.plot.left - tolerance;
            int maxX = cursor.x - state.plot.left + tolerance;
            auto first = std::lower_bound(points.begin(), points.end(), minX,
                [](const HistoryRenderPoint& point, int x) { return point.x < x; });
            double best = double(tolerance) * tolerance;
            size_t nearest = SIZE_MAX;
            for (size_t i = size_t(first - points.begin()); i < points.size(); ++i) {
                if (g_unloading || g_historyWindowCancelRequested) return {};
                POINT b = HistoryPlotPoint(state, line, points[i]);
                double bx = double(cursor.x) - b.x, by = double(cursor.y) - b.y;
                double distance = bx * bx + by * by;
                size_t endpoint = i;
                if (i && points[i].connect) {
                    POINT a = HistoryPlotPoint(state, line, points[i - 1]);
                    double dx = double(b.x) - a.x, dy = double(b.y) - a.y;
                    double ax = double(cursor.x) - a.x, ay = double(cursor.y) - a.y;
                    double length = dx * dx + dy * dy;
                    double t = length > 0 ? std::clamp((ax * dx + ay * dy) / length, 0.0, 1.0) : 0;
                    double ex = ax - t * dx, ey = ay - t * dy;
                    distance = ex * ex + ey * ey;
                    if (ax * ax + ay * ay < bx * bx + by * by) endpoint = i - 1;
                }
                if (distance <= best) {
                    best = distance;
                    nearest = endpoint;
                }
                // Include one endpoint beyond the search band: its incoming segment can cross it.
                if (points[i].x > maxX) break;
            }
            if (nearest != SIZE_MAX) hits.push_back({index, period, nearest});
        }
    }
    return hits;
}

// Native tracking tool and its text belong exclusively to the graph's history thread.
static void HistoryUpdateHover(HistoryWindowState& state, POINT cursor) {
    RECT client{};
    GetClientRect(state.graph, &client);
    if (state.closing || state.loading || state.modal || g_unloading || g_historyWindowCancelRequested ||
        state.renderWidth < 0 || state.layoutWidth != client.right || !PtInRect(&client, cursor)) {
        HistoryClearHover(state);
        return;
    }
    if (state.hoverPoint.x == cursor.x && state.hoverPoint.y == cursor.y) return;
    state.hoverPoint = cursor;
    POINT content{cursor.x, cursor.y + state.scroll};
    std::vector<size_t> highlighted;
    std::wstring text;
    for (size_t index = 0; index < state.visible.size(); ++index) {
        if (g_unloading || g_historyWindowCancelRequested) { HistoryClearHover(state); return; }
        const auto& series = state.visible[index];
        if (!series.plotted || !PtInRect(&series.legendRect, content)) continue;
        highlighted.push_back(index);
        text = series.hoverTitle + (state.overlay
            ? L"\r\nSolid / filled: current week. Dashed / hollow: previous week."
            : L"\r\nSolid / filled: current range.");
        break;
    }
    if (highlighted.empty()) {
        auto hits = HistoryHitTest(state, content);
        for (const auto& hit : hits) {
            if (g_unloading || g_historyWindowCancelRequested) { HistoryClearHover(state); return; }
            const auto& series = state.visible[hit.series];
            const auto& point = series.render[hit.period].points[hit.point];
            if (point.sourceRow >= state.snapshot.samples.size()) continue;
            if (highlighted.empty() || highlighted.back() != hit.series) highlighted.push_back(hit.series);
            if (!text.empty()) text += L"\r\n";
            text += series.hoverTitle + (hit.period ? L" [Previous week]: " : L" [Current]: ") +
                (series.meter == 6 ? L"USD " : L"") + HistoryNumber(point.value) +
                (series.meter < 6 ? L"%" : series.meter == 7 ? L" credits" : L"") + L" · " +
                HistoryLocalDate(state.snapshot.samples[point.sourceRow].timeMs);
        }
    }
    if (highlighted.empty()) {
        HistoryClearHover(state);
        return;
    }
    if (!state.trackingMouse) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, state.graph, 0};
        state.trackingMouse = TrackMouseEvent(&tracking) != FALSE;
    }
    if (state.hoverSeries != highlighted) {
        state.hoverSeries = std::move(highlighted);
        InvalidateRect(state.graph, nullptr, FALSE);
    }
    if (!state.graphTip) return;
    TOOLINFOW tool{};
    tool.cbSize = TTTOOLINFOW_V2_SIZE;
    tool.hwnd = state.graph;
    tool.uId = 1;
    if (state.graphTipText != text) {
        // Keep the old native text pointer alive until its replacement has been processed.
        state.graphTipText.swap(text);
        tool.lpszText = state.graphTipText.data();
        SendMessageW(state.graphTip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    POINT screen = cursor;
    ClientToScreen(state.graph, &screen);
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST), &monitor))
        monitor.rcWork = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    int pad = ScaleForDpi(12, state.dpi);
    SendMessageW(state.graphTip, TTM_SETMAXTIPWIDTH, 0,
                  std::max(1L, std::min(LONG(ScaleForDpi(900, state.dpi)), monitor.rcWork.right - monitor.rcWork.left - 2 * pad)));
    // Native tooltip sizing is unreliable (and can fault in v5) before first activation.
    // Prime it offscreen once; active text updates resize synchronously without hide/show.
    if (!state.graphTipActive) {
        SendMessageW(state.graphTip, TTM_TRACKPOSITION, 0, MAKELPARAM(-32000, -32000));
        SendMessageW(state.graphTip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
        state.graphTipActive = true;
    }
    RECT tip{};
    if (!GetWindowRect(state.graphTip, &tip) || tip.right <= tip.left || tip.bottom <= tip.top) {
        HistoryClearHover(state);
        return;
    }
    int tipWidth = tip.right - tip.left, tipHeight = tip.bottom - tip.top;
    int x = screen.x + pad, y = screen.y + ScaleForDpi(20, state.dpi);
    if (y + tipHeight > monitor.rcWork.bottom) y = screen.y - tipHeight - pad;
    x = std::clamp(x, int(monitor.rcWork.left), std::max(int(monitor.rcWork.left), int(monitor.rcWork.right) - tipWidth));
    y = std::clamp(y, int(monitor.rcWork.top), std::max(int(monitor.rcWork.top), int(monitor.rcWork.bottom) - tipHeight));
    SendMessageW(state.graphTip, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
}

static HistoryStatistics HistoryAnalyze(const HistoryWindowState& state,
                                        const HistoryUiAccount& account, int meter,
                                        ULONGLONG from, ULONGLONG to) {
    HistoryStatistics result;
    const HistorySample* previous = nullptr;
    for (size_t row : account.rows) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        const auto& sample = state.snapshot.samples[row];
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
                    if (a >= state.redThreshold && b >= state.redThreshold) {
                        result.nearLimitMs += elapsed;
                    } else if ((a >= state.redThreshold) != (b >= state.redThreshold)) {
                        // Ratio form avoids overflow when finite endpoints are very far apart.
                        double low = std::min(a, b), high = std::max(a, b);
                        double divisor = std::max({1.0, std::abs(low), std::abs(high)});
                        double fraction = (high / divisor - state.redThreshold / divisor) /
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

static void HistorySetStatus(HistoryWindowState& state, const std::wstring& message) {
    // Keep operational failures visible; explanatory text lives behind Info.
    std::wstring text = message.empty() ? state.snapshotStatus : message;
    if (!state.snapshot.status.empty()) text += L"\r\n" + state.snapshot.status;
    SetWindowTextW(state.status, text.c_str());
}

static void HistoryScroll(HistoryWindowState& state, int position) {
    if (!state.graph) return;
    HistoryClearHover(state);
    HistoryRebuildRenderCache(state);
    RECT client{};
    GetClientRect(state.graph, &client);
    int seriesHeight = std::max(1, ScaleForDpi(kHistorySeriesHeight + (state.overlay ? 44 : 0), state.dpi));
    size_t height = state.visible.empty() && state.noReadings.empty() ? 0 :
        size_t(state.graphHeight) + state.visible.size() * size_t(seriesHeight);
    if (!state.noReadings.empty())
        height += (state.noReadings.size() + 1) * size_t(ScaleForDpi(24, state.dpi));
    int total = int(std::min(height, size_t(INT_MAX)));
    SCROLLINFO scroll{};
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMax = std::max(0, total - 1);
    scroll.nPage = UINT(std::max(0L, client.bottom));
    scroll.nPos = std::clamp(position, 0, std::max(0, total - int(scroll.nPage)));
    SetScrollInfo(state.graph, SB_VERT, &scroll, TRUE);
    state.scroll = scroll.nPos;
    InvalidateRect(state.graph, nullptr, FALSE);
}

// Runs only on the history thread. Selection changes reuse the loaded file snapshot.
static void HistoryRebuildView(HistoryWindowState& state) {
    HistoryClearHover(state);
    state.visible.clear();
    state.noReadings.clear();
    state.renderWidth = -1;
    state.axes = {};
    state.axes[0].high = 100;
    // Keep percentages on the left even if only dollar/credit readings are available.
    state.axes[0].visible = true;
    HistoryView view = state.selected < 0 ? state.allAccountsView : state.accountView;
    for (size_t i = 0; i < state.accounts.size(); ++i) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        if (state.selected >= 0 && size_t(state.selected) != i) continue;
        const auto& account = state.accounts[i];
        unsigned meters = account.provider < ARRAYSIZE(kHistoryProviderMeters)
            ? kHistoryProviderMeters[account.provider] & kHistoryViewMeters[size_t(view)] : 0;
        // Only Weekly follows taskbar choices; All meters and removed accounts stay unfiltered.
        if (view == HistoryView::Weekly && account.provider == 1 && account.configured) {
            if (!account.showWeekly) meters &= ~(1U << 1);
            if (!account.showFableWeekly) meters &= ~(1U << 2);
        }
        if (!meters) continue;
        size_t firstSeries = state.visible.size();
        std::wstring status;
        if (account.configured && state.enabled) {
            if (!account.recording) status = L"  [recording off]";
            else if (account.hidden && state.skipHidden) status = L"  [paused]";
        }
        for (int meter = 0; meter < int(kHistoryMeterCount); ++meter) {
            if (g_unloading || g_historyWindowCancelRequested) break;
            if (!(meters & (1U << meter))) continue;
            HistoryUiSeries series;
            series.account = i;
            series.meter = meter;
            series.current = HistoryAnalyze(state, account, meter, state.from, state.to);
            series.previous = state.overlay
                ? HistoryAnalyze(state, account, meter, state.readFrom, state.from) : HistoryStatistics{};
            // No empty per-meter legends. A USD-only account still has real detail data.
            if (!series.current.hasValue && !series.previous.hasValue) continue;
            series.plotted = meter != 6 || view == HistoryView::AllMeters;
            bool scoped = meter == 2 || meter == 4 || meter == 5;
            series.stroke = scoped ? 2 : 3;
            series.color = kHistoryColors[(state.selected < 0 ? i : size_t(meter)) % ARRAYSIZE(kHistoryColors)];
            if (state.selected < 0 && scoped) {
                // Keep scoped quotas in the account's color family; width survives high contrast.
                COLORREF color = series.color;
                series.color = RGB((GetRValue(color) * 3 + 255) / 4,
                                   (GetGValue(color) * 3 + 255) / 4,
                                   (GetBValue(color) * 3 + 255) / 4);
            }
            std::wstring label = kHistoryMeterNames[meter];
            if (account.provider == 3 && meter == 1) label = L"Gemini weekly used (%)";
            auto& axis = state.axes[meter < 6 ? 0 : meter - 5];
            if (series.plotted) axis.visible = true;
            ULONGLONG shortDuration = 0;
            bool hasDuration = false, mixedDuration = false;
            for (size_t row : account.rows) {
                if (g_unloading || g_historyWindowCancelRequested) break;
                const auto& sample = state.snapshot.samples[row];
                if (sample.timeMs < state.readFrom || sample.timeMs >= state.to ||
                    !HistoryHasValue(sample, meter)) continue;
                double value = sample.meters[meter].value;
                if (series.plotted) {
                    axis.low = std::min(axis.low, value);
                    axis.high = std::max(axis.high, value);
                }
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
                label += L" used (%)";
            }
            series.hoverTitle = account.label + L" · " + label;
            std::wstring compact = label;
            if (compact.ends_with(L" used (%)")) compact.resize(compact.size() - 9);
            series.legendTitle = (state.selected < 0 ? account.label + L" · " : L"") + compact;
            if (!series.plotted) label += L" — details only";
            series.title = (state.selected < 0 ? account.label + L" · " : L"") + label;
            if (state.visible.size() == firstSeries) series.title += status;
            state.visible.push_back(std::move(series));
        }
        if (state.visible.size() == firstSeries) state.noReadings.push_back(account.title + status);
    }
    // Each account mode keeps its own view selection; single accounts start on All meters.
    SendMessageW(state.meterCombo, CB_RESETCONTENT, 0, 0);
    for (PCWSTR name : kHistoryViewNames)
        SendMessageW(state.meterCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendMessageW(state.meterCombo, CB_SETCURSEL, WPARAM(view), 0);
    EnableWindow(state.exportButton, !state.loading);
    SetWindowTextW(state.exportButton, state.selected < 0 ? L"&Export all CSV..." : L"&Export CSV...");
    HistoryScroll(state, 0);
}

static void HistoryLoad(HistoryWindowState& state) {
    if (state.loading || state.modal || state.closing || g_unloading || g_historyWindowCancelRequested) return;
    HistoryClearHover(state);
    state.loading = true;
    EnableWindow(state.refreshButton, FALSE);
    uint64_t selectedId = state.selected >= 0 && size_t(state.selected) < state.accounts.size()
                              ? state.accounts[state.selected].id : 0;
    std::vector<AccountConfig> configured;
    {
        std::lock_guard lock(g_settingsMutex);
        configured = g_settings.accounts;
        state.enabled = g_settings.historyEnabled;
        state.idsPendingSave = g_historyIdsPendingSave.load();
        state.retention = g_settings.historyRetentionDays;
        state.skipHidden = g_settings.skipHiddenPolling;
        state.redThreshold = g_settings.redThreshold;
    }
    static constexpr int days[] = {1, 7, 30, 90};
    state.to = NowUnixMs();
    ULONGLONG rangeDays = state.overlay ? 7 : state.range == 4
        ? std::max(1, state.retention) : days[state.range];
    ULONGLONG range = rangeDays * kHistoryDayMs;
    state.from = state.to > range ? state.to - range : 0;
    state.readFrom = state.overlay && state.from > kHistoryWeekMs
                        ? state.from - kHistoryWeekMs : state.from;
    // Explicit viewer refresh also prunes retained history while recording is off.
    // No settings lock is held across disk I/O or the history leaf lock.
    MaintainHistory(state.retention);
    state.snapshot = ReadHistorySnapshot(state.readFrom);
    // A close can arrive while the snapshot API is doing I/O. Drop old row indices
    // before returning so even a queued paint cannot index the replacement snapshot.
    state.visible.clear();
    state.noReadings.clear();
    state.renderWidth = -1;
    if (g_unloading || g_historyWindowCancelRequested) {
        state.loading = false;
        PostMessageW(state.window, WM_CLOSE, 0, 0);
        return;
    }
    std::stable_sort(state.snapshot.samples.begin(), state.snapshot.samples.end(),
                     [](const HistorySample& a, const HistorySample& b) {
                         return a.timeMs < b.timeMs;
                     });
    state.accounts.clear();
    std::map<uint64_t, size_t> index;
    for (const auto& config : configured) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        if (!config.historyId || index.contains(config.historyId)) continue;
        HistoryUiAccount account;
        account.id = config.historyId;
        account.provider = config.provider == L"anthropic" ? 1 : config.provider == L"openai" ? 2 : 3;
        account.configured = true;
        account.recording = config.recordHistory;
        account.hidden = config.hidden;
        account.showWeekly = config.showBars[kWeeklyBar];
        account.showFableWeekly = config.showBars[kFableWeeklyBar];
        account.label = config.label;
        account.title = std::wstring(ProviderDisplayName(config.provider)) + L" — " + config.label;
        index.emplace(account.id, state.accounts.size());
        state.accounts.push_back(std::move(account));
    }
    for (size_t row = 0; row < state.snapshot.samples.size(); ++row) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        const auto& sample = state.snapshot.samples[row];
        if (!sample.accountId || sample.timeMs < state.readFrom || sample.timeMs >= state.to) continue;
        auto [it, inserted] = index.emplace(sample.accountId, state.accounts.size());
        if (inserted) {
            HistoryUiAccount account;
            account.id = sample.accountId;
            account.provider = sample.provider;
            PCWSTR provider = sample.provider == 1 ? L"Anthropic" : sample.provider == 2 ? L"OpenAI" :
                              sample.provider == 3 ? L"Google Antigravity" : L"Unknown provider";
            account.label = L"removed " + Utf8ToWide(HistoryCsvId("account_", sample.accountId));
            account.title = std::wstring(provider) + L" — " + account.label;
            state.accounts.push_back(std::move(account));
        }
        state.accounts[it->second].rows.push_back(row);
    }
    SendMessageW(state.accountCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(state.accountCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All accounts"));
    state.selected = -1;
    for (size_t i = 0; i < state.accounts.size(); ++i) {
        SendMessageW(state.accountCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(state.accounts[i].title.c_str()));
        if (state.accounts[i].id == selectedId) state.selected = int(i);
    }
    SendMessageW(state.accountCombo, CB_SETCURSEL, state.selected + 1, 0);
    state.snapshotStatus = std::wstring(!state.enabled ? L"Recording off" : state.idsPendingSave
        ? L"Recording pending" : L"Recording on") + L"  ·  Updated " + HistoryLocalDate(state.to);
    if (state.enabled && !state.idsPendingSave && state.snapshot.samples.empty()) {
        state.snapshotStatus = L"Recording on  ·  Waiting for the next poll";
    }
    SetWindowTextW(state.legend, state.overlay ? L"Solid: current    Dashed: previous" : L"Solid: current");
    state.loading = false;
    EnableWindow(state.refreshButton, TRUE);
    HistorySetStatus(state, {});
    HistoryRebuildView(state);
    HistoryLayout(state, false);
}

// src/history/export.h
// Native dialog tracking and CSV export.

// A thread-local CBT hook tracks native save/confirmation dialogs, including nested
// overwrite prompts. Owner-close defers destruction until the modal API has returned.
struct HistoryModalScope;
static thread_local HistoryModalScope* g_historyModalScope = nullptr;

struct HistoryModalScope {
    HistoryWindowState& state;
    HHOOK hook = nullptr;
    std::array<HWND, 16> dialogs{};
    size_t count = 0;
    explicit HistoryModalScope(HistoryWindowState& owner);
    ~HistoryModalScope() {
        if (!hook) return;  // A rejected nested scope must not clear its active outer scope.
        UnhookWindowsHookEx(hook);
        g_historyModalScope = nullptr;
        g_historyDialog.store(nullptr);
        state.modal = false;
        if (state.closing || g_unloading || g_historyWindowCancelRequested)
            PostMessageW(state.window, WM_CLOSE, 0, 0);
    }
};

static LRESULT CALLBACK HistoryDialogCbtProc(int code, WPARAM wParam, LPARAM lParam) {
    // Only fixed-size storage and Win32 calls here: no exceptions across a hook boundary.
    auto* scope = g_historyModalScope;
    HWND window = reinterpret_cast<HWND>(wParam);
    if (scope && code == HCBT_ACTIVATE) {
        wchar_t name[24]{};
        if (GetClassNameW(window, name, ARRAYSIZE(name)) && wcscmp(name, L"#32770") == 0) {
            auto end = scope->dialogs.begin() + scope->count;
            if (std::find(scope->dialogs.begin(), end, window) == end && scope->count < scope->dialogs.size())
                scope->dialogs[scope->count++] = window;
            g_historyDialog.store(window);
            if (scope->state.closing || g_unloading || g_historyWindowCancelRequested) {
                PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
        }
    } else if (scope && code == HCBT_DESTROYWND) {
        for (size_t i = 0; i < scope->count; ++i) {
            if (scope->dialogs[i] != window) continue;
            for (size_t j = i + 1; j < scope->count; ++j) scope->dialogs[j - 1] = scope->dialogs[j];
            --scope->count;
            break;
        }
        HWND remaining = scope->count ? scope->dialogs[scope->count - 1] : nullptr;
        g_historyDialog.store(remaining);
        if (remaining && (scope->state.closing || g_unloading || g_historyWindowCancelRequested)) {
            PostMessageW(remaining, WM_COMMAND, IDCANCEL, 0);
            PostMessageW(remaining, WM_CLOSE, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

HistoryModalScope::HistoryModalScope(HistoryWindowState& owner) : state(owner) {
    if (state.modal || state.closing || g_unloading || g_historyWindowCancelRequested) return;
    HistoryClearHover(state);
    state.modal = true;
    g_historyModalScope = this;
    hook = SetWindowsHookExW(WH_CBT, HistoryDialogCbtProc, nullptr, GetCurrentThreadId());
    if (!hook) {
        g_historyModalScope = nullptr;
        state.modal = false;
        SetWindowTextW(state.status, L"ERROR: cannot track native dialogs safely; operation cancelled.");
    }
}

// Keep an existing CSV intact if writing fails. This owns the temporary file until
// a same-directory rename succeeds; labels never enter CSV or the suggested filename.
struct HistoryCsvFile {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring temporary;
    ~HistoryCsvFile() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (!temporary.empty()) DeleteFileW(temporary.c_str());
    }
    bool Write(const std::string& text) {
        size_t written = 0;
        while (written < text.size()) {
            if (g_unloading || g_historyWindowCancelRequested) {
                SetLastError(ERROR_CANCELLED);
                return false;
            }
            DWORD count = DWORD(std::min(text.size() - written, size_t(65536)));
            DWORD actual = 0;
            if (!WriteFile(handle, text.data() + written, count, &actual, nullptr)) return false;
            if (!actual) { SetLastError(ERROR_WRITE_FAULT); return false; }
            written += actual;
        }
        return true;
    }
    // Serialization boundary for both UI export and isolated file-content tests.
    // Use the unreduced, stably timestamp-sorted snapshot, never the render cache.
    bool WriteSnapshot(const HistoryWindowState& state, size_t* exported) {
        *exported = 0;
        uint64_t accountId = state.selected >= 0 ? state.accounts.at(state.selected).id : 0;
        std::string buffer = "timestamp_utc,timestamp_unix_ms,account_id,provider,provider_id,meter_id,unit,value,reset_utc,reset_unix_ms,duration_ms,poll_interval_ms,session_id,failed,missing\r\n";
        ULONGLONG nextProgressMs = 0;
        for (const auto& sample : state.snapshot.samples) {
            if (g_unloading || g_historyWindowCancelRequested) { SetLastError(ERROR_CANCELLED); return false; }
            if (!sample.accountId || (accountId && sample.accountId != accountId) ||
                sample.timeMs < state.readFrom || sample.timeMs >= state.to) continue;
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
                ++*exported;
            }
            if (buffer.size() >= 65536) {
                if (state.status && GetTickCount64() >= nextProgressMs) {
                    std::wstring progress = L"Exporting " + std::to_wstring(*exported) + L" rows...";
                    SetWindowTextW(state.status, progress.c_str());
                    UpdateWindow(state.status);
                    nextProgressMs = GetTickCount64() + 250;
                }
                if (!Write(buffer)) return false;
                buffer.clear();
            }
        }
        return Write(buffer);
    }
};

static void HistoryExport(HistoryWindowState& state) {
    if ((state.selected >= 0 && size_t(state.selected) >= state.accounts.size()) || state.modal || state.loading) return;
    size_t estimatedRows = 0;
    uint64_t selectedId = state.selected < 0 ? 0 : state.accounts[state.selected].id;
    for (const auto& sample : state.snapshot.samples) {
        if ((!selectedId || sample.accountId == selectedId) &&
            sample.timeMs >= state.readFrom && sample.timeMs < state.to) {
            estimatedRows += sample.provider == 1 ? 5 : sample.provider == 2 ? 3 : 4;
        }
    }
    if (estimatedRows > 500000) {
        std::wstring warning = L"This export contains about " + std::to_wstring(estimatedRows) +
            L" rows. It may take a while and exceed spreadsheet row limits. "
            L"The history window will be busy while writing; Explorer's taskbar remains available. Continue?";
        HistoryModalScope modal(state);
        if (!modal.hook || MessageBoxW(state.window, warning.c_str(), L"Large history export",
                                      MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) return;
    }
    if (state.closing || g_unloading || g_historyWindowCancelRequested) return;
    wchar_t path[32768] = L"ai-quota-history.csv";
    DWORD dialogError = 0;
    bool accepted = false;
    {
        HistoryModalScope modal(state);
        if (!modal.hook) return;
        OPENFILENAMEW picker{};
        picker.lStructSize = sizeof(picker);
        picker.hwndOwner = state.window;
        picker.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0";
        picker.lpstrFile = path;
        picker.nMaxFile = ARRAYSIZE(path);
        picker.lpstrDefExt = L"csv";
        picker.lpstrTitle = state.selected < 0
            ? L"Export all accounts — all meters, selected range (both weeks if comparing)"
            : L"Export selected account — all meters, selected range (both weeks if comparing)";
        picker.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                       OFN_NOCHANGEDIR | OFN_ENABLESIZING;
        accepted = GetSaveFileNameW(&picker) != FALSE;
        if (!accepted) dialogError = CommDlgExtendedError();
    }
    if (state.closing || g_unloading || g_historyWindowCancelRequested) return;
    if (!accepted) {
        HistorySetStatus(state, dialogError ? L"ERROR: Save dialog failed (code " + std::to_wstring(dialogError) + L")."
                                            : L"CSV export cancelled; no file written.");
        return;
    }
    HistoryCsvFile output;
    std::wstring candidate = std::wstring(path) + L".quota-" + std::to_wstring(GetCurrentProcessId()) +
                             L"-" + std::to_wstring(GetTickCount64()) + L".tmp";
    output.handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output.handle == INVALID_HANDLE_VALUE) {
        HistorySetStatus(state, L"ERROR: cannot create CSV temporary file (Windows error " +
                         std::to_wstring(GetLastError()) + L"). Existing destination unchanged.");
        return;
    }
    output.temporary = std::move(candidate);
    size_t exported = 0;
    HistorySetStatus(state, L"Exporting local history...");
    UpdateWindow(state.status);
    bool success = output.WriteSnapshot(state, &exported);
    if (success) success = FlushFileBuffers(output.handle) != FALSE;
    DWORD error = success ? ERROR_SUCCESS : GetLastError();
    if (!CloseHandle(output.handle) && success) { success = false; error = GetLastError(); }
    output.handle = INVALID_HANDLE_VALUE;
    if (success && (g_unloading || g_historyWindowCancelRequested)) { success = false; error = ERROR_CANCELLED; }
    if (success) {
        success = MoveFileExW(output.temporary.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (success) output.temporary.clear();
        else error = GetLastError();
    }
    HistorySetStatus(state, success
        ? L"Exported " + std::to_wstring(exported) + (state.selected < 0 ? L" meter rows (all accounts) to " : L" meter rows (selected account) to ") +
          path + L". Labels omitted; times UTC." +
          (state.overlay ? L" Includes both elapsed 7-day periods." : L"")
        : L"ERROR: CSV export failed (Windows error " + std::to_wstring(error) + L"). Existing destination unchanged.");
}

// src/history/paint.h
// History graph painting.

// Paint resources are scoped so allocation/formatting exceptions cannot leak a DC or pen.
struct HistoryPaint {
    HWND window;
    PAINTSTRUCT paint{};
    HDC dc = nullptr;
    int saved = 0;
    HPEN solid = nullptr, dashed = nullptr;
    explicit HistoryPaint(HWND owner) : window(owner), dc(BeginPaint(owner, &paint)) {
        if (dc) saved = SaveDC(dc);
    }
    ~HistoryPaint() {
        if (saved) RestoreDC(dc, saved);
        else if (dc) { SelectObject(dc, GetStockObject(BLACK_PEN)); SelectObject(dc, GetStockObject(SYSTEM_FONT)); }
        if (solid) DeleteObject(solid);
        if (dashed) DeleteObject(dashed);
        if (dc) EndPaint(window, &paint);
    }
};

static void HistorySetLinePens(HistoryPaint& paint, COLORREF color, int width, bool overlay) {
    SelectObject(paint.dc, GetStockObject(DC_PEN));
    SetDCPenColor(paint.dc, color);
    if (paint.solid) DeleteObject(paint.solid);
    if (paint.dashed) DeleteObject(paint.dashed);
    LOGBRUSH brush{BS_SOLID, color, 0};
    constexpr DWORD style = PS_GEOMETRIC | PS_ENDCAP_ROUND | PS_JOIN_ROUND;
    paint.solid = ExtCreatePen(style | PS_SOLID, width, &brush, 0, nullptr);
    // CreatePen silently turns wide dashed pens solid; geometric pens retain the period key.
    paint.dashed = overlay ? ExtCreatePen(style | PS_DASH, width, &brush, 0, nullptr) : nullptr;
}

static void HistoryDrawText(HDC dc, const std::wstring& text, RECT rect, UINT flags = DT_LEFT) {
    DrawTextW(dc, text.c_str(), int(text.size()), &rect, flags | DT_NOPREFIX);
}

static void HistoryPaintGraph(HistoryWindowState& state) {
    HistoryPaint paint(state.graph);
    if (!paint.dc) return;
    HDC dc = paint.dc;
    RECT client{};
    GetClientRect(state.graph, &client);
    FillRect(dc, &paint.paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
    SelectObject(dc, state.font ? state.font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    COLORREF textColor = GetSysColor(COLOR_WINDOWTEXT);
    SetTextColor(dc, textColor);
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    int pad = ScaleForDpi(12, state.dpi);
    int graphHeight = state.graphHeight;
    int seriesHeight = std::max(1, ScaleForDpi(kHistorySeriesHeight + (state.overlay ? 44 : 0), state.dpi));
    if (state.visible.empty() && state.noReadings.empty()) {
        RECT rect{pad, pad, client.right - pad, client.bottom - pad};
        HistoryDrawText(dc, state.accounts.empty() ? L"No accounts to display." : L"No accounts support this view.",
                        rect, DT_WORDBREAK);
        return;
    }
    // Synchronous resize paints may precede the matching cache; never lay out in WM_PAINT.
    if (state.layoutWidth != client.right || state.renderWidth < 0) return;
    // All lines share a timeline; unlike units have separate, explicitly labeled axes.
    int top = -state.scroll;
    RECT plot = state.plot;
    OffsetRect(&plot, 0, top);
    if (top + graphHeight >= paint.paint.rcPaint.top && top <= paint.paint.rcPaint.bottom) {
        RECT title{pad, top + ScaleForDpi(6, state.dpi), client.right - pad, top + ScaleForDpi(30, state.dpi)};
        HistoryView view = state.selected < 0 ? state.allAccountsView : state.accountView;
        std::wstring heading = state.selected < 0 ? L"All accounts" : state.accounts.at(state.selected).title;
        HistoryDrawText(dc, heading + L" · " + kHistoryViewNames[size_t(view)],
                        title, DT_SINGLELINE | DT_END_ELLIPSIS);
        for (size_t index = 0; index < state.visible.size(); ++index) {
            if (g_unloading || g_historyWindowCancelRequested) break;
            const auto& series = state.visible[index];
            RECT rect = series.legendRect;
            OffsetRect(&rect, 0, top);
            if (!series.plotted || rect.right <= rect.left || rect.bottom < paint.paint.rcPaint.top ||
                rect.top > paint.paint.rcPaint.bottom) continue;
            COLORREF color = HistoryLineColor(state, index, highContrast);
            int strokeWidth = std::max(1, ScaleForDpi(series.stroke, state.dpi));
            HistorySetLinePens(paint, color, strokeWidth, state.overlay);
            if (rect.right - rect.left >= ScaleForDpi(34, state.dpi)) {
                for (int period = 0; period < (state.overlay ? 2 : 1); ++period) {
                    if (!(period ? series.previous : series.current).hasValue) continue;
                    SelectObject(dc, period && paint.dashed ? paint.dashed :
                                     paint.solid ? paint.solid : GetStockObject(DC_PEN));
                    int y = rect.top + ScaleForDpi(state.overlay ? 6 + period * 10 : 10, state.dpi);
                    MoveToEx(dc, rect.left + ScaleForDpi(2, state.dpi), y, nullptr);
                    LineTo(dc, rect.left + ScaleForDpi(26, state.dpi), y);
                }
                rect.left += ScaleForDpi(34, state.dpi);
            }
            SetTextColor(dc, state.hoverSeries.empty() ||
                std::find(state.hoverSeries.begin(), state.hoverSeries.end(), index) != state.hoverSeries.end()
                ? textColor : GetSysColor(COLOR_GRAYTEXT));
            HistoryDrawText(dc, series.legendTitle, rect, DT_WORDBREAK | DT_END_ELLIPSIS);
        }
        SetTextColor(dc, textColor);
        if (plot.right <= plot.left) {
            RECT narrow{pad, plot.top, client.right - pad, plot.bottom};
            HistoryDrawText(dc, L"Widen the window to show the plot.", narrow, DT_WORDBREAK);
        }
    }
    if (plot.bottom + ScaleForDpi(30, state.dpi) >= paint.paint.rcPaint.top && plot.top <= paint.paint.rcPaint.bottom &&
        plot.right > plot.left && plot.bottom > plot.top) {
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, GetSysColor(COLOR_GRAYTEXT));
        for (int tick = 0; tick < 3; ++tick) {
            int y = plot.bottom - (plot.bottom - plot.top) * tick / 2;
            MoveToEx(dc, plot.left, y, nullptr);
            LineTo(dc, plot.right, y);
            static constexpr PCWSTR units[] = {L"%", L" USD", L" cr"};
            int axisPosition = 0;
            for (size_t i = 0; i < state.axes.size(); ++i) {
                const auto& axis = state.axes[i];
                if (!axis.visible) continue;
                double value = tick == 0 ? axis.low : tick == 2 ? axis.high : axis.low / 2 + axis.high / 2;
                wchar_t label[64];
                swprintf(label, ARRAYSIZE(label), L"%.6g%ls", value, units[i]);
                RECT labelRect{pad, y - ScaleForDpi(10, state.dpi), plot.left - ScaleForDpi(6, state.dpi), y + ScaleForDpi(12, state.dpi)};
                if (axisPosition) {
                    labelRect.left = plot.right + ScaleForDpi(6 + (axisPosition - 1) * 110, state.dpi);
                    labelRect.right = labelRect.left + ScaleForDpi(104, state.dpi);
                }
                HistoryDrawText(dc, label, labelRect, (axisPosition ? DT_LEFT : DT_RIGHT) | DT_SINGLELINE);
                ++axisPosition;
            }
        }
        bool hasValue = false, hasDetails = false;
        int lineClip = SaveDC(dc);
        if (lineClip) IntersectClipRect(dc, plot.left, plot.top, plot.right + 1, plot.bottom + 1);
        // Muted lines go first so a selected coincident series is never hidden beneath them.
        for (size_t draw = 0; lineClip && draw < state.visible.size() * (state.hoverSeries.empty() ? 1 : 2); ++draw) {
            if (g_unloading || g_historyWindowCancelRequested) break;
            size_t index = draw % state.visible.size();
            const auto& line = state.visible[index];
            if (!line.plotted) { hasDetails = true; continue; }
            bool highlighted = std::find(state.hoverSeries.begin(), state.hoverSeries.end(), index) != state.hoverSeries.end();
            if (!state.hoverSeries.empty() && highlighted != (draw >= state.visible.size())) continue;
            hasValue |= line.current.hasValue || (state.overlay && line.previous.hasValue);
            COLORREF color = HistoryLineColor(state, index, highContrast);
            int strokeWidth = std::max(1, ScaleForDpi(line.stroke, state.dpi));
            // Keep line colors in both periods; dashes/hollow points identify the prior week.
            HistorySetLinePens(paint, color, strokeWidth, state.overlay);
            // Width mismatches can occur during synchronous resize paints. Do not rebuild
            // here: size/selection handlers publish the matching cache before invalidating.
            for (int period = state.overlay ? 1 : 0; period >= 0 && state.renderWidth == plot.right - plot.left; --period) {
                const auto& series = line.render[period];
                int radius = std::max(1, ScaleForDpi(2, state.dpi));
                std::vector<POINT> segment;
                segment.reserve(series.points.size());
                size_t segmentSamples = 0;
                // One stroke per continuous run preserves dash phase and rounded joins.
                // Coincident vertices must not fill the gaps in a dashed stroke.
                auto drawSegment = [&] {
                    if (segment.empty() || g_unloading || g_historyWindowCancelRequested) return;
                    SelectObject(dc, period && paint.dashed ? paint.dashed :
                                     paint.solid ? paint.solid : GetStockObject(DC_PEN));
                    SetDCPenColor(dc, color);
                    if (segment.size() > 1) {
                        Polyline(dc, segment.data(), int(segment.size()));
                    } else {
                        int x = segment.front().x, y = segment.front().y;
                        if (segmentSamples > 1) {
                            // Compressed flat runs still use the stroke's width, not a stray pixel.
                            RECT mark{x - strokeWidth / 2, y - strokeWidth / 2,
                                      x - strokeWidth / 2 + strokeWidth, y - strokeWidth / 2 + strokeWidth};
                            SetDCBrushColor(dc, color);
                            FillRect(dc, &mark, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                        } else {
                            // Keep lone readings visible across gaps without dotting connected curves.
                            SelectObject(dc, GetStockObject(DC_PEN));
                            SetDCPenColor(dc, color);
                            SelectObject(dc, period ? GetStockObject(HOLLOW_BRUSH) : GetStockObject(DC_BRUSH));
                            SetDCBrushColor(dc, color);
                            Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
                        }
                    }
                    segment.clear();
                    segmentSamples = 0;
                };
                for (const auto& point : series.points) {
                    if (g_unloading || g_historyWindowCancelRequested) break;
                    if (!point.connect) drawSegment();
                    POINT position = HistoryPlotPoint(state, line, point);
                    position.y -= state.scroll;
                    if (segment.empty() || position.x != segment.back().x || position.y != segment.back().y)
                        segment.push_back(position);
                    ++segmentSamples;
                }
                drawSegment();
            }
        }
        if (lineClip) RestoreDC(dc, lineClip);
        if (!hasValue) {
            RECT empty{plot.left + pad, plot.top + pad, plot.right - pad, plot.bottom - pad};
            HistoryDrawText(dc, hasDetails ? L"No percentage readings to plot. Dollar spending is shown below."
                                          : L"No readings in this range.",
                            empty, DT_WORDBREAK);
        }
        for (int tick = 0; tick < 3; ++tick) {
            ULONGLONG time = state.from + (state.to - state.from) / 2 * tick;
            int width = std::min(ScaleForDpi(190, state.dpi), int(plot.right - plot.left) / 3);
            int x = tick == 0 ? plot.left : tick == 2 ? plot.right - width : (plot.left + plot.right - width) / 2;
            RECT rect{x, plot.bottom + ScaleForDpi(7, state.dpi), x + width, plot.bottom + ScaleForDpi(29, state.dpi)};
            HistoryDrawText(dc, HistoryLocalDate(time), rect, DT_SINGLELINE | DT_END_ELLIPSIS | (tick == 2 ? DT_RIGHT : tick == 1 ? DT_CENTER : DT_LEFT));
        }
    }

    // Cached titles/styles match the plotted lines; USD details never claim a swatch or axis.
    size_t firstView = size_t(std::max(0LL, static_cast<long long>(state.scroll) + paint.paint.rcPaint.top - graphHeight)) / seriesHeight;
    size_t endView = std::min(state.visible.size(),
        size_t(std::max(0LL, static_cast<long long>(state.scroll) + paint.paint.rcPaint.bottom - graphHeight)) / seriesHeight + 1);
    for (size_t view = firstView; view < endView; ++view) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        long long top64 = graphHeight + static_cast<long long>(view) * seriesHeight - state.scroll;
        if (top64 + seriesHeight < paint.paint.rcPaint.top || top64 > paint.paint.rcPaint.bottom) continue;
        int seriesTop = int(top64);
        const auto& series = state.visible[view];
        int meter = series.meter;
        COLORREF color = highContrast ? textColor : series.color;
        int strokeWidth = std::max(1, ScaleForDpi(series.stroke, state.dpi));
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, color);
        int swatchY = seriesTop + ScaleForDpi(16, state.dpi);
        RECT swatch{pad, swatchY - strokeWidth / 2, pad + ScaleForDpi(26, state.dpi),
                    swatchY - strokeWidth / 2 + strokeWidth};
        SetDCBrushColor(dc, color);
        if (series.plotted) FillRect(dc, &swatch, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        RECT title{pad + (series.plotted ? ScaleForDpi(34, state.dpi) : 0), seriesTop + ScaleForDpi(6, state.dpi),
                    client.right - pad, seriesTop + ScaleForDpi(30, state.dpi)};
        HistoryDrawText(dc, series.title, title, DT_SINGLELINE | DT_END_ELLIPSIS);
        for (int period = 0; period < (state.overlay ? 2 : 1); ++period) {
            const auto& stats = period ? series.previous : series.current;
            std::wstring prefix = !state.overlay ? L"" : period ? L"Previous: " : L"Current: ";
            std::wstring level = (meter == 6 ? L"USD " : L"") + HistoryNumber(stats.peak) +
                (meter < 6 ? L"%" : meter == 7 ? L" credits" : L"");
            std::wstring peak = prefix + (stats.hasValue
                ? L"Peak " + level + L"  ·  " + HistoryLocalDate(stats.peakTime)
                : L"Peak —");
            int y = seriesTop + ScaleForDpi(34 + period * 44, state.dpi);
            RECT line{pad, y, client.right - pad, y + ScaleForDpi(21, state.dpi)};
            if (!series.plotted) {
                std::wstring latest = prefix + (stats.hasValue
                    ? L"Last recorded spend: USD " + HistoryNumber(stats.latest) + L"  ·  " + HistoryLocalDate(stats.latestTime)
                    : L"No dollar readings in this period.");
                HistoryDrawText(dc, latest, line, DT_SINGLELINE | DT_END_ELLIPSIS);
                line.top += ScaleForDpi(21, state.dpi);
                line.bottom += ScaleForDpi(21, state.dpi);
                if (stats.hasValue) HistoryDrawText(dc, peak, line, DT_SINGLELINE | DT_END_ELLIPSIS);
                continue;
            }
            HistoryDrawText(dc, peak, line, DT_SINGLELINE | DT_END_ELLIPSIS);
            double duration = double(state.to - state.from);
            wchar_t coverage[32];
            swprintf(coverage, ARRAYSIZE(coverage), L" (%.1f%%)", duration ? 100 * stats.coverageMs / duration : 0);
            std::wstring summary = L"Coverage " + HistoryHours(stats.coverageMs) + coverage;
            if (meter < 6) {
                summary += L"  ·  Est. ≥" + std::to_wstring(state.redThreshold) + L"%: " +
                    (stats.coverageMs ? HistoryHours(stats.nearLimitMs) : L"—");
            }
            line.top += ScaleForDpi(21, state.dpi);
            line.bottom += ScaleForDpi(21, state.dpi);
            HistoryDrawText(dc, summary, line, DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, GetSysColor(COLOR_GRAYTEXT));
        MoveToEx(dc, pad, seriesTop + seriesHeight - 1, nullptr);
        LineTo(dc, client.right - pad, seriesTop + seriesHeight - 1);
    }
    // Accounts without readings get one compact entry, not an empty block per quota.
    if (!state.noReadings.empty()) {
        int rowHeight = ScaleForDpi(24, state.dpi);
        long long listTop = graphHeight + static_cast<long long>(state.visible.size()) * seriesHeight - state.scroll;
        for (size_t row = 0; row <= state.noReadings.size(); ++row) {
            if (g_unloading || g_historyWindowCancelRequested) break;
            long long y = listTop + static_cast<long long>(row) * rowHeight;
            if (y + rowHeight < paint.paint.rcPaint.top) continue;
            if (y > paint.paint.rcPaint.bottom) break;
            RECT rect{pad, int(y), client.right - pad, int(y + rowHeight)};
            HistoryDrawText(dc, row ? state.noReadings[row - 1] : L"No readings in range:",
                            rect, DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    if (GetFocus() == state.graph) {
        InflateRect(&client, -1, -1);
        DrawFocusRect(dc, &client);
    }
}

// src/history/window.h
// History window layout, procedures, thread, and open/close.

static void HistoryLayout(HistoryWindowState& state, bool newFont) {
    HistoryClearHover(state);
    RECT client{};
    GetClientRect(state.window, &client);
    if (newFont) {
        state.renderWidth = state.layoutWidth = -1;
        HFONT font = CreateFontW(-MulDiv(9, state.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (font) {
            EnumChildWindows(state.window, [](HWND child, LPARAM value) -> BOOL {
                SendMessageW(child, WM_SETFONT, WPARAM(value), TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(font));
            if (state.graphTip) SendMessageW(state.graphTip, WM_SETFONT, WPARAM(font), FALSE);
            if (state.font) DeleteObject(state.font);
            state.font = font;
        }
    }
    // Layout table uses logical pixels; combo heights include their drop-down lists.
    int width = MulDiv(client.right, 96, state.dpi);
    int height = MulDiv(client.bottom, 96, state.dpi);
    bool showBanner = state.to && !state.enabled;
    int bannerHeight = showBanner ? 54 : 0;
    struct Placement { HWND control; int x, y, w, h; };
    const Placement positions[] = {
        {state.recordingBanner, 12, 10, width - 24, 42},
        {state.enableButton, width - 184, 17, 160, 28},
        {state.accountLabel, 12, 15, 80, 22}, {state.accountCombo, 96, 10, width - 108, 320},
        {state.meterLabel, 12, 51, 80, 22}, {state.meterCombo, 96, 46, width - 328, 310},
        {state.rangeLabel, width - 220, 51, 64, 22}, {state.rangeCombo, width - 154, 46, 142, 180},
        {state.overlayCheck, 12, 80, 280, 26}, {state.legend, 312, 84, width - 324, 22},
        {state.refreshButton, 12, 113, 105, 28}, {state.exportButton, 127, 113, 145, 28},
        {state.clearButton, 282, 113, 135, 28}, {state.settingsButton, 427, 113, 105, 28},
        {state.infoButton, 542, 113, 80, 28},
        {state.graph, 12, 153 + bannerHeight, width - 24, std::max(0, height - 225 - bannerHeight)},
        {state.status, 12, std::max(153 + bannerHeight, height - 64), width - 24, 60},
    };
    for (const auto& p : positions) {
        int y = p.y;
        if (p.control != state.recordingBanner && p.control != state.enableButton &&
            p.control != state.graph && p.control != state.status) y += bannerHeight;
        MoveWindow(p.control, ScaleForDpi(p.x, state.dpi), ScaleForDpi(y, state.dpi),
                    ScaleForDpi(std::max(0, p.w), state.dpi), ScaleForDpi(std::max(0, p.h), state.dpi), TRUE);
    }
    ShowWindow(state.recordingBanner, showBanner ? SW_SHOW : SW_HIDE);
    ShowWindow(state.enableButton, showBanner ? SW_SHOW : SW_HIDE);
    EnableWindow(state.enableButton, showBanner && !state.loading);
    SetWindowPos(state.enableButton, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    HistoryScroll(state, state.scroll);
}

static LRESULT CALLBACK HistoryGraphWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<HistoryWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    try {
        if (message == WM_NCCREATE) {
            state = static_cast<HistoryWindowState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            state->graph = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
        if (!state) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
            case WM_CREATE: {
                state->graphTip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr,
                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                    CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (state->graphTip) {
                    TOOLINFOW tool{};
                    // Only V2 fields are used; the v6-only size is rejected by comctl32 v5.
                    tool.cbSize = TTTOOLINFOW_V2_SIZE;
                    tool.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
                    tool.hwnd = window;
                    tool.uId = 1;
                    tool.lpszText = const_cast<PWSTR>(L"");
                    if (!SendMessageW(state->graphTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool))) {
                        DestroyWindow(state->graphTip);
                        state->graphTip = nullptr;
                    }
                }
                return 0;
            }
            case WM_PAINT: HistoryPaintGraph(*state); return 0;
            case WM_ERASEBKGND: return 1;
            case WM_SIZE:
                HistoryScroll(*state, state->scroll);
                return 0;
            case WM_MOUSEMOVE:
                HistoryUpdateHover(*state, {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))});
                return 0;
            case WM_MOUSELEAVE:
                HistoryClearHover(*state);
                return 0;
            case WM_CANCELMODE:
                HistoryClearHover(*state);
                break;  // DefWindowProc must release capture and cancel scrollbar thumb tracking.
            case WM_LBUTTONDOWN: SetFocus(window); return 0;
            case WM_KILLFOCUS: HistoryClearHover(*state); InvalidateRect(window, nullptr, FALSE); return 0;
            case WM_SETFOCUS: InvalidateRect(window, nullptr, FALSE); return 0;
            case WM_GETDLGCODE: {
                const auto* key = reinterpret_cast<const MSG*>(lParam);
                if (key && key->message == WM_KEYDOWN && (key->wParam == VK_PRIOR || key->wParam == VK_NEXT ||
                    key->wParam == VK_HOME || key->wParam == VK_END)) return DLGC_WANTMESSAGE;
                return DLGC_WANTARROWS;
            }
            case WM_MOUSEWHEEL: {
                UINT lines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                state->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
                int notches = state->wheelRemainder / WHEEL_DELTA;
                state->wheelRemainder %= WHEEL_DELTA;
                RECT client{};
                GetClientRect(window, &client);
                int step = lines == WHEEL_PAGESCROLL ? client.bottom : ScaleForDpi(20, state->dpi) * int(std::min(lines, 100U));
                long long position = static_cast<long long>(state->scroll) - static_cast<long long>(notches) * step;
                HistoryScroll(*state, int(std::clamp(position, 0LL, static_cast<long long>(INT_MAX))));
                return 0;
            }
            case WM_KEYDOWN: case WM_VSCROLL: {
                SCROLLINFO info{};
                info.cbSize = sizeof(info);
                info.fMask = SIF_ALL;
                GetScrollInfo(window, SB_VERT, &info);
                int code = LOWORD(wParam);
                if (message == WM_KEYDOWN) {
                    switch (wParam) {
                        case VK_UP: code = SB_LINEUP; break;
                        case VK_DOWN: code = SB_LINEDOWN; break;
                        case VK_PRIOR: code = SB_PAGEUP; break;
                        case VK_NEXT: code = SB_PAGEDOWN; break;
                        case VK_HOME: code = SB_TOP; break;
                        case VK_END: code = SB_BOTTOM; break;
                        default: return DefWindowProcW(window, message, wParam, lParam);
                    }
                }
                long long position = state->scroll;
                switch (code) {
                    case SB_LINEUP: position -= ScaleForDpi(32, state->dpi); break;
                    case SB_LINEDOWN: position += ScaleForDpi(32, state->dpi); break;
                    case SB_PAGEUP: position -= info.nPage; break;
                    case SB_PAGEDOWN: position += info.nPage; break;
                    case SB_TOP: position = 0; break;
                    case SB_BOTTOM: position = info.nMax; break;
                    case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
                    default: return 0;
                }
                HistoryScroll(*state, int(std::clamp(position, 0LL, static_cast<long long>(INT_MAX))));
                return 0;
            }
            case WM_DESTROY:
                HistoryClearHover(*state);
                if (state->graphTip) DestroyWindow(state->graphTip);
                state->graphTip = nullptr;
                return 0;
            case WM_NCDESTROY:
                state->graph = nullptr;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    } catch (...) {
        Wh_Log(L"History graph callback failed");
        if (state && state->window) {
            SetWindowTextW(state->status, L"ERROR: history rendering failed; closing the window.");
            PostMessageW(state->window, WM_CLOSE, 0, 0);
        }
        return message == WM_NCCREATE ? FALSE : 0;
    }
}

static LRESULT CALLBACK HistoryWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<HistoryWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    try {
        if (message == WM_NCCREATE) {
            state = static_cast<HistoryWindowState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
        if (!state) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
            case WM_CREATE: {
                state->dpi = WindowDpi(window);
                HINSTANCE instance = GetModuleHandleW(nullptr);
                struct Control { HWND* slot; PCWSTR kind; PCWSTR text; DWORD style; int id; };
                const Control controls[] = {
                    // Z-order alone doesn't clip sibling painting: the banner must not erase its overlaid button.
                    {&state->recordingBanner, L"STATIC", L"   History recording is off.", SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX | WS_BORDER | WS_CLIPSIBLINGS, 0},
                    {&state->enableButton, L"BUTTON", L"E&nable recording", WS_TABSTOP | BS_PUSHBUTTON, kHistoryEnableButton},
                    {&state->accountLabel, L"STATIC", L"&Account:", SS_LEFT, 0},
                    {&state->accountCombo, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kHistoryAccountCombo},
                    {&state->meterLabel, L"STATIC", L"&View:", SS_LEFT, 0},
                    {&state->meterCombo, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kHistoryMeterCombo},
                    {&state->rangeLabel, L"STATIC", L"&Range:", SS_LEFT, 0},
                    {&state->rangeCombo, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, kHistoryRangeCombo},
                    {&state->overlayCheck, L"BUTTON", L"&Compare previous week", WS_TABSTOP | BS_AUTOCHECKBOX, kHistoryOverlayCheck},
                    {&state->refreshButton, L"BUTTON", L"&Refresh (F5)", WS_TABSTOP | BS_PUSHBUTTON, kHistoryRefreshButton},
                    {&state->exportButton, L"BUTTON", L"&Export CSV...", WS_TABSTOP | BS_PUSHBUTTON, kHistoryExportButton},
                    {&state->clearButton, L"BUTTON", L"&Clear all history...", WS_TABSTOP | BS_PUSHBUTTON, kHistoryClearButton},
                    {&state->settingsButton, L"BUTTON", L"&Settings...", WS_TABSTOP | BS_PUSHBUTTON, kHistorySettingsButton},
                    {&state->infoButton, L"BUTTON", L"&Info...", WS_TABSTOP | BS_PUSHBUTTON, kHistoryInfoButton},
                    {&state->legend, L"STATIC", L"Solid: current", SS_LEFT | SS_NOPREFIX, 0},
                    {&state->graph, kHistoryGraphClass, L"Usage history graph and legend; arrows, Page Up/Down, Home/End scroll", WS_TABSTOP | WS_VSCROLL | WS_BORDER, kHistoryGraphControl},
                    {&state->status, L"STATIC", L"Loading local history...", SS_LEFT | SS_NOPREFIX, 0},
                };
                for (const auto& control : controls) {
                    *control.slot = CreateWindowExW(0, control.kind, control.text, WS_CHILD | WS_VISIBLE | control.style,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(INT_PTR(control.id)), instance,
                        control.slot == &state->graph ? state : nullptr);
                    if (!*control.slot) return -1;
                }
                state->exportTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                    CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
                if (state->exportTip) {
                    TOOLINFOW tip{};
                    tip.cbSize = sizeof(tip);
                    tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                    tip.hwnd = window;
                    tip.uId = reinterpret_cast<UINT_PTR>(state->exportButton);
                    tip.lpszText = const_cast<PWSTR>(L"Export all raw meters for the selected account, or every account under All accounts. Uses the selected date range, plus the preceding 7 days in week comparison. UTC timestamps; text IDs; no labels.");
                    SendMessageW(state->exportTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
                    SendMessageW(state->exportTip, TTM_SETMAXTIPWIDTH, 0, ScaleForDpi(420, state->dpi));
                }
                for (int view = 0; view < 3; ++view)
                    SendMessageW(state->meterCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kHistoryViewNames[view]));
                for (auto range : {L"24 hours", L"7 days", L"30 days", L"90 days", L"All retained"})
                    SendMessageW(state->rangeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(range));
                SendMessageW(state->meterCombo, CB_SETCURSEL, WPARAM(state->allAccountsView), 0);
                SendMessageW(state->rangeCombo, CB_SETCURSEL, state->range, 0);
                EnableWindow(state->exportButton, FALSE);
                HistoryLayout(*state, true);
                PostMessageW(window, kHistoryLoadMessage, 0, 0);
                return 0;
            }
            case kHistoryActivateMessage:
                if (state->closing || g_unloading || g_historyWindowCancelRequested) return 0;
                ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
                SetForegroundWindow(g_historyDialog.load() ? g_historyDialog.load() : window);
                HistoryLoad(*state);
                return 0;
            case kHistoryLoadMessage: HistoryLoad(*state); return 0;
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE) HistoryClearHover(*state);
                break;
            case WM_ENABLE:
                if (!wParam) HistoryClearHover(*state);
                break;
            case WM_SIZE: if (state->graph) HistoryLayout(*state, false); return 0;
            case WM_DPICHANGED: {
                state->dpi = HIWORD(wParam);
                const auto* rect = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                             rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
                HistoryLayout(*state, true);
                if (state->exportTip) SendMessageW(state->exportTip, TTM_SETMAXTIPWIDTH, 0, ScaleForDpi(420, state->dpi));
                return 0;
            }
            case WM_GETMINMAXINFO: {
                auto* size = reinterpret_cast<MINMAXINFO*>(lParam);
                size->ptMinTrackSize = {ScaleForDpi(800, state->dpi), ScaleForDpi(590, state->dpi)};
                return 0;
            }
            case WM_SETFOCUS: if (state->accountCombo) SetFocus(state->accountCombo); return 0;
            case WM_MOUSEWHEEL: return SendMessageW(state->graph, message, wParam, lParam);
            case WM_CTLCOLORSTATIC:
                if (reinterpret_cast<HWND>(lParam) == state->recordingBanner) {
                    SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_INFOTEXT));
                    SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_INFOBK));
                    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_INFOBK));
                }
                break;
            case WM_COMMAND: {
                int id = LOWORD(wParam), event = HIWORD(wParam);
                if (id == IDCANCEL) { PostMessageW(window, WM_CLOSE, 0, 0); return 0; }
                if (state->closing || state->modal || state->loading || g_unloading || g_historyWindowCancelRequested) return 0;
                if (event == CBN_SELCHANGE) {
                    if (id == kHistoryAccountCombo) {
                        state->selected = int(SendMessageW(state->accountCombo, CB_GETCURSEL, 0, 0)) - 1;
                        HistoryRebuildView(*state);
                    } else if (id == kHistoryMeterCombo) {
                        int choice = int(SendMessageW(state->meterCombo, CB_GETCURSEL, 0, 0));
                        if (choice != CB_ERR) {
                            auto& view = state->selected < 0 ? state->allAccountsView : state->accountView;
                            view = HistoryView(std::clamp(choice, 0, int(HistoryView::AllMeters)));
                            HistoryRebuildView(*state);
                        }
                    } else if (id == kHistoryRangeCombo) {
                        state->range = std::clamp(int(SendMessageW(state->rangeCombo, CB_GETCURSEL, 0, 0)), 0, 4);
                        HistoryLoad(*state);
                    }
                } else if (event == BN_CLICKED) {
                    switch (id) {
                        case kHistoryOverlayCheck:
                            state->overlay = SendMessageW(state->overlayCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                            // Show the actual comparison range rather than a disabled,
                            // misleading old selection. Restore the user's range afterward.
                            SendMessageW(state->rangeCombo, CB_SETCURSEL, state->overlay ? 1 : state->range, 0);
                            EnableWindow(state->rangeCombo, !state->overlay);
                            HistoryLoad(*state);
                            break;
                        case kHistoryRefreshButton: HistoryLoad(*state); break;
                        case kHistoryExportButton: HistoryExport(*state); break;
                        case kHistorySettingsButton: OpenSettingsWindow(); break;
                        case kHistoryEnableButton: {
                            SettingsApplyResult applied;
                            {
                                std::lock_guard configLock(g_configEditMutex);
                                if (g_unloading) break;
                                Settings settings;
                                {
                                    std::lock_guard settingsLock(g_settingsMutex);
                                    settings = g_settings;
                                }
                                settings.historyEnabled = true;
                                applied = ApplyOwnedSettings(std::move(settings));
                            }
                            // The shared edit path persists this user's consent without
                            // changing account selections. Never marshal with the edit lock held.
                            if (applied == SettingsApplyResult::Changed) FinishSettingsApply();
                            NotifySettingsWindowChanged(true);
                            HistoryLoad(*state);
                            if (applied == SettingsApplyResult::Failed) {
                                HistorySetStatus(*state, L"Could not save recording consent. Recording is still off.");
                            } else {
                                SetFocus(state->accountCombo);
                            }
                            break;
                        }
                        case kHistoryInfoButton: {
                            std::wstring info =
                                L"Reading the charts\r\n"
                                L"These are sampled quota levels, not tokens or daily consumption. Choose Short-term, Weekly, Extra usage, or All meters. "
                                L"Weekly follows each Anthropic account's taskbar weekly selections; removed accounts keep both weeklies. "
                                L"Refresh/F5 picks up changed selections. Antigravity Gemini and Claude/GPT quotas share the matching short/weekly views. "
                                L"Each quota keeps its own line and resets; overlapping limits are never added or merged. "
                                L"Short-window labels use recorded durations (or current limit when unknown). Antigravity shows limiting pools, which can change. "
                                L"All accounts starts on Weekly. A single account starts on All meters. Both modes offer all four views; "
                                L"All meters includes weeklies hidden on the taskbar. "
                                L"Each mode remembers its view until the window closes.\r\n\r\n"
                                L"Legend and hover\r\n"
                                L"The wrapping legend above the plot identifies each plotted account/quota with its line color and width. "
                                L"Hover a legend entry to emphasize that quota and dim the others; both comparison periods stay visible. "
                                L"Hover the plot to identify every nearby or overlapping line, including current and previous weeks. "
                                L"Each line reports a real recorded endpoint nearest its closest drawn segment, not an interpolated reading. "
                                L"Previous-week dates remain the original sample dates. Gaps cannot be hovered as connected lines. "
                                L"Details-only dollar spending and empty statuses have no line legend.\r\n\r\n"
                                L"Extra usage and empty entries\r\n"
                                L"Extra usage shows Anthropic budget used (%) on the left and OpenAI credits remaining on the right. "
                                L"Usage rises with spending; remaining credits fall. Dollars appear in details, including accounts without percentage readings. "
                                L"All meters also plots Anthropic dollar spending on its own axis, separate from percentages and credits. "
                                L"Unsupported quotas are omitted; accounts without readings get a compact status entry. "
                                L"Dollar details report sampled last/peak spending, not total spending across the selected range.\r\n\r\n"
                                L"Lines and markers\r\n"
                                L"All accounts uses an account color family: primary quotas are thicker, scoped quotas thinner and tinted. "
                                L"Single-account colors stay tied to each meter. Solid lines: current. Dashed lines: previous. Dots mark only isolated samples "
                                L"(filled: current, hollow: previous). Resets, missing or failed readings, gaps, restarts, "
                                L"and recording changes break lines without extra markers. "
                                L"Lines are reduced for drawing; statistics and CSV use the raw samples.\r\n\r\n"
                                L"Statistics and time\r\n"
                                L"Peak is the highest recorded value. Coverage counts connected intervals. Time above the red threshold is "
                                L"estimated within those intervals, never across gaps or beyond the last sample. Dates are local. "
                                L"Week comparison uses two elapsed seven-day periods; daylight saving can shift local clock times.\r\n\r\n"
                                L"Recording and export\r\n"
                                L"Enable recording using the banner or Settings. This opts in the current Windows user; "
                                L"per-account selections and hidden-account rules still apply. Recording starts with the next poll. "
                                L"Pending recording waits for account history IDs to be saved. "
                                L"No data can also mean an unsupported meter, failed polls, or expired history. Nothing is backfilled. "
                                L"Refresh / F5 reads local history only. CSV includes all supported meters for the selected accounts and range "
                                L"(both weeks when comparing), with UTC times and IDs instead of labels. Credits are not dollars or a spending ledger.\r\n\r\n"
                                L"Retention: " + std::to_wstring(state->retention) + L" days, also limited to 250,000 records / about 60 MB. "
                                L"Turning recording off keeps saved history. Removed accounts remain under their IDs until expiry or Clear.";
                            if (!state->snapshot.samples.empty()) {
                                info += L"\r\nOldest loaded sample: " + HistoryLocalDate(state->snapshot.samples.front().timeMs);
                            }
                            HistoryModalScope modal(*state);
                            if (modal.hook) MessageBoxW(window, info.c_str(), L"Usage history — Info", MB_OK | MB_ICONINFORMATION);
                            break;
                        }
                        case kHistoryClearButton: {
                            int answer = IDCANCEL;
                            {
                                HistoryModalScope modal(*state);
                                if (!modal.hook) break;
                                answer = MessageBoxW(window,
                                    L"Permanently clear ALL local usage history for this Windows user, including deleted accounts?\r\n\r\nThis cannot be undone. Settings and sign-ins are not changed. If recording stays enabled, future polls will record new history.",
                                    L"Clear all usage history", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
                            }
                            if (state->closing || g_unloading || g_historyWindowCancelRequested) break;
                            if (answer != IDOK) { HistorySetStatus(*state, L"Clear history cancelled."); break; }
                            std::wstring error;
                            if (ClearHistory(&error)) {
                                HistoryLoad(*state);
                                HistorySetStatus(*state, L"All local history cleared. Future polls can start recording again.");
                            } else HistorySetStatus(*state, L"ERROR: could not clear all history. " + error);
                            break;
                        }
                    }
                }
                return 0;
            }
            case WM_CLOSE:
                state->closing = true;
                HistoryClearHover(*state);
                if (state->modal) {
                    if (HWND dialog = g_historyDialog.load()) {
                        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
                        PostMessageW(dialog, WM_CLOSE, 0, 0);
                    }
                    return 0;
                }
                DestroyWindow(window);
                return 0;
            case WM_DESTROY:
                g_historyWindow.store(nullptr);
                PostQuitMessage(0);
                return 0;
            case WM_NCDESTROY:
                // Children are gone; no control can still use the font or stack state.
                if (state->font) { DeleteObject(state->font); state->font = nullptr; }
                state->window = nullptr;
                state->graph = nullptr;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    } catch (...) {
        Wh_Log(L"History window callback failed");
        if (message == WM_NCCREATE) return FALSE;
        if (message == WM_CREATE) return -1;
        if (state && state->status) SetWindowTextW(state->status, L"ERROR: history operation failed; closing the window.");
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
}

static DWORD WINAPI HistoryWindowThreadProc(LPVOID) {
    // COM is only for the native file dialog. No XAML or WinRT references live here.
    HINSTANCE instance = GetModuleHandleW(nullptr);
    HistoryWindowState state;  // Outlives all callbacks, including DestroyWindow below.
    bool registeredWindow = false, registeredGraph = false;
    HRESULT apartment = E_FAIL;
    try {
        using SetThreadDpi_t = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            auto setDpi = reinterpret_cast<SetThreadDpi_t>(GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
            if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&controls);
        if (g_unloading || g_historyWindowCancelRequested) {
            if (SUCCEEDED(apartment)) CoUninitialize();
            return 0;
        }
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.hInstance = instance;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        cls.lpszClassName = kHistoryWindowClass;
        cls.lpfnWndProc = HistoryWindowProc;
        registeredWindow = RegisterClassExW(&cls) != 0;
        if (!registeredWindow && GetLastError() == ERROR_CLASS_ALREADY_EXISTS &&
            UnregisterClassW(kHistoryWindowClass, instance)) {
            registeredWindow = RegisterClassExW(&cls) != 0;
        }
        cls.hbrBackground = nullptr;
        cls.lpszClassName = kHistoryGraphClass;
        cls.lpfnWndProc = HistoryGraphWndProc;
        if (registeredWindow) {
            registeredGraph = RegisterClassExW(&cls) != 0;
            if (!registeredGraph && GetLastError() == ERROR_CLASS_ALREADY_EXISTS &&
                UnregisterClassW(kHistoryGraphClass, instance)) {
                registeredGraph = RegisterClassExW(&cls) != 0;
            }
        }
        if (registeredWindow && registeredGraph && !g_unloading && !g_historyWindowCancelRequested) {
            POINT cursor{};
            GetCursorPos(&cursor);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
            UINT dpi = WindowDpi(nullptr);
            int width = std::min(ScaleForDpi(1100, dpi), int(monitor.rcWork.right - monitor.rcWork.left));
            int height = std::min(ScaleForDpi(830, dpi), int(monitor.rcWork.bottom - monitor.rcWork.top));
            int x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
            int y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
            HWND window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, kHistoryWindowClass,
                L"Taskbar AI Quota Bars — Usage history", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                x, y, width, height, nullptr, nullptr, instance, &state);
            if (window) {
                g_historyWindow.store(window);
                if (g_unloading || g_historyWindowCancelRequested) PostMessageW(window, WM_CLOSE, 0, 0);
                else { ShowWindow(window, SW_SHOW); SetForegroundWindow(window); }
                MSG message{};
                int result;
                while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                    if (message.message == WM_KEYDOWN &&
                        (message.wParam == VK_F5 || (message.wParam == 'R' && GetKeyState(VK_CONTROL) < 0))) {
                        if (!state.modal) PostMessageW(window, kHistoryLoadMessage, 0, 0);
                        continue;
                    }
                    if (!IsDialogMessageW(window, &message)) {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }
                if (result < 0) Wh_Log(L"History message loop failed: %lu", GetLastError());
            } else Wh_Log(L"Could not create history window: %lu", GetLastError());
        } else if (!g_unloading && !g_historyWindowCancelRequested) {
            Wh_Log(L"Could not register history window classes: %lu", GetLastError());
        }
    } catch (...) {
        Wh_Log(L"History worker failed");
    }
    // Native modal APIs have unwound and their hooks are removed before this point.
    if (state.window && IsWindow(state.window)) DestroyWindow(state.window);
    if (state.font) DeleteObject(state.font);
    g_historyDialog.store(nullptr);
    g_historyWindow.store(nullptr);
    if (registeredGraph) UnregisterClassW(kHistoryGraphClass, instance);
    if (registeredWindow) UnregisterClassW(kHistoryWindowClass, instance);
    if (SUCCEEDED(apartment)) CoUninitialize();
    return 0;
}

static void OpenHistoryWindow() {
    if (g_unloading) return;
    std::lock_guard lock(g_historyWindowMutex);
    // Recheck under the same lock as close/thread publication; never spawn during unload.
    if (g_unloading || g_historyWindowCancelRequested) return;
    if (HWND window = g_historyWindow.load(); window && IsWindow(window)) {
        PostMessageW(window, kHistoryActivateMessage, 0, 0);
        return;
    }
    if (g_historyWindowThread) {
        if (WaitForSingleObject(g_historyWindowThread, 0) != WAIT_OBJECT_0) return;
        CloseHandle(g_historyWindowThread);
        g_historyWindowThread = nullptr;
    }
    g_historyWindowThread = CreateThread(nullptr, 0, HistoryWindowThreadProc, nullptr, 0, nullptr);
    if (!g_historyWindowThread) Wh_Log(L"Could not create history window thread: %lu", GetLastError());
}

// Lifecycle API: call from the unload thread, after g_unloading=true. Never from this
// window's thread and never while holding settings/history/auth locks.
static void CloseHistoryWindow() {
    HANDLE thread = nullptr;
    {
        std::lock_guard lock(g_historyWindowMutex);
        g_historyWindowCancelRequested = true;
        if (HWND dialog = g_historyDialog.load()) {
            PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            PostMessageW(dialog, WM_CLOSE, 0, 0);
        }
        if (HWND window = g_historyWindow.load()) PostMessageW(window, WM_CLOSE, 0, 0);
        thread = g_historyWindowThread;
        g_historyWindowThread = nullptr;
    }
    if (thread) {
        // A finite wait could unload the DLL while a dialog/hook still runs mod code.
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    g_historyDialog.store(nullptr);
    g_historyWindow.store(nullptr);
}

static HANDLE g_fetchThread = nullptr;
static void* g_mtaUsageCookie = nullptr;
static HRESULT (WINAPI* g_coDecrementMTAUsage)(void*) = nullptr;
static bool g_winsockStarted = false;

BOOL Wh_ModInit() {
    Wh_Log(L"Init");
    g_unloading = false;
    g_refreshing = false;
    g_refreshAccountIdentity = 0;
    g_refreshGeneration = 0;
    g_uiInjected.store(false, std::memory_order_release);
    g_fetchThreadStarted.store(false, std::memory_order_release);
    g_loginInProgress.store(false);
    g_loginAccountIdentity.store(0);
    g_settingsWindowCancelRequested = false;
    g_historyWindowCancelRequested = false;
    g_winsockStarted = false;

    // C++/WinRT caches agile activation factories process-wide. Keep the MTA alive so a
    // temporary worker apartment can't tear down a factory that another thread later reuses.
    HMODULE combase = GetModuleHandleW(L"combase.dll");
    auto coIncrementMTAUsage = reinterpret_cast<HRESULT (WINAPI*)(void**)>(
        combase ? GetProcAddress(combase, "CoIncrementMTAUsage") : nullptr);
    g_coDecrementMTAUsage = reinterpret_cast<HRESULT (WINAPI*)(void*)>(
        combase ? GetProcAddress(combase, "CoDecrementMTAUsage") : nullptr);
    HRESULT mtaResult = coIncrementMTAUsage && g_coDecrementMTAUsage ?
                            coIncrementMTAUsage(&g_mtaUsageCookie) : E_NOINTERFACE;
    if (FAILED(mtaResult)) {
        Wh_Log(L"CoIncrementMTAUsage failed: 0x%08X", (unsigned)mtaResult);
        g_mtaUsageCookie = nullptr;
        g_coDecrementMTAUsage = nullptr;
        return FALSE;
    }
    LoadSettings();

    g_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    g_refreshEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    g_injectEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!g_stopEvent || !g_refreshEvent || !g_injectEvent) {
        Wh_Log(L"Event creation failed");
        if (g_stopEvent) CloseHandle(g_stopEvent);
        if (g_refreshEvent) CloseHandle(g_refreshEvent);
        if (g_injectEvent) CloseHandle(g_injectEvent);
        g_stopEvent = nullptr;
        g_refreshEvent = nullptr;
        g_injectEvent = nullptr;
        g_coDecrementMTAUsage(g_mtaUsageCookie);
        g_mtaUsageCookie = nullptr;
        g_coDecrementMTAUsage = nullptr;
        return FALSE;
    }

    if (!HookTaskbarDllSymbols()) {
        Wh_Log(L"HookTaskbarDllSymbols failed");
        CloseHandle(g_stopEvent);
        CloseHandle(g_refreshEvent);
        CloseHandle(g_injectEvent);
        g_stopEvent = nullptr;
        g_refreshEvent = nullptr;
        g_injectEvent = nullptr;
        g_coDecrementMTAUsage(g_mtaUsageCookie);
        g_mtaUsageCookie = nullptr;
        g_coDecrementMTAUsage = nullptr;
        return FALSE;
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
        g_winsockStarted = true;
    } else {
        Wh_Log(L"WSAStartup failed; OpenAI sign-in unavailable");
    }
    return TRUE;
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
    CloseHistoryWindow();

    g_settingsWindowCancelRequested = true;
    HANDLE settingsWindowThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_settingsWindowMutex);
        if (HWND settingsWindow = g_settingsWindow.load()) {
            PostMessageW(settingsWindow, WM_CLOSE, 0, 0);
        }
        settingsWindowThread = g_settingsWindowThread;
        g_settingsWindowThread = nullptr;
    }
    if (settingsWindowThread) {
        WaitForSingleObject(settingsWindowThread, INFINITE);
        CloseHandle(settingsWindowThread);
    }
    g_settingsWindow.store(nullptr);

    // Unblock an in-flight sign-in: close the paste dialog and/or the loopback listener so the
    // login thread falls out of its message/accept loop, then join it. The lock pairs with
    // StartLogin (g_unloading is already set) so a concurrent click can't spawn a thread we miss.
    {
        std::lock_guard<std::mutex> lk(g_loginThreadMutex);
        if (HWND loginWnd = g_loginWnd.load()) PostMessageW(loginWnd, WM_CLOSE, 0, 0);
        if (SOCKET s = g_loginSocket.exchange(INVALID_SOCKET); s != INVALID_SOCKET) closesocket(s);
        if (g_loginThread) {
            WaitForSingleObject(g_loginThread, INFINITE);
            CloseHandle(g_loginThread);
            g_loginThread = nullptr;
        }
    }
    g_loginAccountIdentity.store(0);
    g_loginInProgress.store(false);

    HANDLE retryThread = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_retryThreadMutex);
        retryThread = g_retryThread;
        g_retryThread = nullptr;
    }
    if (retryThread) {
        WaitForSingleObject(retryThread, INFINITE);
        CloseHandle(retryThread);
    }

    if (g_fetchThread) {
        WaitForSingleObject(g_fetchThread, INFINITE);
        CloseHandle(g_fetchThread);
        g_fetchThread = nullptr;
    }
    g_fetchThreadStarted.store(false, std::memory_order_release);

    // Both journal clients have exited. No process-wide destructor needs to flush
    // history, and another Explorer session can safely take ownership now.
    {
        std::lock_guard lock(g_historyMutex);
        g_historyOwnership.close();
        g_historySamples.clear();
        g_historyPath.clear();
        SetHistoryStatus({});
        g_historyLoaded = false;
        g_historyWritable = true;
        g_historyDirty = false;
        g_historyCapped = false;
        g_historyMaintenanceMs = 0;
        g_historyWriteRetryMs = 0;
    }

    // All worker threads that can issue finite taskbar marshals are joined. These synchronous
    // cleanup sends are the final per-UI-thread lifetime barrier before the DLL can unload.
    RemoveAllQuotaGrids(true);

    // A taskbar thread that terminated without window teardown has no owner thread left for its
    // XAML releases. Leak those unreachable states rather than release them under loader lock.
    {
        std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
        if (g_uiInstances && !g_uiInstances->empty()) {
            Wh_Log(L"Leaking %zu orphaned UI state(s) after owner thread exit",
                   g_uiInstances->size());
            for (auto& state : *g_uiInstances) state.release();
        }
        g_uiInstances.reset();
    }

    if (g_stopEvent) CloseHandle(g_stopEvent);
    if (g_refreshEvent) CloseHandle(g_refreshEvent);
    if (g_injectEvent) CloseHandle(g_injectEvent);
    g_stopEvent = nullptr;
    g_refreshEvent = nullptr;
    g_injectEvent = nullptr;
    if (g_winsockStarted) {
        WSACleanup();
        g_winsockStarted = false;
    }
    if (g_mtaUsageCookie) {
        g_coDecrementMTAUsage(g_mtaUsageCookie);
        g_mtaUsageCookie = nullptr;
    }
    g_coDecrementMTAUsage = nullptr;
}
