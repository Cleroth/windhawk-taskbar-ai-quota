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
    kAccountClaudeFiveHour,
    kAccountClaudeWeekly,
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
