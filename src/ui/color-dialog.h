// The system color dialog as a tracked modal of a custom-drawn window (native). While it runs,
// `dialog` holds its window: a close cancels it (WM_COMMAND IDCANCEL posted to it) and retries
// once the call has returned. Custom colors persist in the caller's array.

// The chosen color (0xBBGGRR); nothing when cancelled or ended by a close.
static std::optional<COLORREF> UiChooseColor(HWND owner, COLORREF initial,
                                             std::array<COLORREF, 16>& custom, HWND& dialog) {
    CHOOSECOLORW chooser{};
    chooser.lStructSize = sizeof(chooser);
    chooser.hwndOwner = owner;
    chooser.rgbResult = initial & 0x00FFFFFF;
    chooser.lpCustColors = custom.data();
    chooser.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ENABLEHOOK;
    chooser.lCustData = reinterpret_cast<LPARAM>(&dialog);
    chooser.lpfnHook = [](HWND window, UINT message, WPARAM, LPARAM lParam) -> UINT_PTR {
        if (message == WM_INITDIALOG) {
            *reinterpret_cast<HWND*>(reinterpret_cast<CHOOSECOLORW*>(lParam)->lCustData) = window;
        }
        return 0;
    };
    bool chosen = ChooseColorW(&chooser);
    dialog = nullptr;
    if (!chosen) return std::nullopt;
    return chooser.rgbResult & 0x00FFFFFF;
}
