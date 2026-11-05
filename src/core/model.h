// Account configuration, settings, and fetched quota data shared by every domain.

enum QuotaBarIndex {
    kFiveHourBar,
    kWeeklyBar,
    kFableWeeklyBar,  // Anthropic Fable weekly, or Antigravity's Gemini Argon weekly.
    kExtraUsageBar,
    kQuotaBarCount,
};

// What marks an account on the taskbar: beside Stacked and Vertical bars at the identity position
// (Settings::labelPosition), and on tiles.
enum class TileIdentity {
    LogoBadge,  // Provider logo, plus a number badge when several accounts, hidden ones included, share the provider.
    Logo,
    Text,  // "Label": the label on bars; tileText on tiles, or the label when tileText is empty.
};

struct AccountConfig {
    std::wstring provider;  // "anthropic", "openai", "antigravity", "copilot", or "opencode".
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

// Tile styles, in the gallery's order.
enum class BarLayout {
    Sparkline,      // Each quota's burn-up over its window side by side, the identity beside.
    Stacked,
    Vertical,
    SegmentedRing,
    ConcentricRings,
    Gauge,          // The most-used quota's 270-degree arc around its number, the identity beside.
    ClockPies,      // A pie per quota with an elapsed-time hand, the identity beside.
    RoundedSquare,  // Segmented-ring arcs along a rounded square around the identity.
    LogoHeadline,
    QuotaMatrix,    // Each quota's key and number in up to two rows and columns, the identity beside.
};
static constexpr BarLayout kLastBarLayout = BarLayout::QuotaMatrix;

// How Stacked and Vertical bars draw behind their fill.
enum class BarStyle {
    Track,  // A full-thickness track; the fill's end toward 100% is a straight cut.
    Rail,   // A thin centred rail under a full-thickness fill.
};

static constexpr int kRingSizeMin = 24;
static constexpr int kRingSizeMax = 40;
static constexpr int kRingThicknessMin = 1;
static constexpr int kRingThicknessMax = 6;
// Headline tiles: the number's text size and each quota bar's length and thickness, in DIPs.
static constexpr int kHeadlineFontSizeMin = 10;
static constexpr int kHeadlineFontSizeMax = 24;
static constexpr int kHeadlineBarLengthMin = 4;
static constexpr int kHeadlineBarLengthMax = 60;
static constexpr int kHeadlineBarThicknessMin = 2;
static constexpr int kHeadlineBarThicknessMax = 10;
// Sparkline tiles' chart width, in DIPs, and height, in percent of the tile's height.
static constexpr int kSparklineWidthMin = 24;
static constexpr int kSparklineWidthMax = 120;
static constexpr int kSparklineHeightMin = 50;
static constexpr int kSparklineHeightMax = 100;
// Their keys' and extra usage's text size, in DIPs.
static constexpr int kSparklineTextSizeMin = 6;
static constexpr int kSparklineTextSizeMax = 16;
// Their current reading's size, in percent of its standard size.
static constexpr int kSparklineReadingScaleMin = 50;
static constexpr int kSparklineReadingScaleMax = 200;

// What a sparkline's weekly chart shows in its top-left corner of the account's extra usage
// (Anthropic extra usage, OpenAI credits) while the provider reports it.
enum class SparklineExtra {
    Off,
    Amount,   // $ or credits.
    Percent,  // Of the limit; the amount without one.
};

// How much of a window longer than its span a sparkline's chart shows: the whole window, or the
// hours before and after now.
enum class SparklineSpan {
    Window,
    Hours12,
    Hours24,
    Hours36,
    Hours48,
};
// Logo size in percent of each style's standard size; the number badge's text size and offsets
// in DIPs.
static constexpr int kLogoScaleMin = 50;
static constexpr int kLogoScaleMax = 200;
static constexpr int kBadgeSizeMin = 6;
static constexpr int kBadgeSizeMax = 20;
static constexpr int kBadgeOffsetMax = 20;

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
    TimeTrack,  // A small bar under each bar (vertical bars: right of it) filling with the pace.
};

