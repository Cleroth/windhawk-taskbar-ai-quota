// Edit controller of the settings window: drafts, the render projection, the commit queue and
// the navigation and close flushes. Pure: it talks to a SettingsStore (production:
// settings/store.h; tests: an in-memory store with failure injection) and reads no globals.
//
// Drafts are keyed by account key (0: global settings) and field. Every change bumps a revision;
// a commit request carries the revision it was made for and is skipped once the draft is gone
// or newer. The drafts, not the queue, are the source of truth.
//
// An account's key is its history ID, which survives renames, reorders and visibility; one
// without (ID generation failed) gets a session key by identity, moved along by this window's
// renames. Account actions (sign in, sign out, dashboard, visibility, reorder, remove) commit
// that account's drafts first: an invalid label blocks them, so nothing runs under an identity
// about to change.

enum class SettingsField : uint8_t {
    None,
    // Behavior
    ShowOn,  // Monitor mode and number: 0 primary, -1 all, N display N
    ShowDetailsOnHover,
    PollMinutes,
    SkipHiddenPolling,
    Notifications,  // A NotificationLevel
    // History
    RecordHistory,  // This Windows user's recording consent
    KeepHistory,    // The retention: days, or 0 for forever
    RetentionDays,  // A day count's custom days; changes nothing while kept forever
    HistoryRange,   // The range the usage history window opens on: a HistoryUiRange
    // Layout
    BarLayout,        // Tile style: a BarLayout
    BarStyle,         // A BarStyle
    BarLength,
    BarThickness,
    BarGap,
    RingSize,
    RingThickness,
    HeadlineFontSize,
    HeadlineBarLength,
    HeadlineBarThickness,
    HeadlineCountdown,
    SparklineWidth,
    SparklineHeight,  // Percent of the tile's height
    SparklineTextSize,
    SparklineOutline,
    SparklineExtra,   // A SparklineExtra
    SparklineElapsed,
    SparklineNowLine,
    SparklineNowCaret,
    SparklineRing,
    SparklineReadingScale,  // Percent
    SparklineSpan,    // A SparklineSpan
    SparklineHoverSpan,  // A SparklineSpan, or -1: none
    LogoScale,        // Percent
    ClaudeLogoBrand,  // 1: Anthropic's orange
    GeminiLogoBrand,  // 1: the Gemini sparkle's gradient
    BadgeSize,
    BadgeColor,       // 0xBBGGRR
    BadgeOffsetX,
    BadgeOffsetY,
    LabelPosition,    // A LabelPosition
    LabelFontSize,
    LabelGap,
    ShowBarLabels,    // Quota keys
    PercentFontSize,  // Bar text size
    ShowPaceTicks,
    PaceTickStyle,    // A PaceTickStyle
    PaceTickColor,    // 0xBBGGRR
    Placement,        // Position: a TaskbarPlacement
    AccountMargin,
    RightMargin,      // Gap after accounts
    // Display
    Material,               // A Material
    BarMode,                // A BarMode
    PercentTextVisibility,  // A PercentTextVisibility
    PercentTextAlignment,   // A PercentTextAlignment
    ShowExtraBarAmounts,
    UsagePalette,           // A UsagePalette
    // The Custom palette's colors by band, consecutive (0xBBGGRR)
    UsageColorBelowYellow,
    UsageColorYellow,
    UsageColorOrange,
    UsageColorRed,
    YellowThreshold,
    OrangeThreshold,
    RedThreshold,
    ShowStaleWarning,
    ShowOpenAiExtraLimits,
    // Accounts: fields of the account a draft's key names
    AccountLabel,         // Text, trimmed; unique per provider
    AccountTileIdentity,  // A TileIdentity
    AccountTileText,      // Text, up to 4 characters
    AccountBars,          // The quotas shown: bit i = QuotaBarIndex i
    AccountCreditsMax,    // OpenAI credits that count as 100%; 0 hides the credits bar
    AccountRecordHistory,
    // Account actions: each commits the account's drafts first
    AccountHidden,     // Desired: 1 hidden
    AccountMove,       // To this index
    AccountRemove,     // 1: also delete its recorded usage history (the stored sign-in always goes)
    AccountSignIn,
    AccountSignOut,
    AccountDashboard,
    // Operation groups: page resets
    ResetBehavior,
    ResetHistory,
    ResetLayout,
    ResetDisplay,
    // Window commands, never drafts or commits
    OpenHistory,
    OpenHistoryFolder,
    PaceTickColorCustom,  // The color dialog
    BadgeColorCustom,     // The color dialog
    UsageColorCustom,     // The color dialog for a band: 0 below yellow to 3 red
    CopyVersionInfo,      // The mod's and Windows' versions to the clipboard
    ShowOnTaskbar,        // Visual test mode
    PreviewScroll,        // The preview strip's sideways scroll
    AccountAdd,           // The Add account menu: kSettingsProviders' index
    AccountExpand,        // Desired: 1 expanded
};

static constexpr int kSettingsLabelLimit = 64;
static constexpr int kSettingsTileTextLimit = 4;
static constexpr int kSettingsCreditsLimit = 999999999;
static constexpr PCWSTR kSettingsProviders[] = {L"anthropic", L"openai", L"antigravity", L"copilot",
                                                 L"opencode"};

struct SettingsNumberRange {
    int minimum = 0, maximum = 0;
};

static std::optional<SettingsNumberRange> SettingsFieldRange(SettingsField field) {
    switch (field) {
        case SettingsField::PollMinutes: return SettingsNumberRange{2, 24 * 60};
        case SettingsField::RetentionDays: return SettingsNumberRange{1, 3650};
        case SettingsField::BarLength: return SettingsNumberRange{10, 500};
        case SettingsField::BarThickness: return SettingsNumberRange{2, 50};
        case SettingsField::RingSize: return SettingsNumberRange{kRingSizeMin, kRingSizeMax};
        case SettingsField::RingThickness: return SettingsNumberRange{kRingThicknessMin, kRingThicknessMax};
        case SettingsField::HeadlineFontSize: return SettingsNumberRange{kHeadlineFontSizeMin, kHeadlineFontSizeMax};
        case SettingsField::HeadlineBarLength: return SettingsNumberRange{kHeadlineBarLengthMin, kHeadlineBarLengthMax};
        case SettingsField::HeadlineBarThickness:
            return SettingsNumberRange{kHeadlineBarThicknessMin, kHeadlineBarThicknessMax};
        case SettingsField::SparklineWidth: return SettingsNumberRange{kSparklineWidthMin, kSparklineWidthMax};
        case SettingsField::SparklineHeight: return SettingsNumberRange{kSparklineHeightMin, kSparklineHeightMax};
        case SettingsField::SparklineTextSize:
            return SettingsNumberRange{kSparklineTextSizeMin, kSparklineTextSizeMax};
        case SettingsField::SparklineReadingScale:
            return SettingsNumberRange{kSparklineReadingScaleMin, kSparklineReadingScaleMax};
        case SettingsField::LogoScale: return SettingsNumberRange{kLogoScaleMin, kLogoScaleMax};
        case SettingsField::BadgeSize: return SettingsNumberRange{kBadgeSizeMin, kBadgeSizeMax};
        case SettingsField::BadgeOffsetX:
        case SettingsField::BadgeOffsetY: return SettingsNumberRange{-kBadgeOffsetMax, kBadgeOffsetMax};
        case SettingsField::LabelFontSize:
        case SettingsField::PercentFontSize: return SettingsNumberRange{6, 24};
        case SettingsField::BarGap:
        case SettingsField::LabelGap:
        case SettingsField::AccountMargin:
        case SettingsField::RightMargin: return SettingsNumberRange{0, 500};
        case SettingsField::YellowThreshold:
        case SettingsField::OrangeThreshold:
        case SettingsField::RedThreshold: return SettingsNumberRange{0, 100};
        case SettingsField::AccountCreditsMax: return SettingsNumberRange{0, kSettingsCreditsLimit};
        default: return std::nullopt;
    }
}

