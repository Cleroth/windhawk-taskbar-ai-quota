// Window icons drawn by the toolkit (native): a Shape item, its view box filling the icon, as the
// title bar's small and Alt+Tab's big icon at a window's DPI and in its palette. A window doesn't
// own icons set with WM_SETICON: the caller keeps them and destroys each once the window holds
// its successor or is gone. The taskbar shows them only for a window with its own taskbar
// identity; otherwise it shows its process's (Explorer's) app icon.

// Gives `window` its own taskbar identity (AppUserModelID), unpinnable: the taskbar groups it
// apart from its process's windows and shows its window icon. nullptr removes both properties,
// which a window must do before it is destroyed. PreventPinning is ignored once the ID is set.
static void UiSetWindowAppId(HWND window, PCWSTR appId) {
    winrt::com_ptr<IPropertyStore> store;
    if (HRESULT hr = SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(store.put())); FAILED(hr)) {
        Wh_Log(L"Could not open the window's property store: 0x%08lX", hr);
        return;
    }
    // SetValue copies the value.
    PROPVARIANT value{};
    if (!appId) {
        store->SetValue(PKEY_AppUserModel_ID, value);
        store->SetValue(PKEY_AppUserModel_PreventPinning, value);
        return;
    }
    value.vt = VT_BOOL;
    value.boolVal = VARIANT_TRUE;
    store->SetValue(PKEY_AppUserModel_PreventPinning, value);
    value.vt = VT_LPWSTR;
    value.pwszVal = const_cast<PWSTR>(appId);
    if (HRESULT hr = store->SetValue(PKEY_AppUserModel_ID, value); FAILED(hr)) {
        Wh_Log(L"Could not set the window's taskbar identity: 0x%08lX", hr);
    }
}

// Draws `shape` at the small and big icon sizes of `dpi` and sets them on `window`, destroying
// the ones in `icons` (indexed by ICON_SMALL and ICON_BIG) they replace. A size that can't be
// drawn keeps its previous icon.
static void UiSetWindowIcons(HWND window, UiGraphics& g, const UiPalette& palette, const UiItem& shape,
                             UINT dpi, std::array<HICON, 2>& icons) {
    // Not declared by the toolchain's default headers (Windows 10 1607 and later).
    using GetSystemMetricsForDpi_t = int(WINAPI*)(int, UINT);
    static const auto metricsForDpi = reinterpret_cast<GetSystemMetricsForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
    for (WPARAM type : {WPARAM(ICON_SMALL), WPARAM(ICON_BIG)}) {
        bool big = type == ICON_BIG;
        int pixels = std::max(metricsForDpi ? metricsForDpi(big ? SM_CXICON : SM_CXSMICON, dpi) :
                                              MulDiv(big ? 32 : 16, int(dpi), 96), 1);

        // Drawn premultiplied into a top-down 32-bit DIB by a software DC target at 96 DPI (a DIP
        // is a pixel). The target and its brushes are gone before the bits are read.
        BITMAPINFO format{};
        format.bmiHeader = {sizeof(BITMAPINFOHEADER), pixels, -pixels, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP color = dc ? CreateDIBSection(dc, &format, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        HGDIOBJ previous = color ? SelectObject(dc, color) : nullptr;
        bool drawn = false;
        if (previous) {
            try {
                D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
                winrt::com_ptr<ID2D1DCRenderTarget> target;
                winrt::check_hresult(g.d2d->CreateDCRenderTarget(&properties, target.put()));
                RECT bounds{0, 0, pixels, pixels};
                winrt::check_hresult(target->BindDC(dc, &bounds));
                UiTargetCache cache;
                std::vector<UiItem> items{shape};
                items[0].rect = {0, 0, float(pixels), float(pixels)};
                target->BeginDraw();
                target->Clear(D2D1_COLOR_F{});
                UiPaintItems(target.get(), g, cache, palette, items, {}, {0, 0}, 1, -1e9f, 1e9f);
                drawn = SUCCEEDED(target->EndDraw());
            } catch (...) {}
            SelectObject(dc, previous);
            GdiFlush();
        }

        // Icons take straight alpha; the mask marks the fully transparent pixels.
        HICON icon = nullptr;
        if (drawn) {
            int stride = (pixels + 15) / 16 * 2;
            std::vector<BYTE> mask(size_t(stride) * size_t(pixels));
            auto* pixel = static_cast<BYTE*>(bits);
            for (int y = 0; y < pixels; y++) {
                for (int x = 0; x < pixels; x++, pixel += 4) {
                    if (BYTE alpha = pixel[3]) {
                        for (int c = 0; c < 3; c++) pixel[c] = BYTE(std::min(255, (pixel[c] * 255 + alpha / 2) / alpha));
                    } else {
                        mask[size_t(y) * size_t(stride) + size_t(x / 8)] |= BYTE(0x80 >> (x % 8));
                    }
                }
            }
            if (HBITMAP monochrome = CreateBitmap(pixels, pixels, 1, 1, mask.data())) {
                ICONINFO parts{TRUE, 0, 0, monochrome, color};
                icon = CreateIconIndirect(&parts);
                DeleteObject(monochrome);
            }
        }
        if (color) DeleteObject(color);
        if (dc) DeleteDC(dc);
        if (!icon) {
            Wh_Log(L"Could not draw the %d px window icon", pixels);
            continue;
        }
        SendMessageW(window, WM_SETICON, type, LPARAM(icon));
        if (HICON replaced = std::exchange(icons[type], icon)) DestroyIcon(replaced);
    }
}
