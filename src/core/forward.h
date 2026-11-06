// Forward declarations for calls that reach a later fragment.

static UINT WindowDpi(HWND hWnd);
static void UpdateQuotaUi(QuotaUiInstance& state);
static void CloseAccountFlyout(QuotaUiInstance& state);
static void PostUiUpdate();
static void OpenSettingsWindow();
static void OpenHistoryWindow(uint64_t historyId = 0);
static void UiSetWindowAppId(HWND window, PCWSTR appId);
static void SetAppWindowIcons(HWND window, UINT dpi, std::array<HICON, 2>& icons);
static bool SaveAppIconPng(const std::wstring& path);
static std::wstring NotificationProjectionText(const I18nLocale& locale, uint64_t historyId, int slot,
                                               const WindowUsage& reading);
static bool SaveOwnedSettings(const Settings& settings);
static void PublishSettings(Settings settings, uint64_t oldIdentity = 0,
                            uint64_t newIdentity = 0);

static void StartRetryInject(bool removeExisting = false);
static LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                  LPARAM lParam, DWORD_PTR refData);
