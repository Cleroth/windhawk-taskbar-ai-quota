// Forward declarations for calls that reach a later fragment.

static int ScaleForDpi(int value, UINT dpi);
static UINT WindowDpi(HWND hWnd);
static void UpdateQuotaUi(QuotaUiInstance& state);
static void CloseAccountFlyout(QuotaUiInstance& state);
static void PostUiUpdate();
static void OpenSettingsWindow();
// The usage history window a new history thread runs: native controls or custom-drawn.
enum class HistoryWindowKind : uint8_t { Classic, Custom };
static void OpenHistoryWindow(uint64_t historyId = 0, HistoryWindowKind kind = HistoryWindowKind::Classic);
static void NotifyHistoryJournalChanged();
static bool SaveOwnedSettings(const Settings& settings);
static void PublishSettings(Settings settings, uint64_t oldIdentity = 0,
                            uint64_t newIdentity = 0);

static void StartRetryInject(bool removeExisting = false);
static LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                  LPARAM lParam, DWORD_PTR refData);
