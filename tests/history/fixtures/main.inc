#include "support.inc"
#include "interaction.inc"
#include "tooltip.inc"

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--capture-dir") {
        captureDir = std::filesystem::canonical(argv[2]);
        // The runner sets cwd to its unique output directory. Never reuse another run's files.
        if (captureDir != std::filesystem::current_path()) {
            std::cerr << "Captures must stay in this run's working directory\n";
            return 2;
        }
    } else if (argc != 1) {
        std::cerr << "Usage: history.exe [--capture-dir RUN_DIRECTORY]\n";
        return 2;
    }

#include "model.inc"
#include "continuity.inc"
#include "paint.inc"
    RunInteractionTests();
    RunTooltipTests();
    Run("paint lifetime calls balanced", [] { CHECK(beginPaintCalls > 0); CHECK(beginPaintCalls == endPaintCalls); });
    std::cout << "RESULT " << passed << " passed, " << failed << " failed; " << assertions << " assertions\n";
    return failed ? 1 : 0;
}
