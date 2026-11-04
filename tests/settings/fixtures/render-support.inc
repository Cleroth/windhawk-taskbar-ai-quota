// Offscreen render support shared by the settings and history windows' renders: themes, WIC
// bitmaps and their D2D targets, PNG output and the overlay type for popups.

struct RenderTheme {
    const char* name;
    UiThemeInput input;
};

// The taskbar (Windows mode) follows the app mode here; high contrast: Aquatic system colors.
static std::vector<RenderTheme> RenderThemes() {
    UiThemeInput light, dark, hc;
    light.kind = UiThemeKind::Light;
    light.systemLight = true;
    dark.kind = UiThemeKind::Dark;
    hc.kind = UiThemeKind::HighContrast;
    return {{"light", light}, {"dark", dark}, {"hc", hc}};
}

static void SaveRender(IWICImagingFactory* wic, IWICBitmap* bitmap, const std::wstring& path) {
    UINT width = 0, height = 0;
    winrt::check_hresult(bitmap->GetSize(&width, &height));
    winrt::com_ptr<IWICStream> stream;
    winrt::check_hresult(wic->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
    winrt::check_hresult(frame->Initialize(nullptr));
    winrt::check_hresult(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&format));
    winrt::check_hresult(frame->WriteSource(bitmap, nullptr));
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(encoder->Commit());
}

static winrt::com_ptr<IWICBitmap> RenderBitmap(IWICImagingFactory* wic, D2D1_SIZE_F size, UINT dpi) {
    float scale = float(dpi) / 96;
    winrt::com_ptr<IWICBitmap> bitmap;
    winrt::check_hresult(wic->CreateBitmap(UINT(std::lround(size.width * scale)),
                                           UINT(std::lround(size.height * scale)),
                                           GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                           bitmap.put()));
    return bitmap;
}

static winrt::com_ptr<ID2D1RenderTarget> RenderTarget(UiGraphics& g, IWICBitmap* bitmap, UINT dpi) {
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), float(dpi),
        float(dpi));
    winrt::com_ptr<ID2D1RenderTarget> target;
    winrt::check_hresult(g.d2d->CreateWicBitmapRenderTarget(bitmap, &properties, target.put()));
    return target;
}

// Stand-in for a DWM backdrop under a transparent window (render-backdrop-*, the host then draws
// grayscale text): a blue-to-orange gradient a third of the way over the theme's base, as Mica
// tints the wallpaper. Opaque fills drawn over it show as flat blocks.
static void RenderBackdrop(ID2D1RenderTarget* target, D2D1_RECT_F r, const UiPalette& palette) {
    winrt::com_ptr<ID2D1SolidColorBrush> base;
    winrt::check_hresult(target->CreateSolidColorBrush(palette.background, base.put()));
    target->FillRectangle(r, base.get());
    D2D1_GRADIENT_STOP stops[] = {{0, UiRgb(0x2A7FD8, 0.35f)}, {1, UiRgb(0xE8782A, 0.35f)}};
    winrt::com_ptr<ID2D1GradientStopCollection> collection;
    winrt::check_hresult(target->CreateGradientStopCollection(stops, 2, collection.put()));
    winrt::com_ptr<ID2D1LinearGradientBrush> brush;
    winrt::check_hresult(target->CreateLinearGradientBrush({{r.left, r.top}, {r.right, r.bottom}}, collection.get(),
                                                           brush.put()));
    target->FillRectangle(r, brush.get());
}

// Drawn over a render in client DIPs: a popup where the host would open it.
using RenderOverlay = std::function<void(ID2D1RenderTarget*, UiTargetCache&, const UiPalette&)>;
