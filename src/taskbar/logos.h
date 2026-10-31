// The XAML element that draws a provider logo from its path data (logo-paths.h).

// The provider's logo shape in its 24x24 viewBox: a Path filled with `fill`, or with a null `fill`
// a PathIcon, which inherits the surrounding text color like a TextBlock. Throws if it fails to
// load.
static FrameworkElement LoadProviderLogoShape(std::wstring const& provider, Brush const& fill) {
    PCWSTR data = provider == L"openai" ? kOpenAiLogoPath :
                  provider == L"anthropic" ? kClaudeLogoPath : kGeminiLogoPath;
    std::wstring xaml = fill ? L"<Path" : L"<PathIcon";
    xaml += L" xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" Data=\"";
    xaml += data;
    xaml += L"\"/>";
    auto logo = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(winrt::hstring(xaml))
                    .as<FrameworkElement>();
    if (fill) logo.as<wuxs::Path>().Fill(fill);
    return logo;
}

// The provider's logo shape in a 24x24 DIP box. Throws if it fails to load.
static Grid LoadProviderLogo(std::wstring const& provider, Brush const& fill) {
    Grid box;
    box.Width(24);
    box.Height(24);
    box.Children().Append(LoadProviderLogoShape(provider, fill));
    return box;
}
