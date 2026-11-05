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
    // The seconds must fit the SYSTEMTIME casts; SystemTimeToFileTime rejects other bad fields.
    if (!(sec >= 0 && sec < 61)) return 0;

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

// The length of the calendar month ending at `resetUnixMs`: from the same day and time (UTC) of
// the month before, that month's last day when it is shorter; 0 without a reset.
static ULONGLONG MonthBeforeMs(ULONGLONG resetUnixMs) {
    if (!resetUnixMs) return 0;
    ULONGLONG resetFileTime = (resetUnixMs + kFileTimeUnixEpochOffsetMs) * 10000;
    FILETIME resetFt{(DWORD)resetFileTime, (DWORD)(resetFileTime >> 32)};
    SYSTEMTIME start{};
    if (!FileTimeToSystemTime(&resetFt, &start)) return 0;
    if (start.wMonth == 1) {
        start.wMonth = 12;
        start.wYear--;
    } else {
        start.wMonth--;
    }
    static constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = kDaysPerMonth[start.wMonth - 1];
    if (start.wMonth == 2 &&
        (start.wYear % 4 == 0 && (start.wYear % 100 != 0 || start.wYear % 400 == 0))) {
        days = 29;
    }
    start.wDay = (WORD)std::min<int>(start.wDay, days);
    FILETIME startFt;
    if (!SystemTimeToFileTime(&start, &startFt)) return 0;
    ULONGLONG startFileTime = ((ULONGLONG)startFt.dwHighDateTime << 32) | startFt.dwLowDateTime;
    ULONGLONG startUnixMs = startFileTime / 10000 - kFileTimeUnixEpochOffsetMs;
    return startUnixMs < resetUnixMs ? resetUnixMs - startUnixMs : 0;
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

// The local time with its abbreviated weekday, e.g. "Fri 04:59".
static std::wstring FormatLocalDayTime(SYSTEMTIME const& local) {
    wchar_t day[16] = L"";
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, L"ddd", day, ARRAYSIZE(day), nullptr);
    return std::wstring(day) + L" " + FormatLocalTime(local);
}

// A duration in its two largest units, dropping a zero second one: "1d 3h", "2h 15m", "4m 12s" or
// "45s".
static std::wstring FormatShortDuration(const I18nLocale& locale, ULONGLONG seconds) {
    ULONGLONG days = seconds / (24 * 60 * 60);
    ULONGLONG hours = seconds / (60 * 60) % 24;
    ULONGLONG minutes = seconds / 60 % 60;
    ULONGLONG secs = seconds % 60;
    if (days > 0) {
        return hours ? I18nText(locale, TrCtx(L"duration", L"{days}d {hours}h"), {{L"days", days}, {L"hours", hours}}) :
                       I18nText(locale, TrCtx(L"duration", L"{days}d"), {{L"days", days}});
    }
    if (hours > 0) {
        return minutes ? I18nText(locale, TrCtx(L"duration", L"{hours}h {minutes}m"),
                                  {{L"hours", hours}, {L"minutes", minutes}}) :
                         I18nText(locale, TrCtx(L"duration", L"{hours}h"), {{L"hours", hours}});
    }
    if (minutes > 0) {
        return secs ? I18nText(locale, TrCtx(L"duration", L"{minutes}m {seconds}s"),
                               {{L"minutes", minutes}, {L"seconds", secs}}) :
                      I18nText(locale, TrCtx(L"duration", L"{minutes}m"), {{L"minutes", minutes}});
    }
    return I18nText(locale, TrCtx(L"duration", L"{seconds}s"), {{L"seconds", secs}});
}

// When a reset comes, as running text continues it: "in 3d 4h (Fri 04:59)" (rounded up to the
// minute; the local time, with its weekday from a day ahead), "now" once due, "?" without one.
static std::wstring FormatReset(const I18nLocale& locale, ULONGLONG unixMs) {
    if (!unixMs) return L"?";
    LONGLONG delta = (LONGLONG)(unixMs - NowUnixMs());
    if (delta <= 0) return I18nText(locale, TrCtx(L"reset-when", L"now"));

    std::wstring duration = FormatShortDuration(locale, ((ULONGLONG)delta + 59999) / 60000 * 60);
    SYSTEMTIME local;
    if (!UnixMsToLocalSystemTime(unixMs, &local)) {
        return I18nText(locale, TrCtx(L"reset-when", L"in {duration}"), {{L"duration", duration}});
    }
    return I18nText(locale, TrCtx(L"reset-when", L"in {duration} ({time})"),
                    {{L"duration", duration},
                     {L"time", delta < 24LL * 3600 * 1000 ? FormatLocalTime(local) : FormatLocalDayTime(local)}});
}

