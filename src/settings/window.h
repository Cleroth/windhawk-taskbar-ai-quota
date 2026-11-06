// Settings window thread and opener: one window at a time, on its own STA thread. Also the app
// icon for what the toolkit doesn't host: the sign-in window's icons and the notifications' file.

// The app icon on a window the toolkit doesn't host (the sign-in window): drawn at `dpi` in the
// app theme's palette, `icons` kept and destroyed by the caller as UiSetWindowIcons describes.
static void SetAppWindowIcons(HWND window, UINT dpi, std::array<HICON, 2>& icons) {
    try {
        UiGraphics graphics;
        UiCreateGraphics(graphics);
        UiThemeReader themes;
        UiSetWindowIcons(window, graphics, UiPaletteFor(UiReadTheme(themes)), SettingsAppIcon({}), dpi, icons);
    } catch (...) {
        Wh_Log(L"Could not draw the window icons");
    }
}

// The app icon as a 256 px PNG at `path`, the notifications' icon: transparent, straight alpha,
// its tracks in a fixed grey that suits light and dark backgrounds. The file is replaced only
// once written whole. Any thread with COM initialized.
static bool SaveAppIconPng(const std::wstring& path) {
    constexpr UINT kPixels = 256;
    std::wstring temp = path + L".tmp";
    bool written = false;
    try {
        UiGraphics graphics;
        UiCreateGraphics(graphics);
        winrt::com_ptr<IWICImagingFactory> wic;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                              __uuidof(IWICImagingFactory), wic.put_void()));

        // Drawn premultiplied by a software target at 96 DPI (a DIP is a pixel), as the window icon is.
        winrt::com_ptr<IWICBitmap> bitmap;
        winrt::check_hresult(wic->CreateBitmap(kPixels, kPixels, GUID_WICPixelFormat32bppPBGRA,
                                               WICBitmapCacheOnLoad, bitmap.put()));
        {
            D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            winrt::com_ptr<ID2D1RenderTarget> target;
            winrt::check_hresult(graphics.d2d->CreateWicBitmapRenderTarget(bitmap.get(), &properties, target.put()));
            std::vector<UiItem> items{SettingsAppIcon({0, 0, float(kPixels), float(kPixels)})};
            for (UiShapePart& part : items[0].parts) {
                if (part.ink == UiInk::Inherit) continue;
                part.color = UiRgb(0x8C8C8C, 0.42f);
                part.ink = UiInk::Inherit;
            }
            UiTargetCache cache;
            target->BeginDraw();
            target->Clear(D2D1_COLOR_F{});
            UiPaintItems(target.get(), graphics, cache, UiPaletteFor({}), items, {}, {0, 0}, 1, -1e9f, 1e9f);
            winrt::check_hresult(target->EndDraw());
        }

        // Straight alpha, rounded as the window icon's.
        std::vector<BYTE> pixels(size_t(kPixels) * kPixels * 4);
        winrt::check_hresult(bitmap->CopyPixels(nullptr, kPixels * 4, UINT(pixels.size()), pixels.data()));
        for (size_t p = 0; p < pixels.size(); p += 4) {
            if (BYTE alpha = pixels[p + 3]) {
                for (int c = 0; c < 3; c++) {
                    pixels[p + c] = BYTE(std::min(255, (pixels[p + c] * 255 + alpha / 2) / alpha));
                }
            }
        }
        winrt::com_ptr<IWICStream> stream;
        winrt::check_hresult(wic->CreateStream(stream.put()));
        winrt::check_hresult(stream->InitializeFromFilename(temp.c_str(), GENERIC_WRITE));
        winrt::com_ptr<IWICBitmapEncoder> encoder;
        winrt::check_hresult(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
        winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
        winrt::com_ptr<IWICBitmapFrameEncode> frame;
        winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
        winrt::check_hresult(frame->Initialize(nullptr));
        winrt::check_hresult(frame->SetSize(kPixels, kPixels));
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        winrt::check_hresult(frame->SetPixelFormat(&format));
        if (format != GUID_WICPixelFormat32bppBGRA) winrt::throw_hresult(WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT);
        winrt::check_hresult(frame->WritePixels(kPixels, kPixels * 4, UINT(pixels.size()), pixels.data()));
        winrt::check_hresult(frame->Commit());
        winrt::check_hresult(encoder->Commit());
        written = true;
    } catch (...) {}
    // The stream closed the file with the try block.
    if (written && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
    DeleteFileW(temp.c_str());
    Wh_Log(L"Could not write the notification icon");
    return false;
}

static UINT GetSettingsActivateMessage() {
    static const UINT message =
        RegisterWindowMessageW(L"Windhawk_ActivateSettings_" WH_MOD_ID);
    return message;
}

// The window keeps its graphics and WinRT objects inside UiHostRun, which returns before the
// apartment is uninitialized.
static DWORD WINAPI SettingsWindowThreadProc(LPVOID) {
    using SetThreadDpiAwarenessContext_t = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto setDpi = reinterpret_cast<SetThreadDpiAwarenessContext_t>(
            GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        apartmentInitialized = true;
    } catch (...) {}

    if (apartmentInitialized && !g_unloading && !g_settingsWindowCancelRequested) {
        try {
            SettingsUiClient client;
            client.titleLocale = CurrentLocale();
            std::wstring title = SettingsWindowTitle(client.titleLocale);
            UiHostConfig config;
            config.mainClass = kSettingsUiClass;
            config.viewportClass = kSettingsUiViewportClass;
            config.popupClass = kSettingsUiPopupClass;
            config.tooltipClass = kSettingsUiTooltipClass;
            config.title = title.c_str();
            config.activateMessage = GetSettingsActivateMessage();
            config.minSize = kSettingsWindowMinSize;
            config.icon = SettingsAppIcon({});
            config.appId = kWindowAppId;
            if (!UiHostRun(config, client)) Wh_Log(L"Could not open the settings window");
        } catch (...) {
            Wh_Log(L"Settings worker failed");
        }
    }
    SetVisualTestMode(false);
    g_settingsWindow.store(nullptr);
    if (apartmentInitialized) winrt::uninit_apartment();
    return 0;
}

// Opens the settings window, or activates the one already open.
static void OpenSettingsWindow() {
    if (g_unloading) return;
    std::lock_guard<std::mutex> lock(g_settingsWindowMutex);
    if (HWND window = g_settingsWindow.load(); window && IsWindow(window)) {
        if (UINT message = GetSettingsActivateMessage()) {
            PostMessageW(window, message, 0, 0);
        }
        return;
    }
    if (g_settingsWindowThread) {
        if (WaitForSingleObject(g_settingsWindowThread, 0) != WAIT_OBJECT_0) return;
        CloseHandle(g_settingsWindowThread);
        g_settingsWindowThread = nullptr;
    }
    if (g_unloading) return;
    g_settingsWindowCancelRequested = false;
    g_settingsWindowThread = CreateThread(nullptr, 0, SettingsWindowThreadProc, nullptr, 0, nullptr);
    if (!g_settingsWindowThread) Wh_Log(L"Could not create settings window thread");
}
