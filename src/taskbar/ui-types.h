// Per-taskbar XAML UI instances and the registry that owns them.

// Inputs of a tile's meter shapes; any change regenerates them.
struct TileShapeKey {
    int layout = -1;
    int barMask = -1;
    double size = -1;
    double stroke = -1;
    double gap = -1;
    int light = -1;
    int paceColor = -1;  // -1 without pace markers.

    bool operator==(const TileShapeKey&) const = default;
};

struct AppliedState {
    std::array<int, kQuotaBarCount> fillPx{-1, -1, -1, -1};
    std::array<uint32_t, kQuotaBarCount> fillColor{0, 0, 0, 0};
    std::array<int, kQuotaBarCount> pacePx{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> paceVisible{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> percentAlignments{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> percentDark{-1, -1, -1, -1};
    std::array<std::wstring, kQuotaBarCount> percentTexts;
    std::wstring labelText;
    double labelOpacity = -1;
    double columnOpacity = -1;
    int barMask = -1;
    int visible = -1;  // -1 unset, 0 collapsed, 1 visible.

    // Tile layouts. Per slot, fillPx and pacePx hold the fill and pace position in thousandths of
    // the meter.
    TileShapeKey tileShape;
    double identityOpacity = -1;
    int tileWarn = -1;
    int highlight = -1;  // 0 none, 1 hover, 2 open flyout.
    std::wstring headline;
    uint32_t headlineColor = 0;
};

struct PointerHandlers {
    UIElement element{nullptr};
    winrt::event_token tappedToken{};
    winrt::event_token pointerEnteredToken{};
    winrt::event_token pointerPressedToken{};
    winrt::event_token pointerExitedToken{};
    winrt::event_token pointerCanceledToken{};
};

struct MenuItemClickHandler {
    MenuFlyoutItem item{nullptr};
    winrt::event_token token{};
};

// A tile layout's elements (rings, logo + mini bars, logo + headline). Mini bar and headline
// segment fills and ticks use AccountUiRefs::fills and paceTicks.
struct TileUiRefs {
    BarLayout layout = BarLayout::Stacked;
    // Rings: the ring area, holding the meters with the identity on top.
    Grid face{nullptr};
    // Meter shapes, regenerated whenever their TileShapeKey changes.
    Panel meters{nullptr};
    // Logo (with an optional number badge) or text, plus the stale-warning mark.
    Grid identity{nullptr};
    FrameworkElement logo{nullptr};
    TextBlock text{nullptr};
    double textWidthPerPt = 0;  // Text width per DIP of font size.
    Border badge{nullptr};
    TextBlock badgeText{nullptr};
    TextBlock warn{nullptr};
    // Logo + headline: the most-used quota's number and window key.
    TextBlock headline{nullptr};
    TextBlock headlineKey{nullptr};
    // Ring fills and pace dots per quota slot.
    std::array<wuxs::Path, kQuotaBarCount> arcs{
        wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}};
    std::array<wuxs::Path, kQuotaBarCount> dots{
        wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}};
};

struct AccountUiRefs {
    // Tile root in every layout: hit target and flyout anchor.
    StackPanel column{nullptr};
    FrameworkElement barArea{nullptr};
    // Captured at build time so the flyout matches the tile, including visual-test accounts.
    std::wstring provider;
    uint64_t identity = 0;
    uint64_t historyId = 0;
    MenuFlyout menu{nullptr};
    winrt::event_token menuOpeningToken{};
    std::array<Border, kQuotaBarCount> tracks{
        Border{nullptr}, Border{nullptr}, Border{nullptr}, Border{nullptr}};
    std::array<Grid, kQuotaBarCount> barItems{
        Grid{nullptr}, Grid{nullptr}, Grid{nullptr}, Grid{nullptr}};
    std::array<Border, kQuotaBarCount> fills{
        Border{nullptr}, Border{nullptr}, Border{nullptr}, Border{nullptr}};
    std::array<Border, kQuotaBarCount> paceTicks{
        Border{nullptr}, Border{nullptr}, Border{nullptr}, Border{nullptr}};
    std::array<TextBlock, kQuotaBarCount> percents{
        TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}};
    TextBlock label{nullptr};
    TileUiRefs tile;
};

