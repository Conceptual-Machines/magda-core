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
    // Pass single clicks through to parent for selection (double-click still edits)
    nameLabel_.setInterceptsMouseClicks(false, false);
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
    amountSlider_.onAltClicked = [this]() {
        // Alt+click on slider: if a param is selected, show amount popup
        if (selectedParam_.isValid()) {
            const auto* existingLink = currentMod_.getLink(selectedParam_);
            bool isLinked = existingLink != nullptr ||
                            (currentMod_.target.deviceId == selectedParam_.deviceId &&
                             currentMod_.target.paramIndex == selectedParam_.paramIndex);

            if (isLinked) {
                float currentAmount = existingLink ? existingLink->amount : currentMod_.amount;
                showAmountSlider(currentAmount, false);
            } else {
                showAmountSlider(0.5f, true);
            }
        }
    };
    addAndMakeVisible(amountSlider_);
}

void ModKnobComponent::setModInfo(const magda::ModInfo& mod) {
    currentMod_ = mod;
    nameLabel_.setText(mod.name, juce::dontSendNotification);
    updateAmountDisplay();
    repaint();  // Update link indicator
}

void ModKnobComponent::setSelectedParam(const magda::ModTarget& param) {
    selectedParam_ = param;
    updateAmountDisplay();
    repaint();
}

void ModKnobComponent::clearSelectedParam() {
    selectedParam_ = magda::ModTarget{};
    updateAmountDisplay();
    repaint();
}

void ModKnobComponent::updateAmountDisplay() {
    // If a param is selected, show the link amount for that param (if linked)
    if (selectedParam_.isValid()) {
        if (const auto* link = currentMod_.getLink(selectedParam_)) {
            amountSlider_.setValue(link->amount, juce::dontSendNotification);
            return;
        }
        // Legacy check
        if (currentMod_.target.deviceId == selectedParam_.deviceId &&
            currentMod_.target.paramIndex == selectedParam_.paramIndex) {
            amountSlider_.setValue(currentMod_.amount, juce::dontSendNotification);
            return;
        }
        // Not linked to selected param - show 0
        amountSlider_.setValue(0.0, juce::dontSendNotification);
    } else {
        // No param selected - show the mod's global amount
        amountSlider_.setValue(currentMod_.amount, juce::dontSendNotification);
    }
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
            // Alt+click: if a param is selected, directly show amount slider
            if (e.mods.isAltDown() && selectedParam_.isValid()) {
                // Check if linked to selected param
                const auto* existingLink = currentMod_.getLink(selectedParam_);
                bool isLinked = existingLink != nullptr ||
                                (currentMod_.target.deviceId == selectedParam_.deviceId &&
                                 currentMod_.target.paramIndex == selectedParam_.paramIndex);

                if (isLinked) {
                    float currentAmount = existingLink ? existingLink->amount : currentMod_.amount;
                    showAmountSlider(currentAmount, false);
                } else {
                    // Create new link with default 50%
                    showAmountSlider(0.5f, true);
                }
            } else if (onClicked) {
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

void ModKnobComponent::showAmountSlider(float currentAmount, bool isNewLink) {
    // Create a simple slider component for the popup
    auto* slider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    slider->setRange(0.0, 100.0, 1.0);
    slider->setValue(currentAmount * 100.0, juce::dontSendNotification);
    slider->setTextValueSuffix("%");
    slider->setSize(200, 30);

    auto safeThis = juce::Component::SafePointer<ModKnobComponent>(this);
    auto target = selectedParam_;

    // When slider changes, update the amount
    slider->onValueChange = [safeThis, slider, target, isNewLink]() {
        if (safeThis == nullptr)
            return;
        float amount = static_cast<float>(slider->getValue() / 100.0);

        if (isNewLink) {
            if (safeThis->onNewLinkCreated) {
                safeThis->onNewLinkCreated(target, amount);
            }
        } else {
            if (safeThis->onLinkAmountChanged) {
                safeThis->onLinkAmountChanged(target, amount);
            }
        }
    };

    // Show as callout box
    auto& callout = juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(slider),
                                                           getScreenBounds(), nullptr);
    (void)callout;
}

void ModKnobComponent::paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area) {
    // Link indicator dot
    int dotSize = 4;
    auto dotBounds = area.withSizeKeepingCentre(dotSize, dotSize);

    // Check if linked to the selected param
    bool linkedToSelectedParam = false;
    if (selectedParam_.isValid()) {
        linkedToSelectedParam = currentMod_.getLink(selectedParam_) != nullptr ||
                                (currentMod_.target.deviceId == selectedParam_.deviceId &&
                                 currentMod_.target.paramIndex == selectedParam_.paramIndex);
    }

    if (linkedToSelectedParam) {
        // Orange filled dot when linked to selected param
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillEllipse(dotBounds.toFloat());
    } else if (currentMod_.isLinked()) {
        // Purple filled dot when linked (but not to selected param)
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

    // Mock param names (same as DeviceSlotComponent)
    static const char* mockParamNames[16] = {
        "Cutoff",   "Resonance", "Drive",    "Mix",   "Attack", "Decay", "Sustain", "Release",
        "LFO Rate", "LFO Depth", "Feedback", "Width", "Low",    "Mid",   "High",    "Output"};

    menu.addSectionHeader("Link to Parameter...");
    menu.addSeparator();

    // Add submenu for each available device
    int itemId = 1;
    for (const auto& [deviceId, deviceName] : availableTargets_) {
        juce::PopupMenu deviceMenu;

        // Use proper param names
        for (int paramIdx = 0; paramIdx < 16; ++paramIdx) {
            juce::String paramName = mockParamNames[paramIdx];

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