// The fields a page reset returns to their defaults.
static std::vector<SettingsField> SettingsGroupFields(SettingsField group) {
    switch (group) {
        case SettingsField::ResetBehavior:
            return {SettingsField::ShowOn, SettingsField::ShowDetailsOnHover, SettingsField::PollMinutes,
                    SettingsField::SkipHiddenPolling, SettingsField::Notifications};
        case SettingsField::ResetHistory:
            return {SettingsField::RecordHistory, SettingsField::KeepHistory, SettingsField::RetentionDays,
                    SettingsField::HistoryRange};
        case SettingsField::ResetLayout:
            return {SettingsField::BarLayout,       SettingsField::BarStyle,
                    SettingsField::BarLength,       SettingsField::BarThickness,
                    SettingsField::BarGap,          SettingsField::RingSize,
                    SettingsField::RingThickness,   SettingsField::HeadlineFontSize,
                    SettingsField::HeadlineBarLength, SettingsField::HeadlineBarThickness,
                    SettingsField::HeadlineCountdown, SettingsField::SparklineWidth,
                    SettingsField::SparklineHeight, SettingsField::SparklineTextSize,
                    SettingsField::SparklineOutline,
                    SettingsField::SparklineExtra,  SettingsField::SparklineElapsed,
                    SettingsField::SparklineNowLine, SettingsField::SparklineNowCaret,
                    SettingsField::SparklineRing,   SettingsField::SparklineReadingScale,
                    SettingsField::SparklineSpan,   SettingsField::SparklineHoverSpan,
                    SettingsField::LogoScale,
                    SettingsField::ClaudeLogoBrand, SettingsField::GeminiLogoBrand,
                    SettingsField::BadgeSize,       SettingsField::BadgeColor,
                    SettingsField::BadgeOffsetX,    SettingsField::BadgeOffsetY,
                    SettingsField::LabelPosition,
                    SettingsField::LabelFontSize,   SettingsField::LabelGap,
                    SettingsField::ShowBarLabels,   SettingsField::PercentFontSize,
                    SettingsField::ShowPaceTicks,   SettingsField::PaceTickStyle,
                    SettingsField::PaceTickColor,   SettingsField::Placement,
                    SettingsField::AccountMargin,   SettingsField::RightMargin};
        case SettingsField::ResetDisplay:
            return {SettingsField::Material,            SettingsField::BarMode,
                    SettingsField::PercentTextVisibility, SettingsField::PercentTextAlignment,
                    SettingsField::ShowExtraBarAmounts, SettingsField::UsagePalette,
                    SettingsField::UsageColorBelowYellow, SettingsField::UsageColorYellow,
                    SettingsField::UsageColorOrange,    SettingsField::UsageColorRed,
                    SettingsField::YellowThreshold,     SettingsField::OrangeThreshold,
                    SettingsField::RedThreshold,        SettingsField::ShowStaleWarning,
                    SettingsField::ShowOpenAiExtraLimits};
        default: return {};
    }
}

static int SettingsFieldValue(const Settings& s, SettingsField field) {
    switch (field) {
        case SettingsField::ShowOn:
            return s.taskbarMonitorMode == TaskbarMonitorMode::Primary ? 0 :
                   s.taskbarMonitorMode == TaskbarMonitorMode::All     ? -1 :
                                                                         s.taskbarMonitorNumber;
        case SettingsField::ShowDetailsOnHover: return s.showDetailsOnHover;
        case SettingsField::PollMinutes: return s.pollMinutes;
        case SettingsField::SkipHiddenPolling: return s.skipHiddenPolling;
        case SettingsField::Notifications: return int(s.notificationLevel);
        case SettingsField::RecordHistory: return s.historyEnabled;
        case SettingsField::KeepHistory: return s.historyRetentionDays;
        case SettingsField::RetentionDays: return s.historyRetentionDays > 0 ? s.historyRetentionDays : 3650;
        case SettingsField::HistoryRange: return int(s.historyDefaultRange);
        case SettingsField::BarLayout: return int(s.barLayout);
        case SettingsField::BarStyle: return int(s.barStyle);
        case SettingsField::BarLength: return s.barLength;
        case SettingsField::BarThickness: return s.barThickness;
        case SettingsField::BarGap: return s.barGap;
        case SettingsField::RingSize: return s.ringSize;
        case SettingsField::RingThickness: return s.ringThickness;
        case SettingsField::HeadlineFontSize: return s.headlineFontSize;
        case SettingsField::HeadlineBarLength: return s.headlineBarLength;
        case SettingsField::HeadlineBarThickness: return s.headlineBarThickness;
        case SettingsField::HeadlineCountdown: return s.headlineCountdown;
        case SettingsField::SparklineWidth: return s.sparklineWidth;
        case SettingsField::SparklineHeight: return s.sparklineHeight;
        case SettingsField::SparklineTextSize: return s.sparklineTextSize;
        case SettingsField::SparklineOutline: return s.sparklineOutline;
        case SettingsField::SparklineExtra: return int(s.sparklineExtra);
        case SettingsField::SparklineElapsed: return s.sparklineElapsed;
        case SettingsField::SparklineNowLine: return s.sparklineNowLine;
        case SettingsField::SparklineNowCaret: return s.sparklineNowCaret;
        case SettingsField::SparklineRing: return s.sparklineRing;
        case SettingsField::SparklineReadingScale: return s.sparklineReadingScale;
        case SettingsField::SparklineSpan: return int(s.sparklineSpan);
        case SettingsField::SparklineHoverSpan: return s.sparklineHoverSpan ? int(*s.sparklineHoverSpan) : -1;
        case SettingsField::LogoScale: return s.logoScale;
        case SettingsField::ClaudeLogoBrand: return s.logoBrands.claude;
        case SettingsField::GeminiLogoBrand: return s.logoBrands.gemini;
        case SettingsField::BadgeSize: return s.badgeSize;
        case SettingsField::BadgeColor: return int(s.badgeColor & 0x00FFFFFF);
        case SettingsField::BadgeOffsetX: return s.badgeOffsetX;
        case SettingsField::BadgeOffsetY: return s.badgeOffsetY;
        case SettingsField::LabelPosition: return int(s.labelPosition);
        case SettingsField::LabelFontSize: return s.labelFontSize;
        case SettingsField::LabelGap: return s.labelGap;
        case SettingsField::ShowBarLabels: return s.showBarLabels;
        case SettingsField::PercentFontSize: return s.percentFontSize;
        case SettingsField::ShowPaceTicks: return s.showPaceTicks;
        case SettingsField::PaceTickStyle: return int(s.paceTickStyle);
        case SettingsField::PaceTickColor: return int(s.paceTickColor & 0x00FFFFFF);
        case SettingsField::Placement: return int(s.placement);
        case SettingsField::AccountMargin: return s.accountMargin;
        case SettingsField::RightMargin: return s.rightMargin;
        case SettingsField::Material: return int(s.material);
        case SettingsField::BarMode: return int(s.barMode);
        case SettingsField::PercentTextVisibility: return int(s.percentTextVisibility);
        case SettingsField::PercentTextAlignment: return int(s.percentTextAlignment);
        case SettingsField::ShowExtraBarAmounts: return s.showExtraBarAmounts;
        case SettingsField::UsagePalette: return int(s.usagePalette);
        case SettingsField::UsageColorBelowYellow:
        case SettingsField::UsageColorYellow:
        case SettingsField::UsageColorOrange:
        case SettingsField::UsageColorRed:
            return int(s.customUsageColors[size_t(field) - size_t(SettingsField::UsageColorBelowYellow)] & 0x00FFFFFF);
        case SettingsField::YellowThreshold: return s.yellowThreshold;
        case SettingsField::OrangeThreshold: return s.orangeThreshold;
        case SettingsField::RedThreshold: return s.redThreshold;
        case SettingsField::ShowStaleWarning: return s.showStaleWarning;
        case SettingsField::ShowOpenAiExtraLimits: return s.showOpenAiExtraLimits;
        default: return 0;
    }
}

