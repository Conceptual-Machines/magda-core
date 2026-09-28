// The value axis an automation lane header paints: which ticks a parameter
// gets, and which of them survive a lane too short to label them all.
//
// The builder is a chain of eight cases picked by scale, target and which
// label source the parameter carries, and nothing in either suite paints a
// lane header, so a wrong branch would ship as silently wrong labels.

#include <juce_gui_basics/juce_gui_basics.h>

#include <ranges>
#include <vector>

#include "magda/daw/core/RangesHelpers.hpp"
#include "magda/daw/ui/components/automation/AutomationLaneHeader.hpp"

namespace {

using magda::AutomationGridTick;

std::vector<juce::String> labelsOf(const std::vector<AutomationGridTick>& ticks) {
    const auto labelOf = [](const AutomationGridTick& tick) { return tick.second; };
    return ticks | std::views::transform(labelOf) | magda::toStd<std::vector<juce::String>>();
}

magda::ParameterInfo linearParam(float minValue, float maxValue, const juce::String& unit) {
    magda::ParameterInfo param;
    param.scale = magda::ParameterScale::Linear;
    param.minValue = minValue;
    param.maxValue = maxValue;
    param.teMinValue = minValue;
    param.teMaxValue = maxValue;
    param.unit = unit;
    return param;
}

magda::AutomationTarget targetOfKind(magda::ControlTarget::Kind kind) {
    magda::AutomationTarget target;
    target.kind = kind;
    return target;
}

std::vector<AutomationGridTick> ticksFor(const magda::ParameterInfo& param) {
    return magda::automationGridTicks(targetOfKind(magda::ControlTarget::Kind::PluginParam), param);
}

}  // namespace

class AutomationGridTicksTest final : public juce::UnitTest {
  public:
    AutomationGridTicksTest() : juce::UnitTest("Automation Grid Ticks Tests", "magda") {}

    void runTest() override {
        beginTest("A pan lane is labelled L to R, whatever its range says");
        {
            const auto ticks = magda::automationGridTicks(
                targetOfKind(magda::ControlTarget::Kind::TrackPan), linearParam(-1.0f, 1.0f, ""));

            expect(labelsOf(ticks) == std::vector<juce::String>{"R", "50R", "C", "50L", "L"},
                   "The pan lane reads top-down R, 50R, C, 50L, L");
        }

        beginTest("A switch gets two positions, not a percentage grid");
        {
            magda::ParameterInfo param;
            param.scale = magda::ParameterScale::Boolean;
            param.minValue = 0.0f;
            param.maxValue = 1.0f;

            const auto ticks = ticksFor(param);

            expect(labelsOf(ticks) == std::vector<juce::String>{"On", "Off"});
        }

        beginTest("A fader is labelled in dB, loudest first");
        {
            magda::ParameterInfo param;
            param.scale = magda::ParameterScale::FaderDB;
            param.minValue = -60.0f;
            param.maxValue = 6.0f;
            param.teMinValue = -60.0f;
            param.teMaxValue = 6.0f;

            const auto ticks = ticksFor(param);

            expectEquals(static_cast<int>(ticks.size()), 9);
            expectEquals(ticks.front().second, juce::String("6"));
            expectEquals(ticks.back().second, juce::String("-48"));
        }

        beginTest("A bipolar parameter signs its labels and carries its unit");
        {
            auto param = linearParam(-12.0f, 12.0f, "dB");

            const auto ticks = ticksFor(param);

            expect(labelsOf(ticks) ==
                       std::vector<juce::String>{"+12dB", "+6dB", "0dB", "-6dB", "-12dB"},
                   "Bipolar ticks run +max, +half, 0, -half, -max");
            expectWithinAbsoluteError(ticks[2].first, 0.5, 1.0e-6);
        }

        beginTest("A discrete parameter uses its choices, one tick each");
        {
            magda::ParameterInfo param;
            param.scale = magda::ParameterScale::Discrete;
            param.choices = {"6 dB", "12 dB", "24 dB"};
            param.minValue = 0.0f;
            param.maxValue = 2.0f;
            param.teMinValue = 0.0f;
            param.teMaxValue = 2.0f;

            const auto ticks = ticksFor(param);

            expect(labelsOf(ticks) == std::vector<juce::String>{"6 dB", "12 dB", "24 dB"});
        }

        beginTest("Curated labelTicks win over the choices behind them");
        {
            magda::ParameterInfo param;
            param.scale = magda::ParameterScale::Discrete;
            param.choices = {"1/4", "1/4T", "1/8", "1/8T"};
            param.labelTicks = {{0.0f, "1/4"}, {2.0f, "1/8"}};
            param.minValue = 0.0f;
            param.maxValue = 3.0f;
            param.teMinValue = 0.0f;
            param.teMaxValue = 3.0f;

            const auto ticks = ticksFor(param);

            expect(labelsOf(ticks) == std::vector<juce::String>{"1/4", "1/8"},
                   "The triplet entries the parameter left out must stay out");
        }

        beginTest("A unipolar parameter with a unit is labelled in that unit");
        {
            const auto ticks = ticksFor(linearParam(0.0f, 100.0f, "%"));

            expect(labelsOf(ticks) == std::vector<juce::String>{"0%", "25%", "50%", "75%", "100%"});
        }

        beginTest("A parameter with nothing to label falls back to percentages");
        {
            const auto ticks = ticksFor(linearParam(0.0f, 1.0f, ""));

            expectEquals(static_cast<int>(ticks.size()), 9);
            expectEquals(ticks.front().second, juce::String("10%"));
            expectEquals(ticks.back().second, juce::String("90%"));
        }

        beginTest("A lane with room for every label keeps them all");
        {
            const auto ticks = ticksFor(linearParam(0.0f, 100.0f, "%"));

            expect(magda::thinAutomationGridTicks(ticks, 5) == ticks);
            expect(magda::thinAutomationGridTicks(ticks, 99) == ticks);
        }

        beginTest("Thinning keeps both endpoints and spaces the rest");
        {
            const auto ticks = ticksFor(linearParam(0.0f, 100.0f, "%"));

            const auto thinned = magda::thinAutomationGridTicks(ticks, 3);

            expect(labelsOf(thinned) == std::vector<juce::String>{"0%", "50%", "100%"});
        }

        beginTest("One slot shows the middle label, not an edge");
        {
            const auto ticks = ticksFor(linearParam(0.0f, 100.0f, "%"));

            const auto thinned = magda::thinAutomationGridTicks(ticks, 1);

            expect(labelsOf(thinned) == std::vector<juce::String>{"50%"});
        }
    }
};

static AutomationGridTicksTest automationGridTicksTest;
