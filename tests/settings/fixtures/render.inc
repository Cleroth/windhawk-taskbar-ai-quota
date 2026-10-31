// Offscreen renders of the settings window's client area (navigation, pinned header, viewport)
// into WIC bitmaps: PNG evidence for review, no pixel goldens. The composition mirrors the host:
// main items, then the content clipped to the viewport at the scroll offset, then the scrollbar.

struct RenderTheme {
    const char* name;
    UiThemeInput input;
};

static std::vector<RenderTheme> RenderThemes() {
    UiThemeInput light, dark, hc;
    light.kind = UiThemeKind::Light;
    dark.kind = UiThemeKind::Dark;
    hc.kind = UiThemeKind::HighContrast;  // Aquatic system colors.
    return {{"light", light}, {"dark", dark}, {"hc", hc}};
}

// A row of buttons in every style and state, for the states sheet.
static void AddButtonRow(std::vector<UiItem>& items, uint32_t& id, float x, float y) {
    struct Sample {
        PCWSTR text, icon;
        UiButtonStyle style;
        bool enabled;
    };
    const Sample samples[] = {
        {L"Standard", L"", UiButtonStyle::Standard, true},
        {L"Add account", L"\uE710", UiButtonStyle::Accent, true},
        {L"Subtle", L"", UiButtonStyle::Subtle, true},
        {L"Remove account", L"", UiButtonStyle::Danger, true},
        {L"Disabled", L"", UiButtonStyle::Standard, false},
    };
    for (const Sample& sample : samples) {
        UiItem button;
        button.id = id++;
        button.kind = UiItemKind::Button;
        button.text = sample.text;
        button.icon = sample.icon;
        button.iconSize = 12;
        button.button = sample.style;
        button.enabled = sample.enabled;
        button.action = kTestOpen;
        float width = sample.icon[0] ? 150 : 120;
        button.rect = {x, y, x + width, y + 32};
        items.push_back(std::move(button));
        x += width + 8;
    }
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

static void RenderFrame(UiGraphics& g, IWICImagingFactory* wic, const std::wstring& path,
                        const UiFrame& frame, D2D1_SIZE_F client, UINT dpi,
                        const UiThemeInput& theme, const UiPaintState& state, float scroll,
                        bool scrollbarExpanded) {
    float scale = float(dpi) / 96;
    winrt::com_ptr<IWICBitmap> bitmap;
    winrt::check_hresult(wic->CreateBitmap(UINT(std::lround(client.width * scale)),
                                           UINT(std::lround(client.height * scale)),
                                           GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                           bitmap.put()));
    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), float(dpi),
        float(dpi));
    winrt::com_ptr<ID2D1RenderTarget> target;
    winrt::check_hresult(g.d2d->CreateWicBitmapRenderTarget(bitmap.get(), &properties, target.put()));
    UiTargetCache cache;
    UiPalette palette = UiPaletteFor(theme);
    target->BeginDraw();
    target->Clear(palette.background);
    UiPaintItems(target.get(), g, cache, palette, frame.main, state, {0, 0}, scale, -1e9f, 1e9f);
    const D2D1_RECT_F& v = frame.viewport;
    float height = v.bottom - v.top;
    target->PushAxisAlignedClip(&v, D2D1_ANTIALIAS_MODE_ALIASED);
    UiPaintItems(target.get(), g, cache, palette, frame.content, state, {v.left, v.top - scroll},
                 scale, scroll, scroll + height);
    D2D1::Matrix3x2F shift = D2D1::Matrix3x2F::Translation(v.left, v.top);
    target->SetTransform(&shift);
    UiPaintScrollbar(target.get(), cache, palette,
                     UiScrollbarFor({v.right - v.left, height}, scroll, frame.contentHeight,
                                    scrollbarExpanded),
                     scrollbarExpanded, scale);
    D2D1::Matrix3x2F identity = D2D1::Matrix3x2F::Identity();
    target->SetTransform(&identity);
    target->PopAxisAlignedClip();
    winrt::check_hresult(target->EndDraw());
    cache.Clear();
    SaveRender(wic, bitmap.get(), path);
    std::string narrow;
    for (wchar_t c : path) narrow += c < 0x80 ? char(c) : '?';
    std::cout << "RENDER " << narrow << '\n';
}

static void RunRenders(const std::wstring& directory) {
    winrt::check_hresult(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    {
        UiGraphics g;
        UiCreateGraphics(g);
        winrt::com_ptr<IWICImagingFactory> wic;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                              __uuidof(IWICImagingFactory), wic.put_void()));
        // The approved mockups' window is 1000 x 860 with a 40 DIP caption: 1000 x 820 client.
        const D2D1_SIZE_F client{1000, 820};
        for (const RenderTheme& theme : RenderThemes()) {
            for (UINT dpi : {96u, 144u, 192u}) {
                for (SettingsUiPage page : {SettingsUiPage::About, SettingsUiPage::Accounts}) {
                    std::string name = std::string("render-") +
                                       (page == SettingsUiPage::About ? "about" : "placeholder") +
                                       "-" + theme.name + "-" + std::to_string(dpi) + ".png";
                    Run(name, [&] {
                        SettingsView view;
                        view.page = page;
                        view.version = L"2.0.1";
                        UiFrame frame;
                        SettingsUiBuildFrame(g, view, client, frame);
                        UiPaintState state;
                        state.focus = {uint32_t(int(page) + 1)};
                        RenderFrame(g, wic.get(), directory + L"\\" + std::wstring(name.begin(), name.end()),
                                    frame, client, dpi, theme.input, state, 0, false);
                        CHECK(true);
                    });
                }
            }

            // Interaction states at 144 DPI on a short window, scrolled: a hovered link card, the
            // keyboard focus ring on an inline link, a button row and the expanded scrollbar;
            // then a hovered license link.
            std::string name = std::string("render-states-") + theme.name + "-144.png";
            Run(name, [&] {
                SettingsView view;
                view.page = SettingsUiPage::About;
                view.version = L"2.0.1";
                UiFrame frame;
                const D2D1_SIZE_F shortClient{1000, 560};
                SettingsUiBuildFrame(g, view, shortClient, frame);
                uint32_t id = 900;
                AddButtonRow(frame.content, id, 20, frame.contentHeight);
                frame.contentHeight += 32 + 28;
                UiPaintState state;
                uint32_t linkCard = 0, logos = 0, license = 0;
                for (const UiItem& item : frame.content) {
                    if (item.kind == UiItemKind::Card && item.action.kind != UiActionKind::None && !linkCard) {
                        linkCard = item.id;
                    }
                    if (item.runs.size() == 3) logos = item.id;
                    if (item.kind == UiItemKind::Link) license = item.id;
                }
                state.hot = {linkCard};
                state.focus = {logos, 0};
                state.focusVisible = true;
                RenderFrame(g, wic.get(), directory + L"\\" + std::wstring(name.begin(), name.end()),
                            frame, shortClient, 144, theme.input, state, 120, true);
                UiPaintState linkState;
                linkState.hot = {license};
                std::string second = std::string("render-states-link-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), directory + L"\\" + std::wstring(second.begin(), second.end()),
                            frame, shortClient, 144, theme.input, linkState, 300, false);
                CHECK(linkCard && logos && license);
            });
        }
    }
    CoUninitialize();
}
