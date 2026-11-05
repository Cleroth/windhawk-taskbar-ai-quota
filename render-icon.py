"""Render the app icon (SettingsAppIcon, src/settings/page-about.h) to icon.png and icon.ico.

Extracts the icon with the settings suite's pure sections it builds on (model, toolkit, settings
page support) from the generated mod, renders each size
natively with Windhawk's bundled clang, Direct2D and WIC, and assembles the ICO from those PNGs.
Stdlib only; build files go to .out/icon/.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys

REPO = Path(__file__).resolve().parent
sys.path.insert(0, str(REPO / "tests" / "settings"))
import extract  # noqa: E402

COMPILER = Path(r"C:\Program Files\Windhawk\Compiler\bin\clang++.exe")
SOURCE = REPO / "local@taskbar-ai-quota.wh.cpp"
BUILD = REPO / ".out" / "icon"
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
# The settings suite's pure sections up to the About page, in its declaration order.
SECTIONS = extract.MODEL[:extract.MODEL.index(("fragment", "settings/page-about.h")) + 1]

DRIVER = r"""
// Renders SettingsAppIcon at each size in argv[2...] into argv[1]\icon-<size>.png: one render per
// size (no scaling), transparent, straight alpha.
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    // Static files can't follow the theme: the themed parts (the tracks, Primary ink at 20%) take
    // one grey that matches them over the settings window's light and dark backgrounds.
    const D2D1_COLOR_F track = UiRgb(0x8C8C8C, 0.42f);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    {
        UiGraphics g;
        UiCreateGraphics(g);
        winrt::com_ptr<IWICImagingFactory> wic;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                              __uuidof(IWICImagingFactory), wic.put_void()));
        for (int arg = 2; arg < argc; arg++) {
            UINT size = UINT(_wtoi(argv[arg]));

            // Drawn premultiplied by a software target at 96 DPI (a DIP is a pixel), as the window
            // icon is.
            winrt::com_ptr<IWICBitmap> bitmap;
            winrt::check_hresult(wic->CreateBitmap(size, size, GUID_WICPixelFormat32bppPBGRA,
                                                   WICBitmapCacheOnLoad, bitmap.put()));
            {
                D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
                winrt::com_ptr<ID2D1RenderTarget> target;
                winrt::check_hresult(g.d2d->CreateWicBitmapRenderTarget(bitmap.get(), &properties, target.put()));
                std::vector<UiItem> items{SettingsAppIcon({0, 0, float(size), float(size)})};
                for (UiShapePart& part : items[0].parts) {
                    if (part.ink != UiInk::Inherit) {
                        part.color = track;
                        part.ink = UiInk::Inherit;
                    }
                }
                UiTargetCache cache;
                target->BeginDraw();
                target->Clear(D2D1_COLOR_F{});
                UiPaintItems(target.get(), g, cache, UiPaletteFor({}), items, {}, {0, 0}, 1, -1e9f, 1e9f);
                winrt::check_hresult(target->EndDraw());
            }

            // Straight alpha, rounded as the window icon's.
            std::vector<BYTE> pixels(size_t(size) * size * 4);
            winrt::check_hresult(bitmap->CopyPixels(nullptr, size * 4, UINT(pixels.size()), pixels.data()));
            for (size_t p = 0; p < pixels.size(); p += 4) {
                if (BYTE alpha = pixels[p + 3]) {
                    for (int c = 0; c < 3; c++) pixels[p + c] = BYTE(std::min(255, (pixels[p + c] * 255 + alpha / 2) / alpha));
                }
            }
            winrt::com_ptr<IWICBitmap> straight;
            winrt::check_hresult(wic->CreateBitmapFromMemory(size, size, GUID_WICPixelFormat32bppBGRA, size * 4,
                                                             UINT(pixels.size()), pixels.data(), straight.put()));

            std::wstring path = std::wstring(argv[1]) + L"\\icon-" + std::to_wstring(size) + L".png";
            winrt::com_ptr<IWICStream> stream;
            winrt::check_hresult(wic->CreateStream(stream.put()));
            winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
            winrt::com_ptr<IWICBitmapEncoder> encoder;
            winrt::check_hresult(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
            winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
            winrt::com_ptr<IWICBitmapFrameEncode> frame;
            winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
            winrt::check_hresult(frame->Initialize(nullptr));
            winrt::check_hresult(frame->SetSize(size, size));
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            winrt::check_hresult(frame->SetPixelFormat(&format));
            if (format != GUID_WICPixelFormat32bppBGRA) return 1;
            winrt::check_hresult(frame->WriteSource(straight.get(), nullptr));
            winrt::check_hresult(frame->Commit());
            winrt::check_hresult(encoder->Commit());
        }
    }
    winrt::uninit_apartment();
    return 0;
}
"""


def main():
    # A stale generated file would render an icon that src/ no longer has.
    subprocess.run(["pwsh", "-NoProfile", "-File", str(REPO / "generate-mod.ps1"), "-Check"], check=True)
    BUILD.mkdir(parents=True, exist_ok=True)

    # Production fragments verbatim (diagnostics point at the generated file), then the driver.
    text = SOURCE.read_bytes().decode("utf-8-sig")
    chunks = ['#include "harness.hpp"\n']
    for kind, name in SECTIONS:
        a, b = extract.fragment(text, name) if kind == "fragment" else extract.definition(text, name)
        chunks.append(f"\n#line {text.count(chr(10), 0, a) + 1} {json.dumps(SOURCE.as_posix())}\n{text[a:b]}\n")
    chunks.append(f'\n#line 1 "render-icon.py: DRIVER"{DRIVER}')
    cpp, exe = BUILD / "render-icon.cpp", BUILD / "render-icon.exe"
    cpp.write_text("".join(chunks), encoding="utf-8", newline="\n")
    subprocess.run([str(COMPILER), "--target=x86_64-w64-mingw32", "-std=c++23", "-DUNICODE", "-D_UNICODE",
                    "-O0", "-static", "-municode", "-I", str(REPO / "tests" / "settings"), str(cpp), "-o",
                    str(exe), "-ld2d1", "-ldwrite", "-lwindowscodecs", "-lole32", "-loleaut32", "-lruntimeobject"],
                   check=True)
    subprocess.run([str(exe), str(BUILD), *map(str, SIZES)], check=True)

    # ICO: a directory of PNG-compressed entries (256 is stored as 0).
    images = [(BUILD / f"icon-{size}.png").read_bytes() for size in SIZES]
    offset = 6 + 16 * len(images)
    entries = []
    for size, png in zip(SIZES, images):
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset))
        offset += len(png)
    (REPO / "icon.ico").write_bytes(struct.pack("<HHH", 0, 1, len(images)) + b"".join(entries) + b"".join(images))
    (REPO / "icon.png").write_bytes(images[SIZES.index(256)])
    print(f"Wrote icon.png (256 px) and icon.ico ({', '.join(map(str, SIZES))} px)")


if __name__ == "__main__":
    main()
