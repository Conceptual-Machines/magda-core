#include "ModKnobComponent.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

ModKnobComponent::ModKnobComponent(int modIndex) : modIndex_(modIndex) {
    // Initialize mod with default values
    currentMod_ = magda::ModInfo(modIndex);

    // Name label - editable on double-click
    nameLabel_.setText(currentMod_.name, juce::dontSendNotification);
    nameLabel_.setFont(FontManager::getInstance().getUIFont(8.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setEditable(false, true, false);  // Single-click doesn't edit, double-click does
    nameLabel_.onTextChange = [this]() { onNameLabelEdited(); };
    addAndMakeVisible(nameLabel_);

    // Amount slider (modulation depth)
    amountSlider_.setRange(0.0, 1.0, 0.01);
    amountSlider_.setValue(currentMod_.amount, juce::dontSendNotification);
    amountSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    amountSlider_.onValueChanged = [this](double value) {
        currentMod_.amount = static_cast<float>(value);
        if (onAmountChanged) {
            onAmountChanged(currentMod_.amount);
        }
    };
    addAndMakeVisible(amountSlider_);
}

void ModKnobComponent::setModInfo(const magda::ModInfo& mod) {
    currentMod_ = mod;
    nameLabel_.setText(mod.name, juce::dontSendNotification);
    amountSlider_.setValue(mod.amount, juce::dontSendNotification);
    repaint();  // Update link indicator
}

void ModKnobComponent::setAvailableTargets(
    const std::vector<std::pair<magda::DeviceId, juce::String>>& devices) {
    availableTargets_ = devices;
}

void ModKnobComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void ModKnobComponent::paint(juce::Graphics& g) {
    // Background - highlight when selected
    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.3f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).brighter(0.04f));
    }
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);

    // Border - orange when selected
    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 2.0f);
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    }

    // Link indicator at bottom
    auto linkArea = getLocalBounds().removeFromBottom(LINK_INDICATOR_HEIGHT);
    paintLinkIndicator(g, linkArea);
}

void ModKnobComponent::resized() {
    auto bounds = getLocalBounds().reduced(1);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(NAME_LABEL_HEIGHT));

    // Amount slider
    bounds.removeFromTop(1);
    amountSlider_.setBounds(bounds.removeFromTop(AMOUNT_SLIDER_HEIGHT));

    // Link indicator area (painted, not a component)
    // bounds.removeFromTop(LINK_INDICATOR_HEIGHT) is handled in paint
}

void ModKnobComponent::mouseDown(const juce::MouseEvent& e) {
    // Left-click triggers onClicked callback to open modulator panel
    if (!e.mods.isPopupMenu()) {
        // Check if click is not on slider (slider needs to handle its own drag)
        if (!amountSlider_.getBounds().contains(e.getPosition())) {
            if (onClicked) {
                onClicked();
            }
        }
    }
}

void ModKnobComponent::mouseUp(const juce::MouseEvent& e) {
    // Right-click shows link menu
    if (e.mods.isPopupMenu()) {
        showLinkMenu();
    }
}

void ModKnobComponent::paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area) {
    // Link indicator dot
    int dotSize = 4;
    auto dotBounds = area.withSizeKeepingCentre(dotSize, dotSize);

    if (currentMod_.isLinked()) {
        // Purple filled dot when linked
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
        g.fillEllipse(dotBounds.toFloat());
    } else {
        // Gray outline dot when not linked
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.5f));
        g.drawEllipse(dotBounds.toFloat(), 1.0f);
    }
}

void ModKnobComponent::showLinkMenu() {
    juce::PopupMenu menu;

    menu.addSectionHeader("Link to Parameter...");
    menu.addSeparator();

    // Add submenu for each available device
    int itemId = 1;
    for (const auto& [deviceId, deviceName] : availableTargets_) {
        juce::PopupMenu deviceMenu;

        // Mock parameters (Param 1-16) for now
        for (int paramIdx = 0; paramIdx < 16; ++paramIdx) {
            juce::String paramName = "Param " + juce::String(paramIdx + 1);

            // Check if this is the currently linked target
            bool isCurrentTarget = currentMod_.target.deviceId == deviceId &&
                                   currentMod_.target.paramIndex == paramIdx;

            deviceMenu.addItem(itemId, paramName, true, isCurrentTarget);
            itemId++;
        }

        menu.addSubMenu(deviceName, deviceMenu);
    }

    menu.addSeparator();

    // Clear link option
    int clearLinkId = 10000;
    menu.addItem(clearLinkId, "Clear Link", currentMod_.isLinked());

    // Show menu and handle selection
    auto safeThis = juce::Component::SafePointer<ModKnobComponent>(this);
    auto targets = availableTargets_;  // Capture by value for async safety

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, targets, clearLinkId](int result) {
        if (safeThis == nullptr || result == 0) {
            return;
        }

        if (result == clearLinkId) {
            // Clear the link
            safeThis->currentMod_.target = magda::ModTarget{};
            safeThis->repaint();
            if (safeThis->onTargetChanged) {
                safeThis->onTargetChanged(safeThis->currentMod_.target);
            }
            return;
        }

        // Calculate which device and param was selected
        int itemId = 1;
        for (const auto& [deviceId, deviceName] : targets) {
            for (int paramIdx = 0; paramIdx < 16; ++paramIdx) {
                if (itemId == result) {
                    // Set the new target
                    safeThis->currentMod_.target.deviceId = deviceId;
                    safeThis->currentMod_.target.paramIndex = paramIdx;
                    safeThis->repaint();
                    if (safeThis->onTargetChanged) {
                        safeThis->onTargetChanged(safeThis->currentMod_.target);
                    }
                    return;
                }
                itemId++;
            }
        }
    });
}

void ModKnobComponent::onNameLabelEdited() {
    auto newName = nameLabel_.getText().trim();
    if (newName.isEmpty()) {
        // Reset to default name if empty
        newName = magda::ModInfo::getDefaultName(modIndex_, currentMod_.type);
        nameLabel_.setText(newName, juce::dontSendNotification);
    }

    if (newName != currentMod_.name) {
        currentMod_.name = newName;
        if (onNameChanged) {
            onNameChanged(newName);
        }
    }
}

}  // namespace magda::daw::ui
