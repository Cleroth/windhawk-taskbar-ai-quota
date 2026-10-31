// Forward declarations for calls that reach a later fragment.

static int ScaleForDpi(int value, UINT dpi);
static UINT WindowDpi(HWND hWnd);
static void UpdateQuotaUi(QuotaUiInstance& state);
static void CloseAccountFlyout(QuotaUiInstance& state);
static void PostUiUpdate();
// Classic is every opener's window; New is the rebuilt one behind "Settings (new)".
enum class SettingsWindowKind { Classic, New };
static void OpenSettingsWindow(SettingsWindowKind kind = SettingsWindowKind::Classic);
static void OpenHistoryWindow(uint64_t historyId = 0);
static bool SaveOwnedSettings(const Settings& settings);
static void PublishSettings(Settings settings, uint64_t oldIdentity = 0,
                            uint64_t newIdentity = 0);

static void StartRetryInject(bool removeExisting = false);
static LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                  LPARAM lParam, DWORD_PTR refData);