// Account details flyout. Brushes and styles come from LoadFlyoutTheme and are shared by the
// card's elements.
struct FlyoutTheme {
    bool light = false;
    bool highContrast = false;
    Brush card{nullptr};
    Brush cardBorder{nullptr};
    Brush footer{nullptr};
    Brush separator{nullptr};
    Brush chip{nullptr};
    Brush chipBorder{nullptr};
    Brush text{nullptr};
    Brush secondaryText{nullptr};
    Brush mutedText{nullptr};
    Brush warning{nullptr};
    Brush error{nullptr};
    Brush accent{nullptr};
    Brush track{nullptr};
    Brush trackBorder{nullptr};
    Brush paceTick{nullptr};
    Brush paceHalo{nullptr};
    // Burn-up charts: opaque card color around markers, guide lines (even pace, now) and the
    // legend's projection sample.
    Brush markerRing{nullptr};
    Brush chartGuide{nullptr};
    Brush chartKey{nullptr};
    Style buttonStyle{nullptr};
};

enum FlyoutRowIndex {
    kFlyoutFiveHourRow,
    kFlyoutWeeklyRow,
    kFlyoutPoolShortRow,  // Antigravity Claude/GPT pools.
    kFlyoutPoolWeeklyRow,
    kFlyoutFableRow,
    kFlyoutExtraRow,      // Anthropic extra usage or OpenAI credits.
    kFlyoutRowCount,
};

// A burn-up chart's drawn geometry in plot DIPs. It is the chart's update key: shapes are
// regenerated only when it changes.
struct FlyoutChartShape {
    std::vector<std::vector<winrt::Windows::Foundation::Point>> lines;  // Runs of 2+ readings.
    std::vector<winrt::Windows::Foundation::Point> dots;                // Isolated readings.
    winrt::Windows::Foundation::Point now{-1.f, -1.f};    // Current point; X is the now line.
    winrt::Windows::Foundation::Point limit{-1.f, -1.f};  // Projection end; X < 0 without one.
    uint32_t color = 0;

    bool operator==(const FlyoutChartShape&) const = default;
};

struct FlyoutQuotaRow {
    // Separator, name/number cell and details cell share one grid row; they collapse together.
    FrameworkElement row{nullptr};
    FrameworkElement labels{nullptr};
    FrameworkElement details{nullptr};
    TextBlock name{nullptr};
    TextBlock number{nullptr};
    TextBlock unit{nullptr};
    FrameworkElement trend{nullptr};

    // Bar: hatched track, usage fill and pace tick.
    Grid visual{nullptr};
    ColumnDefinition fillColumn{nullptr};
    ColumnDefinition emptyColumn{nullptr};
    Border fill{nullptr};
    ColumnDefinition paceColumn{nullptr};
    ColumnDefinition paceRestColumn{nullptr};
    FrameworkElement paceTick{nullptr};
    uint32_t fillColor = 0;

    // Burn-up chart, shown instead of the bar while the row has a recorded curve.
    FrameworkElement chart{nullptr};
    wuxs::Path chartArea{nullptr};
    wuxs::Path chartLines{nullptr};
    wuxs::Path chartDots{nullptr};
    wuxs::Line chartProjection{nullptr};
    wuxs::Line chartNow{nullptr};
    wuxs::Ellipse chartMarker{nullptr};
    FlyoutChartShape chartShape;

    TextBlock amount{nullptr};
    TextBlock reset{nullptr};
    TextBlock projection{nullptr};
};

struct FlyoutButtonHandler {
    Button button{nullptr};
    winrt::event_token token{};
};

// Everything owned by one open popup; reset as a whole when it closes.
struct FlyoutPopup {
    Flyout flyout{nullptr};
    FrameworkElement card{nullptr};
    winrt::event_token closedToken{};
    std::vector<FlyoutButtonHandler> buttonHandlers;
    // Visible top-level windows of this thread before the popup opened; a visible one that is
    // not listed is the popup's own window.
    std::vector<HWND> windowsBeforeOpen;
    HWND lastForeground = nullptr;
    FlyoutTheme theme;

