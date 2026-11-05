// Details flyout card materials drawn with composition (Mica, Mica Alt, Tinted wallpaper): one
// effect brush over the system's blurred wallpaper, in a visual behind the card's content.

// Direct2D description of an effect graph node, read by Compositor::CreateEffectFactory. From
// windows.graphics.effects.interop.h, which the bundled headers lack.
MIDL_INTERFACE("2FC57384-A068-44D7-A331-30982FCF7177")
IGraphicsEffectD2D1Interop : ::IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) = 0;
    // `mapping` receives a GRAPHICS_EFFECT_PROPERTY_MAPPING.
    virtual HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR name, UINT* index,
                                                              int* mapping) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) = 0;
    // Receives an IPropertyValue.
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UINT index, void** value) = 0;
    // Receives an IGraphicsEffectSource.
    virtual HRESULT STDMETHODCALLTYPE GetSource(UINT index, void** source) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) = 0;
};
__CRT_UUID_DECL(IGraphicsEffectD2D1Interop, 0x2fc57384, 0xa068, 0x44d7, 0xa3, 0x31, 0x30, 0x98,
                0x2f, 0xcf, 0x71, 0x77)

// One node of an effect graph: a Direct2D effect, its properties in the effect's property index
// order and its inputs, fixed at construction. `live` counts its graph's nodes alive.
struct FlyoutEffectNode
    : winrt::implements<FlyoutEffectNode, winrt::Windows::Graphics::Effects::IGraphicsEffect,
                        winrt::Windows::Graphics::Effects::IGraphicsEffectSource,
                        IGraphicsEffectD2D1Interop, winrt::no_weak_ref> {
    std::shared_ptr<std::atomic<int>> live;
    GUID id;
    std::vector<winrt::Windows::Foundation::IPropertyValue> properties;
    std::vector<winrt::Windows::Graphics::Effects::IGraphicsEffectSource> sources;
    winrt::hstring name;

    FlyoutEffectNode(
        std::shared_ptr<std::atomic<int>> graphLive, GUID const& effect,
        std::initializer_list<winrt::Windows::Foundation::IInspectable> values,
        std::initializer_list<winrt::Windows::Graphics::Effects::IGraphicsEffectSource> inputs)
        : live(std::move(graphLive)), id(effect), sources(inputs) {
        for (auto const& value : values) {
            properties.push_back(value.as<winrt::Windows::Foundation::IPropertyValue>());
        }
        // Last, so a throwing constructor leaves the count alone.
        (*live)++;
    }
    ~FlyoutEffectNode() { (*live)--; }

    // IGraphicsEffect
    winrt::hstring Name() const { return name; }
    void Name(winrt::hstring const& value) { name = value; }

    // IGraphicsEffectD2D1Interop
    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* out) noexcept override {
        if (!out) return E_POINTER;
        *out = id;
        return S_OK;
    }
    // No property is animatable.
    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR, UINT*, int*) noexcept override {
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) noexcept override {
        if (!count) return E_POINTER;
        *count = (UINT)properties.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetProperty(UINT index, void** value) noexcept override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (index >= properties.size()) return E_INVALIDARG;
        winrt::copy_to_abi(properties[index], *value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSource(UINT index, void** source) noexcept override {
        if (!source) return E_POINTER;
        *source = nullptr;
        if (index >= sources.size()) return E_INVALIDARG;
        winrt::copy_to_abi(sources[index], *source);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) noexcept override {
        if (!count) return E_POINTER;
        *count = (UINT)sources.size();
        return S_OK;
    }
};

// Logs the first failure only.
static void LogFlyoutMaterialFailure(PCWSTR reason, int32_t error) {
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
        Wh_Log(L"Flyout material unavailable (%s, 0x%08X); using its opaque color", reason,
               (unsigned)error);
    }
}

