#include "MacroKnobComponent.hpp"

#include "BinaryData.h"
#include "core/LinkModeManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

MacroKnobComponent::MacroKnobComponent(int macroIndex) : macroIndex_(macroIndex) {
    // Initialize macro with default values
    currentMacro_ = magda::MacroInfo(macroIndex);

    // Name label - editable on double-click
    nameLabel_.setText(currentMacro_.name, juce::dontSendNotification);
    nameLabel_.setFont(FontManager::getInstance().getUIFont(8.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setEditable(false, true, false);  // Single-click doesn't edit, double-click does
    nameLabel_.onTextChange = [this]() { onNameLabelEdited(); };
    // Pass single clicks through to parent for selection (double-click still edits)
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Value slider - visible for macros (unlike mods)
    valueSlider_.setRange(0.0, 1.0, 0.01);
    valueSlider_.setValue(currentMacro_.value, juce::dontSendNotification);
    valueSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    valueSlider_.onValueChanged = [this](double value) {
        currentMacro_.value = static_cast<float>(value);
        if (onValueChanged) {
            onValueChanged(currentMacro_.value);
        }
    };
    addAndMakeVisible(valueSlider_);

    // Link button - toggles link mode for this macro (using link_flat icon)
    linkButton_ = std::make_unique<magda::SvgButton>("Link", BinaryData::link_flat_svg,
                                                     BinaryData::link_flat_svgSize);
    linkButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    linkButton_->setHoverColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    linkButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    linkButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.2f));
    linkButton_->onClick = [this]() { onLinkButtonClicked(); };
    addAndMakeVisible(*linkButton_);

    // Register for link mode notifications
    magda::LinkModeManager::getInstance().addListener(this);
}

MacroKnobComponent::~MacroKnobComponent() {
    magda::LinkModeManager::getInstance().removeListener(this);
}

void MacroKnobComponent::setMacroInfo(const magda::MacroInfo& macro) {
    currentMacro_ = macro;
    nameLabel_.setText(macro.name, juce::dontSendNotification);
    valueSlider_.setValue(macro.value, juce::dontSendNotification);
    repaint();  // Update link indicator
}

void MacroKnobComponent::setAvailableTargets(
    const std::vector<std::pair<magda::DeviceId, juce::String>>& devices) {
    availableTargets_ = devices;
}

void MacroKnobComponent::setDeviceParamNames(
    const std::map<magda::DeviceId, std::vector<juce::String>>& paramNames) {
    deviceParamNames_ = paramNames;
}

void MacroKnobComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void MacroKnobComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Guard against invalid bounds
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0) {
        return;
    }

    // Check if this macro is in link mode (link button is active)
    bool isInLinkMode =
        magda::LinkModeManager::getInstance().isMacroInLinkMode(parentPath_, macroIndex_);

    // Background - purple tint when in link mode, normal otherwise
    if (isInLinkMode) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.15f));
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

    // Draw knob below the name label
    auto knobBounds = bounds.reduced(1);
    knobBounds.removeFromTop(NAME_LABEL_HEIGHT);  // Skip name label
    auto knobArea = knobBounds.removeFromTop(KNOB_SIZE);

    // Center the knob horizontally
    float knobDiameter = static_cast<float>(KNOB_SIZE - 4);
    float knobX = knobArea.getCentreX() - knobDiameter / 2.0f;
    float knobY = knobArea.getCentreY() - knobDiameter / 2.0f;
    auto knobRect = juce::Rectangle<float>(knobX, knobY, knobDiameter, knobDiameter);

    // Knob body (dark circle)
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillEllipse(knobRect);

    // Knob border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).brighter(0.2f));
    g.drawEllipse(knobRect.reduced(0.5f), 1.0f);

    // Value arc - JUCE addCentredArc uses 0 at TOP (12 o'clock), clockwise positive
    // 7 o'clock = 210° = 7π/6, 5 o'clock = 150° = 5π/6
    // Sweep clockwise from 7 through 9, 12, 3 to 5 = 300°
    const float startAngle = juce::MathConstants<float>::pi * (7.0f / 6.0f);  // 7π/6 = 7 o'clock
    const float sweepRange = juce::MathConstants<float>::pi * (5.0f / 3.0f);  // 300° sweep
    float valueAngle = startAngle + (currentMacro_.value * sweepRange);

    // Draw value arc
    juce::Path arcPath;
    float arcRadius = knobDiameter / 2.0f - 3.0f;
    arcPath.addCentredArc(knobRect.getCentreX(), knobRect.getCentreY(), arcRadius, arcRadius, 0.0f,
                          startAngle, valueAngle, true);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    g.strokePath(arcPath, juce::PathStrokeType(2.0f));

    // Draw pointer line - JUCE angles: 0 at top, clockwise positive
    // x = sin(angle), y = -cos(angle) for screen coords
    float pointerLength = knobDiameter / 2.0f - 5.0f;
    float pointerX = knobRect.getCentreX() + std::sin(valueAngle) * pointerLength;
    float pointerY = knobRect.getCentreY() - std::cos(valueAngle) * pointerLength;

    g.setColour(DarkTheme::getTextColour());
    g.drawLine(knobRect.getCentreX(), knobRect.getCentreY(), pointerX, pointerY, 1.5f);
}

