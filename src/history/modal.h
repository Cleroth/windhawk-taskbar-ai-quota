// Native dialogs of the custom-drawn usage history window (its message boxes, the save dialog
// and that dialog's overwrite prompt): a scope tracks every dialog (#32770) its thread activates
// while it runs, nested ones included, and ends them once its context says so (the window's
// close latch or unload): those running then, and any activated after. Its context names where
// the latest dialog is published for other threads (unload posts it a cancel) and the predicate;
// nothing else is global. One scope at a time per thread. Native, extractable on its own.

struct HistoryDialogContext {
    std::atomic<HWND>* published = nullptr;
    std::function<bool()> cancelled;
};

struct HistoryDialogScope;
static thread_local HistoryDialogScope* g_historyDialogScope = nullptr;

static LRESULT CALLBACK HistoryDialogScopeHook(int code, WPARAM wParam, LPARAM lParam);

struct HistoryDialogScope {
    // No hook (nothing may show) while another scope runs on the thread, without a predicate,
    // once it says cancelled, or when the hook can't be set.
    explicit HistoryDialogScope(const HistoryDialogContext& context) : context(context) {
        if (g_historyDialogScope || !context.published || !context.cancelled || context.cancelled()) return;
        g_historyDialogScope = this;
        hook = SetWindowsHookExW(WH_CBT, HistoryDialogScopeHook, nullptr, GetCurrentThreadId());
        if (!hook) g_historyDialogScope = nullptr;
    }
    ~HistoryDialogScope() {
        if (!hook) return;
        UnhookWindowsHookEx(hook);
        g_historyDialogScope = nullptr;
        context.published->store(nullptr);
    }
    HistoryDialogScope(const HistoryDialogScope&) = delete;
    HistoryDialogScope& operator=(const HistoryDialogScope&) = delete;

    const HistoryDialogContext& context;
    HHOOK hook = nullptr;
    std::array<HWND, 16> dialogs{};  // Activation order: the last is the topmost
    size_t count = 0;
};

// Ends a dialog as its safest button would: Cancel where it has one (a message box with only OK
// takes Cancel as OK), else No (a Yes/No prompt ignores Cancel); then closes it.
static void HistoryDialogEnd(HWND dialog) {
    int button = GetDlgItem(dialog, IDCANCEL) || !GetDlgItem(dialog, IDNO) ? IDCANCEL : IDNO;
    PostMessageW(dialog, WM_COMMAND, button, 0);
    PostMessageW(dialog, WM_CLOSE, 0, 0);
}

static LRESULT CALLBACK HistoryDialogScopeHook(int code, WPARAM wParam, LPARAM lParam) {
    // Only fixed-size storage, Win32 calls and the predicate: nothing may throw across the hook.
    HistoryDialogScope* scope = g_historyDialogScope;
    HWND window = reinterpret_cast<HWND>(wParam);
    if (scope && code == HCBT_ACTIVATE) {
        wchar_t name[16]{};
        if (GetClassNameW(window, name, ARRAYSIZE(name)) && wcscmp(name, L"#32770") == 0) {
            auto end = scope->dialogs.begin() + scope->count;
            if (std::find(scope->dialogs.begin(), end, window) == end && scope->count < scope->dialogs.size()) {
                scope->dialogs[scope->count++] = window;
            }
            scope->context.published->store(window);
            if (scope->context.cancelled()) HistoryDialogEnd(window);
        }
    } else if (scope && code == HCBT_DESTROYWND) {
        // A tracked dialog went: the one under it is the latest again, and ends too once cancelled.
        auto end = scope->dialogs.begin() + scope->count;
        auto found = std::find(scope->dialogs.begin(), end, window);
        if (found != end) {
            std::move(found + 1, end, found);
            scope->count--;
            HWND remaining = scope->count ? scope->dialogs[scope->count - 1] : nullptr;
            scope->context.published->store(remaining);
            if (remaining && scope->context.cancelled()) HistoryDialogEnd(remaining);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// The window's close (after its latch): the topmost running dialog ends, and the one under it as
// that one goes. True while a scope runs, its modal call not returned yet (the close retries).
static bool HistoryDialogCancel() {
    HistoryDialogScope* scope = g_historyDialogScope;
    if (!scope) return false;
    if (scope->count) HistoryDialogEnd(scope->dialogs[scope->count - 1]);
    return true;
}