// Sets one field, or a group's fields to their defaults, to the desired value (never toggles).
// Numbers clamp to their range and choices to their values, so every assigned value is one the
// preview can draw; normalization follows when the settings are applied.
static void SettingsAssign(Settings& s, SettingsField field, int value) {
    const Settings defaults;
    if (std::optional<SettingsNumberRange> range = SettingsFieldRange(field)) {
        value = std::clamp(value, range->minimum, range->maximum);
    }
    switch (field) {
        case SettingsField::ShowOn:
            s.taskbarMonitorMode = value == 0 ? TaskbarMonitorMode::Primary :
                                   value < 0  ? TaskbarMonitorMode::All : TaskbarMonitorMode::Specific;
            if (value > 0) s.taskbarMonitorNumber = value;
            break;
        case SettingsField::ShowDetailsOnHover: s.showDetailsOnHover = value != 0; break;
        case SettingsField::PollMinutes: s.pollMinutes = value; break;
        case SettingsField::SkipHiddenPolling: s.skipHiddenPolling = value != 0; break;
        case SettingsField::Notifications:
            s.notificationLevel = NotificationLevel(std::clamp(value, int(NotificationLevel::Off),
                                                               int(NotificationLevel::Red)));
            break;
        case SettingsField::RecordHistory: s.historyEnabled = value != 0; break;
        case SettingsField::KeepHistory: s.historyRetentionDays = std::clamp(value, 0, 3650); break;
        case SettingsField::RetentionDays:
            // Only Keep history leaves forever: the window asks first.
            if (s.historyRetentionDays > 0) s.historyRetentionDays = value;
            break;
        case SettingsField::HistoryRange:
            s.historyDefaultRange = HistoryUiRange(std::clamp(value, int(HistoryUiRange::Day), int(HistoryUiRange::All)));
            break;
        case SettingsField::BarLayout:
            s.barLayout = BarLayout(std::clamp(value, int(BarLayout::Sparkline), int(kLastBarLayout)));
            break;
        case SettingsField::BarStyle:
            s.barStyle = BarStyle(std::clamp(value, int(BarStyle::Track), int(BarStyle::Rail)));
            break;
        case SettingsField::BarLength: s.barLength = value; break;
        case SettingsField::BarThickness: s.barThickness = value; break;
        case SettingsField::BarGap: s.barGap = value; break;
        case SettingsField::RingSize: s.ringSize = value; break;
        case SettingsField::RingThickness: s.ringThickness = value; break;
        case SettingsField::HeadlineFontSize: s.headlineFontSize = value; break;
        case SettingsField::HeadlineBarLength: s.headlineBarLength = value; break;
        case SettingsField::HeadlineBarThickness: s.headlineBarThickness = value; break;
        case SettingsField::HeadlineCountdown: s.headlineCountdown = value != 0; break;
        case SettingsField::SparklineWidth: s.sparklineWidth = value; break;
        case SettingsField::SparklineHeight: s.sparklineHeight = value; break;
        case SettingsField::SparklineTextSize: s.sparklineTextSize = value; break;
        case SettingsField::SparklineOutline: s.sparklineOutline = value != 0; break;
        case SettingsField::SparklineExtra:
            s.sparklineExtra = SparklineExtra(std::clamp(value, int(SparklineExtra::Off), int(SparklineExtra::Percent)));
            break;
        case SettingsField::SparklineElapsed: s.sparklineElapsed = value != 0; break;
        case SettingsField::SparklineNowLine: s.sparklineNowLine = value != 0; break;
        case SettingsField::SparklineNowCaret: s.sparklineNowCaret = value != 0; break;
        case SettingsField::SparklineRing: s.sparklineRing = value != 0; break;
        case SettingsField::SparklineReadingScale: s.sparklineReadingScale = value; break;
        case SettingsField::SparklineSpan:
            s.sparklineSpan = SparklineSpan(std::clamp(value, int(SparklineSpan::Window), int(SparklineSpan::Hours48)));
            break;
        case SettingsField::SparklineHoverSpan:
            s.sparklineHoverSpan = value < 0 ? std::nullopt :
                                   std::optional(SparklineSpan(std::min(value, int(SparklineSpan::Hours48))));
            break;
        case SettingsField::LogoScale: s.logoScale = value; break;
        case SettingsField::ClaudeLogoBrand: s.logoBrands.claude = value != 0; break;
        case SettingsField::GeminiLogoBrand: s.logoBrands.gemini = value != 0; break;
        case SettingsField::BadgeSize: s.badgeSize = value; break;
        case SettingsField::BadgeColor: s.badgeColor = COLORREF(value) & 0x00FFFFFF; break;
        case SettingsField::BadgeOffsetX: s.badgeOffsetX = value; break;
        case SettingsField::BadgeOffsetY: s.badgeOffsetY = value; break;
        case SettingsField::LabelPosition:
            s.labelPosition = LabelPosition(std::clamp(value, int(LabelPosition::Hidden),
                                                       int(LabelPosition::Bottom)));
            break;
        case SettingsField::LabelFontSize: s.labelFontSize = value; break;
        case SettingsField::LabelGap: s.labelGap = value; break;
        case SettingsField::ShowBarLabels: s.showBarLabels = value != 0; break;
        case SettingsField::PercentFontSize: s.percentFontSize = value; break;
        case SettingsField::ShowPaceTicks: s.showPaceTicks = value != 0; break;
        case SettingsField::PaceTickStyle:
            s.paceTickStyle = PaceTickStyle(std::clamp(value, int(PaceTickStyle::Caret),
                                                       int(PaceTickStyle::TimeTrack)));
            break;
        case SettingsField::PaceTickColor: s.paceTickColor = COLORREF(value) & 0x00FFFFFF; break;
        case SettingsField::Placement:
            s.placement = TaskbarPlacement(std::clamp(value, int(TaskbarPlacement::BeforeIcons),
                                                      int(kLastTaskbarPlacement)));
            break;
        case SettingsField::AccountMargin: s.accountMargin = value; break;
        case SettingsField::RightMargin: s.rightMargin = value; break;
        case SettingsField::Material:
            s.material = Material(std::clamp(value, int(Material::Acrylic), int(Material::Solid)));
            break;
        case SettingsField::BarMode:
            s.barMode = BarMode(std::clamp(value, int(BarMode::Used), int(BarMode::Remaining)));
            break;
        case SettingsField::PercentTextVisibility:
            s.percentTextVisibility = PercentTextVisibility(std::clamp(
                value, int(PercentTextVisibility::Never), int(PercentTextVisibility::Always)));
            break;
        case SettingsField::PercentTextAlignment:
            s.percentTextAlignment = PercentTextAlignment(std::clamp(
                value, int(PercentTextAlignment::Adaptive), int(PercentTextAlignment::Right)));
            break;
        case SettingsField::ShowExtraBarAmounts: s.showExtraBarAmounts = value != 0; break;
        case SettingsField::UsagePalette:
            s.usagePalette = UsagePalette(std::clamp(value, int(UsagePalette::Default), int(UsagePalette::Custom)));
            break;
        case SettingsField::UsageColorBelowYellow:
        case SettingsField::UsageColorYellow:
        case SettingsField::UsageColorOrange:
        case SettingsField::UsageColorRed:
            s.customUsageColors[size_t(field) - size_t(SettingsField::UsageColorBelowYellow)] =
                COLORREF(value) & 0x00FFFFFF;
            break;
        case SettingsField::YellowThreshold: s.yellowThreshold = value; break;
        case SettingsField::OrangeThreshold: s.orangeThreshold = value; break;
        case SettingsField::RedThreshold: s.redThreshold = value; break;
        case SettingsField::ShowStaleWarning: s.showStaleWarning = value != 0; break;
        case SettingsField::ShowOpenAiExtraLimits: s.showOpenAiExtraLimits = value != 0; break;
        case SettingsField::ResetDisplay:
            s.material = defaults.material;
            s.barMode = defaults.barMode;
            s.percentTextVisibility = defaults.percentTextVisibility;
            s.percentTextAlignment = defaults.percentTextAlignment;
            s.showExtraBarAmounts = defaults.showExtraBarAmounts;
            s.usagePalette = defaults.usagePalette;
            s.customUsageColors = defaults.customUsageColors;
            s.yellowThreshold = defaults.yellowThreshold;
            s.orangeThreshold = defaults.orangeThreshold;
            s.redThreshold = defaults.redThreshold;
            s.showStaleWarning = defaults.showStaleWarning;
            s.showOpenAiExtraLimits = defaults.showOpenAiExtraLimits;
            break;
        case SettingsField::ResetLayout:
            s.barLayout = defaults.barLayout;
            s.barStyle = defaults.barStyle;
            s.barLength = defaults.barLength;
            s.barThickness = defaults.barThickness;
            s.barGap = defaults.barGap;
            s.ringSize = defaults.ringSize;
            s.ringThickness = defaults.ringThickness;
            s.headlineFontSize = defaults.headlineFontSize;
            s.headlineBarLength = defaults.headlineBarLength;
            s.headlineBarThickness = defaults.headlineBarThickness;
            s.headlineCountdown = defaults.headlineCountdown;
            s.sparklineWidth = defaults.sparklineWidth;
            s.sparklineHeight = defaults.sparklineHeight;
            s.sparklineTextSize = defaults.sparklineTextSize;
            s.sparklineOutline = defaults.sparklineOutline;
            s.sparklineExtra = defaults.sparklineExtra;
            s.sparklineElapsed = defaults.sparklineElapsed;
            s.sparklineNowLine = defaults.sparklineNowLine;
            s.sparklineNowCaret = defaults.sparklineNowCaret;
            s.sparklineRing = defaults.sparklineRing;
            s.sparklineReadingScale = defaults.sparklineReadingScale;
            s.sparklineSpan = defaults.sparklineSpan;
            s.sparklineHoverSpan = defaults.sparklineHoverSpan;
            s.logoScale = defaults.logoScale;
            s.logoBrands = defaults.logoBrands;
            s.badgeSize = defaults.badgeSize;
            s.badgeColor = defaults.badgeColor;
            s.badgeOffsetX = defaults.badgeOffsetX;
            s.badgeOffsetY = defaults.badgeOffsetY;
            s.labelPosition = defaults.labelPosition;
            s.labelFontSize = defaults.labelFontSize;
            s.labelGap = defaults.labelGap;
            s.showBarLabels = defaults.showBarLabels;
            s.percentFontSize = defaults.percentFontSize;
            s.showPaceTicks = defaults.showPaceTicks;
            s.paceTickStyle = defaults.paceTickStyle;
            s.paceTickColor = defaults.paceTickColor;
            s.placement = defaults.placement;
            s.accountMargin = defaults.accountMargin;
            s.rightMargin = defaults.rightMargin;
            break;
        case SettingsField::ResetBehavior:
            s.taskbarMonitorMode = defaults.taskbarMonitorMode;
            s.taskbarMonitorNumber = defaults.taskbarMonitorNumber;
            s.showDetailsOnHover = defaults.showDetailsOnHover;
            s.pollMinutes = defaults.pollMinutes;
            s.skipHiddenPolling = defaults.skipHiddenPolling;
            s.notificationLevel = defaults.notificationLevel;
            break;
        case SettingsField::ResetHistory:
            s.historyEnabled = defaults.historyEnabled;
            s.historyRetentionDays = defaults.historyRetentionDays;
            s.historyDefaultRange = defaults.historyDefaultRange;
            break;
        default: break;
    }
}

