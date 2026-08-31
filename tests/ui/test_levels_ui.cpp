#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "../magda/daw/ui/components/chain/custom_ui/LevelsUI.hpp"

namespace {

class CountingLevelsTelemetrySource final : public magda::daw::ui::LevelsTelemetrySource {
  public:
    void setActive(bool active) override {
        activationStates.push_back(active);
    }

    void requestReset() override {
        ++resetRequests;
    }

    magda::daw::audio::TrackMeasurementSnapshot snapshot() const override {
        return held;
    }

    std::vector<bool> activationStates;
    int resetRequests = 0;
    magda::daw::audio::TrackMeasurementSnapshot held;
};

}  // namespace

TEST_CASE("LevelsUI keeps an unchanged telemetry source active", "[levels-ui][telemetry]") {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::Component parent;
    auto levels = std::make_unique<magda::daw::ui::LevelsUI>();
    auto firstSource = std::make_shared<CountingLevelsTelemetrySource>();
    auto secondSource = std::make_shared<CountingLevelsTelemetrySource>();

    parent.addAndMakeVisible(*levels);
    levels->setTelemetrySource(firstSource);

    REQUIRE(firstSource->activationStates.size() == 1);
    CHECK(firstSource->activationStates.front());

    levels->setTelemetrySource(firstSource);

    CHECK(firstSource->activationStates.size() == 1);

    levels->setTelemetrySource(secondSource);

    REQUIRE(firstSource->activationStates.size() == 2);
    CHECK_FALSE(firstSource->activationStates.back());
    REQUIRE(secondSource->activationStates.size() == 1);
    CHECK(secondSource->activationStates.front());

    levels->setTelemetrySource(nullptr);

    REQUIRE(secondSource->activationStates.size() == 2);
    CHECK_FALSE(secondSource->activationStates.back());
}

TEST_CASE("LevelsUI reset forwards to the telemetry source and clears held values",
          "[levels-ui][telemetry]") {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::Component parent;
    auto levels = std::make_unique<magda::daw::ui::LevelsUI>();
    auto source = std::make_shared<CountingLevelsTelemetrySource>();

    // Held figures the plugin would still be reporting after the signal stopped.
    source->held.valid = true;
    source->held.integratedLufs = -12.0f;
    source->held.truePeakDb = -1.0f;
    source->held.truePeakValid = true;
    source->held.plr = 11.0f;
    source->held.plrValid = true;

    parent.addAndMakeVisible(*levels);
    levels->setBounds(0, 0, 460, 160);
    levels->setTelemetrySource(source);

    // The Reset control must actually land inside the panel.
    auto* resetButton = levels->getChildComponent(0);
    REQUIRE(resetButton != nullptr);
    CHECK(resetButton->isVisible());
    CHECK(resetButton->isEnabled());
    CHECK_FALSE(resetButton->getBounds().isEmpty());
    CHECK(levels->getLocalBounds().contains(resetButton->getBounds()));

    levels->resetMeasurement();

    CHECK(source->resetRequests == 1);

    // A reset with no source bound must be a no-op rather than a crash.
    levels->setTelemetrySource(nullptr);
    levels->resetMeasurement();

    CHECK(source->resetRequests == 1);
}
