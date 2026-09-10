// Which engine a run comes up on (#2559). Three inputs decide it - the
// environment variable, the setting, and the default - and the order between
// them is the whole contract: a bug report asking "does it still happen on the
// other engine" is answered by one run rather than by editing preferences.

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

#include "magda/daw/core/Config.hpp"
#include "magda/daw/engine/AudioEngineChoice.hpp"

using magda::AudioEngineChoice;
using magda::chosenAudioEngine;
using magda::Config;
using magda::parseAudioEngine;
using magda::settingWordFor;

namespace {

/// The variable, set and put back. Windows has no setenv, and _putenv_s with an
/// empty value is how it removes one.
class ScopedEngineVariable {
  public:
    explicit ScopedEngineVariable(const char* value) {
        if (const auto* previous = std::getenv(kName))
            previous_ = previous;
        set(value);
    }

    ~ScopedEngineVariable() {
        set(previous_.isEmpty() ? nullptr : previous_.toRawUTF8());
    }

  private:
    static void set(const char* value) {
#if JUCE_WINDOWS
        _putenv_s(kName, value != nullptr ? value : "");
#else
        if (value != nullptr)
            setenv(kName, value, 1);
        else
            unsetenv(kName);
#endif
    }

    static constexpr const char* kName = "MAGDA_AUDIO_ENGINE";
    juce::String previous_;
};

/// The setting, restored the same way: the singleton outlives one test case.
class ScopedEngineSetting {
  public:
    explicit ScopedEngineSetting(AudioEngineChoice choice)
        : previous_(Config::getInstance().getAudioEngine()) {
        Config::getInstance().setAudioEngine(settingWordFor(choice));
    }

    ~ScopedEngineSetting() {
        Config::getInstance().setAudioEngine(previous_);
    }

  private:
    std::string previous_;
};

}  // namespace

TEST_CASE("The engine's name is the wire format both sides read", "[engine][engine-choice]") {
    CHECK(parseAudioEngine("magda") == AudioEngineChoice::Magda);
    CHECK(parseAudioEngine("tracktion") == AudioEngineChoice::Tracktion);

    // The variable is typed by hand, so it is trimmed and case-folded.
    CHECK(parseAudioEngine("  MAGDA  ") == AudioEngineChoice::Magda);
    CHECK(parseAudioEngine("Tracktion") == AudioEngineChoice::Tracktion);

    // Anything else is not a choice, and the caller decides what to do about it.
    CHECK_FALSE(parseAudioEngine("").has_value());
    CHECK_FALSE(parseAudioEngine("native").has_value());
    CHECK_FALSE(parseAudioEngine("magda engine").has_value());

    // The word the file stores round-trips, which is what a preference set on
    // one run and read on the next depends on.
    CHECK(parseAudioEngine(settingWordFor(AudioEngineChoice::Magda)) == AudioEngineChoice::Magda);
    CHECK(parseAudioEngine(settingWordFor(AudioEngineChoice::Tracktion)) ==
          AudioEngineChoice::Tracktion);
}

TEST_CASE("The setting survives the config file", "[engine][engine-choice]") {
    // The key is spelled twice, once to write and once to read, and a mismatch
    // between them loses the choice without failing anything else. Safe against
    // the developer's own settings: TestConfigRedirect points the path
    // somewhere disposable before main().
    auto& config = Config::getInstance();
    const auto previous = config.getAudioEngine();

    config.setAudioEngine(settingWordFor(AudioEngineChoice::Magda));
    config.save();

    config.setAudioEngine(settingWordFor(AudioEngineChoice::Tracktion));
    config.load();
    CHECK(parseAudioEngine(config.getAudioEngine()) == AudioEngineChoice::Magda);

    config.setAudioEngine(previous);
    config.save();
}

TEST_CASE("An unset preference is the fork", "[engine][engine-choice]") {
    const ScopedEngineVariable variable(nullptr);
    const ScopedEngineSetting setting(AudioEngineChoice::Tracktion);

    CHECK(chosenAudioEngine() == AudioEngineChoice::Tracktion);
}

TEST_CASE("The setting decides when the environment says nothing", "[engine][engine-choice]") {
    const ScopedEngineVariable variable(nullptr);

    {
        const ScopedEngineSetting setting(AudioEngineChoice::Magda);
        CHECK(chosenAudioEngine() == AudioEngineChoice::Magda);
    }
    {
        const ScopedEngineSetting setting(AudioEngineChoice::Tracktion);
        CHECK(chosenAudioEngine() == AudioEngineChoice::Tracktion);
    }
}

TEST_CASE("The environment wins over the setting, both ways", "[engine][engine-choice]") {
    {
        const ScopedEngineSetting setting(AudioEngineChoice::Tracktion);
        const ScopedEngineVariable variable("magda");
        CHECK(chosenAudioEngine() == AudioEngineChoice::Magda);
    }
    {
        const ScopedEngineSetting setting(AudioEngineChoice::Magda);
        const ScopedEngineVariable variable("tracktion");
        CHECK(chosenAudioEngine() == AudioEngineChoice::Tracktion);
    }
}

TEST_CASE("A variable that names neither engine leaves the setting alone",
          "[engine][engine-choice]") {
    const ScopedEngineSetting setting(AudioEngineChoice::Magda);
    const ScopedEngineVariable variable("nonsense");

    CHECK(chosenAudioEngine() == AudioEngineChoice::Magda);
}