// Where the bars sit on the taskbar: a slot in the system tray, or (experimental) beside Start,
// where the taskbar's own layout makes room for them.
enum class TaskbarPlacement {
    BeforeIcons,       // Before the notification icons and their chevron
    BeforeNetwork,     // Before network, volume and battery
    BeforeClock,
    AfterClock,        // Before Show desktop
    AfterShowDesktop,  // The taskbar's far end
    LeftSide,          // The taskbar's leading edge; Widgets, Start and the apps move over
    RightOfStart,      // Between Start and the buttons after it
};
static constexpr TaskbarPlacement kLastTaskbarPlacement = TaskbarPlacement::RightOfStart;

static bool IsStartAreaPlacement(TaskbarPlacement placement) {
    return placement == TaskbarPlacement::LeftSide || placement == TaskbarPlacement::RightOfStart;
}

// Background of the details flyout and the settings and usage history windows. Every material
// but Solid needs Windows' transparency effects; without them, or in high contrast, it is solid.
// The windows have no Tinted wallpaper backdrop and use Mica for it.
enum class Material {
    Acrylic,          // Tinted blur of whatever is behind
    Mica,             // Windows' Mica over the blurred desktop wallpaper
    MicaAlt,          // Mica's tabbed-window variant
    TintedWallpaper,  // The blurred wallpaper under the theme's surface color at 80%
    Solid,
};

// Provider logos drawn in their brand colors instead of the taskbar's text color.
struct LogoBrands {
    bool claude = false;  // Anthropic's orange
    bool gemini = false;  // The Gemini sparkle's gradient (Antigravity)

    bool operator==(const LogoBrands&) const = default;
};

// The usage colors meters show, by band: green, yellow, orange and red by default; blue to
// vermilion for color vision deficiencies; or the user's own four.
enum class UsagePalette {
    Default,
    Colorblind,
    Custom,
};
// A palette's colors (0xBBGGRR) below the yellow threshold, then from the yellow, orange and red
// thresholds on.
static constexpr std::array<COLORREF, 4> kDefaultUsageColors = {
    RGB(0x43, 0xA0, 0x47), RGB(0xFD, 0xD8, 0x35), RGB(0xFB, 0x8C, 0x00), RGB(0xE5, 0x39, 0x35)};
static constexpr std::array<COLORREF, 4> kColorblindUsageColors = {
    RGB(0x00, 0x72, 0xB2), RGB(0x56, 0xB4, 0xE9), RGB(0xE6, 0x9F, 0x00), RGB(0xD5, 0x5E, 0x00)};

static constexpr COLORREF kDefaultPaceTickColor = RGB(222, 222, 222);
// The number badge's default color: white stands for the taskbar's text color, whatever the theme.
static constexpr COLORREF kBadgeTaskbarTextColor = RGB(0xFF, 0xFF, 0xFF);

// Usage history window ranges: 24 hours, 7, 30 and 90 days, and every retained day.
enum class HistoryUiRange : int { Day, Week, Month, Quarter, All };

// The usage color threshold at which a selected taskbar quota notifies, once per crossing.
enum class NotificationLevel {
    Off,
    Yellow,
    Orange,
    Red,
};

