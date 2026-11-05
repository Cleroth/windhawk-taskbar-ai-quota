// The process's current UI language and this Windows user's language preference. Native: pure
// code never reads these; native code snapshots one I18nLocale per frame, notification or dialog
// and passes it down (i18n/AGENTS.md).

// The current language: an immutable registry entry (i18n/languages.h) any thread reads without
// locks; text built from it is cached under its locale. Writers hold g_configEditMutex (mod init
// aside).
static std::atomic<const I18nLanguage*> g_language{&kI18nLanguages[0]};

// This Windows user's settings, never in machine-shared settings_v1: the recording consent
// (history/journal.h) and the language preference, a tag (missing or empty: Automatic).
static constexpr PCWSTR kUserRegistryKey = L"Software\\Windhawk\\TaskbarAIQuota\\" WH_MOD_ID;
static constexpr PCWSTR kLanguageRegistryValue = L"Language";  // i18n-en

// The stored tag, spelled as kI18nLanguages spells it; empty (Automatic) when missing, empty,
// unreadable or naming no language.
static std::wstring ReadLanguagePreference() {
    wchar_t tag[64] = {};
    DWORD size = sizeof(tag);
    if (RegGetValueW(HKEY_CURRENT_USER, kUserRegistryKey, kLanguageRegistryValue, RRF_RT_REG_SZ, nullptr, tag,
                     &size) != ERROR_SUCCESS) {
        return {};
    }
    const I18nLanguage* language = I18nLanguageByTag(tag);
    return language ? std::wstring(language->tag) : std::wstring();
}

// Stores a tag, or an empty one for Automatic.
static bool WriteLanguagePreference(const std::wstring& tag) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUserRegistryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS result = RegSetValueExW(key, kLanguageRegistryValue, 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(tag.c_str()), DWORD((tag.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
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
