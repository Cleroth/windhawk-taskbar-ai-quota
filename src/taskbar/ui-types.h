// Per-taskbar XAML UI instances and the registry that owns them.

struct AppliedState {
    std::array<int, kQuotaBarCount> fillPx{-1, -1, -1, -1};
    std::array<uint32_t, kQuotaBarCount> fillColor{0, 0, 0, 0};
    std::array<int, kQuotaBarCount> pacePx{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> paceVisible{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> percentAlignments{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> percentDark{-1, -1, -1, -1};
    std::wstring tip;
    std::array<std::wstring, kQuotaBarCount> percentTexts;
    std::wstring labelText;
    double labelOpacity = -1;
    double columnOpacity = -1;
    int barMask = -1;
    int visible = -1;  // -1 unset, 0 collapsed, 1 visible.
};

struct PointerHandlers {
    UIElement element{nullptr};
    winrt::event_token tappedToken{};
    winrt::event_token pointerEnteredToken{};
    winrt::event_token pointerMovedToken{};
    winrt::event_token pointerExitedToken{};
    winrt::event_token pointerCaptureLostToken{};
    winrt::event_token pointerCanceledToken{};
};

struct MenuItemClickHandler {
    MenuFlyoutItem item{nullptr};
    winrt::event_token token{};
};

struct AccountUiRefs {
    StackPanel column{nullptr};
    FrameworkElement barArea{nullptr};
    ToolTip toolTip{nullptr};
    winrt::event_token toolTipOpenedToken{};
    DispatcherTimer manualToolTipTimer{nullptr};
    winrt::event_token manualToolTipTimerToken{};
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
    POINT toolTipOpenCursor{};
    bool hasToolTipOpenCursor = false;
    bool reopenToolTipOnMove = false;
    bool manualToolTipOpen = false;
};

struct QuotaUiInstance {
    HWND hWnd = nullptr;
    DWORD ownerThreadId = 0;
    double rasterizationScale = 1.0;
    bool windowSubclassed = false;
    ULONGLONG buildSettingsGeneration = 0;
    bool buildVisualTestMode = false;
    Grid quotaGrid{nullptr};
    Panel injectionParent{nullptr};
    ColumnDefinition quotaColumnDefinition{nullptr};
    std::vector<PointerHandlers> pointerHandlers;
    std::vector<MenuItemClickHandler> menuItemClickHandlers;
    std::vector<AccountUiRefs> accountRefs;
    std::vector<AppliedState> applied;
    DispatcherTimer paceTimer{nullptr};
    winrt::event_token paceTimerToken{};
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
