#include "MacroPanelComponent.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

MacroPanelComponent::MacroPanelComponent() : PagedControlPanel(8) {
    // Default 8 macros - will be updated when setMacros is called
    ensureKnobCount(magda::NUM_MACROS);
}

void MacroPanelComponent::ensureKnobCount(int count) {
    // Add new knobs if needed
    while (static_cast<int>(knobs_.size()) < count) {
        int i = static_cast<int>(knobs_.size());
        auto knob = std::make_unique<MacroKnobComponent>(i);

        // Wire up callbacks with macro index
        knob->onValueChanged = [this, i](float value) {
            if (onMacroValueChanged) {
                onMacroValueChanged(i, value);
            }
        };

        knob->onTargetChanged = [this, i](magda::MacroTarget target) {
            if (onMacroTargetChanged) {
                onMacroTargetChanged(i, target);
            }
        };

        knob->onNameChanged = [this, i](juce::String name) {
            if (onMacroNameChanged) {
                onMacroNameChanged(i, name);
            }
        };

        knob->setAvailableTargets(availableDevices_);
        addAndMakeVisible(*knob);
        knobs_.push_back(std::move(knob));
    }
}

void MacroPanelComponent::setMacros(const magda::MacroArray& macros) {
    ensureKnobCount(static_cast<int>(macros.size()));

    for (size_t i = 0; i < macros.size() && i < knobs_.size(); ++i) {
        knobs_[i]->setMacroInfo(macros[i]);
    }

    resized();
    repaint();
}

void MacroPanelComponent::setAvailableDevices(
    const std::vector<std::pair<magda::DeviceId, juce::String>>& devices) {
    availableDevices_ = devices;
    for (auto& knob : knobs_) {
        knob->setAvailableTargets(devices);
    }
}

int MacroPanelComponent::getTotalItemCount() const {
    return static_cast<int>(knobs_.size());
}

juce::Component* MacroPanelComponent::getItemComponent(int index) {
    if (index >= 0 && index < static_cast<int>(knobs_.size())) {
        return knobs_[index].get();
    }
    return nullptr;
}

}  // namespace magda::daw::ui
