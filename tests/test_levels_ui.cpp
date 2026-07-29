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

    magda::daw::audio::TrackMeasurementSnapshot snapshot() const override {
        return {};
    }

    std::vector<bool> activationStates;
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
