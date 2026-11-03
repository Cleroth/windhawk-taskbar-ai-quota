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
        if (a.tileIdentity < TileIdentity::LogoBadge || a.tileIdentity > TileIdentity::Text) {
            a.tileIdentity = TileIdentity::LogoBadge;
        }
        // Tile text is trimmed and capped at 4 UTF-16 units without splitting a surrogate pair.
        a.tileText.erase(0, a.tileText.find_first_not_of(L" \t\r\n"));
        if (a.tileText.size() > 4) a.tileText.resize(IS_HIGH_SURROGATE(a.tileText[3]) ? 3 : 4);
        a.tileText.erase(a.tileText.find_last_not_of(L" \t\r\n") + 1);
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
    s->historyRetentionDays = std::clamp(s->historyRetentionDays, 0, 3650);
    if (s->historyDefaultRange < HistoryUiRange::Day || s->historyDefaultRange > HistoryUiRange::All) {
        s->historyDefaultRange = HistoryUiRange::Week;
    }
    s->taskbarMonitorNumber = std::clamp(s->taskbarMonitorNumber > 0 ?
                                             s->taskbarMonitorNumber : 1, 1, 64);
    s->barLength = std::clamp(s->barLength > 0 ? s->barLength : 100, 10, 500);
    s->barThickness = std::clamp(s->barThickness > 0 ? s->barThickness : 8, 2, 50);
    s->ringSize = std::clamp(s->ringSize > 0 ? s->ringSize : 32, kRingSizeMin, kRingSizeMax);
    s->ringThickness = std::clamp(s->ringThickness > 0 ? s->ringThickness : 3,
                                  kRingThicknessMin, kRingThicknessMax);
    s->labelFontSize = std::clamp(s->labelFontSize > 0 ? s->labelFontSize : 11, 6, 24);
    s->percentFontSize = std::clamp(s->percentFontSize > 0 ? s->percentFontSize : 9, 6, 24);
    s->accountMargin = std::clamp(s->accountMargin, 0, 500);
    s->labelGap = std::clamp(s->labelGap, 0, 500);
    s->barGap = std::clamp(s->barGap, 0, 500);
    s->rightMargin = std::clamp(s->rightMargin, 0, 500);
    if (s->barLayout < BarLayout::Stacked || s->barLayout > BarLayout::LogoHeadline) {
        s->barLayout = BarLayout::Stacked;
    }
    if (s->barStyle < BarStyle::Track || s->barStyle > BarStyle::Rail) {
        s->barStyle = BarStyle::Track;
    }
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
            account.SetNamedValue(L"tileIdentity", JsonValue::CreateStringValue(
                a.tileIdentity == TileIdentity::Logo ? L"logo" :
                a.tileIdentity == TileIdentity::Text ? L"text" : L"logoBadge"));
            account.SetNamedValue(L"tileText", JsonValue::CreateStringValue(winrt::hstring(a.tileText)));
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
        setBool(L"showDetailsOnHover", s.showDetailsOnHover);
        setNumber(L"pollMinutes", s.pollMinutes);
        // Forever is its own key: builds without it read the longest day count instead.
        setNumber(L"historyRetentionDays", s.historyRetentionDays > 0 ? s.historyRetentionDays : 3650);
        setBool(L"historyKeepForever", s.historyRetentionDays <= 0);
        setString(L"historyDefaultRange", s.historyDefaultRange == HistoryUiRange::Day ? L"day" :
                                          s.historyDefaultRange == HistoryUiRange::Month ? L"month" :
                                          s.historyDefaultRange == HistoryUiRange::Quarter ? L"quarter" :
                                          s.historyDefaultRange == HistoryUiRange::All ? L"all" : L"week");
        setBool(L"skipHiddenPolling", s.skipHiddenPolling);
        setNumber(L"barLength", s.barLength);
        setNumber(L"barThickness", s.barThickness);
        setString(L"barStyle", s.barStyle == BarStyle::Rail ? L"rail" : L"track");
        setNumber(L"ringSize", s.ringSize);
        setNumber(L"ringThickness", s.ringThickness);
        setString(L"barLayout", s.barLayout == BarLayout::Vertical ? L"vertical" :
                                s.barLayout == BarLayout::SegmentedRing ? L"segmentedRing" :
                                s.barLayout == BarLayout::ConcentricRings ? L"concentricRings" :
                                s.barLayout == BarLayout::LogoHeadline ? L"logoHeadline" :
                                                                         L"stacked");
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
                std::wstring tileIdentity = GetStr(obj, L"tileIdentity");
                a.tileIdentity = tileIdentity == L"logo" ? TileIdentity::Logo :
                                 tileIdentity == L"text" ? TileIdentity::Text :
                                                           TileIdentity::LogoBadge;
                a.tileText = GetStr(obj, L"tileText");
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
        s.showDetailsOnHover = getBoolDefault(L"showDetailsOnHover", true);
        s.pollMinutes = (int)GetNum(root, L"pollMinutes", 10);
        // Settings saved before forever retention keep their day count.
        double retentionDays = GetNum(root, L"historyRetentionDays", 90);
        s.historyRetentionDays = getBoolDefault(L"historyKeepForever", false) ? 0 :
                                 std::isfinite(retentionDays) ?
                                     (int)std::clamp(retentionDays, 1.0, 3650.0) : 90;
        std::wstring historyDefaultRange = GetStr(root, L"historyDefaultRange");
        s.historyDefaultRange = historyDefaultRange == L"day" ? HistoryUiRange::Day :
                                historyDefaultRange == L"month" ? HistoryUiRange::Month :
                                historyDefaultRange == L"quarter" ? HistoryUiRange::Quarter :
                                historyDefaultRange == L"all" ? HistoryUiRange::All : HistoryUiRange::Week;
        s.skipHiddenPolling = getBoolDefault(L"skipHiddenPolling", true);
        s.barLength = (int)GetNum(root, L"barLength", 100);
        s.barThickness = (int)GetNum(root, L"barThickness", 8);
        s.barStyle = GetStr(root, L"barStyle") == L"rail" ? BarStyle::Rail : BarStyle::Track;
        // Clamp before the int conversion; 0 (non-positive or non-finite) restores the default
        // during normalization.
        double ringSize = GetNum(root, L"ringSize", 32);
        double ringThickness = GetNum(root, L"ringThickness", 3);
        s.ringSize = std::isfinite(ringSize) ?
                         (int)std::clamp(ringSize, 0.0, (double)kRingSizeMax) : 0;
        s.ringThickness = std::isfinite(ringThickness) ?
                              (int)std::clamp(ringThickness, 0.0, (double)kRingThicknessMax) : 0;
        std::wstring barLayout = GetStr(root, L"barLayout");
        s.barLayout = barLayout == L"vertical" ? BarLayout::Vertical :
                      barLayout == L"segmentedRing" ? BarLayout::SegmentedRing :
                      barLayout == L"concentricRings" ? BarLayout::ConcentricRings :
                      barLayout == L"logoHeadline" ? BarLayout::LogoHeadline :
                                                     BarLayout::Stacked;
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
    // Burn-up curves follow what may be charted: consent, retention, and each history ID's
    // presence, provider and recording. Callers then update the UI and wake the fetch thread
    // after releasing their locks (FinishSettingsApply), which rebuilds them locally.
    bool curvePolicyChanged = s.historyEnabled != g_settings.historyEnabled ||
                              s.historyRetentionDays != g_settings.historyRetentionDays;
    for (const auto& old : g_settings.accounts) {
        auto current = std::find_if(s.accounts.begin(), s.accounts.end(),
            [&](const AccountConfig& account) { return old.historyId && account.historyId == old.historyId; });
        if (old.historyId && (current == s.accounts.end() || current->provider != old.provider ||
                              current->recordHistory != old.recordHistory)) {
            curvePolicyChanged = true;
        }
    }
    if (curvePolicyChanged) {
        ++g_burnUpPolicyGeneration;
        ResetBurnUpCurvesLocked();
    }
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
    g_historyRetentionDays.store(s.historyRetentionDays);
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