struct Settings {
    std::vector<AccountConfig> accounts;
    TaskbarMonitorMode taskbarMonitorMode = TaskbarMonitorMode::Primary;
    // Hovering a column opens its details flyout.
    bool showDetailsOnHover = true;
    // The default for new installs and the Layout reset; stored settings and legacy imports keep
    // theirs.
    BarLayout barLayout = BarLayout::Sparkline;
    BarMode barMode = BarMode::Used;
    int taskbarMonitorNumber = 1;
    int pollMinutes = 10;
    // This Windows user's history recording consent and language preference (a language tag;
    // empty: Automatic), stored per user beside settings_v1, never in it.
    bool historyEnabled = true;
    std::wstring language;
    // Days of usage history kept; 0: forever, with neither the age limit nor the size cap.
    int historyRetentionDays = 0;
    // The range the usage history window opens on.
    HistoryUiRange historyDefaultRange = HistoryUiRange::Week;
    bool skipHiddenPolling = true;
    int barLength = 100;
    int barThickness = 8;
    BarStyle barStyle = BarStyle::Track;
    // Ring diameter and stroke, in DIPs, also the gauge's, the rounded square's and (size only)
    // the pies' height; rendering shrinks the stroke and gaps further when the meters wouldn't fit
    // around the identity.
    int ringSize = 32;
    int ringThickness = 3;
    // Headline tiles: the most-used quota's number size (also the quota matrix's), and the size
    // of each quota's bar under it (the bar gap is the bars').
    int headlineFontSize = 14;
    int headlineBarLength = 16;
    int headlineBarThickness = 4;
    // Headline tiles show the time until the quota resets under the number instead of the bars.
    bool headlineCountdown = false;
    int sparklineWidth = 48;   // Sparkline tiles' chart width, in DIPs.
    int sparklineHeight = 80;  // Their height, in percent of the tile's height.
    int sparklineTextSize = 8;
    bool sparklineOutline = true;  // A faint outline around each chart.
    SparklineExtra sparklineExtra = SparklineExtra::Amount;
    // Sparklines' current position cues: the window's elapsed part lit (else the whole plot), a
    // line and a caret at the current time, and a ring around the current reading (else a plain
    // dot).
    bool sparklineElapsed = true;
    bool sparklineNowLine = true;
    bool sparklineNowCaret = false;
    bool sparklineRing = true;
    int sparklineReadingScale = 100;  // Percent; the ring and its dot, or the plain dot.
    // Sparklines' charts of long windows, and theirs while the pointer rests on the account (none:
    // as without it).
    SparklineSpan sparklineSpan = SparklineSpan::Window;
    std::optional<SparklineSpan> sparklineHoverSpan = SparklineSpan::Hours24;
    int logoScale = 100;  // Percent; rings keep the logo inside their hole.
    LogoBrands logoBrands;
    // The number badge's text size and color, and its move right and down from its default spot.
    int badgeSize = 13;
    COLORREF badgeColor = kBadgeTaskbarTextColor;
    int badgeOffsetX = 0;
    int badgeOffsetY = 0;
    int labelFontSize = 11;  // The Label identity on bars.
    int percentFontSize = 9;
    int accountMargin = 3;
    int labelGap = 3;  // "Identity gap": between the identity's box and the bars.
    int barGap = 2;
    TaskbarPlacement placement = TaskbarPlacement::BeforeIcons;
    int rightMargin = 4;  // "Gap after accounts": after the last account, wherever the bars sit.
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
    UsagePalette usagePalette = UsagePalette::Default;
    // The Custom palette's colors, kept while another palette is chosen.
    std::array<COLORREF, 4> customUsageColors = kDefaultUsageColors;
    bool showStaleWarning = true;
    // Reads the providers' public status pages and marks their accounts while a watched component
    // isn't operational (fetch/provider-status.h).
    bool showProviderOutages = true;
    NotificationLevel notificationLevel = NotificationLevel::Red;
    Material material = Material::Acrylic;

    bool operator==(const Settings&) const = default;
};

static constexpr ULONGLONG kFiveHourWindowMs = 5ULL * 60 * 60 * 1000;
static constexpr ULONGLONG kWeeklyWindowMs = 7ULL * 24 * 60 * 60 * 1000;

struct WindowUsage {
    double pct = -1;
    ULONGLONG resetUnixMs = 0;
    ULONGLONG windowDurationMs = 0;
    bool notYetStarted = false;  // Anthropic: no usage yet and an explicitly null reset.
};

// A quota the details flyout lists after its main rows, without a taskbar bar, history or alerts:
// Anthropic's per-model and per-surface limits, OpenAI's additional rate limits, Antigravity's
// other reported quotas.
struct NamedQuota {
    std::wstring name;  // As reported
    WindowUsage usage;
    // The window whose length the flyout adds to the name (ExtraQuotaName): the one it was first
    // listed with, kept when a later reading replaces `usage`.
    ULONGLONG nameWindowMs = 0;
    // A pool without a reported window, named "{name} current limit" (Antigravity's Argon).
    bool currentLimit = false;
};
static constexpr size_t kMaxExtraQuotas = 8;