// Number text: optional surrounding spaces and sign, digits with optional digit grouping
// (`separator`; any space character when it is one), groups of three after the first.
// Empty, malformed and overflowing text fails; out-of-range values clamp.
struct SettingsParsedNumber {
    bool ok = false;
    int value = 0;
};

static SettingsParsedNumber SettingsParseNumber(std::wstring_view text, SettingsNumberRange range,
                                                wchar_t separator) {
    auto space = [](wchar_t c) { return iswspace(c) || c == 0x00A0 || c == 0x202F; };
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    bool negative = false;
    if (!text.empty() && (text.front() == L'-' || text.front() == L'+')) {
        negative = text.front() == L'-';
        text.remove_prefix(1);
    }
    bool spaceSeparator = space(separator);
    long long value = 0;
    int digits = 0, group = 0;
    bool grouped = false;
    for (wchar_t c : text) {
        if (c >= L'0' && c <= L'9') {
            value = value * 10 + (c - L'0');
            if (value > INT_MAX) return {};
            digits++;
            group++;
            continue;
        }
        bool isSeparator = c == separator || (spaceSeparator && space(c));
        // The first group has 1-3 digits; every later one exactly three.
        if (!isSeparator || group == 0 || group > 3 || (grouped && group != 3)) return {};
        grouped = true;
        group = 0;
    }
    if (!digits || (grouped && group != 3)) return {};
    long long signedValue = negative ? -value : value;
    return {true, int(std::clamp<long long>(signedValue, range.minimum, range.maximum))};
}

