// This Windows user's part of the mod's Windhawk storage, which Windhawk shares between the PC's
// users and deletes with the mod: values named after the user's SID (the recording consent,
// history/journal.h, and the language preference, core/language.h) and a folder named after it
// (the usage history journal and the notifications' icon). Also the HKCU key older versions kept
// the preferences in: readers fall back to it until the taskbar's process moves them.

// The Windhawk value names of this user's preferences, each completed by the SID.
static constexpr PCWSTR kConsentValuePrefix = L"recordHistory_";
static constexpr PCWSTR kLanguageValuePrefix = L"language_";

// Where older versions kept them, as RecordHistory (DWORD, missing: on) and Language (a tag).
static constexpr PCWSTR kLegacyUserRegistryParent = L"Software\\Windhawk\\TaskbarAIQuota";
static constexpr PCWSTR kLegacyUserRegistryKey = L"Software\\Windhawk\\TaskbarAIQuota\\" WH_MOD_ID;
static constexpr PCWSTR kLegacyConsentValue = L"RecordHistory";  // i18n-en
static constexpr PCWSTR kLegacyLanguageValue = L"Language";      // i18n-en

// The current Windows user's SID (S-1-5-21-...); empty when it can't be read.
static std::wstring CurrentUserSid() {
    winrt::handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put())) return {};
    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<BYTE> user(size);
    PWSTR text = nullptr;
    if (!size || !GetTokenInformation(token.get(), TokenUser, user.data(), size, &size) ||
        !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &text)) {
        return {};
    }
    std::unique_ptr<wchar_t, decltype([](wchar_t* p) { LocalFree(p); })> owned(text);
    return owned.get();
}

// This user's folder in the mod's storage, <storage>\<SID>, whether or not it exists; empty when
// either part can't be read. Windhawk creates the storage folder itself.
static std::wstring UserStorageFolder() {
    std::wstring root(32768, L'\0');
    size_t length = Wh_GetModStoragePath(root.data(), root.size());
    std::wstring sid = CurrentUserSid();
    if (!length || sid.empty()) return {};
    root.resize(length);
    return root + L"\\" + sid;
}

// Creates this user's storage folder (UserStorageFolder) if missing. 0 once it is ready, else the
// error: ERROR_REPARSE_POINT_ENCOUNTERED when the storage folder or this one is a reparse point,
// refused because every user can write Windhawk's storage.
static DWORD PrepareUserStorageFolder(const std::wstring& folder) {
    if (folder.empty()) return ERROR_PATH_NOT_FOUND;
    auto plainFolder = [](const std::wstring& path) -> DWORD {
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return ERROR_REPARSE_POINT_ENCOUNTERED;
        return attributes & FILE_ATTRIBUTE_DIRECTORY ? ERROR_SUCCESS : ERROR_DIRECTORY;
    };
    if (DWORD error = plainFolder(folder.substr(0, folder.rfind(L'\\')))) return error;
    if (!CreateDirectoryW(folder.c_str(), nullptr)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) return error;
    }
    return plainFolder(folder);
}

