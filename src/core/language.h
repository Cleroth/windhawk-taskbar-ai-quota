// The process's current UI language, this Windows user's language preference and regional number
// format. Native: pure code never reads these; native code snapshots one I18nLocale per frame,
// notification or dialog (CurrentLocale) and passes it down (i18n/AGENTS.md).

// The current language: an immutable registry entry (i18n/languages.h) any thread reads without
// locks; text built from it is cached under its locale. Writers hold g_configEditMutex (mod init
// aside).
static std::atomic<const I18nLanguage*> g_language{&kI18nLanguages[0]};

// This Windows user's language preference, never in machine-shared settings_v1: a tag in Windhawk
// storage (core/user-storage.h), Automatic stored as "auto", which names no language.

// The stored tag, spelled as kI18nLanguages spells it; empty (Automatic) when missing, unreadable
// or naming no language. Without the Windhawk value, the HKCU one older versions wrote, rereading
// the Windhawk value once after finding that absent: the taskbar's process may have just moved it.
static std::wstring ReadLanguagePreference() {
    const std::wstring sid = CurrentUserSid();
    const std::wstring name = kLanguageValuePrefix + sid;
    wchar_t tag[64] = {};
    auto stored = [&] { return !sid.empty() && Wh_GetStringValue(name.c_str(), tag, std::size(tag)); };
    if (!stored()) {
        DWORD size = sizeof(tag);
        LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, kLegacyUserRegistryKey, kLegacyLanguageValue, RRF_RT_REG_SZ,
                                      nullptr, tag, &size);
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
            if (!stored()) return {};
        } else if (result != ERROR_SUCCESS) {
            return {};
        }
    }
    const I18nLanguage* language = I18nLanguageByTag(tag);
    return language ? std::wstring(language->tag) : std::wstring();
}

// Stores a tag, or Automatic for an empty one, under UserPreferencesLock; fails, writing nothing,
// when the lock can't be had.
static bool WriteLanguagePreference(const std::wstring& tag) {
    const std::wstring sid = CurrentUserSid();
    if (sid.empty()) return false;
    UserPreferencesLock lock(sid);
    return lock.held && Wh_SetStringValue((kLanguageValuePrefix + sid).c_str(), tag.empty() ? L"auto" : tag.c_str());
}

// The language a preference shows: its tag's, else (Automatic) the first finished language the
// user's preferred Windows display languages name, else English.
static const I18nLanguage& ResolveLanguage(const std::wstring& tag) {
    if (const I18nLanguage* language = I18nLanguageByTag(tag)) return *language;
    ULONG count = 0, size = 0;
    std::vector<wchar_t> names;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size) {
        names.resize(size);
        if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names.data(), &size)) names.clear();
    }
    std::vector<std::wstring_view> preferred;
    for (const wchar_t* name = names.data(); !names.empty() && *name; name += wcslen(name) + 1) {
        preferred.push_back(name);
    }
    return I18nMatchLanguage(preferred);
}

static void SetCurrentLanguage(const I18nLanguage& language) {
    g_language.store(&language, std::memory_order_release);
}

// This Windows user's regional number format (their Region settings), read anew each time, so a
// change shows from the next snapshot; a part Windows can't give stays en-US's.
static I18nNumberFormat ReadNumberFormat() {
    // Each part as long as Windows says (its count holds the terminating null; 0: none, empty).
    auto read = [](LCTYPE type, auto& buffer) {
        int count = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, buffer, int(std::size(buffer)));
        return std::wstring_view(buffer, count > 0 ? size_t(count - 1) : 0);
    };
    wchar_t decimal[8], group[8], grouping[16];
    return I18nNumberFormatFrom(read(LOCALE_SDECIMAL, decimal), read(LOCALE_STHOUSAND, group),
                                read(LOCALE_SGROUPING, grouping));
}

// The localization context of a frame, notification or dialog: the current language with this
// user's regional number format.
static I18nLocale CurrentLocale() {
    return I18nLocale(*g_language.load(std::memory_order_acquire), ReadNumberFormat());
}