// Digit grouping by threes with `separator`.
static std::wstring SettingsFormatNumber(int value, wchar_t separator) {
    std::wstring digits = std::to_wstring(value < 0 ? -(long long)value : (long long)value);
    std::wstring text;
    for (size_t i = 0; i < digits.size(); i++) {
        if (i && (digits.size() - i) % 3 == 0) text += separator;
        text += digits[i];
    }
    return value < 0 ? L"-" + text : text;
}

static std::wstring SettingsRangeError(SettingsNumberRange range) {
    return L"Enter a whole number from " + std::to_wstring(range.minimum) + L" to " +
           std::to_wstring(range.maximum) + L".";
}

// Account fields ---------------------------------------------------------------------------------

static int SettingsAccountValue(const AccountConfig& a, SettingsField field) {
    switch (field) {
        case SettingsField::AccountTileIdentity: return int(a.tileIdentity);
        case SettingsField::AccountBars: {
            int mask = 0;
            for (int i = 0; i < kQuotaBarCount; i++) mask |= a.showBars[size_t(i)] ? 1 << i : 0;
            return mask;
        }
        case SettingsField::AccountCreditsMax: return a.creditsMax;
        case SettingsField::AccountRecordHistory: return a.recordHistory;
        case SettingsField::AccountHidden: return a.hidden;
        default: return 0;
    }
}

// Sets one account field to its desired value: `text` for the label and short text, else
// `value`, clamped to its choices or range. Normalization follows when settings are saved.
static void SettingsAccountAssign(AccountConfig& a, SettingsField field, int value, const std::wstring& text) {
    switch (field) {
        case SettingsField::AccountLabel: a.label = text; break;
        case SettingsField::AccountTileText: a.tileText = text; break;
        case SettingsField::AccountTileIdentity:
            a.tileIdentity = TileIdentity(std::clamp(value, int(TileIdentity::LogoBadge), int(TileIdentity::Text)));
            break;
        case SettingsField::AccountBars:
            for (int i = 0; i < kQuotaBarCount; i++) a.showBars[size_t(i)] = (value >> i) & 1;
            break;
        case SettingsField::AccountCreditsMax: a.creditsMax = std::clamp(value, 0, kSettingsCreditsLimit); break;
        case SettingsField::AccountRecordHistory: a.recordHistory = value != 0; break;
        case SettingsField::AccountHidden: a.hidden = value != 0; break;
        default: break;
    }
}

// A label as saved: surrounding spaces trimmed.
static std::wstring SettingsTrimLabel(std::wstring_view text) {
    size_t first = text.find_first_not_of(L" \t\r\n"), last = text.find_last_not_of(L" \t\r\n");
    return first == std::wstring_view::npos ? std::wstring() : std::wstring(text.substr(first, last - first + 1));
}

// Store ------------------------------------------------------------------------------------------

struct SettingsDisplay {
    int number = 0;  // 1-based, the taskbars' order (primary first)
    int width = 0, height = 0;
    bool primary = false;
};

// A taskbar that shows the bars in the tray instead of at a position beside Start.
struct SettingsPlacementFallback {
    int display = 0;        // As SettingsDisplay::number
    bool conflict = false;  // Another mod holds the space; else what the position needs isn't there
    bool side = false;      // The taskbar runs down a side of the screen
};

// A taskbar on a left or right edge, where the bars stack down it.
struct SettingsSideTaskbar {
    int display = 0;           // As SettingsDisplay::number
    bool unsupported = false;  // Its Windows build's side tray takes no bars: none show there
};

// Recording state while consent is on.
enum class SettingsRecording : uint8_t { Recording, PendingSave, StorageError };

// What is known about an account beside its settings: its last poll and its sign-in.
struct SettingsAccountStatus {
    std::wstring plan;         // Empty if unknown
    std::wstring error;        // The last poll's
    bool polled = false;       // A poll has succeeded
    bool signedIn = false;     // A sign-in is stored (all but Antigravity)
    bool needsSignIn = false;  // The last poll asked for one
    bool signingIn = false;    // Its sign-in window is open
};

struct SettingsStatus {
    std::vector<SettingsDisplay> displays;
    std::vector<SettingsPlacementFallback> placementFallbacks;  // Display order
    std::vector<SettingsSideTaskbar> sideTaskbars;              // Display order
    SettingsRecording recording = SettingsRecording::Recording;
    std::wstring historyFolder;
    wchar_t groupSeparator = L',';
    // The primary taskbar's thickness (its height across the screen, its width on a side), in DIPs
    // at its own DPI; 0 if unknown
    double taskbarHeight = 0;
    bool visualTest = false;   // The taskbar shows the sample accounts
    std::vector<SettingsAccountStatus> accounts;  // Parallel to the settings' accounts
    bool signInRunning = false;                   // Some account's sign-in window is open
};

struct SettingsBaseline {
    Settings settings;
    SettingsStatus status;
};

enum class SettingsSaveResult : uint8_t { Changed, Unchanged, Failed };

// How the store finds an account in fresh settings: by its history ID, else (none) by its
// provider+label identity.
struct SettingsAccountRef {
    uint64_t historyId = 0;
    uint64_t identity = 0;
};

enum class SettingsNoticeKind : uint8_t { Error, Warning, Information };

struct SettingsAccountResult {
    SettingsSaveResult save = SettingsSaveResult::Unchanged;
    bool missing = false;  // No such account (any more)
    std::wstring notice;   // What the window tells the user, if anything
    SettingsNoticeKind kind = SettingsNoticeKind::Error;
    SettingsAccountRef account;  // The account afterwards: added or renamed
};

// What the controller reads and writes. Each Apply, Account and AddAccount is one transaction on
// fresh settings.
struct SettingsStore {
    virtual SettingsBaseline Load() = 0;
    // A field (or a group's fields) set to `value`, saved and published.
    virtual SettingsSaveResult Apply(SettingsField field, int value) = 0;
    // An account field set to its desired value (`text` for the label and short text), or an
    // account action, on the account freshly resolved from `account`.
    virtual SettingsAccountResult Account(SettingsAccountRef account, SettingsField field, int value,
                                          const std::wstring& text) = 0;
    // A new account of `provider`, labelled with the first of `labels` no account of its
    // provider uses.
    virtual SettingsAccountResult AddAccount(const std::wstring& provider,
                                             const std::vector<std::wstring>& labels) = 0;
    // Whether a sign-in is stored for an identity (presence only).
    virtual bool HasStoredSignIn(uint64_t identity) = 0;
    // Unload or cancellation: stop committing.
    virtual bool Unloading() = 0;

protected:
    ~SettingsStore() = default;
};

// Controller -------------------------------------------------------------------------------------

struct SettingsDraft {
    uint64_t key = 0;
    SettingsField field = SettingsField::None;
    uint64_t revision = 0;
    bool text = false;   // Typed into a number box: `raw` is its text
    std::wstring raw;
    int value = 0;       // Desired value; for text, the last valid one
    bool valid = true;   // `raw` parses
    bool failed = false; // Its last save failed; not retried until the user acts on the field
    std::wstring error;  // Shown with the control
};

struct SettingsRequest {
    uint64_t key = 0;
    SettingsField field = SettingsField::None;
    uint64_t revision = 0;  // 0: a group or an account action, which have no draft
    int value = 0;
    std::wstring text;      // The label or short text to save
};