// Opens `path` in this user's storage folder without following a reparse point and keeps the
// handle only for a plain file (no reparse point, one link), so a link planted in the shared
// storage can't redirect a write: a mitigation, not a guarantee against hostile folder contents.
// Adds the read-attributes access the check needs; a refused file fails with
// ERROR_REPARSE_POINT_ENCOUNTERED.
static winrt::file_handle OpenUserStorageFile(const std::wstring& path, DWORD access, DWORD share,
                                              DWORD disposition) {
    winrt::file_handle file{CreateFileW(path.c_str(), access | FILE_READ_ATTRIBUTES, share, nullptr, disposition,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    BY_HANDLE_FILE_INFORMATION info{};
    if (file && (!GetFileInformationByHandle(file.get(), &info) ||
                 (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || info.nNumberOfLinks != 1)) {
        file.close();
        SetLastError(ERROR_REPARSE_POINT_ENCOUNTERED);
    }
    return file;
}

// Creates a uniquely named staging file beside `target`, `<target>.<random>.staging`, to be moved
// over it once written: new (CREATE_NEW never opens an existing file) and opened as
// OpenUserStorageFile opens. Its path in `staging`.
static winrt::file_handle CreateUserStagingFile(const std::wstring& target, std::wstring& staging) {
    uint64_t random[2] = {};
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(random), sizeof(random),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        SetLastError(ERROR_GEN_FAILURE);
        return {};
    }
    wchar_t suffix[48];
    swprintf(suffix, std::size(suffix), L".%016llx%016llx.staging", (unsigned long long)random[0],
             (unsigned long long)random[1]);
    staging = target + suffix;
    return OpenUserStorageFile(staging, GENERIC_WRITE, 0, CREATE_NEW);
}

// Deletes the staging files a crash left beside `target` (CreateUserStagingFile's). Only its
// writer calls it, holding what serializes its writes.
static void DeleteStaleStagingFiles(const std::wstring& target) {
    const std::wstring folder = target.substr(0, target.rfind(L'\\') + 1);
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileExW((target + L".*.staging").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch,
                                   nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return;
    std::unique_ptr<void, decltype([](HANDLE h) { FindClose(h); })> search(find);
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((folder + found.cFileName).c_str());
    } while (FindNextFileW(find, &found));
}

// Serializes this user's preference writes and their migration across Explorer processes and
// sessions: a named mutex, waited for up to 2 s; an abandoned one counts as acquired, so holders
// re-read what they change. A leaf lock: acquire nothing while holding it.
struct UserPreferencesLock {
    HANDLE mutex = nullptr;
    bool held = false;

    explicit UserPreferencesLock(const std::wstring& sid) {
        mutex = CreateMutexW(nullptr, FALSE, (L"Global\\TaskbarAIQuota." WH_MOD_ID L"." + sid).c_str());
        if (!mutex) return;
        DWORD wait = WaitForSingleObject(mutex, 2000);
        held = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    }
    ~UserPreferencesLock() {
        if (held) ReleaseMutex(mutex);
        if (mutex) CloseHandle(mutex);
    }
    UserPreferencesLock(const UserPreferencesLock&) = delete;
    UserPreferencesLock& operator=(const UserPreferencesLock&) = delete;
};

// Once from the taskbar's fetch thread at start: moves this user's preferences from the HKCU key
// older versions kept them in into Windhawk storage, under UserPreferencesLock. A value already in
// Windhawk storage wins and the HKCU one goes; otherwise the HKCU one is copied, as readers take it
// (unreadable: off, Automatic), and deleted only once the copy succeeded. Never writes a default.
// Then each key goes once it holds no values and no subkeys (never Software\Windhawk).
static void MigrateUserPreferences() {
    const std::wstring sid = CurrentUserSid();
    if (sid.empty()) return;
    UserPreferencesLock lock(sid);
    if (!lock.held) {
        Wh_Log(L"Could not move the preferences: their lock is busy");
        return;
    }
    // Present: read, or there with another type or size.
    auto present = [](LSTATUS read) {
        return read == ERROR_SUCCESS || read == ERROR_UNSUPPORTED_TYPE || read == ERROR_MORE_DATA;
    };
    const std::wstring consent = kConsentValuePrefix + sid;
    const std::wstring language = kLanguageValuePrefix + sid;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kLegacyUserRegistryKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) ==
        ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD size = sizeof(value);
        LSTATUS read = RegGetValueW(key, nullptr, kLegacyConsentValue, RRF_RT_REG_DWORD, nullptr, &value, &size);
        if (present(read)) {
            bool moved = Wh_GetIntValue(consent.c_str(), -1) != -1 ||
                         Wh_SetIntValue(consent.c_str(), read == ERROR_SUCCESS && value == 1 ? 1 : 0);
            if (moved) RegDeleteValueW(key, kLegacyConsentValue);
            else Wh_Log(L"Could not move the recording choice");
        }
        wchar_t tag[64] = {};
        size = sizeof(tag);
        read = RegGetValueW(key, nullptr, kLegacyLanguageValue, RRF_RT_REG_SZ, nullptr, tag, &size);
        if (present(read)) {
            wchar_t stored[64];
            bool moved = Wh_GetStringValue(language.c_str(), stored, std::size(stored)) ||
                         Wh_SetStringValue(language.c_str(), read == ERROR_SUCCESS && tag[0] ? tag : L"auto");
            if (moved) RegDeleteValueW(key, kLegacyLanguageValue);
            else Wh_Log(L"Could not move the language preference");
        }
        RegCloseKey(key);
    }
    for (PCWSTR path : {kLegacyUserRegistryKey, kLegacyUserRegistryParent}) {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) continue;
        DWORD subkeys = 0, values = 0;
        bool empty = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr,
                                      nullptr, nullptr, nullptr) == ERROR_SUCCESS &&
                     !subkeys && !values;
        RegCloseKey(key);
        if (empty) RegDeleteKeyW(HKEY_CURRENT_USER, path);
    }
}
