// ==WindhawkMod==
// @id              taskbar-ai-quota
// @name            Taskbar AI Quota
// @description     Shows configurable AI agent/LLM subscription quota bars for Anthropic, OpenAI, Google Antigravity, GitHub Copilot, and OpenCode Go on the Windows 11 taskbar
// @description:de-DE Zeigt konfigurierbare Kontingentbalken für KI-Agenten- und LLM-Abos von Anthropic, OpenAI, Google Antigravity, GitHub Copilot und OpenCode Go in der Taskleiste von Windows 11
// @description:es-ES Muestra barras configurables de las cuotas de suscripciones de agentes de IA y LLM de Anthropic, OpenAI, Google Antigravity, GitHub Copilot y OpenCode Go en la barra de tareas de Windows 11
// @description:fr-FR Affiche des barres configurables des quotas d’abonnements d’agents IA et de LLM d’Anthropic, OpenAI, Google Antigravity, GitHub Copilot et OpenCode Go dans la barre des tâches de Windows 11
// @description:ja-JP Anthropic、OpenAI、Google Antigravity、GitHub Copilot、OpenCode Go の AI エージェント/LLM サブスクリプションのクォータを、設定可能なバーで Windows 11 のタスク バーに表示します
// @description:pt-BR Mostra barras configuráveis das cotas de assinaturas de agentes de IA e LLMs da Anthropic, OpenAI, Google Antigravity, GitHub Copilot e OpenCode Go na barra de tarefas do Windows 11
// @description:ru-RU Показывает настраиваемые полосы квот подписок на ИИ-агентов и LLM от Anthropic, OpenAI, Google Antigravity, GitHub Copilot и OpenCode Go на панели задач Windows 11
// @description:zh-CN 在 Windows 11 任务栏上以可配置的条形显示 Anthropic、OpenAI、Google Antigravity、GitHub Copilot 和 OpenCode Go 的 AI 智能体/LLM 订阅配额
// @version         2.17.0
// @author          Cleroth
// @github          https://github.com/Cleroth
// @include         explorer.exe
// @architecture    x86-64
// @license         MIT
// @compilerOptions -DWIN32_LEAN_AND_MEAN -lole32 -loleaut32 -lruntimeobject -lwindowsapp -lwinhttp -luser32 -lshell32 -lgdi32 -ladvapi32 -lws2_32 -liphlpapi -lcrypt32 -lbcrypt -lcomctl32 -lcomdlg32 -luuid -ld2d1 -ldwrite -ldwmapi -lwindowscodecs
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar AI Quota

