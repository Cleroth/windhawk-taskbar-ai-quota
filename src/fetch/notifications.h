// Fetch-thread threshold notifications: a toast under the mod's own name and icon, or, when the
// toast fails, the balloon of a hidden tray icon, which Windows attributes to Explorer.

// Fetch-thread-owned: hidden message-only window that owns the mod's tray icon.
static HWND g_notifyWnd = nullptr;

static const UINT kNotifyIconId = 1;
static PCWSTR kNotifyClassName = L"AiQuotaNotify_" WH_MOD_ID;

// The toasts' app identity (AppUserModelID), its name and icon registered under its HKCU key while
// notifications are on. The mod ID keeps local and catalog copies apart. The icon file sits in the
// per-user folder history uses; Windows caches it by path, so a changed app icon needs a new name.
static constexpr PCWSTR kToastAppId = L"Windhawk.TaskbarAIQuota." WH_MOD_ID;
static constexpr PCWSTR kToastAppIdKey = L"Software\\Classes\\AppUserModelId\\Windhawk.TaskbarAIQuota." WH_MOD_ID;
static constexpr PCWSTR kToastIconFile = L"notification-icon.png";

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
// for it. Returns whether it is gone.
static bool RemoveToastAppId() {
    LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, kToastAppIdKey);
    if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) return true;
    Wh_Log(L"Could not remove the notifications' app identity: %ld", status);
    return false;
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
    // Windows shows a generic icon), then show it.
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

        std::wstring icon;
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
            std::wstring folder = std::wstring(localAppData) + L"\\TaskbarAIQuota";
            CreateDirectoryW(folder.c_str(), nullptr);
            folder += L"\\" WH_MOD_ID;
            CreateDirectoryW(folder.c_str(), nullptr);
            icon = folder + L"\\" + kToastIconFile;
            if (GetFileAttributesW(icon.c_str()) == INVALID_FILE_ATTRIBUTES && !SaveAppIconPng(icon)) icon.clear();
        }
        CoTaskMemFree(localAppData);

        HKEY key = nullptr;
        LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kToastAppIdKey, 0, nullptr, 0, KEY_SET_VALUE,
                                         nullptr, &key, nullptr);
        if (status == ERROR_SUCCESS) {
            auto setValue = [key](PCWSTR name, const std::wstring& value) {
                return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                      DWORD((value.size() + 1) * sizeof(wchar_t)));
            };
            status = setValue(L"DisplayName", L"Taskbar AI Quota");
            if (status == ERROR_SUCCESS && !icon.empty()) {
                if (LSTATUS iconStatus = setValue(L"IconUri", icon); iconStatus != ERROR_SUCCESS) {
                    Wh_Log(L"Could not register the notifications' icon: %ld", iconStatus);
                }
            }
            RegCloseKey(key);
        }
        winrt::check_win32(status);

        auto escaped = [](const std::wstring& text) {
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
        if (url) xml += L" activationType=\"protocol\" launch=\"" + escaped(url) + L"\"";
        xml += L"><visual><binding template=\"ToastGeneric\"><text>" + escaped(title) + L"</text><text>" +
               escaped(body) + L"</text>";
        if (!projection.empty()) xml += L"<text>" + escaped(projection) + L"</text>";
        xml += L"</binding></visual></toast>";
        winrt::Windows::Data::Xml::Dom::XmlDocument document;
        document.LoadXml(xml);
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
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, (projection.empty() ? body : body + L"\n" + projection).c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        // Explorer can recreate the notification area without restarting this process.
        addNotifyIcon(g_notifyWnd);
        if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
            Wh_Log(L"Shell_NotifyIcon NIM_MODIFY failed");
        }
    }
}
