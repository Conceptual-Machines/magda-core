#include "ModKnobComponent.hpp"

#include "BinaryData.h"
#include "core/LinkModeManager.hpp"
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

    // Amount slider (modulation depth) - hidden, amount is set per-parameter link
    amountSlider_.setRange(0.0, 1.0, 0.01);
    amountSlider_.setValue(currentMod_.amount, juce::dontSendNotification);
    amountSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    amountSlider_.onValueChanged = [this](double value) {
        currentMod_.amount = static_cast<float>(value);
        if (onAmountChanged) {
            onAmountChanged(currentMod_.amount);
        }
    };
    amountSlider_.setVisible(false);  // Hide - amount is per-parameter, not global
    addChildComponent(amountSlider_);

    // Waveform display
    addAndMakeVisible(waveformDisplay_);

    // Link button - toggles link mode for this mod (using link_flat icon)
    linkButton_ = std::make_unique<magda::SvgButton>("Link", BinaryData::link_flat_svg,
                                                     BinaryData::link_flat_svgSize);
    linkButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    linkButton_->setHoverColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    linkButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    linkButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.2f));
    linkButton_->onClick = [this]() { onLinkButtonClicked(); };
    addAndMakeVisible(*linkButton_);

    // Register for link mode notifications
    magda::LinkModeManager::getInstance().addListener(this);
}

ModKnobComponent::~ModKnobComponent() {
    magda::LinkModeManager::getInstance().removeListener(this);
}

void ModKnobComponent::setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod) {
    currentMod_ = mod;
    liveModPtr_ = liveMod;
    // Use live mod pointer if available (for animation), otherwise use local copy
    waveformDisplay_.setModInfo(liveMod ? liveMod : &currentMod_);
    nameLabel_.setText(mod.name, juce::dontSendNotification);
    amountSlider_.setValue(mod.amount, juce::dontSendNotification);
    repaint();
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
    auto bounds = getLocalBounds();

    // Guard against invalid bounds
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0) {
        return;
    }

    // Check if this mod is in link mode (link button is active)
    bool isInLinkMode =
        magda::LinkModeManager::getInstance().isModInLinkMode(parentPath_, modIndex_);

    // Background - orange tint when in link mode, normal otherwise
    if (isInLinkMode) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f));
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).brighter(0.04f));
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    }

    // Border - grey when selected, default otherwise
    if (selected_) {
        g.setColour(juce::Colour(0xff888888));  // Grey for selection
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 2.0f);
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
    }

    // Draw indicator dot above link button if mod is linked to any parameters
    if (currentMod_.isLinked()) {
        float dotSize = 5.0f;
        float centerX = bounds.getWidth() * 0.5f;
        float dotY = bounds.getHeight() - LINK_BUTTON_HEIGHT - dotSize - 2.0f;

        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillEllipse(centerX - dotSize * 0.5f, dotY, dotSize, dotSize);
    }
}

void ModKnobComponent::resized() {
    auto bounds = getLocalBounds().reduced(1);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(NAME_LABEL_HEIGHT));

    // Link button at the very bottom
    auto linkButtonBounds = bounds.removeFromBottom(LINK_BUTTON_HEIGHT);
    linkButton_->setBounds(linkButtonBounds);

    // Waveform display takes remaining space in the middle
    if (bounds.getHeight() > 4) {
        waveformDisplay_.setBounds(bounds.reduced(2));
    }
}

void ModKnobComponent::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu()) {
        // Track drag start position
        dragStartPos_ = e.getPosition();
        isDragging_ = false;
    }
}

void ModKnobComponent::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;

    // Check if we've moved enough to start a drag
    if (!isDragging_) {
        auto distance = e.getPosition().getDistanceFrom(dragStartPos_);
        if (distance > DRAG_THRESHOLD) {
            isDragging_ = true;

            // Find a DragAndDropContainer ancestor
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
                // Create drag description: "mod_drag:trackId:topLevelDeviceId:modIndex"
                // (For now, only supporting top-level devices)
                juce::String desc = DRAG_PREFIX;
                desc += juce::String(parentPath_.trackId) + ":";
                desc += juce::String(parentPath_.topLevelDeviceId) + ":";
                desc += juce::String(modIndex_);

                // Create a snapshot of this component for drag image
                auto snapshot = createComponentSnapshot(getLocalBounds());

                container->startDragging(desc, this, juce::ScaledImage(snapshot), true);
            }
        }
    }
}

void ModKnobComponent::mouseUp(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        // Right-click shows link menu
        showLinkMenu();
    } else if (!isDragging_) {
        // Left-click (no drag) - select this mod
        if (onClicked) {
            onClicked();
        }
    }
    isDragging_ = false;
}

void ModKnobComponent::modLinkModeChanged(bool active, const magda::ModSelection& selection) {
    // Update button appearance if this is our mod
    bool isOurMod =
        active && selection.parentPath == parentPath_ && selection.modIndex == modIndex_;
    linkButton_->setActive(isOurMod);
    repaint();  // Update orange border
}

void ModKnobComponent::onLinkButtonClicked() {
    // Toggle link mode for this mod
    magda::LinkModeManager::getInstance().toggleModLinkMode(parentPath_, modIndex_);
}

void ModKnobComponent::paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area) {
    // No longer needed - link button handles this
    (void)g;
    (void)area;
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
