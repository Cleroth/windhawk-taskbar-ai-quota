// Forward declarations for calls that reach a later fragment.

static int ScaleForDpi(int value, UINT dpi);
static UINT WindowDpi(HWND hWnd);
static void UpdateQuotaUi(QuotaUiInstance& state);
static void CloseAccountFlyout(QuotaUiInstance& state);
static void PostUiUpdate();
static void OpenSettingsWindow();
static void OpenHistoryWindow();
static bool SaveOwnedSettings(const Settings& settings);
static void PublishSettings(Settings settings, uint64_t oldIdentity = 0,
                            uint64_t newIdentity = 0);

static void StartRetryInject(bool removeExisting = false);
static LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                  LPARAM lParam, DWORD_PTR refData);
