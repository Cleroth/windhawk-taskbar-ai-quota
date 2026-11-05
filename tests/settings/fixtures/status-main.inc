// The provider status target: the status pages' parser against captured pages and variants of
// them, and the read rules (fetch/status-feeds.h).
#include "check.inc"
#include "status.inc"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: status.exe SAMPLES_DIRECTORY\n";
        return 2;
    }
    g_statusSamples = std::filesystem::path(argv[1]);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    RunStatusTests();
    std::cout << "RESULT " << passed << " passed, " << failed << " failed; " << assertions << " assertions\n";
    winrt::uninit_apartment();
    return failed ? 1 : 0;
}
