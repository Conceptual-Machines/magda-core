#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <iostream>

/**
 * @brief Main entry point for JUCE unit tests
 *
 * This file provides the main() function for the magda_juce_tests executable.
 * All test classes register themselves automatically via static initialization,
 * so we just need to create a UnitTestRunner and run all registered tests.
 */

int main(int argc, char* argv[]) {
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);

    std::cout << "========================================\n";
    std::cout << "Running MAGDA JUCE Unit Tests\n";
    std::cout << "========================================\n\n";

    runner.runAllTests();

    std::cout << "\n========================================\n";
    std::cout << "Test Results Summary\n";
    std::cout << "========================================\n";

    int numFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        auto* result = runner.getResult(i);
        std::cout << result->unitTestName << ": "
                  << result->passes << " passed, "
                  << result->failures << " failed\n";
        numFailures += result->failures;
    }

    std::cout << "\n========================================\n";

    if (numFailures == 0) {
        std::cout << "All tests PASSED!\n";
    } else {
        std::cout << "FAILED: " << numFailures << " test(s) failed\n";
    }

    std::cout << "========================================\n";

    return numFailures > 0 ? 1 : 0;
}