// Adds the popup theme's Mica, MicaAlt or TintedWallpaper material to `host`, an empty element
// spanning the card inside its border, clipped to `cornerRadius`; XAML positions, clips and
// animates it with the card. It stays hidden over the card's opaque fallback until
// SettleFlyoutMaterial shows it. Its effect factory is reused from `fo` when its key matches, else
// built and kept there. Nothing is added without a blurred wallpaper brush or on a failure
// building it.
static void AttachFlyoutMaterial(FlyoutUi& fo, UIElement const& host, float cornerRadius) {
    namespace wuc = winrt::Windows::UI::Composition;
    using winrt::Windows::Foundation::IInspectable;
    using winrt::Windows::Foundation::PropertyValue;
    using winrt::Windows::Graphics::Effects::IGraphicsEffect;
    using winrt::Windows::Graphics::Effects::IGraphicsEffectSource;
    using winrt::Windows::UI::Xaml::Hosting::ElementCompositionPreview;
    // Direct2D effects (d2d1effects.h, d2d1effects_2.h) and the property values used.
    static constexpr GUID kFlood{0x61c23c20, 0xae69, 0x4d8e,
                                 {0x94, 0xcf, 0x50, 0x07, 0x8d, 0xf6, 0x38, 0xf2}};
    static constexpr GUID kBlend{0x81c5b77b, 0x13f8, 0x4cdd,
                                 {0xad, 0x20, 0xc8, 0x90, 0x54, 0x7a, 0xc6, 0x5d}};
    static constexpr GUID kComposite{0x48fc9f51, 0xf6ac, 0x48f1,
                                     {0x8b, 0x58, 0x3b, 0x28, 0xac, 0x46, 0xf7, 0x6d}};
    constexpr uint32_t kBlendColor = 22;       // D2D1_BLEND_MODE_COLOR
    constexpr uint32_t kBlendLuminosity = 23;  // D2D1_BLEND_MODE_LUMINOSITY
    constexpr uint32_t kSourceOver = 0;        // D2D1_COMPOSITE_MODE_SOURCE_OVER
    FlyoutPopup& popup = fo.popup;
    FlyoutTheme const& theme = popup.theme;
    // CreateEffectFactory copies the graph. Once system code keeps a node, which it could release
    // after the mod unloads, no more nodes are handed out.
    static std::atomic<bool> nodesRetained{false};
    if (g_unloading || nodesRetained) return;
    std::shared_ptr<std::atomic<int>> live;
    try {
        live = std::make_shared<std::atomic<int>>(0);
        wuc::Visual hostVisual = ElementCompositionPreview::GetElementVisual(host);
        wuc::Compositor compositor = hostVisual.Compositor();
        wuc::CompositionBrush wallpaper = compositor.TryCreateBlurredWallpaperBackdropBrush();
        if (!wallpaper) {
            LogFlyoutMaterialFailure(L"no blurred wallpaper brush", S_OK);  // i18n-en: log text
            return;
        }

        if (fo.materialFactory && (fo.materialFactoryMaterial != theme.material ||
                                   fo.materialFactoryLight != theme.light ||
                                   fo.materialFactory.Compositor() != compositor)) {
            fo.materialFactory = nullptr;
        }
        if (!fo.materialFactory) {
            wuc::CompositionEffectFactory factory{nullptr};
            {
                auto node = [&](GUID const& effect, std::initializer_list<IInspectable> values,
                                std::initializer_list<IGraphicsEffectSource> inputs) {
                    return winrt::make<FlyoutEffectNode>(live, effect, values, inputs);
                };
                // The tint at an opacity, as its straight alpha.
                auto tint = [&](float opacity) {
                    winrt::Windows::UI::Color c = theme.materialTint;
                    float rgba[] = {c.R / 255.f, c.G / 255.f, c.B / 255.f, opacity * c.A / 255.f};
                    return node(kFlood, {PropertyValue::CreateSingleArray(rgba)}, {});
                };
                wuc::CompositionEffectSourceParameter backdrop(L"Wallpaper");  // i18n-en: effect parameter
                IGraphicsEffect graph{nullptr};
                if (theme.material == Material::TintedWallpaper) {
                    graph = node(kComposite, {PropertyValue::CreateUInt32(kSourceOver)},
                                 {backdrop, tint(theme.materialTintOpacity)});
                } else {
                    // Mica: the luminosity flood blended over the wallpaper, then the tint over
                    // that. Composition swaps the Color and Luminosity blend modes, so the
                    // luminosity step passes Color and the tint step Luminosity, as
                    // MicaController does.
                    IGraphicsEffect luminosity =
                        node(kBlend, {PropertyValue::CreateUInt32(kBlendColor)},
                             {backdrop, tint(theme.materialLuminosityOpacity)});
                    graph = node(kBlend, {PropertyValue::CreateUInt32(kBlendLuminosity)},
                                 {luminosity, tint(theme.materialTintOpacity)});
                }
                factory = compositor.CreateEffectFactory(graph);
            }
            if (*live != 0) {
                nodesRetained = true;
                LogFlyoutMaterialFailure(L"effect graph retained", S_OK);  // i18n-en: log text
                return;
            }
            fo.materialFactory = factory;
            fo.materialFactoryMaterial = theme.material;
            fo.materialFactoryLight = theme.light;
        }
        auto status = fo.materialFactory.LoadStatus();
        if (status != wuc::CompositionEffectFactoryLoadStatus::Success &&
            status != wuc::CompositionEffectFactoryLoadStatus::Pending) {
            LogFlyoutMaterialFailure(L"effect failed to load",  // i18n-en: log text
                                     fo.materialFactory.ExtendedError().value);
            fo.materialFactory = nullptr;
            return;
        }

        wuc::CompositionEffectBrush brush = fo.materialFactory.CreateBrush();
        brush.SetSourceParameter(L"Wallpaper", wallpaper);  // i18n-en: effect parameter
        wuc::SpriteVisual sprite = compositor.CreateSpriteVisual();
        sprite.Brush(brush);
        sprite.IsVisible(false);
        // The host's layout size, for the sprite and its rounded clip.
        wuc::ExpressionAnimation size = compositor.CreateExpressionAnimation(L"host.Size");
        size.SetReferenceParameter(L"host", hostVisual);
        sprite.StartAnimation(L"Size", size);  // i18n-en: property name
        wuc::CompositionRoundedRectangleGeometry outline =
            compositor.CreateRoundedRectangleGeometry();
        outline.CornerRadius({cornerRadius, cornerRadius});
        outline.StartAnimation(L"Size", size);  // i18n-en: property name
        sprite.Clip(compositor.CreateGeometricClip(outline));
        ElementCompositionPreview::SetElementChildVisual(host, sprite);
        popup.materialVisual = sprite;
    } catch (winrt::hresult_error const& e) {
        if (live && *live != 0) nodesRetained = true;
        fo.materialFactory = nullptr;
        LogFlyoutMaterialFailure(L"exception", e.code().value);
    } catch (...) {
        if (live && *live != 0) nodesRetained = true;
        fo.materialFactory = nullptr;
        LogFlyoutMaterialFailure(L"exception", E_FAIL);
    }
}

