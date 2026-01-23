#include "ModsPanelComponent.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// AddModButton implementation
AddModButton::AddModButton() = default;

void AddModButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Dashed border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.2f));
    float dashLengths[2] = {4.0f, 4.0f};
    g.drawDashedLine(
        juce::Line<float>(bounds.getX(), bounds.getY(), bounds.getRight(), bounds.getY()),
        dashLengths, 2, 1.0f);
    g.drawDashedLine(
        juce::Line<float>(bounds.getRight(), bounds.getY(), bounds.getRight(), bounds.getBottom()),
        dashLengths, 2, 1.0f);
    g.drawDashedLine(
        juce::Line<float>(bounds.getRight(), bounds.getBottom(), bounds.getX(), bounds.getBottom()),
        dashLengths, 2, 1.0f);
    g.drawDashedLine(
        juce::Line<float>(bounds.getX(), bounds.getBottom(), bounds.getX(), bounds.getY()),
        dashLengths, 2, 1.0f);

    // + icon
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    auto centerX = bounds.getCentreX();
    auto centerY = bounds.getCentreY();
    float size = 20.0f;
    g.fillRect(centerX - size * 0.5f, centerY - 1.5f, size, 3.0f);
    g.fillRect(centerX - 1.5f, centerY - size * 0.5f, 3.0f, size);

    // "Add Mod" text
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText("Add Mod", bounds.removeFromBottom(16), juce::Justification::centred);
}

void AddModButton::mouseDown(const juce::MouseEvent& /*e*/) {
    if (onClick) {
        onClick();
    }
}

void AddModButton::mouseEnter(const juce::MouseEvent& /*e*/) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void AddModButton::mouseExit(const juce::MouseEvent& /*e*/) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

// ModsPanelComponent implementation

ModsPanelComponent::ModsPanelComponent() : PagedControlPanel(magda::MODS_PER_PAGE) {
    // Disable page management - users add individual mods via + buttons
    setCanAddPage(false);
    setCanRemovePage(false);
    setMinPages(0);

    // Always show a full page (8 slots) with + buttons in empty slots
    ensureSlotCount(magda::MODS_PER_PAGE);
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

        knob->setAvailableTargets(availableDevices_);
        knob->setParentPath(parentPath_);
        addAndMakeVisible(*knob);
        knobs_.push_back(std::move(knob));
    }
}

void ModsPanelComponent::ensureSlotCount(int count) {
    // Ensure we have enough knobs (created on demand as mods are added)
    ensureKnobCount(count);

    // Ensure we have enough add buttons for empty slots
    while (static_cast<int>(addButtons_.size()) < count) {
        int slotIndex = static_cast<int>(addButtons_.size());
        auto addButton = std::make_unique<AddModButton>();

        // Wire up callback with slot index
        addButton->onClick = [this, slotIndex]() {
            if (onAddModRequested) {
                // Only add LFO for now (as per user request)
                onAddModRequested(slotIndex, magda::ModType::LFO);
            }
        };

        addChildComponent(*addButton);  // Hidden by default
        addButtons_.push_back(std::move(addButton));
    }
}

void ModsPanelComponent::setMods(const magda::ModArray& mods) {
    currentModCount_ = static_cast<int>(mods.size());
    ensureKnobCount(currentModCount_);

    // Update existing mods
    for (size_t i = 0; i < mods.size() && i < knobs_.size(); ++i) {
        // Pass pointer to live mod for waveform animation
        knobs_[i]->setModInfo(mods[i], &mods[i]);
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
    // Always show at least MODS_PER_PAGE (8) slots
    return juce::jmax(magda::MODS_PER_PAGE, currentModCount_);
}

juce::Component* ModsPanelComponent::getItemComponent(int index) {
    if (index < 0)
        return nullptr;

    // If this slot has a mod, return the knob
    if (index < currentModCount_ && index < static_cast<int>(knobs_.size())) {
        return knobs_[index].get();
    }

    // Otherwise, return the add button for this empty slot
    if (index < static_cast<int>(addButtons_.size())) {
        return addButtons_[index].get();
    }

    return nullptr;
}

}  // namespace magda::daw::ui
