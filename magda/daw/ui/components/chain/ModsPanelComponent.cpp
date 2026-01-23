#include "ModsPanelComponent.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

ModsPanelComponent::ModsPanelComponent() : PagedControlPanel(magda::MODS_PER_PAGE) {
    // Enable adding/removing mod pages
    setCanAddPage(true);
    setCanRemovePage(true);
    setMinPages(2);  // Minimum 2 pages (16 mods)

    // Default mods - will be updated when setMods is called
    ensureKnobCount(magda::NUM_MODS);
}

void ModsPanelComponent::ensureKnobCount(int count) {
    // Add new knobs if needed
    while (static_cast<int>(knobs_.size()) < count) {
        int i = static_cast<int>(knobs_.size());
        auto knob = std::make_unique<ModKnobComponent>(i);

        // Wire up callbacks with mod index
        knob->onAmountChanged = [this, i](float amount) {
            if (onModAmountChanged) {
                onModAmountChanged(i, amount);
            }
        };

        knob->onTargetChanged = [this, i](magda::ModTarget target) {
            if (onModTargetChanged) {
                onModTargetChanged(i, target);
            }
        };

        knob->onNameChanged = [this, i](juce::String name) {
            if (onModNameChanged) {
                onModNameChanged(i, name);
            }
        };

        knob->onClicked = [this, i]() {
            // Deselect all other knobs and select this one
            for (auto& k : knobs_) {
                k->setSelected(false);
            }
            knobs_[i]->setSelected(true);

            if (onModClicked) {
                onModClicked(i);
            }
        };

        knob->onWaveformChanged = [this, i](magda::LFOWaveform waveform) {
            if (onModWaveformChanged) {
                onModWaveformChanged(i, waveform);
            }
        };

        knob->onRateChanged = [this, i](float rate) {
            if (onModRateChanged) {
                onModRateChanged(i, rate);
            }
        };

        knob->setAvailableTargets(availableDevices_);
        knob->setParentPath(parentPath_);
        addAndMakeVisible(*knob);
        knobs_.push_back(std::move(knob));
    }
}

void ModsPanelComponent::setMods(const magda::ModArray& mods) {
    ensureKnobCount(static_cast<int>(mods.size()));

    for (size_t i = 0; i < mods.size() && i < knobs_.size(); ++i) {
        knobs_[i]->setModInfo(mods[i]);
    }

    resized();
    repaint();
}

void ModsPanelComponent::setAvailableDevices(
    const std::vector<std::pair<magda::DeviceId, juce::String>>& devices) {
    availableDevices_ = devices;
    for (auto& knob : knobs_) {
        knob->setAvailableTargets(devices);
    }
}

void ModsPanelComponent::setParentPath(const magda::ChainNodePath& path) {
    parentPath_ = path;
    for (auto& knob : knobs_) {
        knob->setParentPath(path);
    }
}

void ModsPanelComponent::setSelectedModIndex(int modIndex) {
    for (size_t i = 0; i < knobs_.size(); ++i) {
        knobs_[i]->setSelected(static_cast<int>(i) == modIndex);
    }
}

int ModsPanelComponent::getTotalItemCount() const {
    return static_cast<int>(knobs_.size());
}

juce::Component* ModsPanelComponent::getItemComponent(int index) {
    if (index >= 0 && index < static_cast<int>(knobs_.size())) {
        return knobs_[index].get();
    }
    return nullptr;
}

}  // namespace magda::daw::ui
