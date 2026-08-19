#pragma once

#include <sstream>
#include <string>

/**
 * @file TextDifference.hpp
 * @brief The first line two canonical dumps disagree on.
 *
 * Shared by the plan goldens (#2076) and the DAWproject cross-check (#2080),
 * which compare the same kind of text for the same reason: a failure should
 * name a decision, not print two documents at each other.
 */

namespace magda {

/// The first differing line of @p expected and @p actual, or an empty string.
inline std::string firstDifference(const std::string& expected, const std::string& actual) {
    std::istringstream expectedLines(expected);
    std::istringstream actualLines(actual);

    std::string expectedLine;
    std::string actualLine;
    int number = 1;

    while (true) {
        const bool haveExpected = static_cast<bool>(std::getline(expectedLines, expectedLine));
        const bool haveActual = static_cast<bool>(std::getline(actualLines, actualLine));

        if (!haveExpected && !haveActual)
            return {};

        std::ostringstream difference;
        difference << "line " << number << ": ";

        if (!haveExpected) {
            difference << "expected end, got '" << actualLine << "'";
            return difference.str();
        }
        if (!haveActual) {
            difference << "expected '" << expectedLine << "', got end";
            return difference.str();
        }
        if (expectedLine != actualLine) {
            difference << "\n  expected: " << expectedLine << "\n  actual:   " << actualLine;
            return difference.str();
        }

        ++number;
    }
}

}  // namespace magda