void MacroKnobComponent::resized() {
    auto bounds = getLocalBounds().reduced(1);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(NAME_LABEL_HEIGHT));

    // Skip knob area (drawn in paint())
    bounds.removeFromTop(KNOB_SIZE);

    // Position link button at the very bottom, horizontally centered to match mod knob sizing
    auto linkArea = bounds.removeFromBottom(LINK_BUTTON_HEIGHT);
    int linkWidth = juce::jmin(linkArea.getWidth(), LINK_BUTTON_HEIGHT * 3);
    linkButton_->setBounds(linkArea.withSizeKeepingCentre(linkWidth, LINK_BUTTON_HEIGHT));

    // Value slider right above link button
    valueSlider_.setBounds(bounds.removeFromBottom(VALUE_SLIDER_HEIGHT));
}

juce::Rectangle<int> MacroKnobComponent::getKnobBounds() const {
    auto bounds = getLocalBounds().reduced(1);
    bounds.removeFromTop(NAME_LABEL_HEIGHT);  // Skip name label
    return bounds.removeFromTop(KNOB_SIZE);
}

void MacroKnobComponent::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu()) {
        dragStartPos_ = e.getPosition();
        isDragging_ = false;

        // Check if click is in knob area
        if (getKnobBounds().contains(e.getPosition())) {
            isKnobDragging_ = true;
            dragStartValue_ = currentMacro_.value;
        } else {
            isKnobDragging_ = false;
        }
    }
}

void MacroKnobComponent::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;

    if (isKnobDragging_) {
        // Knob dragging - change value based on vertical movement
        // Drag up = increase, drag down = decrease
        float deltaY = static_cast<float>(dragStartPos_.y - e.getPosition().y);
        float sensitivity = 0.005f;  // Adjust for feel
        float newValue = juce::jlimit(0.0f, 1.0f, dragStartValue_ + deltaY * sensitivity);

        if (newValue != currentMacro_.value) {
            currentMacro_.value = newValue;
            valueSlider_.setValue(newValue, juce::dontSendNotification);
            repaint();
            if (onValueChanged) {
                onValueChanged(newValue);
            }
        }
        return;
    }

    // Check if we've moved enough to start a link drag
    if (!isDragging_) {
        auto distance = e.getPosition().getDistanceFrom(dragStartPos_);
        if (distance > DRAG_THRESHOLD) {
            isDragging_ = true;

            // Find a DragAndDropContainer ancestor
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
                // Create drag description: "macro_drag:trackId:topLevelDeviceId:macroIndex"
                juce::String desc = DRAG_PREFIX;
                desc += juce::String(parentPath_.trackId) + ":";
                desc += juce::String(parentPath_.topLevelDeviceId) + ":";
                desc += juce::String(macroIndex_);

                // Create a snapshot of this component for drag image
                auto snapshot = createComponentSnapshot(getLocalBounds());

                container->startDragging(desc, this, juce::ScaledImage(snapshot), true);
            }
        }
    }
}

void MacroKnobComponent::mouseUp(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        // Right-click shows link menu
        showLinkMenu();
    } else if (!isDragging_ && !isKnobDragging_) {
        // Left-click (no drag) - select this macro
        if (onClicked) {
            onClicked();
        }
    }
    isDragging_ = false;
    isKnobDragging_ = false;
}

void MacroKnobComponent::macroLinkModeChanged(bool active, const magda::MacroSelection& selection) {
    // Update button appearance if this is our macro
    bool isOurMacro =
        active && selection.parentPath == parentPath_ && selection.macroIndex == macroIndex_;
    linkButton_->setActive(isOurMacro);
    repaint();  // Update purple border
}

void MacroKnobComponent::onLinkButtonClicked() {
    // Toggle link mode for this macro
    magda::LinkModeManager::getInstance().toggleMacroLinkMode(parentPath_, macroIndex_);
}

void MacroKnobComponent::paintLinkIndicator(juce::Graphics& g, juce::Rectangle<int> area) {
    // No longer needed - link button handles this
    (void)g;
    (void)area;
}

