// Fetch-thread threshold notifications: a toast under the mod's own name and icon, or, when the
// toast fails, the balloon of a hidden tray icon, which Windows attributes to Explorer.

// Fetch-thread-owned: hidden message-only window that owns the mod's tray icon.
static HWND g_notifyWnd = nullptr;

static const UINT kNotifyIconId = 1;
static PCWSTR kNotifyClassName = L"AiQuotaNotify_" WH_MOD_ID;

// The toasts' app identity (AppUserModelID): its name and icon registered once per fetch thread
// under a volatile HKCU key, which Windows drops at sign-out, and removed while notifications are
// off and at unload. The mod ID keeps local and catalog copies apart. The icon file sits in this
// user's storage folder (core/user-storage.h); Windows caches it by path, so a changed app icon
// needs a new name.
static constexpr PCWSTR kToastAppId = L"Windhawk.TaskbarAIQuota." WH_MOD_ID;
static constexpr PCWSTR kToastAppIdParent = L"Software\\Classes\\AppUserModelId";
static constexpr PCWSTR kToastAppIdKey = L"Software\\Classes\\AppUserModelId\\Windhawk.TaskbarAIQuota." WH_MOD_ID;
static constexpr PCWSTR kToastIconFile = L"notification-icon.png";
// Fetch-thread-owned: whether this fetch thread registered the app identity, name and icon.
static bool g_toastAppIdRegistered = false;

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

// Fetch-thread only. Deletes the toasts' app identity; Windows keeps its own notification settings
// for it. Returns whether it is gone; the next toast registers it again.
static bool RemoveToastAppId() {
    LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, kToastAppIdKey);
    if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) {
        g_toastAppIdRegistered = false;
        return true;
    }
    Wh_Log(L"Could not remove the notifications' app identity: %ld", status);
    return false;
}

// A threshold notification's title and body in `locale`: the quota's "... usage at {percent}%"
// (the percentage rounded as %.0f), then the provider's name with `resets` (the reset's
// FormatReset; empty: none) after it. A GitHub Copilot quota goes by its allowance's unit,
// Antigravity's by their pools.
static std::pair<std::wstring, std::wstring> ThresholdNotificationText(const I18nLocale& locale,
                                                                        const std::wstring& provider,
                                                                        const AccountData& data, int slot,
                                                                        const std::wstring& resets) {
    const std::array<const WindowUsage*, kQuotaBarCount> usage = {&data.win5h, &data.winWeek, &data.fableWeek,
                                                                  &data.extraUsage};
    // Room for any double's digits: reported percentages have no upper bound.
    wchar_t number[400];
    if (swprintf(number, ARRAYSIZE(number), L"%.0f", usage[size_t(slot)]->pct) < 0) number[0] = L'\0';
    const std::wstring_view percent = number;
    // Its pool's name (Antigravity's Gemini and Argon, Anthropic's Fable): brands.
    auto weekly = [&](PCWSTR name) {
        return I18nText(locale, TrCtx(L"notification-title", L"{name} weekly usage at {percent}%"),
                        {{L"name", name}, {L"percent", percent}});
    };
    std::wstring title;
    if (provider == L"antigravity" && slot == kFiveHourBar) {
        PCWSTR pool = L"Gemini";  // i18n-en
        ULONGLONG durationMs = data.win5h.windowDurationMs;
        int hours = durationMs == 3ULL * 60 * 60 * 1000 ? 3 : durationMs == 60ULL * 60 * 1000 ? 1 : 5;
        title = durationMs ? I18nText(locale, TrCtx(L"notification-title", L"{name} {hours}h usage at {percent}%"),
                                      {{L"name", pool}, {L"hours", hours}, {L"percent", percent}}) :
                             I18nText(locale, TrCtx(L"notification-title", L"{name} current limit usage at {percent}%"),
                                      {{L"name", pool}, {L"percent", percent}});
    } else if (provider == L"antigravity" && (slot == kWeeklyBar || slot == kFableWeeklyBar)) {
        title = weekly(slot == kWeeklyBar ? L"Gemini" : L"Argon");  // i18n-en
    } else if (provider == L"copilot") {
        title = I18nText(locale,
                         data.copilotUnit == CopilotUnit::PremiumRequests ?
                             TrCtx(L"notification-title", L"premium requests usage at {percent}%") :
                         data.copilotUnit == CopilotUnit::ChatRequests ?
                             TrCtx(L"notification-title", L"chat requests usage at {percent}%") :
                             TrCtx(L"notification-title", L"AI credits usage at {percent}%"),
                         {{L"percent", percent}});
    } else if (slot == kFableWeeklyBar) {
        title = weekly(L"Fable");  // i18n-en
    } else {
        I18nKey key = slot == kFiveHourBar    ? TrCtx(L"notification-title", L"5h usage at {percent}%") :
                      slot == kWeeklyBar      ? TrCtx(L"notification-title", L"weekly usage at {percent}%") :
                      provider == L"openai"   ? TrCtx(L"notification-title", L"credits usage at {percent}%") :
                      provider == L"opencode" ? TrCtx(L"notification-title", L"monthly usage at {percent}%") :
                                                TrCtx(L"notification-title", L"monthly extra usage at {percent}%");
        title = I18nText(locale, key, {{L"percent", percent}});
    }
    std::wstring body = resets.empty() ? std::wstring(ProviderDisplayName(provider)) :
                                         I18nText(locale, TrCtx(L"notification-body", L"{provider} - resets {when}"),
                                                  {{L"provider", ProviderDisplayName(provider)}, {L"when", resets}});
    return {std::move(title), std::move(body)};
}

