#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstdlib>
#include <iostream>

#include "JuceTestStateGuard.hpp"

/**
 * @brief Main entry point for JUCE unit tests
 *
 * This file provides the main() function for the magda_juce_tests executable.
 * All test classes register themselves automatically via static initialization,
 * so we just need to create a UnitTestRunner and run all registered tests.
 */

int main(int argc, char* argv[]) {
    // Initialize JUCE GUI subsystem - required for message loop, timers, async updaters, etc.
    // This must be alive for the entire test run to avoid SIGSEGV from singleton cleanup issues
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);

    std::cout << "========================================\n";
    std::cout << "Running MAGDA JUCE Unit Tests\n";
    std::cout << "========================================\n\n";

    int numFailures = 0;
    const auto tests = argc > 1 ? juce::UnitTest::getTestsWithName(argv[1])
                                : juce::UnitTest::getTestsInCategory("magda");

    // UnitTestRunner normally runs every suite in one loop. Running one suite
    // at a time lets us enforce the async teardown boundary between suites.
    for (auto* test : tests) {
        magda::test::cleanJuceTestState();

        juce::Array<juce::UnitTest*> singleTest;
        singleTest.add(test);
        runner.runTests(singleTest);

        for (int i = 0; i < runner.getNumResults(); ++i) {
            auto* result = runner.getResult(i);
            std::cout << result->unitTestName << ": " << result->passes << " passed, "
                      << result->failures << " failed\n";
            numFailures += result->failures;
        }

        magda::test::cleanJuceTestState();
    }

    std::cout << "\n========================================\n";
    std::cout << "Test Results Summary\n";
    std::cout << "========================================\n";

    std::cout << "\n========================================\n";

    if (numFailures == 0) {
        std::cout << "All tests PASSED!\n";
    } else {
        std::cout << "FAILED: " << numFailures << " test(s) failed\n";
    }

    std::cout << "========================================\n";

    // Use std::_Exit() to avoid SIGSEGV during static destruction of TE/JUCE singletons.
    // All test results have already been collected and printed above.
    std::cout.flush();
    int exitCode = numFailures > 0 ? 1 : 0;
    std::_Exit(exitCode);
}