void MacroKnobComponent::showLinkMenu() {
    juce::PopupMenu menu;

    menu.addSectionHeader("Link to Parameter...");
    menu.addSeparator();

    // Add submenu for each available device
    int itemId = 1;
    for (const auto& [deviceId, deviceName] : availableTargets_) {
        juce::PopupMenu deviceMenu;

        // Get real param names for this device, fall back to "Parameter N"
        auto it = deviceParamNames_.find(deviceId);
        int paramCount = (it != deviceParamNames_.end()) ? static_cast<int>(it->second.size()) : 16;

        for (int paramIdx = 0; paramIdx < paramCount; ++paramIdx) {
            juce::String paramName =
                (it != deviceParamNames_.end() && paramIdx < static_cast<int>(it->second.size()))
                    ? it->second[static_cast<size_t>(paramIdx)]
                    : "Parameter " + juce::String(paramIdx + 1);

            // Check if this param is in the links vector
            magda::MacroTarget t;
            t.deviceId = deviceId;
            t.paramIndex = paramIdx;
            bool isCurrentTarget = currentMacro_.getLink(t) != nullptr;

            deviceMenu.addItem(itemId, paramName, true, isCurrentTarget);
            itemId++;
        }

        menu.addSubMenu(deviceName, deviceMenu);
    }

    menu.addSeparator();

    // Individual unlink items for each existing link
    int unlinkBaseId = 10000;
    std::vector<magda::MacroTarget> unlinkTargets;
    for (const auto& link : currentMacro_.links) {
        if (!link.target.isValid())
            continue;
        juce::String paramName;
        auto it = deviceParamNames_.find(link.target.deviceId);
        if (it != deviceParamNames_.end() && link.target.paramIndex >= 0 &&
            link.target.paramIndex < static_cast<int>(it->second.size())) {
            paramName = it->second[static_cast<size_t>(link.target.paramIndex)];
        } else {
            paramName = "P" + juce::String(link.target.paramIndex + 1);
        }
        // Find device name for context
        for (const auto& [devId, devName] : availableTargets_) {
            if (devId == link.target.deviceId) {
                paramName = devName + " - " + paramName;
                break;
            }
        }
        menu.addItem(unlinkBaseId + static_cast<int>(unlinkTargets.size()), "Unlink " + paramName);
        unlinkTargets.push_back(link.target);
    }

    // Clear all links option (only if multiple links)
    int clearAllId = 20000;
    if (unlinkTargets.size() > 1) {
        menu.addItem(clearAllId, "Clear All Links");
    }

    // Show menu and handle selection
    auto safeThis = juce::Component::SafePointer<MacroKnobComponent>(this);
    auto targets = availableTargets_;     // Capture by value for async safety
    auto paramNames = deviceParamNames_;  // Capture by value for async safety

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, targets, paramNames, unlinkBaseId,
                                                    unlinkTargets, clearAllId](int result) {
        if (safeThis == nullptr || result == 0) {
            return;
        }

        // Clear all links
        if (result == clearAllId) {
            safeThis->currentMacro_.target = magda::MacroTarget{};
            safeThis->currentMacro_.links.clear();
            safeThis->repaint();
            if (safeThis->onAllLinksCleared) {
                safeThis->onAllLinksCleared();
            }
            return;
        }

        // Individual unlink
        int unlinkIdx = result - unlinkBaseId;
        if (unlinkIdx >= 0 && unlinkIdx < static_cast<int>(unlinkTargets.size())) {
            auto target = unlinkTargets[static_cast<size_t>(unlinkIdx)];
            safeThis->currentMacro_.removeLink(target);
            safeThis->repaint();
            if (safeThis->onLinkRemoved) {
                safeThis->onLinkRemoved(target);
            }
            return;
        }

        // Calculate which device and param was selected
        int itemId = 1;
        for (const auto& [deviceId, deviceName] : targets) {
            auto it = paramNames.find(deviceId);
            int paramCount = (it != paramNames.end()) ? static_cast<int>(it->second.size()) : 16;
            for (int paramIdx = 0; paramIdx < paramCount; ++paramIdx) {
                if (itemId == result) {
                    // Add to links vector (not legacy target)
                    magda::MacroTarget t;
                    t.deviceId = deviceId;
                    t.paramIndex = paramIdx;
                    if (!safeThis->currentMacro_.getLink(t)) {
                        magda::MacroLink link;
                        link.target = t;
                        link.amount = 1.0f;
                        safeThis->currentMacro_.links.push_back(link);
                    }
                    safeThis->repaint();
                    if (safeThis->onTargetChanged) {
                        safeThis->onTargetChanged(t);
                    }
                    return;
                }
                itemId++;
            }
        }
    });
}

void MacroKnobComponent::onNameLabelEdited() {
    auto newName = nameLabel_.getText().trim();
    if (newName.isEmpty()) {
        // Reset to default name if empty
        newName = "Macro " + juce::String(macroIndex_ + 1);
        nameLabel_.setText(newName, juce::dontSendNotification);
    }

    if (newName != currentMacro_.name) {
        currentMacro_.name = newName;
        if (onNameChanged) {
            onNameChanged(newName);
        }
    }
}

}  // namespace magda::daw::ui