    TextBlock title{nullptr};
    FrameworkElement staleMarker{nullptr};
    FrameworkElement planChip{nullptr};
    TextBlock plan{nullptr};
    std::array<FlyoutQuotaRow, kFlyoutRowCount> rows;
    // Quota grid columns: name/number and details. Charts give details a fixed width.
    ColumnDefinition namesColumn{nullptr};
    ColumnDefinition detailsColumn{nullptr};
    // Chart legend, shown while a row shows a chart.
    FrameworkElement legend{nullptr};
    TextBlock legendSource{nullptr};
    FrameworkElement notes{nullptr};
    TextBlock extraLimits{nullptr};
    TextBlock extraLines{nullptr};
    TextBlock error{nullptr};
    TextBlock status{nullptr};
    TextBlock updated{nullptr};

    int accountIndex = -1;
    uint64_t accountIdentity = 0;
    uint64_t generation = 0;
    bool pinned = false;  // Opened by click (focusable); otherwise a hover quick look.
};

struct FlyoutUi {
    FlyoutPopup popup;
    // Incremented per open so callbacks from an older popup are no-ops.
    uint64_t openGeneration = 0;

    // Created on first use; stopped and revoked by ClearQuotaEventState.
    DispatcherTimer dwellTimer{nullptr};
    winrt::event_token dwellTimerToken{};
    // Runs only while a popup is open.
    DispatcherTimer pollTimer{nullptr};
    winrt::event_token pollTimerToken{};
    DispatcherTimer countdownTimer{nullptr};
    winrt::event_token countdownTimerToken{};

    // Hover and click tracking by account index; -1 when unset.
    int hoverIndex = -1;
    int dwellIndex = -1;
    int hoverSuppressedIndex = -1;  // Explicitly closed while hovered: wait for pointer exit.
    ULONGLONG pointerOutsideTick = 0;  // When the pointer left a hover popup and its tile; 0 inside.
    int pressIndex = -1;
    ULONGLONG pressTick = 0;
    // Last popup closed by the system (light dismiss, Escape): a click that dismissed a pinned
    // popup must not reopen it.
    int dismissedIndex = -1;
    bool dismissedPinned = false;
    ULONGLONG dismissedTick = 0;
};

struct QuotaUiInstance {
    HWND hWnd = nullptr;
    DWORD ownerThreadId = 0;
    double rasterizationScale = 1.0;
    bool windowSubclassed = false;
    ULONGLONG buildSettingsGeneration = 0;
    bool buildVisualTestMode = false;
    BarLayout buildBarLayout = BarLayout::Stacked;
    double taskbarHeight = 0;  // DIPs, measured at build for tile layouts; 0 if unknown.
    bool tileLightTheme = false;  // Light taskbar at the last tile update; picks highlight colors.
    Grid quotaGrid{nullptr};
    Panel injectionParent{nullptr};
    ColumnDefinition quotaColumnDefinition{nullptr};
    std::vector<PointerHandlers> pointerHandlers;
    std::vector<MenuItemClickHandler> menuItemClickHandlers;
    std::vector<AccountUiRefs> accountRefs;
    std::vector<AppliedState> applied;
    DispatcherTimer paceTimer{nullptr};
    winrt::event_token paceTimerToken{};
    FlyoutUi flyout;
    // Per-account show/hide toggle items, paired with their account index, for cross-instance
    // IsChecked sync in UpdateQuotaUi (Click is revoked via menuItemClickHandlers).
    std::vector<std::pair<int, ToggleMenuFlyoutItem>> accountToggleItems;
};

[[clang::no_destroy]] static std::optional<
    std::vector<std::unique_ptr<QuotaUiInstance>>> g_uiInstances{std::in_place};
static std::mutex g_uiInstancesMutex;

static const wchar_t* kRootName = L"AiQuota_Root";

using WindowThreadProc = bool (*)(void*);
struct TaskbarDisplayInfo {
    HWND hWnd = nullptr;
    bool primary = false;
    int monitorNumber = 0;
    RECT rect{};
};