A Windows 11 taskbar mod that shows subscription quota bars next to the system tray, or
elsewhere in the tray or beside Start. On a taskbar at the left or right of the screen (Windows
11's taskbar position setting, 26200.9457 or later), the accounts stack down it and each style
rearranges to fit its width.

Supported providers and quotas:

- **Anthropic Claude:** 5-hour, weekly, Fable weekly, and monthly extra usage
- **OpenAI/Codex:** 5-hour, weekly, and prepaid credits against a max you set
- **Google Antigravity:** short-window and weekly Gemini pools and Gemini Argon weekly; Claude/GPT
  pool limits and its other reported quotas in the details flyout
- **GitHub Copilot:** the monthly AI credits allowance (Copilot Free's included); the additional
  usage GitHub reports past it and an organization's pooled credits in the details flyout
- **OpenCode Go:** 5-hour, weekly, and monthly usage limits

Optional notifications, under the mod's own name and icon, warn when usage reaches the yellow,
orange or red threshold you choose, and say when it is projected to reach the limit.

While a provider's public status page (Claude, OpenAI's Codex, GitHub Copilot, OpenCode Go)
reports a problem, its accounts show a warning triangle, and the details flyout names the
component and incident, with a link to the page. Nothing shows while all is well; turn it off
under Display.

Available in English, German, Spanish, French, Japanese, Brazilian Portuguese, Russian and
Simplified Chinese (machine-translated), following your Windows display language by default.

![Quota tooltip](https://raw.githubusercontent.com/Cleroth/windhawk-taskbar-ai-quota/master/ss1.png)

![Compact taskbar bars](https://raw.githubusercontent.com/Cleroth/windhawk-taskbar-ai-quota/master/ss2.png)

![Threshold notification](https://raw.githubusercontent.com/Cleroth/windhawk-taskbar-ai-quota/master/ss3.png)

## Setup

Click the **AI +** taskbar tile (later: right-click a quota column, **Settings**) and choose
**Add account**, then name it (or keep its default label) and press Enter: its sign-in window
opens. Anthropic and OpenAI use browser sign-in, GitHub Copilot a code you enter on github.com
and OpenCode Go an API key you paste from its console, with tokens and keys encrypted locally
with Windows DPAPI. Antigravity uses its
signed-in local app or CLI session, which must remain running.

## Settings

- **Accounts:** Add, reorder, hide, or remove accounts; per account, set its label, taskbar
  quotas, identity (logo, logo with a number badge, or label, on its bars and tiles), history
  recording, and sign-in. Removing an account deletes its sign-in and, if you choose, its
  history.
- **Layout:** Tile style with a live taskbar preview, position on the taskbar (tray slots, or
  Left side and Right of Start, experimental), bar style (full track or thin rail), sizes,
  usage graph spans at rest and on hover (whole weekly or monthly windows, or 12 to 48 hours
  each side of now),
  logo size and colors (Claude's orange, Gemini's gradient), number badge size, color and
  position, identity position, bar text (percentage text, vertical bars' own, and quota keys),
  pace markers, and spacing.
- **Display:** Material of the details flyout and the settings and history windows (Acrylic,
  Mica, Mica Alt, Tinted wallpaper or Solid; needs Windows transparency effects), used or
  remaining, usage colors (default, colorblind or your own) and thresholds, stale marker,
  provider outages.
- **Behavior:** Choose the language, displays, hover details, polling, and alerts.
- **History:** Local recording is on by default per Windows user; keep history forever (the
  default) or for a number of days, and choose recording per account independently of taskbar
  visibility. Hidden accounts stop polling unless you disable **Do not poll hidden accounts**
  and enable their recording.

## Usage history

Open **Usage history...** from the taskbar context menu or Settings > History. Compare raw
quota timelines on one graph with color-coded account lines, overlay the preceding week,
view extra-spending/credit balances, and export CSV. Refresh/F5 reads local samples;
history starts when recording starts, with no backfill.
All accounts defaults to Weekly over the past 7 days (set the time span in Settings > History);
choose Short-term for short quotas.
Weekly follows each Anthropic account's taskbar weekly selections. All meters is available
for all accounts or a single account (its default), including weeklies hidden on the taskbar.
Percentages, USD, and credits use separate axes. Connected samples use lines only; dots keep
isolated readings visible.
Percentages from different accounts or overlapping windows are not added together.

History follows the configured entry: use a new entry for a different login. Data stays under
`%LOCALAPPDATA%\TaskbarAIQuota\<mod ID>` with your user-folder permissions, without tokens or
labels. It is not encrypted and survives mod removal. Kept forever, it grows without limit;
with a number of days, retention and a roughly 60 MB size cap prune old samples. **Clear all
history...** removes recordings without changing sign-ins, and **Delete history...** on a
removed account's page removes just its recordings.
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
#include <propkey.h>
#include <commctrl.h>
#include <commdlg.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <unknwn.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <dwmapi.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Documents.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
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
namespace wuxi = winrt::Windows::UI::Xaml::Input;
namespace wuxs = winrt::Windows::UI::Xaml::Shapes;

// Unity build: the fragments below form one translation unit, included in dependency
// order. generate-mod.ps1 inlines them into the published single-file mod.

#include "i18n/template.h"

#include "i18n/catalog.h"

#include "i18n/lang-de.h"
#include "i18n/lang-es.h"
#include "i18n/lang-fr.h"
#include "i18n/lang-ja.h"
#include "i18n/lang-pt-BR.h"
#include "i18n/lang-ru.h"
#include "i18n/lang-zh-Hans.h"

#include "i18n/languages.h"

#include "i18n/pseudo.h"

#include "i18n/format.h"

#include "i18n/numbers.h"

#include "core/model.h"

#include "core/account-error.h"

#include "history/types.h"

#include "history/journal-error.h"

#include "history/state.h"

#include "taskbar/ui-types.h"

#include "core/forward.h"

#include "core/state.h"

#include "core/language.h"

#include "core/helpers.h"

#include "core/json.h"

#include "core/crypto.h"

#include "net/http.h"

#include "auth/token-store.h"

#include "auth/oauth.h"

#include "auth/login-layout.h"

#include "auth/login-controls.h"

#include "auth/login.h"

#include "providers/anthropic.h"

#include "providers/openai.h"

#include "providers/antigravity.h"

#include "providers/copilot.h"

#include "providers/opencode.h"

#include "history/journal.h"

#include "history/continuity.h"

#include "history/burn-up.h"

#include "history/burn-up-store.h"

#include "fetch/status-feeds.h"

#include "fetch/provider-status.h"

#include "fetch/notifications.h"

#include "fetch/fetch-thread.h"

#include "taskbar/marshal.h"

#include "taskbar/xaml-access.h"

#include "taskbar/logo-paths.h"

#include "taskbar/theme.h"

#include "taskbar/meter-model.h"

#include "taskbar/logos.h"

#include "taskbar/tiles.h"

#include "taskbar/actions.h"

#include "taskbar/flyout-material.h"

#include "taskbar/flyout-content.h"

#include "taskbar/flyout.h"

#include "taskbar/quota-grid.h"

#include "taskbar/quota-update.h"

#include "taskbar/placement.h"

#include "taskbar/inject.h"

#include "ui/tokens.h"

#include "ui/draw.h"

#include "ui/svg-path.h"

#include "ui/rich-text.h"

#include "ui/items.h"

#include "ui/paint.h"

#include "ui/input.h"
#include "ui/tooltip-layout.h"

#include "i18n/ui-runs.h"

#include "ui/system-theme.h"

#include "ui/surface.h"

#include "ui/window-icon.h"

#include "ui/popup.h"
#include "ui/tooltip.h"

#include "ui/text-box.h"

#include "ui/host.h"

#include "ui/color-dialog.h"

#include "settings/storage.h"

#include "settings/window-state.h"

#include "settings/native.h"

#include "settings/edits.h"

#include "settings/account-commands.h"

#include "settings/store.h"

#include "settings/view.h"

#include "settings/preview.h"

#include "settings/page-about.h"

#include "settings/page-behavior.h"

#include "settings/page-history.h"

#include "settings/page-layout.h"

#include "settings/page-display.h"

#include "settings/page-accounts.h"

#include "settings/shell.h"

#include "settings/ui-window.h"

#include "settings/window.h"

#include "history/series.h"

#include "history/csv.h"

#include "history/window-state.h"

#include "history/view.h"

#include "history/chart.h"

#include "history/controller.h"

#include "history/chart-paint.h"

#include "history/content.h"

#include "history/shell.h"

#include "history/modal.h"

#include "history/ops.h"

#include "history/ui-window.h"

#include "history/window.h"

static HANDLE g_fetchThread = nullptr;
static void* g_mtaUsageCookie = nullptr;
static HRESULT (WINAPI* g_coDecrementMTAUsage)(void*) = nullptr;
static bool g_winsockStarted = false;

BOOL Wh_ModInit() {
    Wh_Log(L"Init");
    g_unloading = false;
    g_refreshRequests.clear();
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
        Wh_Log(L"WSAStartup failed; OpenAI sign-in unavailable, Anthropic's asks for a code");
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
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            g_refreshRequests.clear();
        }
        g_fetchThreadStarted.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(g_dataMutex);
            for (auto& data : g_data) {
                data.stale = true;
                data.error = {AccountErrorCode::FetchThreadFailed};
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

    // Unblock an in-flight sign-in: close the sign-in window and the loopback listener so the
    // login thread falls out of its message loop, then join it. The lock pairs with
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

    // Browser launches, including any the sign-in window started. OpenUrl rechecks g_unloading
    // under the same lock, so none start after this. A stuck launch delays unload; Explorer's UI
    // stays responsive meanwhile.
    std::vector<HANDLE> urlLaunchThreads;
    {
        std::lock_guard<std::mutex> lk(g_urlLaunchMutex);
        urlLaunchThreads.swap(g_urlLaunchThreads);
    }
    for (HANDLE thread : urlLaunchThreads) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }

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
