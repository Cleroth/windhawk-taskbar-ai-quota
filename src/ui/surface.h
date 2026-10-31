// One HWND render target with its per-target caches (native): opaque target at the window DPI,
// resize, device-loss discard, software fallback.

struct UiSurface {
    HWND window = nullptr;
    winrt::com_ptr<ID2D1HwndRenderTarget> target;
    UiTargetCache cache;
    bool software = false;  // Hardware creation failed; later targets are software too.
    int created = 0;        // Targets created so far.
    int failures = 0;       // Consecutive failed frames.
};

// Releases the target and everything created from it.
static void UiDiscardSurface(UiSurface& surface) {
    surface.cache.Clear();
    surface.target = nullptr;
}

// The target, created on demand: hardware, then software once. nullptr when both fail.
static ID2D1HwndRenderTarget* UiSurfaceTarget(UiSurface& surface, ID2D1Factory* factory, UINT dpi) {
    if (surface.target) return surface.target.get();
    RECT client{};
    GetClientRect(surface.window, &client);
    D2D1_HWND_RENDER_TARGET_PROPERTIES window = D2D1::HwndRenderTargetProperties(
        surface.window, D2D1::SizeU(UINT32(std::max<LONG>(client.right, 1)),
                                    UINT32(std::max<LONG>(client.bottom, 1))));
    for (int attempt = surface.software ? 1 : 0; attempt < 2; attempt++) {
        D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            attempt ? D2D1_RENDER_TARGET_TYPE_SOFTWARE : D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), float(dpi),
            float(dpi));
        HRESULT result = factory->CreateHwndRenderTarget(&properties, &window, surface.target.put());
        if (SUCCEEDED(result)) {
            surface.software = attempt == 1;
            surface.created++;
            return surface.target.get();
        }
        Wh_Log(L"Could not create a %s render target: 0x%08X", attempt ? L"software" : L"hardware",
               unsigned(result));
    }
    return nullptr;
}

static void UiResizeSurface(UiSurface& surface) {
    if (!surface.target) return;
    RECT client{};
    GetClientRect(surface.window, &client);
    D2D1_SIZE_U size = D2D1::SizeU(UINT32(std::max<LONG>(client.right, 1)),
                                   UINT32(std::max<LONG>(client.bottom, 1)));
    if (FAILED(surface.target->Resize(&size))) UiDiscardSurface(surface);
}

// EndDraw's result. A lost device (D2DERR_RECREATE_TARGET) or other failure discards the target
// and repaints with a new one. Returns false after repeated failures: the window can't draw.
static bool UiSurfaceDrawResult(UiSurface& surface, HRESULT result) {
    if (SUCCEEDED(result)) {
        surface.failures = 0;
        return true;
    }
    if (result != D2DERR_RECREATE_TARGET) Wh_Log(L"Drawing failed: 0x%08X", unsigned(result));
    UiDiscardSurface(surface);
    InvalidateRect(surface.window, nullptr, FALSE);
    return ++surface.failures < 3;
}
