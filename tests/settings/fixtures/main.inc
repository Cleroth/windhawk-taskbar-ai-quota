#include "support.inc"
#include "meter.inc"
#include "logos.inc"
#include "ui.inc"
#include "controls.inc"
#include "tooltip-support.inc"
#include "tooltip.inc"
#include "edits.inc"
#include "accounts.inc"
#include "layout.inc"
#include "display.inc"
#include "render-support.inc"
#include "render.inc"
#include "render-controls.inc"

int main(int argc, char** argv) {
    bool render = argc == 3 && std::string(argv[1]) == "--render-dir";
    if (argc != 1 && !render) {
        std::cerr << "Usage: settings.exe [--render-dir DIRECTORY]\n";
        return 2;
    }
    if (render) {
        std::wstring directory(MAX_PATH * 4, L'\0');
        directory.resize(size_t(std::max(
            MultiByteToWideChar(CP_ACP, 0, argv[2], -1, directory.data(), int(directory.size())) - 1, 0)));
        RunRenders(directory);
        RunControlRenders(directory);
    } else {
        RunMeterTests();
        RunLogoTests();
        RunUiTests();
        RunControlTests();
        RunTooltipTests();
        RunEditTests();
        RunPageTests();
        RunAccountEditTests();
        RunLayoutTests();
        RunDisplayPageTests();
    }
    std::cout << "RESULT " << passed << " passed, " << failed << " failed; " << assertions << " assertions\n";
    return failed ? 1 : 0;
}
