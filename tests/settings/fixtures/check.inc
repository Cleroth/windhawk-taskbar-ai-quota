// Assertions and test cases, shared by every target's fixtures.
static int passed = 0, failed = 0, assertions = 0;

#define CHECK(condition) do { ++assertions; if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)
#define CHECK_NEAR(actual, expected) do { ++assertions; double a_ = (actual), e_ = (expected); \
    if (!(std::abs(a_ - e_) < 1e-6)) throw std::runtime_error(std::string("line ") + \
    std::to_string(__LINE__) + ": " #actual " = " + std::to_string(a_) + ", expected " + \
    std::to_string(e_)); } while (false)

static void Run(const std::string& name, const std::function<void()>& body) {
    try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}
