// A boolean parameter's checkbox must follow value-only refreshes, not just
// full widget rebuilds (#2072 follow-up). The EQ writes Band N Enabled from
// its own curve view on double-click; the grid cell for that parameter is
// refreshed through ParamSlotComponent::setParamValue, which moved the slider
// and the discrete widgets but left the toggle showing the old tick.

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/components/chain/params/ParamSlotComponent.hpp"

namespace {

using Slot = magda::daw::ui::ParamSlotComponent;

magda::ParameterInfo booleanParam(float currentValue) {
    magda::ParameterInfo param;
    param.paramIndex = 0;
    param.name = "Band 1 Enabled";
    param.scale = magda::ParameterScale::Boolean;
    param.minValue = 0.0f;
    param.maxValue = 1.0f;
    param.currentValue = currentValue;
    return param;
}

// The toggle is a private member, so assert on what the cell actually shows:
// its visible ToggleButton child.
const juce::ToggleButton* visibleToggleIn(const juce::Component& slot) {
    for (int i = 0; i < slot.getNumChildComponents(); ++i) {
        if (const auto* toggle = dynamic_cast<const juce::ToggleButton*>(slot.getChildComponent(i)))
            if (toggle->isVisible())
                return toggle;
    }
    return nullptr;
}

}  // namespace

class ParamSlotBooleanSyncTest final : public juce::UnitTest {
  public:
    ParamSlotBooleanSyncTest() : juce::UnitTest("Param Slot Boolean Sync Tests", "magda") {}

    void runTest() override {
        beginTest("Toggle starts from the parameter's current value");
        {
            Slot slot{0};
            slot.setParameterInfo(booleanParam(1.0f));

            const auto* toggle = visibleToggleIn(slot);
            expect(toggle != nullptr, "A boolean param must show a toggle");
            if (toggle != nullptr)
                expect(toggle->getToggleState(), "A param that is on must start ticked");
        }

        beginTest("Toggle follows a value-only refresh");
        {
            Slot slot{0};
            slot.setParameterInfo(booleanParam(0.0f));

            const auto* toggle = visibleToggleIn(slot);
            expect(toggle != nullptr, "A boolean param must show a toggle");
            if (toggle == nullptr)
                return;

            expect(!toggle->getToggleState(), "A param that is off must start unticked");

            // What a write from the device's own curve view lands on.
            slot.setParamValue(1.0);
            expect(toggle->getToggleState(),
                   "Enabling the band elsewhere must tick the grid checkbox");

            slot.setParamValue(0.0);
            expect(!toggle->getToggleState(),
                   "Disabling the band elsewhere must clear the grid checkbox");
        }
    }
};

static ParamSlotBooleanSyncTest paramSlotBooleanSyncTest;