// A toast's XML: `title`, `body` and an optional `projection` line, each a text element, opening
// `url` (if any) through the shell. The one place their text is escaped.
static std::wstring ThresholdToastXml(const std::wstring& title, const std::wstring& body,
                                      const std::wstring& projection, PCWSTR url) {
    auto escaped = [](std::wstring_view text) {
        std::wstring xml;
        for (wchar_t c : text) {
            switch (c) {
                case L'&': xml += L"&amp;"; break;
                case L'<': xml += L"&lt;"; break;
                case L'>': xml += L"&gt;"; break;
                case L'"': xml += L"&quot;"; break;
                default: xml += c; break;
            }
        }
        return xml;
    };
    std::wstring xml = L"<toast";
    if (url) xml += L" activationType=\"protocol\" launch=\"" + escaped(url) + L"\"";  // i18n-en
    xml += L"><visual><binding template=\"ToastGeneric\"><text>" + escaped(title) + L"</text><text>" +  // i18n-en
           escaped(body) + L"</text>";
    if (!projection.empty()) xml += L"<text>" + escaped(projection) + L"</text>";
    xml += L"</binding></visual></toast>";
    return xml;
}

// Fetch-thread only. Shows `title`, `body` and an optional `projection` line as a toast tagged by
// the account's identity and the quota's taskbar slot, so a repeat replaces it; clicking it opens
// `url` (if any) through the shell, never calling into the mod. The balloon stands in only when
// the toast fails, never while Windows has the mod's notifications turned off.
static void FireThresholdNotification(const std::wstring& title, const std::wstring& body,
                                      const std::wstring& projection, PCWSTR url,
                                      uint64_t accountIdentity, int slot) {
    // Windows reports the mod's notifications turned off (by the user or policy) through the
    // notifier's setting or these errors (WPN_E_NOTIFICATION_DISABLED, _TYPE_DISABLED).
    auto disabled = [](const winrt::hresult_error& e) {
        return e.code() == HRESULT(0x803E0111) || e.code() == HRESULT(0x803E0114);
    };
    // Toast: unless Windows has them off, register the app identity with its icon (without one
    // Windows shows a generic icon) once per fetch thread, then show it.
    try {
        namespace notifications = winrt::Windows::UI::Notifications;
        notifications::ToastNotifier notifier =
            notifications::ToastNotificationManager::CreateToastNotifier(kToastAppId);
        try {
            if (notifier.Setting() != notifications::NotificationSetting::Enabled) {
                Wh_Log(L"Windows has the mod's notifications turned off");
                return;
            }
        } catch (const winrt::hresult_error& e) {
            if (disabled(e)) throw;
            // Windows may have no setting for an app identity until its first toast.
            if (e.code() != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                Wh_Log(L"Could not read the mod's notification setting: 0x%08X", (unsigned)e.code().value);
            }
        }

        if (!g_toastAppIdRegistered) {
            // The icon: drawn once, written to a staging file, then moved into place.
            std::wstring icon;
            const std::wstring folder = UserStorageFolder();
            if (!PrepareUserStorageFolder(folder)) {
                icon = folder + L"\\" + kToastIconFile;
                if (GetFileAttributesW(icon.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    DeleteStaleStagingFiles(icon);
                    std::vector<BYTE> png = AppIconPng();
                    std::wstring staging;
                    winrt::file_handle file;
                    if (!png.empty()) file = CreateUserStagingFile(icon, staging);
                    DWORD written = 0;
                    bool saved = file && WriteFile(file.get(), png.data(), DWORD(png.size()), &written, nullptr) &&
                                 written == png.size();
                    file.close();
                    if (!saved || !MoveFileExW(staging.c_str(), icon.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                        if (!staging.empty()) DeleteFileW(staging.c_str());
                        Wh_Log(L"Could not write the notification icon");
                        icon.clear();
                    }
                }
            }
            // REG_OPTION_VOLATILE is ignored for an existing key, so a lasting one an earlier
            // version left goes first. The parent stays lasting: other apps can't add lasting
            // keys under a volatile one.
            HKEY key = nullptr;
            LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kToastAppIdParent, 0, nullptr, 0, KEY_CREATE_SUB_KEY,
                                             nullptr, &key, nullptr);
            if (status == ERROR_SUCCESS) {
                RegCloseKey(key);
                status = RegDeleteTreeW(HKEY_CURRENT_USER, kToastAppIdKey);
                if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
            }
            if (status == ERROR_SUCCESS) {
                status = RegCreateKeyExW(HKEY_CURRENT_USER, kToastAppIdKey, 0, nullptr, REG_OPTION_VOLATILE,
                                         KEY_SET_VALUE, nullptr, &key, nullptr);
            }
            if (status == ERROR_SUCCESS) {
                auto setValue = [key](PCWSTR name, PCWSTR value) {
                    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                                          DWORD((wcslen(value) + 1) * sizeof(wchar_t)));
                };
                status = setValue(L"DisplayName", L"Taskbar AI Quota");  // i18n-en
                LSTATUS iconStatus = icon.empty() ? ERROR_FILE_NOT_FOUND : setValue(L"IconUri", icon.c_str());
                if (status == ERROR_SUCCESS && iconStatus != ERROR_SUCCESS) {
                    Wh_Log(L"Could not register the notifications' icon: %ld", iconStatus);
                }
                // A missing icon retries with the next toast.
                g_toastAppIdRegistered = status == ERROR_SUCCESS && iconStatus == ERROR_SUCCESS;
                RegCloseKey(key);
            }
            winrt::check_win32(status);
        }

        winrt::Windows::Data::Xml::Dom::XmlDocument document;
        document.LoadXml(ThresholdToastXml(title, body, projection, url));
        notifications::ToastNotification toast(document);
        wchar_t group[17];
        swprintf(group, ARRAYSIZE(group), L"%016llx", (unsigned long long)accountIdentity);
        toast.Group(group);
        toast.Tag(std::to_wstring(slot));
        notifier.Show(toast);
        return;
    } catch (const winrt::hresult_error& e) {
        if (disabled(e)) {
            Wh_Log(L"Windows has the mod's notifications turned off");
            return;
        }
        Wh_Log(L"Toast failed: 0x%08X", (unsigned)e.code().value);
    } catch (...) {
        Wh_Log(L"Toast failed");
    }

    // Balloon: lazily creates a hidden message-only window owning one tray icon.
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
    // Cut to the fields' sizes, never inside a surrogate pair.
    auto copy = [](auto& field, const std::wstring& text) {
        if (wcsncpy_s(field, text.c_str(), _TRUNCATE) == STRUNCATE) {
            size_t end = wcslen(field);
            if (end && IS_HIGH_SURROGATE(field[end - 1])) field[end - 1] = L'\0';
        }
    };
    copy(nid.szInfoTitle, title);
    copy(nid.szInfo, projection.empty() ? body : body + L"\n" + projection);
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        // Explorer can recreate the notification area without restarting this process.
        addNotifyIcon(g_notifyWnd);
        if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
            Wh_Log(L"Shell_NotifyIcon NIM_MODIFY failed");
        }
    }
}