enum class SettingsCloseResult : uint8_t { Saved, Unloading, Blocked };

// Blocked: one of the account's drafts stopped the action (an invalid label keeps its error; a
// failed save has its notice). Failed: the action itself failed (its notice says why).
enum class SettingsActionResult : uint8_t { Done, Blocked, Failed };

// A message box the window owes the user. A failure's explains a failed save.
struct SettingsNotice {
    std::wstring text;
    SettingsNoticeKind kind = SettingsNoticeKind::Error;
    bool failure = false;
};

struct SettingsEdits {
    explicit SettingsEdits(SettingsStore& store) : store(&store) {}

    // Store and its last load
    SettingsStore* store;
    SettingsBaseline baseline;

    // Drafts and requests
    std::vector<SettingsDraft> drafts;
    std::deque<SettingsRequest> queue;
    uint64_t revisions = 0;

    // Accounts: keys parallel to the baseline's accounts, the session keys of accounts without a
    // history ID (identity, key), and a control slot per key ever seen (never reused)
    std::vector<uint64_t> keys;
    std::vector<std::pair<uint64_t, uint64_t>> sessionKeys;
    std::map<uint64_t, uint32_t> slots;
    uint64_t sessions = 0;
    // The added account whose sign-in waits for its naming (CommitLabel); 0 if none
    uint64_t pendingSignIn = 0;

    // Messages for the window to show once the current operation is done
    std::vector<SettingsNotice> notices;

    // Baseline and statuses: drafts, requests and text boxes stay as they are, except those of
    // an account that is gone, which go with a notice. A session key lasts for the window, also
    // once a later save generated the account's history ID.
    void Reload() {
        baseline = store->Load();
        keys.clear();
        for (const AccountConfig& account : baseline.settings.accounts) {
            uint64_t identity = AccountIdentityHash(account);
            auto session = std::find_if(sessionKeys.begin(), sessionKeys.end(),
                                        [&](const auto& entry) { return entry.first == identity; });
            uint64_t key = session != sessionKeys.end() ? session->second : account.historyId;
            if (!key) {
                sessionKeys.emplace_back(identity, (1ULL << 63) | ++sessions);
                key = sessionKeys.back().second;
            }
            keys.push_back(key);
            slots.try_emplace(key, uint32_t(slots.size() + 1));
        }
        size_t before = drafts.size();
        std::erase_if(drafts, [&](const SettingsDraft& d) { return d.key && !IndexOf(d.key); });
        std::erase_if(queue, [&](const SettingsRequest& r) { return r.key && !IndexOf(r.key); });
        if (drafts.size() != before) {
            notices.push_back({L"An account is no longer configured; its unsaved changes were dropped.",
                               SettingsNoticeKind::Warning, false});
        }
    }

    // What builders and previews see: the baseline with each draft's desired value; a label
    // only while it is valid. Thresholds stay ordered as saving orders them
    // (settings/storage.h): each at least the one before, so an unordered draft shows what it
    // will save.
    Settings Projection() const {
        Settings s = baseline.settings;
        for (const SettingsDraft& draft : drafts) {
            if (!draft.key) {
                SettingsAssign(s, draft.field, draft.value);
                continue;
            }
            std::optional<size_t> index = IndexOf(draft.key);
            if (!index) continue;
            AccountConfig& account = s.accounts[*index];
            if (draft.field == SettingsField::AccountLabel) {
                std::wstring label = SettingsTrimLabel(draft.raw);
                if (LabelError(draft.key, label).empty()) account.label = std::move(label);
            } else {
                SettingsAccountAssign(account, draft.field, draft.value, draft.raw);
            }
        }
        s.orangeThreshold = std::max(s.orangeThreshold, s.yellowThreshold);
        s.redThreshold = std::max(s.redThreshold, s.orangeThreshold);
        return s;
    }

    SettingsDraft* Find(uint64_t key, SettingsField field) {
        for (SettingsDraft& draft : drafts) {
            if (draft.key == key && draft.field == field) return &draft;
        }
        return nullptr;
    }

    std::optional<size_t> IndexOf(uint64_t key) const {
        auto at = std::find(keys.begin(), keys.end(), key);
        if (!key || at == keys.end() || size_t(at - keys.begin()) >= baseline.settings.accounts.size()) {
            return std::nullopt;
        }
        return size_t(at - keys.begin());
    }

    uint32_t SlotOf(uint64_t key) const {
        auto slot = slots.find(key);
        return slot == slots.end() ? 0 : slot->second;
    }

    // Empty when `label` (trimmed) can be the account's: not empty, and no other account of its
    // provider uses it.
    std::wstring LabelError(uint64_t key, const std::wstring& label) const {
        if (label.empty()) return L"Enter a label.";
        std::optional<size_t> index = IndexOf(key);
        if (!index) return {};
        const std::vector<AccountConfig>& accounts = baseline.settings.accounts;
        for (size_t i = 0; i < accounts.size(); i++) {
            if (i != *index && accounts[i].provider == accounts[*index].provider && accounts[i].label == label) {
                return L"Another account of this provider uses this label.";
            }
        }
        return {};
    }

    // A text box's text (every change). Number text that doesn't parse keeps the last valid
    // value; a label is checked against the other accounts.
    void SetText(uint64_t key, SettingsField field, std::wstring text) {
        std::optional<SettingsNumberRange> range = SettingsFieldRange(field);
        bool free = field == SettingsField::AccountLabel || field == SettingsField::AccountTileText;
        if (!range && !free) return;
        SettingsDraft& draft = Touch(key, field);
        draft.text = true;
        draft.raw = std::move(text);
        if (free) {
            draft.valid = field != SettingsField::AccountLabel ||
                          LabelError(key, SettingsTrimLabel(draft.raw)).empty();
            return;
        }
        SettingsParsedNumber parsed = SettingsParseNumber(draft.raw, *range, baseline.status.groupSeparator);
        draft.valid = parsed.ok;
        if (parsed.ok) draft.value = parsed.value;
    }

    // Spin buttons and arrow keys: the last valid value stepped, as text. Returns the draft's
    // revision for the debounced commit (CommitStep); 0 for fields without a range.
    uint64_t Step(uint64_t key, SettingsField field, int delta) {
        std::optional<SettingsNumberRange> range = SettingsFieldRange(field);
        if (!range) return 0;
        SettingsDraft& draft = Touch(key, field);
        draft.value = std::clamp(draft.value + delta, range->minimum, range->maximum);
        draft.text = true;
        draft.raw = SettingsFormatNumber(draft.value, baseline.status.groupSeparator);
        draft.valid = true;
        return draft.revision;
    }

    // A step's debounce ran out: commits only the stepped revision, and never retries it after
    // a failed save. Typing or another change since then made a newer draft, which waits for its
    // own Enter, focus loss or flush.
    bool CommitStep(uint64_t key, SettingsField field, uint64_t revision) {
        const SettingsDraft* draft = Find(key, field);
        return draft && revision && draft->revision == revision && !draft->failed &&
               RequestCommit(key, field);
    }

