// Account configuration, settings, and fetched quota data shared by every domain.

enum QuotaBarIndex {
    kFiveHourBar,
    kWeeklyBar,
    kFableWeeklyBar,
    kExtraUsageBar,
    kQuotaBarCount,
};

// What marks an account on the taskbar: beside Stacked and Vertical bars at the identity position
// (Settings::labelPosition), and on ring and headline tiles.
enum class TileIdentity {
    LogoBadge,  // Provider logo, plus a number badge when several accounts share the provider.
    Logo,
    Text,  // "Label": the label on bars; tileText on tiles, or the label when tileText is empty.
};

struct AccountConfig {
    std::wstring provider;  // "anthropic", "openai", or "antigravity".
    std::wstring label;
    TileIdentity tileIdentity = TileIdentity::LogoBadge;
    std::wstring tileText;  // Optional short text for TileIdentity::Text on tiles.
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

enum class BarLayout {
    Stacked,
    Vertical,
    SegmentedRing,
    ConcentricRings,
    LogoHeadline,
};

// How Stacked and Vertical bars draw behind their fill.
enum class BarStyle {
    Track,  // A full-thickness track; the fill's end toward 100% is a straight cut.
    Rail,   // A thin centred rail under a full-thickness fill.
};

static constexpr int kRingSizeMin = 24;
static constexpr int kRingSizeMax = 40;
static constexpr int kRingThicknessMin = 1;
static constexpr int kRingThicknessMax = 6;

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

// Where Stacked and Vertical bars place each account's identity ("Identity position").
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

// Usage history window ranges: 24 hours, 7, 30 and 90 days, and every retained day.
enum class HistoryUiRange : int { Day, Week, Month, Quarter, All };

struct Settings {
    std::vector<AccountConfig> accounts;
    TaskbarMonitorMode taskbarMonitorMode = TaskbarMonitorMode::Primary;
    // Hovering a column opens its details flyout.
    bool showDetailsOnHover = true;
    BarLayout barLayout = BarLayout::Stacked;
    BarMode barMode = BarMode::Used;
    int taskbarMonitorNumber = 1;
    int pollMinutes = 10;
    bool historyEnabled = true;
    // Days of usage history kept; 0: forever, with neither the age limit nor the size cap.
    int historyRetentionDays = 0;
    // The range the usage history window opens on.
    HistoryUiRange historyDefaultRange = HistoryUiRange::Week;
    bool skipHiddenPolling = true;
    int barLength = 100;
    int barThickness = 8;
    BarStyle barStyle = BarStyle::Track;
    // Segmented and concentric ring diameter and stroke, in DIPs; rendering shrinks the stroke
    // and gaps further when the meters wouldn't fit around the identity.
    int ringSize = 32;
    int ringThickness = 3;
    int labelFontSize = 11;  // The Label identity on bars.
    int percentFontSize = 9;
    int accountMargin = 3;
    int labelGap = 3;  // "Identity gap": between the identity's box and the bars.
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