// What GitHub Copilot's monthly allowance counts: AI credits, premium requests where billing
// isn't token based, or legacy Copilot Free's chat requests.
enum class CopilotUnit {
    AiCredits,
    PremiumRequests,
    ChatRequests,
};

// A poll's error as the mod authored it: a code, with each code's arguments in fields of their
// own, formatted in the reader's language at presentation (AccountErrorText); logs and their
// dedupe use its English text. Provider-supplied text rides in `detail` and is shown as is. The
// token endpoints' failures (auth/oauth.h) are AccountErrors too: a failed refresh names one as
// its cause.
enum class AccountErrorCode : uint8_t {
    None,
    Message,  // Provider-supplied text without a code of its own: `detail`, shown and logged as is
    // Sign-in and tokens
    NotSignedIn,
    SessionExpired,
    Unauthorized,
    TokenRefreshFailed,  // `cause`, when known: a token endpoint's failure
    TokenSaveFailed,
    // Usage requests
    Network,
    Http,  // `status`
    RateLimited,
    NoCopilotAccess,
    NoOpenCodeSubscription,
    UnexpectedResponse,  // `cause`, when known: the body's shape
    FetchThreadFailed,
    // Antigravity's local language server
    AntigravityNotRunning,
    AntigravityUnavailable,
    LanguageServerNetwork,
    LanguageServerHttp,  // `status`
    LanguageServerResponse,
    NoQuotaGroups,
    QuotaGroupsNotArray,
    NoUsableQuotaBuckets,
    QuotaSummaryInvalid,
    NoUserStatus,
    UserStatusInvalid,
    NoUsableQuota,
    // Token endpoint failures, besides Network, Http and Message
    NoAccessToken,
    InvalidTokenResponse,
    UnexpectedTokenResponse,
    InternalError,
    // Response body shapes (core/json.h)
    EmptyBody,
    NoBodyKeys,
    BodyKeys,  // `detail`: its first keys
    InvalidJsonBody,
};

struct AccountError {
    AccountErrorCode code = AccountErrorCode::None;
    std::wstring detail;
    int status = 0;  // HTTP status
    // What caused it, for a code whose text names a cause: a code of its own, which reads `detail`
    // and `status` as its own.
    AccountErrorCode cause = AccountErrorCode::None;

    explicit operator bool() const { return code != AccountErrorCode::None; }
    bool operator==(const AccountError&) const = default;
};

// A note the details flyout shows about what a provider reported: Anthropic's extra usage without
// a limit, OpenAI's credits without rate-limit windows, a GitHub Copilot plan without an
// allowance in its unit.
enum class AccountNote : uint8_t {
    None,
    ExtraUsageUnlimited,
    NoRateLimits,
    NoPremiumRequestAllowance,
    NoAiCreditAllowance,
};

