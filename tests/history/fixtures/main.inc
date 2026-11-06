#include "support.inc"

int main(int argc, char**) {
    if (argc != 1) {
        std::cerr << "Usage: history.exe\n";
        return 2;
    }

#include "model.inc"
#include "csv.inc"
#include "continuity.inc"
#include "thinning.inc"
#include "burn-up.inc"
#include "journal.inc"
    std::cout << "RESULT " << passed << " passed, " << failed << " failed; " << assertions << " assertions\n";
    return failed ? 1 : 0;
}
