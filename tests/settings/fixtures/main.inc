#include "support.inc"
#include "meter.inc"
#include "logos.inc"

int main(int argc, char**) {
    if (argc != 1) {
        std::cerr << "Usage: settings.exe\n";
        return 2;
    }
    RunMeterTests();
    RunLogoTests();
    std::cout << "RESULT " << passed << " passed, " << failed << " failed; " << assertions << " assertions\n";
    return failed ? 1 : 0;
}