    // Enter, focus loss, a finished step: queues a valid draft (the caller then has the queue
    // processed). An invalid draft gets its error and stays unqueued. A failed draft retries,
    // except on focus loss, which isn't an action on the field.
    bool RequestCommit(uint64_t key, SettingsField field, bool focusLoss = false) {
        SettingsDraft* draft = Find(key, field);
        if (!draft || (focusLoss && draft->failed)) return false;
        if (!Valid(*draft)) {
            draft->error = InvalidError(*draft);
            return false;
        }
        draft->failed = false;
        draft->error.clear();
        bool queued = std::any_of(queue.begin(), queue.end(), [&](const SettingsRequest& request) {
            return request.key == key && request.field == field && request.revision == draft->revision;
        });
        if (!queued) queue.push_back(RequestFor(*draft));
        return true;
    }

    // Esc: back to the committed value; its queued requests go too.
    void Revert(uint64_t key, SettingsField field) {
        std::erase_if(drafts, [&](const SettingsDraft& d) { return d.key == key && d.field == field; });
        std::erase_if(queue, [&](const SettingsRequest& r) { return r.key == key && r.field == field; });
    }

    // A slider drag: the desired value (clamped to the field's range) as a draft, committed when
    // the drag ends (RequestCommit). Returns the draft's revision.
    uint64_t SetValue(uint64_t key, SettingsField field, int value) {
        if (std::optional<SettingsNumberRange> range = SettingsFieldRange(field)) {
            value = std::clamp(value, range->minimum, range->maximum);
        }
        SettingsDraft& draft = Touch(key, field);
        draft.text = false;
        draft.raw.clear();
        draft.value = value;
        draft.valid = true;
        return draft.revision;
    }

    // Discrete controls: the desired value, committed at once with whatever is queued. Returns
    // the number of failed saves.
    int Commit(uint64_t key, SettingsField field, int value) {
        uint64_t revision = SetValue(key, field, value);
        queue.push_back({key, field, revision, Find(key, field)->value});
        return Process();
    }

    // A page reset: its fields' drafts and requests are dropped, then the group commits.
    int Reset(SettingsField group) {
        for (SettingsField field : SettingsGroupFields(group)) Revert(0, field);
        queue.push_back({0, group, 0, 0});
        return Process();
    }

    // Runs the queue in order, one transaction per request, skipping superseded ones. A sign-in
    // queued behind its label (CommitLabel) runs only once that label saved; otherwise it waits
    // for the next naming.
    int Process() {
        int failures = 0;
        while (!queue.empty()) {
            if (store->Unloading()) {
                queue.clear();
                break;
            }
            SettingsRequest request = queue.front();
            queue.pop_front();
            if (request.revision) {
                const SettingsDraft* draft = Find(request.key, request.field);
                if (!draft || draft->revision != request.revision) continue;
            }
            if (request.field == SettingsField::AccountSignIn && Find(request.key, SettingsField::AccountLabel)) {
                pendingSignIn = request.key;
                continue;
            }
            if (!Run(request)) failures++;
        }
        return failures;
    }

    // Page change, which never blocks: valid drafts commit in order, invalid ones revert
    // without saving, failed ones stay with their error. Sign-ins queued behind labels follow.
    int Navigate() {
        std::erase_if(queue, [](const SettingsRequest& r) { return r.field != SettingsField::AccountSignIn; });
        int failures = 0;
        for (const auto& [key, field] : DraftKeys()) {
            SettingsDraft* draft = Find(key, field);
            if (!draft || draft->failed) continue;
            if (!Valid(*draft)) {
                Revert(key, field);
                continue;
            }
            if (store->Unloading()) break;
            if (!Run(RequestFor(*draft))) failures++;
        }
        return failures + Process();
    }

    // Deliberate close: every draft commits, one transaction at a time, rechecking unload in
    // between. An invalid or failing draft stops it with its error (Blocked): the window stays
    // unless the user discards.
    SettingsCloseResult PrepareClose() {
        queue.clear();
        for (const auto& [key, field] : DraftKeys()) {
            SettingsDraft* draft = Find(key, field);
            if (!draft) continue;
            if (store->Unloading()) return SettingsCloseResult::Unloading;
            if (!Valid(*draft)) {
                draft->error = InvalidError(*draft);
                return SettingsCloseResult::Blocked;
            }
            if (!Run(RequestFor(*draft))) return SettingsCloseResult::Blocked;
        }
        return SettingsCloseResult::Saved;
    }

    void Discard() {
        drafts.clear();
        queue.clear();
    }

    // Accounts --------------------------------------------------------------------------------

    // Before an action on an account: its drafts commit, one transaction each, its queued
    // requests included (a sign-in queued behind its label stays queued). An invalid label blocks
    // before anything saves, with its error; so does a failed save. Other invalid drafts stay as
    // they are.
    SettingsActionResult FlushAccount(uint64_t key) {
        std::erase_if(queue, [&](const SettingsRequest& request) {
            return request.key == key && request.field != SettingsField::AccountSignIn;
        });
        if (SettingsDraft* label = Find(key, SettingsField::AccountLabel); label && !Valid(*label)) {
            label->error = InvalidError(*label);
            return SettingsActionResult::Blocked;
        }
        for (const auto& [draftKey, field] : DraftKeys()) {
            SettingsDraft* draft = draftKey == key ? Find(key, field) : nullptr;
            if (!draft || !draft->valid) continue;
            if (store->Unloading() || !Run(RequestFor(*draft))) return SettingsActionResult::Blocked;
        }
        return SettingsActionResult::Done;
    }

    // An account action (sign in or out, dashboard, visibility, reorder, remove) with its
    // desired value, after the account's drafts committed; the account is resolved again
    // afterwards. A removal drops the account's drafts and requests. A sign-in replaces one that
    // waits for the account's naming.
    SettingsActionResult AccountAction(uint64_t key, SettingsField action, int value) {
        if (action == SettingsField::AccountSignIn) {
            if (pendingSignIn == key) pendingSignIn = 0;
            std::erase_if(queue, [&](const SettingsRequest& request) {
                return request.key == key && request.field == SettingsField::AccountSignIn;
            });
        }
        if (SettingsActionResult flushed = FlushAccount(key); flushed != SettingsActionResult::Done) {
            return flushed;
        }
        if (store->Unloading()) return SettingsActionResult::Blocked;
        std::optional<size_t> index = IndexOf(key);
        SettingsAccountResult result;
        result.missing = !index;
        if (index) result = store->Account(AccountRef(*index), action, value, {});
        if (action == SettingsField::AccountRemove &&
            (result.missing || result.save == SettingsSaveResult::Changed)) {
            DropAccount(key);
            if (!result.notice.empty()) notices.push_back({result.notice, result.kind, false});
            Reload();
            return SettingsActionResult::Done;
        }
        return Settle({key, action, 0, value}, result) ? SettingsActionResult::Done : SettingsActionResult::Failed;
    }

    // Enter or focus loss in an account's label box, or its card collapsing: the label commits as
    // RequestCommit's, and an added account waiting for its naming gets its sign-in queued behind
    // it, unless the label keeps an error. Returns whether anything was queued.
    bool CommitLabel(uint64_t key, bool focusLoss) {
        bool queued = RequestCommit(key, SettingsField::AccountLabel, focusLoss);
        if (!key || key != pendingSignIn || (!queued && Find(key, SettingsField::AccountLabel))) return queued;
        pendingSignIn = 0;
        queue.push_back({key, SettingsField::AccountSignIn, 0, 0, {}});
        return true;
    }