// Once the material's effect has compiled (asynchronously, after its factory is built), shows the
// material and drops the card's opaque fallback in the same update: a background fading under the
// material would show through it mid-animation. With a compiled factory that is the open itself.
// An effect that fails to compile stays hidden over the fallback, and its factory is dropped so a
// later open rebuilds it. Polled while the popup is open; a no-op once settled.
static void SettleFlyoutMaterial(FlyoutUi& fo) {
    using winrt::Windows::UI::Composition::CompositionEffectFactoryLoadStatus;
    FlyoutPopup& popup = fo.popup;
    if (!popup.materialVisual) return;
    try {
        auto status = fo.materialFactory ? fo.materialFactory.LoadStatus() :
                                           CompositionEffectFactoryLoadStatus::Other;
        if (status == CompositionEffectFactoryLoadStatus::Pending) return;
        if (status == CompositionEffectFactoryLoadStatus::Success) {
            Border card = popup.card.try_as<Border>();
            Brush transparent = FlyoutBrush(winrt::Windows::UI::Colors::Transparent());
            popup.materialVisual.IsVisible(true);
            if (card) card.Background(transparent);
        } else if (fo.materialFactory) {
            LogFlyoutMaterialFailure(L"effect failed to compile",  // i18n-en: log text
                                     fo.materialFactory.ExtendedError().value);
            fo.materialFactory = nullptr;
        }
    } catch (winrt::hresult_error const& e) {
        fo.materialFactory = nullptr;
        LogFlyoutMaterialFailure(L"exception", e.code().value);
    } catch (...) {
        fo.materialFactory = nullptr;
        LogFlyoutMaterialFailure(L"exception", E_FAIL);
    }
    popup.materialVisual = nullptr;
}
