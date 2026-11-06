#include "journal-support.inc"

int main(int argc, char**) {
    if (argc != 1) {
        std::cerr << "Usage: journal.exe (synthetic journal files under journal-files\\ of the working folder)\n";
        return 2;
    }
    g_testRoot = std::filesystem::current_path() / "journal-files";
    std::error_code error;
    std::filesystem::remove_all(g_testRoot, error);

#include "journal-file.inc"
#include "journal-rewrite.inc"
#include "journal-window.inc"
    std::cout << "RESULT " << passed << " passed, " << failed << " failed; " << assertions << " assertions\n";
    return failed ? 1 : 0;
}