struct AccountData {
    WindowUsage win5h;
    WindowUsage winWeek;
    WindowUsage fableWeek;  // Antigravity: Gemini Argon weekly.
    WindowUsage extraUsage;
    WindowUsage antigravityThirdParty5h;
    WindowUsage antigravityThirdPartyWeek;
    std::vector<NamedQuota> extraQuotas;  // At most kMaxExtraQuotas.
    std::wstring plan;
    AccountNote note = AccountNote::None;
    // Free rate-limit resets (Anthropic, OpenAI): -1 when not reported; the next one's expiry, 0
    // when unknown or none.
    int freeResets = -1;
    ULONGLONG freeResetsExpiryMs = 0;
    // Amounts behind the extra-usage slot, in dollars (Anthropic) or credits (OpenAI, GitHub
    // Copilot); -1 when unknown. Left = limit - used. Used may go negative if a credits balance
    // exceeds the configured max.
    double extraUsedAmount = -1;
    double extraLimitAmount = -1;
    // OpenAI prepaid credits; balance is -1 when the API reports none or hides it.
    bool hasCredits = false;
    bool creditsUnlimited = false;
    double creditsBalance = -1;
    // GitHub Copilot's monthly allowance in the extra-usage slot: unlimited (an organization's
    // pooled budget, without a percentage; extraUsedAmount holds its credits used when
    // reported), that pool used up, the additional usage GitHub reports past a finite allowance,
    // and the allowance's unit.
    bool copilotUnlimited = false;
    bool copilotExhausted = false;
    double copilotOverage = 0;
    CopilotUnit copilotUnit = CopilotUnit::AiCredits;
    // OpenCode Go's windows (5-hour, weekly, monthly in the extra-usage slot) that reached their
    // limit, by QuotaBarIndex.
    std::array<bool, kQuotaBarCount> opencodeLimited{};
    AccountError error;
    ULONGLONG lastSuccessMs = 0;
    ULONGLONG retryDeadlineMs = 0;
    bool stale = true;
    bool needsLogin = false;  // Sign-in required; left-click signs in instead of refreshing.
};

// A provider's status page state, by severity: its worst watched component that isn't operational.
enum class OutageLevel : uint8_t {
    None,
    Maintenance,
    Degraded,
    Partial,
    Major,
};

// A status page's reason for an outage that the mod names in the reader's language (OpenCode's
// reason codes); None: the page's own title, if any, names it.
enum class OutageReason : uint8_t {
    None,
    Latency,
    Errors,
    Upstream,
    Availability,
};

// What a provider's accounts show of its status page (fetch/status-feeds.h): the worst watched
// component's name and level, how many other watched components aren't operational, and the
// incident or reason behind it with its start (Unix ms, 0 when unknown) when the page says: the
// page's title as written, or a reason the mod names, and the models it reports affected (their
// IDs, as listed).
struct ProviderOutage {
    OutageLevel level = OutageLevel::None;
    std::wstring component;
    int others = 0;
    std::wstring title;
    ULONGLONG sinceMs = 0;
    OutageReason reason = OutageReason::None;
    std::wstring models;

    bool operator==(const ProviderOutage&) const = default;
};

// Adds a reported quota to the account's extra quotas (`currentLimit`: NamedQuota's). One whose
// name reads as a listed one's in English, its window's length included ("Sonnet weekly", "Spark
// 5-hour"), replaces that one's reading when more used; past kMaxExtraQuotas quotas are dropped.
static void AddExtraQuota(AccountData* d, std::wstring name, const WindowUsage& usage, bool currentLimit = false) {
    if (name.empty() || !std::isfinite(usage.pct) || usage.pct < 0) return;
    name.resize(std::min<size_t>(name.size(), 64));
    // ExtraQuotaName's English, which the model can't format: the merge key.
    auto english = [](const std::wstring& name, ULONGLONG durationMs, bool currentLimit) {
        constexpr ULONGLONG kHourMs = 60ULL * 60 * 1000, kDayMs = 24 * kHourMs;
        if (currentLimit) return name + L" current limit";  // i18n-en
        if (durationMs == kWeeklyWindowMs) return name + L" weekly";  // i18n-en
        if (durationMs && durationMs % kDayMs == 0) {
            return name + L" " + std::to_wstring(durationMs / kDayMs) + L"-day";  // i18n-en
        }
        if (durationMs && durationMs % kHourMs == 0) {
            return name + L" " + std::to_wstring(durationMs / kHourMs) + L"-hour";  // i18n-en
        }
        return name;
    };
    std::wstring key = english(name, usage.windowDurationMs, currentLimit);
    for (NamedQuota& listed : d->extraQuotas) {
        if (english(listed.name, listed.nameWindowMs, listed.currentLimit) != key) continue;
        if (usage.pct > listed.usage.pct) listed.usage = usage;
        return;
    }
    if (d->extraQuotas.size() < kMaxExtraQuotas) {
        d->extraQuotas.push_back({std::move(name), usage, usage.windowDurationMs, currentLimit});
    }
}

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
