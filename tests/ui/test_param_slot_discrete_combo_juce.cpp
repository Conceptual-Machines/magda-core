// A discrete parameter's dropdown must not write the parameter when the cell
// is rebuilt, and must show / write the choice the parameter's value actually
// selects.
//
// Serum loaded with a saved parameter config that marks Bend Up, Bend Down,
// Transpose, A Octave and A Semi as discrete came back transposed after a view
// switch. Slots are reused across rebuilds; configureDiscreteCombo cleared the
// combo with ComboBox::clear(), whose default notification is async, so the
// second configure queued an onChange that fired after the choices were
// re-added and the current one re-selected, and wrote that selection back.
// With the index read as round(0.54167) = 1 and written back as 1.0, an
// octave control landed on its rail.

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "magda/daw/core/ParameterUtils.hpp"
#include "magda/daw/ui/components/chain/params/ParamSlotComponent.hpp"

namespace {

using Slot = magda::daw::ui::ParamSlotComponent;

// Serum's "Bend Up" as the saved config describes it: 49 semitone choices
// over a display range of [0, 48], while TE keeps the parameter normalized and
// the model holds that normalized value. +2 semitones is 26/48.
magda::ParameterInfo externalDiscreteParam(float currentValue) {
    magda::ParameterInfo param;
    param.paramIndex = 7;
    param.name = "Bend Up";
    param.scale = magda::ParameterScale::Discrete;
    for (int semitones = -24; semitones <= 24; ++semitones)
        param.choices.push_back(juce::String(semitones));
    param.minValue = 0.0f;
    param.maxValue = 48.0f;
    param.teMinValue = 0.0f;
    param.teMaxValue = 1.0f;
    param.displayText = std::make_shared<magda::ParameterInfo::DisplayTextProvider>();
    param.currentValue = currentValue;
    return param;
}

// An internal device's discrete parameter: the model value is the choice index.
magda::ParameterInfo internalDiscreteParam(float currentValue) {
    magda::ParameterInfo param;
    param.paramIndex = 0;
    param.name = "Slope";
    param.scale = magda::ParameterScale::Discrete;
    param.choices = {"6 dB", "12 dB", "24 dB"};
    param.minValue = 0.0f;
    param.maxValue = 2.0f;
    param.teMinValue = 0.0f;
    param.teMaxValue = 2.0f;
    param.currentValue = currentValue;
    return param;
}

// The combo is a private member, so assert on what the cell actually shows:
// its visible ComboBox child.
juce::ComboBox* visibleComboIn(juce::Component& slot) {
    for (int i = 0; i < slot.getNumChildComponents(); ++i) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(slot.getChildComponent(i)))
            if (combo->isVisible())
                return combo;
    }
    return nullptr;
}

void drainMessageQueue() {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}

}  // namespace

class ParamSlotDiscreteComboTest final : public juce::UnitTest {
  public:
    ParamSlotDiscreteComboTest() : juce::UnitTest("Param Slot Discrete Combo Tests", "magda") {}

    void runTest() override {
        const float plusTwo = 26.0f / 48.0f;

        beginTest("Rebuilding a discrete cell does not write the parameter");
        {
            Slot slot{0};
            std::vector<double> writes;
            slot.onValueChanged = [&writes](double v) { writes.push_back(v); };

            // First configure, then the rebuild every view / track switch and
            // setNodePath performs on the same, reused slot.
            slot.setParameterInfo(externalDiscreteParam(plusTwo));
            drainMessageQueue();
            slot.setParameterInfo(externalDiscreteParam(plusTwo));
            drainMessageQueue();

            expectEquals(static_cast<int>(writes.size()), 0,
                         "Reconfiguring the dropdown must not write the parameter");
        }

        beginTest("The dropdown shows the choice the plugin is at");
        {
            Slot slot{0};
            slot.setParameterInfo(externalDiscreteParam(plusTwo));

            auto* combo = visibleComboIn(slot);
            expect(combo != nullptr, "A discrete param with choices must show a dropdown");
            if (combo != nullptr) {
                expectEquals(combo->getSelectedItemIndex(), 26);
                expectEquals(combo->getText(), juce::String("2"));
            }

            // A value-only refresh (automation / plugin UI echo) moves it too.
            slot.setParamValue(0.0);
            if (combo != nullptr)
                expectEquals(combo->getText(), juce::String("-24"));
        }

        beginTest("Picking a choice writes the value the plugin wants for it");
        {
            Slot slot{0};
            std::vector<double> writes;
            slot.onValueChanged = [&writes](double v) { writes.push_back(v); };
            slot.setParameterInfo(externalDiscreteParam(plusTwo));

            auto* combo = visibleComboIn(slot);
            expect(combo != nullptr);
            if (combo == nullptr)
                return;

            combo->setSelectedItemIndex(27, juce::sendNotificationSync);  // "+3"
            expectEquals(static_cast<int>(writes.size()), 1);
            if (!writes.empty())
                expectWithinAbsoluteError(writes.back(), 27.0 / 48.0, 1.0e-5);
        }

        beginTest("An internal discrete parameter still carries the choice index");
        {
            Slot slot{0};
            std::vector<double> writes;
            slot.onValueChanged = [&writes](double v) { writes.push_back(v); };
            slot.setParameterInfo(internalDiscreteParam(1.0f));

            auto* combo = visibleComboIn(slot);
            expect(combo != nullptr);
            if (combo == nullptr)
                return;

            expectEquals(combo->getSelectedItemIndex(), 1);
            combo->setSelectedItemIndex(2, juce::sendNotificationSync);
            expectEquals(static_cast<int>(writes.size()), 1);
            if (!writes.empty())
                expectWithinAbsoluteError(writes.back(), 2.0, 1.0e-9);
        }
    }
};

static ParamSlotDiscreteComboTest paramSlotDiscreteComboTest;
