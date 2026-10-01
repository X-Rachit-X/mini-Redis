// Runs every registered TEST and reports the result.

#include "test_framework.h"

int main() {
    int failed_tests = 0;
    for (const TestCase& test : all_tests()) {
        int failures_before = failure_count();
        test.function();
        bool passed = failure_count() == failures_before;
        if (!passed) failed_tests++;
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << test.name << "\n";
    }
    std::cout << "\n" << all_tests().size() - failed_tests << "/" << all_tests().size()
              << " tests passed\n";
    return failed_tests == 0 ? 0 : 1;
}
