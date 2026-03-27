#include "MacroPanelComponent.hpp"

namespace magda::daw::ui {

MacroPanelComponent::MacroPanelComponent() : PagedControlPanel(magda::MACROS_PER_PAGE) {
    // Enable adding/removing macro pages
    setCanAddPage(true);
    setCanRemovePage(true);
    setMinPages(2);  // Minimum 2 pages (16 macros)

    // Default macros - will be updated when setMacros is called
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

        knob->onLinkRemoved = [this, i](magda::MacroTarget target) {
            if (onMacroLinkRemoved) {
                onMacroLinkRemoved(i, target);
            }
        };

        knob->onAllLinksCleared = [this, i]() {
            if (onMacroAllLinksCleared) {
                onMacroAllLinksCleared(i);
            }
        };

        knob->onNameChanged = [this, i](juce::String name) {
            if (onMacroNameChanged) {
                onMacroNameChanged(i, name);
            }
        };

        knob->onClicked = [this, i]() {
            // Deselect all other knobs and select this one
            for (auto& k : knobs_) {
                k->setSelected(false);
            }
            knobs_[i]->setSelected(true);

            if (onMacroClicked) {
                onMacroClicked(i);
            }
        };

        knob->setAvailableTargets(availableDevices_);
        knob->setParentPath(parentPath_);
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

void MacroPanelComponent::setDeviceParamNames(
    const std::map<magda::DeviceId, std::vector<juce::String>>& paramNames) {
    for (auto& knob : knobs_) {
        knob->setDeviceParamNames(paramNames);
    }
}

void MacroPanelComponent::setSelectedMacroIndex(int macroIndex) {
    for (size_t i = 0; i < knobs_.size(); ++i) {
        knobs_[i]->setSelected(static_cast<int>(i) == macroIndex);
    }
}

void MacroPanelComponent::setParentPath(const magda::ChainNodePath& path) {
    parentPath_ = path;
    for (auto& knob : knobs_) {
        knob->setParentPath(path);
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
