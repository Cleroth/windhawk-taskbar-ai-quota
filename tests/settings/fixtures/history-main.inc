// The usage history window's target: its view, formats, chart, controller (live refresh and
// Enable recording included) and frame builders, the tooltip's promises in its formats, and
// offscreen renders.
#include "support.inc"
#include "history-view.inc"
#include "history-controller.inc"
#include "history-frame.inc"
#include "history-refresh.inc"
#include "tooltip-support.inc"
#include "render-support.inc"
#include "history-tooltip.inc"
#include "history-chart.inc"
#include "history-render.inc"

int main(int argc, char** argv) {
    bool render = argc == 3 && std::string(argv[1]) == "--render-dir";
    if (argc != 1 && !render) {
        std::cerr << "Usage: history.exe [--render-dir DIRECTORY]\n";
        return 2;
    }
    winrt::check_hresult(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (render) {
        std::wstring directory(MAX_PATH * 4, L'\0');
        directory.resize(size_t(std::max(
            MultiByteToWideChar(CP_ACP, 0, argv[2], -1, directory.data(), int(directory.size())) - 1, 0)));
        RunHistoryRenders(directory);
        RunHistoryTooltipRenders(directory);
    } else {
        RunHistoryViewTests();
        RunHistoryControllerTests();
        RunHistoryFrameTests();
        RunHistoryRefreshTests();
        RunHistoryTooltipTests();
        RunHistoryChartTests();
    }
    std::cout << "RESULT " << passed << " passed, " << failed << " failed; " << assertions << " assertions\n";
    CoUninitialize();
    return failed ? 1 : 0;
}