// The details flyout's last update; an unreadable time shows as "?".
static std::wstring FormatUpdated(const I18nLocale& locale, ULONGLONG unixMs, bool stale) {
    if (!unixMs) return I18nText(locale, TrCtx(L"flyout-footer", L"No data yet"));
    SYSTEMTIME local;
    if (!UnixMsToLocalSystemTime(unixMs, &local)) {
        return I18nText(locale, TrCtx(L"flyout-footer", L"Updated {time}"), {{L"time", L"?"}});
    }
    return I18nText(locale, stale ? TrCtx(L"flyout-footer", L"Updated {time} (stale)") :
                                    TrCtx(L"flyout-footer", L"Updated {time}"),
                    {{L"time", FormatLocalTime(local)}});
}

// The shell's Explorer, the only one that polls and records history. Windhawk also loads the mod
// into other Explorer processes (folder windows in a separate process, COM servers). Every
// primary taskbar window is checked: another app's window of that class may come first.
static bool IsTaskbarOwnerProcess() {
    bool owner = false;
    EnumWindows([](HWND hWnd, LPARAM lp) CALLBACK -> BOOL {
        DWORD processId = 0;
        wchar_t cls[32] = {};
        if (GetWindowThreadProcessId(hWnd, &processId) && processId == GetCurrentProcessId() &&
            GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) && _wcsicmp(cls, L"Shell_TrayWnd") == 0) {
            *reinterpret_cast<bool*>(lp) = true;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&owner));
    return owner;
}

// Browser launch threads still to be joined; Wh_ModUninit collects them. Leaf lock.
static std::mutex g_urlLaunchMutex;
static std::vector<HANDLE> g_urlLaunchThreads;

// Opens url in the default browser from a launch thread. ShellExecute runs a message loop on its
// calling thread; on a taskbar thread that loop would run marshaled teardown (including
// unload's) and XAML input inside the caller. Foreground permission is per process, and the shell
// creates the browser process on its own thread either way, so the browser still comes to front.
static void OpenUrl(PCWSTR url) {
    if (g_unloading || !url || !*url) return;
    auto request = std::make_unique<std::wstring>(url);
    std::lock_guard<std::mutex> lock(g_urlLaunchMutex);
    // Pairs with the unload join, which sets g_unloading before collecting the threads.
    if (g_unloading) return;
    std::erase_if(g_urlLaunchThreads, [](HANDLE thread) {
        if (WaitForSingleObject(thread, 0) != WAIT_OBJECT_0) return false;
        CloseHandle(thread);
        return true;
    });
    // Reserve first so recording the handle can't fail once the thread runs.
    g_urlLaunchThreads.reserve(g_urlLaunchThreads.size() + 1);
    HANDLE thread = CreateThread(nullptr, 0, [](LPVOID param) WINAPI -> DWORD {
        std::unique_ptr<std::wstring> url(static_cast<std::wstring*>(param));
        // Some shell execute handlers need an STA.
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (!g_unloading) {
            SHELLEXECUTEINFOW info{sizeof(info)};
            // No error dialogs, so unload's join can't wait on them. Security prompts still show.
            info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            info.lpVerb = L"open";
            info.lpFile = url->c_str();
            info.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&info)) Wh_Log(L"Could not open a link: %lu", GetLastError());
        }
        if (SUCCEEDED(com)) CoUninitialize();
        return 0;
    }, request.get(), 0, nullptr);
    if (!thread) {
        Wh_Log(L"Could not start a link launch thread: %lu", GetLastError());
        return;
    }
    request.release();
    g_urlLaunchThreads.push_back(thread);
}

// Brand names, the same in every language.
static PCWSTR ProviderDisplayName(const std::wstring& provider) {
    if (provider == L"anthropic") return L"Anthropic";  // i18n-en
    if (provider == L"openai") return L"OpenAI";  // i18n-en
    if (provider == L"copilot") return L"GitHub Copilot";  // i18n-en
    if (provider == L"opencode") return L"OpenCode Go";  // i18n-en
    return L"Google Antigravity";  // i18n-en
}

// OpenCode Go has no OAuth client: its sign-in takes an API key copied from the console, which
// also shows the usage.
static constexpr PCWSTR kOpenCodeConsoleUrl = L"https://opencode.ai/console";

// The provider's usage page: the dashboard actions and notifications open it. Antigravity has
// none.
static PCWSTR ProviderDashboardUrl(const std::wstring& provider) {
    if (provider == L"anthropic") return L"https://claude.ai/settings/usage";
    if (provider == L"copilot") return L"https://github.com/settings/copilot";
    if (provider == L"opencode") return kOpenCodeConsoleUrl;
    if (provider == L"openai") return L"https://chatgpt.com/codex/cloud/settings/analytics#usage";
    return nullptr;
}

// A GitHub Copilot allowance's unit, as running text names it (the details flyout's row has its
// own name).
static std::wstring CopilotUnitName(const I18nLocale& locale, CopilotUnit unit) {
    return I18nText(locale, unit == CopilotUnit::PremiumRequests ? TrCtx(L"copilot-unit", L"premium requests") :
                            unit == CopilotUnit::ChatRequests    ? TrCtx(L"copilot-unit", L"chat requests") :
                                                                   TrCtx(L"copilot-unit", L"AI credits"));
}
