// The magda_tests half of this guard lives in test_config_path_redirect.cpp.
// It is Catch2, so it cannot run here, and this target is the one that most
// needs it: the shared-engine tests live here, where a stray Config::save() is
// likeliest. Without a guard in this binary, dropping TestConfigRedirect.cpp
// from the target would silently put it back to writing the developer's real
// config.json.

#include <juce_core/juce_core.h>

#include "magda/daw/core/AppPaths.hpp"

class ConfigPathRedirectTest final : public juce::UnitTest {
  public:
    ConfigPathRedirectTest() : juce::UnitTest("Config Path Redirect Tests", "magda") {}

    void runTest() override {
        beginTest("Tests never address the real settings file");
        {
            const auto configFile = magda::paths::configFile();
            const auto real =
                juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("MAGDA")
                    .getChildFile("config.json");

            expect(configFile != real,
                   "Config would be saved over the developer's real settings file");
            expect(configFile.getFullPathName().isNotEmpty());
        }
    }
};

static ConfigPathRedirectTest configPathRedirectTest;
