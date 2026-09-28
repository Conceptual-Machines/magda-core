// The guard that keeps a test run from replacing the developer's settings.
//
// Config::save() writes the whole singleton, and no test loads it first, so any
// save from a test process would overwrite ~/Library/MAGDA/config.json with
// defaults. TestConfigRedirect.cpp points MAGDA_CONFIG_FILE somewhere
// disposable before main(); this asserts it actually took, so the protection
// cannot rot silently.

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AppPaths.hpp"

TEST_CASE("Tests never address the real settings file", "[config][config-path]") {
    const auto configFile = magda::paths::configFile();
    const auto real = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("MAGDA")
                          .getChildFile("config.json");

    REQUIRE(configFile != real);
    REQUIRE_FALSE(configFile.getFullPathName().isEmpty());
}