    // The Add account menu: an account of `provider`, created at once. Its label is the first
    // default (A, A2, A3, ...; O, G, C and OC for the others) whose identity has no stored sign-in,
    // so it never adopts an old one; the store takes the first of those no account of the provider
    // uses. One with a sign-in (all but Antigravity) waits for its naming to start it. Returns its
    // key; 0 when none was added.
    uint64_t AddAccount(const std::wstring& provider) {
        if (store->Unloading()) return 0;
        std::wstring base = provider == L"openai" ? L"O" : provider == L"antigravity" ? L"G" :
                            provider == L"copilot" ? L"C" : provider == L"opencode" ? L"OC" : L"A";
        std::vector<std::wstring> labels;
        size_t wanted = baseline.settings.accounts.size() + 8;
        for (int n = 1; labels.size() < wanted && n <= 10000; n++) {
            AccountConfig candidate;
            candidate.provider = provider;
            candidate.label = n == 1 ? base : base + std::to_wstring(n);
            if (!store->HasStoredSignIn(AccountIdentityHash(candidate))) labels.push_back(std::move(candidate.label));
        }
        SettingsAccountResult result = store->AddAccount(provider, labels);
        bool added = result.save == SettingsSaveResult::Changed;
        if (!result.notice.empty()) notices.push_back({result.notice, result.kind, !added});
        if (!added) return 0;
        Reload();
        for (size_t i = 0; i < keys.size(); i++) {
            SettingsAccountRef ref = AccountRef(i);
            if (result.account.historyId ? ref.historyId == result.account.historyId :
                                           ref.identity == result.account.identity) {
                if (provider != L"antigravity") pendingSignIn = keys[i];
                return keys[i];
            }
        }
        return 0;
    }

private:
    // The draft of a user action, created from the projection; a new revision clears any error.
    // Keep history and its custom days set the same retention: either one's edit drops the
    // other's draft and requests.
    SettingsDraft& Touch(uint64_t key, SettingsField field) {
        if (!key && (field == SettingsField::KeepHistory || field == SettingsField::RetentionDays)) {
            Revert(0, field == SettingsField::KeepHistory ? SettingsField::RetentionDays : SettingsField::KeepHistory);
        }
        SettingsDraft* draft = Find(key, field);
        if (!draft) {
            SettingsDraft created;
            created.key = key;
            created.field = field;
            Settings projection = Projection();
            std::optional<size_t> index = IndexOf(key);
            created.value = !key  ? SettingsFieldValue(projection, field) :
                            index ? SettingsAccountValue(projection.accounts[*index], field) : 0;
            drafts.push_back(std::move(created));
            draft = &drafts.back();
        }
        draft->revision = ++revisions;
        draft->failed = false;
        draft->error.clear();
        return *draft;
    }

    std::vector<std::pair<uint64_t, SettingsField>> DraftKeys() const {
        std::vector<std::pair<uint64_t, SettingsField>> list;
        for (const SettingsDraft& draft : drafts) list.emplace_back(draft.key, draft.field);
        return list;
    }

    // A label's validity follows the other accounts, which may have changed since it was typed.
    bool Valid(SettingsDraft& draft) const {
        if (draft.field == SettingsField::AccountLabel) {
            draft.valid = LabelError(draft.key, SettingsTrimLabel(draft.raw)).empty();
        }
        return draft.valid;
    }

    std::wstring InvalidError(const SettingsDraft& draft) const {
        if (draft.field == SettingsField::AccountLabel) return LabelError(draft.key, SettingsTrimLabel(draft.raw));
        std::optional<SettingsNumberRange> range = SettingsFieldRange(draft.field);
        return range ? SettingsRangeError(*range) : std::wstring();
    }

    SettingsRequest RequestFor(const SettingsDraft& draft) const {
        std::wstring text = draft.field == SettingsField::AccountLabel    ? SettingsTrimLabel(draft.raw) :
                            draft.field == SettingsField::AccountTileText ? draft.raw : std::wstring();
        return {draft.key, draft.field, draft.revision, draft.value, std::move(text)};
    }

    SettingsAccountRef AccountRef(size_t index) const {
        const AccountConfig& account = baseline.settings.accounts[index];
        return {account.historyId, AccountIdentityHash(account)};
    }

    // A removed (or vanished) account: its drafts, requests, waiting sign-in and session key go.
    void DropAccount(uint64_t key) {
        if (pendingSignIn == key) pendingSignIn = 0;
        std::erase_if(drafts, [&](const SettingsDraft& d) { return d.key == key; });
        std::erase_if(queue, [&](const SettingsRequest& r) { return r.key == key; });
        std::erase_if(sessionKeys, [&](const auto& entry) { return entry.second == key; });
    }

    // One transaction. Saved (or already so): the draft it was made for is done and the
    // baseline reloads. Failed: that draft keeps its value with the error.
    bool Run(const SettingsRequest& request) {
        if (request.key) {
            std::optional<size_t> index = IndexOf(request.key);
            SettingsAccountResult result;
            result.missing = !index;
            if (index) result = store->Account(AccountRef(*index), request.field, request.value, request.text);
            return Settle(request, result);
        }
        SettingsSaveResult result = store->Apply(request.field, request.value);
        auto same = [&](const SettingsDraft& d) {
            return d.key == request.key && d.field == request.field && d.revision == request.revision;
        };
        if (result == SettingsSaveResult::Failed) {
            if (request.revision) {
                auto draft = std::find_if(drafts.begin(), drafts.end(), same);
                if (draft != drafts.end()) {
                    draft->failed = true;
                    draft->error = L"This change couldn't be saved.";
                }
            }
            return false;
        }
        if (request.revision) std::erase_if(drafts, same);
        Reload();
        return true;
    }

    // An account transaction's outcome, as Run's: its notice goes to the window (a failure
    // always has one). A missing account's drafts go; a renamed session key follows its account.
    bool Settle(const SettingsRequest& request, const SettingsAccountResult& result) {
        if (result.missing) {
            DropAccount(request.key);
            notices.push_back({L"This account is no longer configured; its unsaved changes were dropped.",
                               SettingsNoticeKind::Warning, true});
            Reload();
            return false;
        }
        bool failed = result.save == SettingsSaveResult::Failed;
        if (!result.notice.empty()) {
            notices.push_back({result.notice, result.kind, failed});
        } else if (failed) {
            notices.push_back({L"Could not save the account. The change is kept here with its error; change it "
                               L"again to retry.", SettingsNoticeKind::Error, true});
        }
        auto same = [&](const SettingsDraft& d) {
            return d.key == request.key && d.field == request.field && d.revision == request.revision;
        };
        if (failed) {
            auto draft = request.revision ? std::find_if(drafts.begin(), drafts.end(), same) : drafts.end();
            if (draft != drafts.end()) {
                draft->failed = true;
                draft->error = L"This change couldn't be saved.";
            }
            return false;
        }
        if (request.revision) std::erase_if(drafts, same);
        for (auto& [identity, key] : sessionKeys) {
            if (key == request.key && result.account.identity) identity = result.account.identity;
        }
        Reload();
        return true;
    }
};
