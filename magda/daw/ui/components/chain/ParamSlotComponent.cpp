#include "ParamSlotComponent.hpp"

#include "core/LinkModeManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

ParamSlotComponent::ParamSlotComponent(int paramIndex) : paramIndex_(paramIndex) {
    // Register for link mode notifications
    magda::LinkModeManager::getInstance().addListener(this);

    // Allow painting outside bounds for tooltip
    setInterceptsMouseClicks(true, true);

    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    valueSlider_.setRange(0.0, 1.0, 0.01);
    valueSlider_.setValue(0.5, juce::dontSendNotification);
    valueSlider_.setTextColour(juce::Colours::white);  // Bright white for better visibility
    valueSlider_.setBackgroundColour(juce::Colours::transparentBlack);  // Transparent background
    valueSlider_.onValueChanged = [this](double value) {
        if (onValueChanged) {
            onValueChanged(value);
        }
    };
    valueSlider_.onClicked = [this]() {
        // Regular click (no Shift): select this param
        if (devicePath_.isValid()) {
            magda::SelectionManager::getInstance().selectParam(devicePath_, paramIndex_);
        }
    };
    valueSlider_.onRightClicked = [this]() {
        // Show link menu on right-click
        showLinkMenu();
    };

    // Amount label for link mode drag tooltip
    amountLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    amountLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    amountLabel_.setColour(juce::Label::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.95f));
    amountLabel_.setJustificationType(juce::Justification::centred);
    amountLabel_.setVisible(false);
    amountLabel_.setAlwaysOnTop(true);
    addChildComponent(amountLabel_);

    // Shift+drag: edit mod amount when a mod is selected
    valueSlider_.onShiftDragStart = [this](float /*startValue*/) {
        if (selectedModIndex_ < 0 || !availableMods_ ||
            selectedModIndex_ >= static_cast<int>(availableMods_->size())) {
            return;
        }

        const auto& selectedMod = (*availableMods_)[static_cast<size_t>(selectedModIndex_)];
        magda::ModTarget thisTarget{deviceId_, paramIndex_};

        // Check if already linked
        const auto* existingLink = selectedMod.getLink(thisTarget);
        bool isLinked = existingLink != nullptr || (selectedMod.target.deviceId == deviceId_ &&
                                                    selectedMod.target.paramIndex == paramIndex_);

        float startAmount = 0.5f;
        if (!isLinked) {
            // Create link at 50%
            if (onModLinkedWithAmount) {
                onModLinkedWithAmount(selectedModIndex_, thisTarget, 0.5f);
            }
        } else {
            // Use existing amount as start value
            startAmount = existingLink ? existingLink->amount : selectedMod.amount;
        }
        valueSlider_.setShiftDragStartValue(startAmount);

        isModAmountDrag_ = true;
        modAmountDragModIndex_ = selectedModIndex_;

        // Show amount label
        int percent = static_cast<int>(startAmount * 100);
        amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);
        amountLabel_.setBounds(getLocalBounds().withHeight(14).translated(0, -16));
        amountLabel_.setVisible(true);
    };

    valueSlider_.onShiftDrag = [this](float newAmount) {
        if (!isModAmountDrag_ || modAmountDragModIndex_ < 0) {
            return;
        }
        magda::ModTarget thisTarget{deviceId_, paramIndex_};
        if (onModAmountChanged) {
            onModAmountChanged(modAmountDragModIndex_, thisTarget, newAmount);
        }

        // Update amount label
        int percent = static_cast<int>(newAmount * 100);
        amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);

        repaint();
    };

    valueSlider_.onShiftDragEnd = [this]() {
        isModAmountDrag_ = false;
        modAmountDragModIndex_ = -1;
        amountLabel_.setVisible(false);
    };

    valueSlider_.onShiftClicked = [this]() {
        // Shift+click (no drag): just create link at 50% if not linked
        if (selectedModIndex_ < 0 || !availableMods_ ||
            selectedModIndex_ >= static_cast<int>(availableMods_->size())) {
            return;
        }

        const auto& selectedMod = (*availableMods_)[static_cast<size_t>(selectedModIndex_)];
        magda::ModTarget thisTarget{deviceId_, paramIndex_};

        const auto* existingLink = selectedMod.getLink(thisTarget);
        bool isLinked = existingLink != nullptr || (selectedMod.target.deviceId == deviceId_ &&
                                                    selectedMod.target.paramIndex == paramIndex_);

        if (!isLinked && onModLinkedWithAmount) {
            onModLinkedWithAmount(selectedModIndex_, thisTarget, 0.5f);
            repaint();
        }
    };

    // Disable right-click editing - we use right-click for link menu
    valueSlider_.setRightClickEditsText(false);
    addAndMakeVisible(valueSlider_);

    setInterceptsMouseClicks(true, true);
}

ParamSlotComponent::~ParamSlotComponent() {
    // Clean up tooltip if it's on desktop
    if (amountLabel_.isOnDesktop()) {
        amountLabel_.removeFromDesktop();
    }
    magda::LinkModeManager::getInstance().removeListener(this);
}

void ParamSlotComponent::modLinkModeChanged(bool active, const magda::ModSelection& selection) {
    // CRITICAL: Only respond if this parameter is within the scope of the mod's parent
    bool isInScope = isInScopeOf(selection.parentPath);

    // Only enter link mode if we're in scope
    isInLinkMode_ = active && isInScope;

    // CRITICAL: Only set activeMod_ when entering AND in scope, clear when exiting
    if (active && isInScope) {
        activeMod_ = selection;
        activeMacro_ = magda::MacroSelection{};  // Clear macro state
    } else {
        activeMod_ = magda::ModSelection{};  // Clear mod state on exit
    }

    // Clean up tooltip if link mode is being exited
    if (!active || !isInScope) {
        isLinkModeDrag_ = false;
        amountLabel_.setVisible(false);
        if (amountLabel_.isOnDesktop()) {
            amountLabel_.removeFromDesktop();
        }
    }

    // Disable value slider interaction when in link mode
    valueSlider_.setInterceptsMouseClicks(!isInLinkMode_, !isInLinkMode_);

    // Update cursor if mouse is hovering
    if (isMouseOver()) {
        setMouseCursor(isInLinkMode_ ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
    }

    repaint();  // Show link mode highlight
}

void ParamSlotComponent::macroLinkModeChanged(bool active, const magda::MacroSelection& selection) {
    DBG("macroLinkModeChanged param=" << paramIndex_ << " active=" << (active ? 1 : 0)
                                      << " macroIndex=" << selection.macroIndex);

    // CRITICAL: Only respond if this parameter is within the scope of the macro's parent
    bool isInScope = isInScopeOf(selection.parentPath);
    DBG("  isInScope=" << (isInScope ? 1 : 0));

    // Only enter link mode if we're in scope
    isInLinkMode_ = active && isInScope;

    // CRITICAL: Only set activeMacro_ when entering AND in scope, clear when exiting
    if (active && isInScope) {
        activeMacro_ = selection;
        activeMod_ = magda::ModSelection{};  // Clear mod state
    } else {
        activeMacro_ = magda::MacroSelection{};  // Clear macro state on exit
    }

    // Clean up tooltip if link mode is being exited
    if (!active || !isInScope) {
        isLinkModeDrag_ = false;
        amountLabel_.setVisible(false);
        if (amountLabel_.isOnDesktop()) {
            amountLabel_.removeFromDesktop();
        }
    }

    // Disable value slider interaction when in link mode
    valueSlider_.setInterceptsMouseClicks(!isInLinkMode_, !isInLinkMode_);

    // Update cursor if mouse is hovering
    if (isMouseOver()) {
        setMouseCursor(isInLinkMode_ ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
    }

    repaint();  // Show link mode highlight
}

void ParamSlotComponent::handleLinkModeClick() {
    if (!isInLinkMode_) {
        return;
    }

    // Check if a mod or macro is active
    if (activeMod_.isValid() && availableMods_ && activeMod_.modIndex >= 0 &&
        activeMod_.modIndex < static_cast<int>(availableMods_->size())) {
        // Mod link mode
        const auto& mod = (*availableMods_)[static_cast<size_t>(activeMod_.modIndex)];
        magda::ModTarget thisTarget{deviceId_, paramIndex_};

        // Check if already linked
        const auto* existingLink = mod.getLink(thisTarget);
        bool isLinked = existingLink != nullptr ||
                        (mod.target.deviceId == deviceId_ && mod.target.paramIndex == paramIndex_);

        float initialAmount = isLinked ? (existingLink ? existingLink->amount : mod.amount) : 0.5f;

        if (!isLinked) {
            // Create new link
            if (onModLinkedWithAmount) {
                onModLinkedWithAmount(activeMod_.modIndex, thisTarget, initialAmount);
            }
        }

        // Show overlay slider
        showLinkModeSlider(!isLinked, initialAmount);
    } else if (activeMacro_.isValid() && availableMacros_ && activeMacro_.macroIndex >= 0 &&
               activeMacro_.macroIndex < static_cast<int>(availableMacros_->size())) {
        // Macro link mode
        const auto& macro = (*availableMacros_)[static_cast<size_t>(activeMacro_.macroIndex)];
        magda::MacroTarget thisTarget{deviceId_, paramIndex_};

        // Check if already linked (check both legacy target and new links vector)
        const auto* existingLink = macro.getLink(thisTarget);
        bool isLinked = existingLink != nullptr || (macro.target.deviceId == deviceId_ &&
                                                    macro.target.paramIndex == paramIndex_);

        float initialAmount = isLinked ? (existingLink ? existingLink->amount : 0.5f) : 0.5f;

        if (!isLinked) {
            // Create new link with initial amount
            if (onMacroLinkedWithAmount) {
                onMacroLinkedWithAmount(activeMacro_.macroIndex, thisTarget, initialAmount);
            }
        }

        // Show overlay slider (just like mods)
        showLinkModeSlider(!isLinked, initialAmount);
    }
}

void ParamSlotComponent::showLinkModeSlider(bool /*isNewLink*/, float initialAmount) {
    const char* type = activeMod_.isValid() ? "MOD" : "MACRO";
    int index = activeMod_.isValid() ? activeMod_.modIndex : activeMacro_.macroIndex;
    DBG("SHOW SLIDER: " << type << " " << index << " on param " << paramIndex_
                        << " amount=" << initialAmount);

    if (!linkModeSlider_) {
        linkModeSlider_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                         juce::Slider::TextBoxRight);
        linkModeSlider_->setRange(0.0, 100.0, 1.0);
        linkModeSlider_->setTextValueSuffix("%");
        linkModeSlider_->setColour(juce::Slider::backgroundColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));

        auto safeThis = juce::Component::SafePointer<ParamSlotComponent>(this);
        linkModeSlider_->onValueChange = [safeThis]() {
            if (safeThis == nullptr || !safeThis->linkModeSlider_) {
                return;
            }
            float amount = static_cast<float>(safeThis->linkModeSlider_->getValue() / 100.0);

            if (safeThis->activeMod_.isValid() && safeThis->onModAmountChanged) {
                magda::ModTarget thisTarget{safeThis->deviceId_, safeThis->paramIndex_};
                safeThis->onModAmountChanged(safeThis->activeMod_.modIndex, thisTarget, amount);
            } else if (safeThis->activeMacro_.isValid() && safeThis->onMacroAmountChanged) {
                magda::MacroTarget thisTarget{safeThis->deviceId_, safeThis->paramIndex_};
                safeThis->onMacroAmountChanged(safeThis->activeMacro_.macroIndex, thisTarget,
                                               amount);
            }
        };

        addAndMakeVisible(*linkModeSlider_);
        DBG("  Created NEW slider widget");
    } else {
        DBG("  Reusing existing slider widget, visible=" << (linkModeSlider_->isVisible() ? 1 : 0));
    }

    // Update slider colors based on whether it's a mod or macro
    auto accentColor = activeMod_.isValid() ? DarkTheme::getColour(DarkTheme::ACCENT_ORANGE)
                                            : DarkTheme::getColour(DarkTheme::ACCENT_PURPLE);
    linkModeSlider_->setColour(juce::Slider::thumbColourId, accentColor);
    linkModeSlider_->setColour(juce::Slider::trackColourId, accentColor.withAlpha(0.5f));

    linkModeSlider_->setValue(initialAmount * 100.0, juce::dontSendNotification);
    linkModeSlider_->setBounds(getLocalBounds().reduced(2));
    linkModeSlider_->toFront(true);
    linkModeSlider_->setVisible(true);
    DBG("  Slider NOW visible=" << (linkModeSlider_->isVisible() ? 1 : 0));
    // Don't grab keyboard focus - let sliders stay visible on multiple parameters
    // linkModeSlider_->grabKeyboardFocus();
}

void ParamSlotComponent::hideLinkModeSlider() {
    if (linkModeSlider_) {
        DBG("HIDE SLIDER on param "
            << paramIndex_ << " (was visible=" << (linkModeSlider_->isVisible() ? 1 : 0) << ")");
        linkModeSlider_->setVisible(false);
    }
}

void ParamSlotComponent::setParamName(const juce::String& name) {
    nameLabel_.setText(name, juce::dontSendNotification);
}

void ParamSlotComponent::setParamValue(double value) {
    valueSlider_.setValue(value, juce::dontSendNotification);
}

void ParamSlotComponent::setFonts(const juce::Font& labelFont, const juce::Font& valueFont) {
    nameLabel_.setFont(labelFont);
    valueSlider_.setFont(valueFont);
    valueSlider_.setTextColour(juce::Colours::white);  // Ensure white text after font change
    valueSlider_.setBackgroundColour(
        juce::Colours::transparentBlack);  // Ensure transparent background
}

void ParamSlotComponent::paint(juce::Graphics& /*g*/) {
    // Selection highlight is drawn in paintOverChildren() so it appears on top
}

void ParamSlotComponent::paintOverChildren(juce::Graphics& g) {
    // Draw link mode highlight (background tint when a mod/macro is in link mode)
    if (isInLinkMode_) {
        auto color = activeMod_.isValid()
                         ? DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f)
                         : DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.15f);
        g.setColour(color);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    }
    // Draw drag-over highlight (background tint when a mod is being dragged over)
    else if (isDragOver_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    }
    // Draw selection highlight (border only)
    else if (selected_) {
        g.setColour(juce::Colour(0xff888888));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 2.0f, 2.0f);
    }

    // Draw modulation indicators as horizontal bars
    paintModulationIndicators(g);
}

void ParamSlotComponent::resized() {
    auto bounds = getLocalBounds();

    // Label takes top portion
    int labelHeight = juce::jmin(12, getHeight() / 3);
    nameLabel_.setBounds(bounds.removeFromTop(labelHeight));

    // Value slider takes the rest
    valueSlider_.setBounds(bounds);
}

void ParamSlotComponent::mouseEnter(const juce::MouseEvent& /*e*/) {
    // Change cursor to pointing hand when in link mode
    if (isInLinkMode_) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
}

void ParamSlotComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    // Restore default cursor
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ParamSlotComponent::mouseDown(const juce::MouseEvent& e) {
    // Handle right-click anywhere on the component
    if (e.mods.isPopupMenu()) {
        showLinkMenu();
        return;
    }

    // Handle link mode - prepare for drag to set amount/value
    if (isInLinkMode_ && e.mods.isLeftButtonDown()) {
        // Mod link mode - drag to set per-parameter amount
        if (activeMod_.isValid()) {
            float initialAmount = 0.5f;
            bool isLinked = false;

            if (availableMods_ && activeMod_.modIndex >= 0 &&
                activeMod_.modIndex < static_cast<int>(availableMods_->size())) {
                const auto& mod = (*availableMods_)[static_cast<size_t>(activeMod_.modIndex)];
                magda::ModTarget thisTarget{deviceId_, paramIndex_};

                const auto* existingLink = mod.getLink(thisTarget);
                isLinked = existingLink != nullptr || (mod.target.deviceId == deviceId_ &&
                                                       mod.target.paramIndex == paramIndex_);

                if (isLinked) {
                    initialAmount = existingLink ? existingLink->amount : mod.amount;
                }
            }

            // Start link mode drag for mods
            isLinkModeDrag_ = true;
            linkModeDragStartAmount_ = initialAmount;
            linkModeDragCurrentAmount_ = initialAmount;
            linkModeDragStartY_ = e.getMouseDownY();

            // Show tooltip-style amount label (orange for mods)
            int percent = static_cast<int>(initialAmount * 100);
            amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);
            amountLabel_.setColour(juce::Label::backgroundColourId,
                                   DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.95f));

            // Add tooltip to desktop so it can render outside component bounds
            if (!amountLabel_.isOnDesktop()) {
                amountLabel_.addToDesktop(juce::ComponentPeer::windowIsTemporary |
                                          juce::ComponentPeer::windowIgnoresMouseClicks);
            }

            // Position above the parameter cell in screen coordinates
            auto screenBounds = getScreenBounds();
            amountLabel_.setBounds(screenBounds.getX(), screenBounds.getY() - 22,
                                   screenBounds.getWidth(), 20);
            amountLabel_.setVisible(true);
            amountLabel_.toFront(true);

            repaint();
            return;
        }

        // Macro link mode - drag to set macro's global value
        if (activeMacro_.isValid()) {
            float initialValue = 0.5f;

            if (availableMacros_ && activeMacro_.macroIndex >= 0 &&
                activeMacro_.macroIndex < static_cast<int>(availableMacros_->size())) {
                const auto& macro =
                    (*availableMacros_)[static_cast<size_t>(activeMacro_.macroIndex)];
                initialValue = macro.value;
            }

            // Start link mode drag for macros
            isLinkModeDrag_ = true;  // Reuse same drag flag
            linkModeDragStartAmount_ = initialValue;
            linkModeDragCurrentAmount_ = initialValue;
            linkModeDragStartY_ = e.getMouseDownY();

            // Show tooltip-style value label (purple for macros)
            int percent = static_cast<int>(initialValue * 100);
            amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);
            amountLabel_.setColour(juce::Label::backgroundColourId,
                                   DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.95f));

            // Add tooltip to desktop so it can render outside component bounds
            if (!amountLabel_.isOnDesktop()) {
                amountLabel_.addToDesktop(juce::ComponentPeer::windowIsTemporary |
                                          juce::ComponentPeer::windowIgnoresMouseClicks);
            }

            // Position above the parameter cell in screen coordinates
            auto screenBounds = getScreenBounds();
            amountLabel_.setBounds(screenBounds.getX(), screenBounds.getY() - 22,
                                   screenBounds.getWidth(), 20);
            amountLabel_.setVisible(true);
            amountLabel_.toFront(true);

            repaint();
            return;
        }
    }

    // Regular click on label area (not slider): select param
    if (e.mods.isLeftButtonDown() && !e.mods.isShiftDown() &&
        !valueSlider_.getBounds().contains(e.getPosition())) {
        if (devicePath_.isValid()) {
            magda::SelectionManager::getInstance().selectParam(devicePath_, paramIndex_);
        }
    }
    // Note: Shift+drag and regular drag on slider are handled by valueSlider_ callbacks
}

void ParamSlotComponent::mouseDrag(const juce::MouseEvent& e) {
    // Handle link mode drag - vertical drag sets modulation amount or macro value
    if (isLinkModeDrag_) {
        // Calculate amount based on vertical drag distance
        // Drag down = decrease, drag up = increase
        int deltaY = linkModeDragStartY_ - e.getPosition().y;  // Inverted: up = positive
        float sensitivity = 0.005f;                            // 200 pixels = full range
        float newAmount =
            juce::jlimit(0.0f, 1.0f, linkModeDragStartAmount_ + (deltaY * sensitivity));

        // Store current drag amount for visual preview
        linkModeDragCurrentAmount_ = newAmount;

        // Update tooltip label
        int percent = static_cast<int>(newAmount * 100);
        amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);

        // Handle mod drag - set per-parameter amount
        if (activeMod_.isValid() && availableMods_ && activeMod_.modIndex >= 0 &&
            activeMod_.modIndex < static_cast<int>(availableMods_->size())) {
            magda::ModTarget thisTarget{deviceId_, paramIndex_};

            // Check if link exists
            const auto& mod = (*availableMods_)[static_cast<size_t>(activeMod_.modIndex)];
            const auto* existingLink = mod.getLink(thisTarget);
            bool isLinked = existingLink != nullptr || (mod.target.deviceId == deviceId_ &&
                                                        mod.target.paramIndex == paramIndex_);

            if (isLinked) {
                // Update existing link amount
                if (onModAmountChanged) {
                    onModAmountChanged(activeMod_.modIndex, thisTarget, newAmount);
                }
            } else {
                // Create new link with current amount
                if (onModLinkedWithAmount) {
                    onModLinkedWithAmount(activeMod_.modIndex, thisTarget, newAmount);
                }
            }

            repaint();
        }
        // Handle macro drag - set macro's global value and create link if needed
        else if (activeMacro_.isValid() && availableMacros_ && activeMacro_.macroIndex >= 0 &&
                 activeMacro_.macroIndex < static_cast<int>(availableMacros_->size())) {
            const auto& macro = (*availableMacros_)[static_cast<size_t>(activeMacro_.macroIndex)];
            magda::MacroTarget thisTarget{deviceId_, paramIndex_};

            // Check if already linked
            bool isLinked =
                macro.target.deviceId == deviceId_ && macro.target.paramIndex == paramIndex_;

            // Create link on first drag if not already linked
            if (!isLinked && onMacroLinked) {
                onMacroLinked(activeMacro_.macroIndex, thisTarget);
            }

            // Update macro's global value
            if (onMacroValueChanged) {
                onMacroValueChanged(activeMacro_.macroIndex, newAmount);
            }

            repaint();
        }
        return;
    }

    // Regular drag handling is done by valueSlider_ callbacks
}

void ParamSlotComponent::mouseUp(const juce::MouseEvent& /*e*/) {
    // Clean up link mode drag state (for both mods and macros)
    if (isLinkModeDrag_) {
        isLinkModeDrag_ = false;
        amountLabel_.setVisible(false);

        // Remove from desktop if it was added
        if (amountLabel_.isOnDesktop()) {
            amountLabel_.removeFromDesktop();
        }

        repaint();
        return;
    }

    // Mouse up handling is done by valueSlider_ callbacks
}

std::vector<std::pair<int, const magda::ModLink*>> ParamSlotComponent::getLinkedMods() const {
    std::vector<std::pair<int, const magda::ModLink*>> linked;
    if (!availableMods_ || deviceId_ == magda::INVALID_DEVICE_ID) {
        return linked;
    }

    magda::ModTarget thisTarget{deviceId_, paramIndex_};

    // If a mod is selected, only check that specific mod
    if (selectedModIndex_ >= 0 && selectedModIndex_ < static_cast<int>(availableMods_->size())) {
        const auto& mod = (*availableMods_)[static_cast<size_t>(selectedModIndex_)];
        if (const auto* link = mod.getLink(thisTarget)) {
            linked.push_back({selectedModIndex_, link});
        }
        // Legacy check
        else if (mod.target.deviceId == deviceId_ && mod.target.paramIndex == paramIndex_) {
            static magda::ModLink legacyLink;
            legacyLink.target = mod.target;
            legacyLink.amount = mod.amount;
            linked.push_back({selectedModIndex_, &legacyLink});
        }
        return linked;
    }

    // No mod selected - show all linked mods
    for (size_t i = 0; i < availableMods_->size(); ++i) {
        const auto& mod = (*availableMods_)[i];
        // Check new links vector
        if (const auto* link = mod.getLink(thisTarget)) {
            linked.push_back({static_cast<int>(i), link});
        }
        // Legacy: also check old target field
        else if (mod.target.deviceId == deviceId_ && mod.target.paramIndex == paramIndex_) {
            // Create a temporary link for legacy data
            static magda::ModLink legacyLink;
            legacyLink.target = mod.target;
            legacyLink.amount = mod.amount;
            linked.push_back({static_cast<int>(i), &legacyLink});
        }
    }
    return linked;
}

std::vector<std::pair<int, const magda::MacroInfo*>> ParamSlotComponent::getLinkedMacros() const {
    std::vector<std::pair<int, const magda::MacroInfo*>> linked;
    if (!availableMacros_ || deviceId_ == magda::INVALID_DEVICE_ID) {
        return linked;
    }

    // If a macro is selected, only check that specific macro
    if (selectedMacroIndex_ >= 0 &&
        selectedMacroIndex_ < static_cast<int>(availableMacros_->size())) {
        const auto& macro = (*availableMacros_)[static_cast<size_t>(selectedMacroIndex_)];
        if (macro.target.deviceId == deviceId_ && macro.target.paramIndex == paramIndex_) {
            linked.push_back({selectedMacroIndex_, &macro});
        }
        return linked;
    }

    // No macro selected - show all linked macros
    for (size_t i = 0; i < availableMacros_->size(); ++i) {
        const auto& macro = (*availableMacros_)[i];
        if (macro.target.deviceId == deviceId_ && macro.target.paramIndex == paramIndex_) {
            linked.push_back({static_cast<int>(i), &macro});
        }
    }
    return linked;
}

void ParamSlotComponent::paintModulationIndicators(juce::Graphics& g) {
    auto sliderBounds = valueSlider_.getBounds();

    // Guard against invalid bounds
    if (sliderBounds.getWidth() <= 4 || sliderBounds.getHeight() <= 0) {
        return;
    }

    // Use most of the slider width for the max bar width
    int maxWidth = sliderBounds.getWidth() - 4;
    int leftX = sliderBounds.getX() + 2;

    // Bar height (thickness)
    const int barHeight = 3;

    // Only draw mod/macro indicators when in link mode
    if (!isInLinkMode_) {
        return;
    }

    // If we're dragging in MOD link mode, only show mod preview at BOTTOM
    if (isLinkModeDrag_ && activeMod_.isValid()) {
        int y = sliderBounds.getBottom() - 2;
        int barWidth = juce::jmax(1, static_cast<int>(maxWidth * linkModeDragCurrentAmount_));
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.5f));
        g.fillRoundedRectangle(static_cast<float>(leftX), static_cast<float>(y - barHeight),
                               static_cast<float>(barWidth), static_cast<float>(barHeight), 1.0f);
        return;  // Don't draw other indicators while dragging
    }

    // Draw MACRO indicators (purple) at TOP - only when in macro link mode
    if (activeMacro_.isValid()) {
        int y = sliderBounds.getY() + 2;
        auto linkedMacros = getLinkedMacros();
        for (const auto& [macroIndex, macro] : linkedMacros) {
            int barWidth = juce::jmax(1, static_cast<int>(maxWidth * macro->value));

            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.5f));
            g.fillRoundedRectangle(static_cast<float>(leftX), static_cast<float>(y),
                                   static_cast<float>(barWidth), static_cast<float>(barHeight),
                                   1.0f);

            y += (barHeight + 1);  // Move down for next bar
        }
    }

    // Draw MOD indicators (orange) at BOTTOM - only when in mod link mode
    if (activeMod_.isValid()) {
        int y = sliderBounds.getBottom() - 2;
        auto linkedMods = getLinkedMods();
        for (const auto& [modIndex, link] : linkedMods) {
            float linkAmount = link->amount;
            int barWidth = juce::jmax(1, static_cast<int>(maxWidth * linkAmount));

            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.5f));
            g.fillRoundedRectangle(static_cast<float>(leftX), static_cast<float>(y - barHeight),
                                   static_cast<float>(barWidth), static_cast<float>(barHeight),
                                   1.0f);

            y -= (barHeight + 1);  // Move up for next bar
        }
    }
}

void ParamSlotComponent::showLinkMenu() {
    juce::PopupMenu menu;

    magda::ModTarget thisTarget{deviceId_, paramIndex_};

    // ========================================================================
    // Contextual mode: if a mod is selected, show simple link/unlink options
    // ========================================================================
    if (selectedModIndex_ >= 0 && availableMods_ &&
        selectedModIndex_ < static_cast<int>(availableMods_->size())) {
        const auto& selectedMod = (*availableMods_)[static_cast<size_t>(selectedModIndex_)];
        juce::String modName = selectedMod.name;

        // Check if already linked
        const auto* existingLink = selectedMod.getLink(thisTarget);
        bool isLinked = existingLink != nullptr || (selectedMod.target.deviceId == deviceId_ &&
                                                    selectedMod.target.paramIndex == paramIndex_);

        if (isLinked) {
            float currentAmount = existingLink ? existingLink->amount : selectedMod.amount;
            int amountPercent = static_cast<int>(currentAmount * 100);
            menu.addSectionHeader(modName + " (" + juce::String(amountPercent) + "%)");
            menu.addItem(1, "Unlink from " + modName);
        } else {
            menu.addSectionHeader(modName);
            menu.addItem(2, "Link to " + modName + " (50%)");
        }

        // Show contextual menu
        auto safeThis = juce::Component::SafePointer<ParamSlotComponent>(this);
        auto deviceId = deviceId_;
        auto paramIdx = paramIndex_;
        auto modIndex = selectedModIndex_;

        menu.showMenuAsync(juce::PopupMenu::Options(),
                           [safeThis, deviceId, paramIdx, modIndex](int result) {
                               if (safeThis == nullptr || result == 0) {
                                   return;
                               }

                               magda::ModTarget target{deviceId, paramIdx};

                               if (result == 1) {
                                   // Unlink
                                   if (safeThis->onModUnlinked) {
                                       safeThis->onModUnlinked(modIndex, target);
                                   }
                                   safeThis->repaint();
                               } else if (result == 2) {
                                   // Link with default amount (50%)
                                   if (safeThis->onModLinkedWithAmount) {
                                       safeThis->onModLinkedWithAmount(modIndex, target, 0.5f);
                                   }
                                   safeThis->repaint();
                               }
                           });
        return;
    }

    // ========================================================================
    // Full menu: no mod selected - show all options
    // ========================================================================
    auto linkedMods = getLinkedMods();
    auto linkedMacros = getLinkedMacros();

    // Section: Currently linked mods - unlink option only (Shift+drag to edit amount)
    if (!linkedMods.empty() || !linkedMacros.empty()) {
        menu.addSectionHeader("Currently Linked");

        for (const auto& [modIndex, link] : linkedMods) {
            juce::String modName = "Mod " + juce::String(modIndex + 1);
            if (availableMods_ && modIndex < static_cast<int>(availableMods_->size())) {
                modName = (*availableMods_)[static_cast<size_t>(modIndex)].name;
            }
            int currentAmountPercent = static_cast<int>(link->amount * 100);
            juce::String label = modName + " (" + juce::String(currentAmountPercent) + "%)";
            menu.addItem(1500 + modIndex, "Unlink " + label);
        }

        for (const auto& [macroIndex, macro] : linkedMacros) {
            menu.addItem(2000 + macroIndex, "Unlink from " + macro->name + " (Macro)");
        }

        menu.addSeparator();
    }

    // Section: Link to Mod
    if (availableMods_ && !availableMods_->empty()) {
        juce::PopupMenu modsMenu;
        for (size_t i = 0; i < availableMods_->size(); ++i) {
            const auto& mod = (*availableMods_)[i];
            bool alreadyLinked =
                mod.getLink(thisTarget) != nullptr ||
                (mod.target.deviceId == deviceId_ && mod.target.paramIndex == paramIndex_);

            if (!alreadyLinked) {
                modsMenu.addItem(3000 + static_cast<int>(i), mod.name);
            }
        }
        if (modsMenu.getNumItems() > 0) {
            menu.addSubMenu("Link to Mod", modsMenu);
        }
    }

    // Section: Link to Macro
    if (availableMacros_ && !availableMacros_->empty()) {
        juce::PopupMenu macrosMenu;
        for (size_t i = 0; i < availableMacros_->size(); ++i) {
            const auto& macro = (*availableMacros_)[i];
            bool alreadyLinked =
                (macro.target.deviceId == deviceId_ && macro.target.paramIndex == paramIndex_);
            macrosMenu.addItem(4000 + static_cast<int>(i), macro.name, !alreadyLinked,
                               alreadyLinked);
        }
        menu.addSubMenu("Link to Macro", macrosMenu);
    }

    // Show full menu
    auto safeThis = juce::Component::SafePointer<ParamSlotComponent>(this);
    auto deviceId = deviceId_;
    auto paramIdx = paramIndex_;
    auto linkedModsCopy = linkedMods;  // Copy for use in lambda

    menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, deviceId, paramIdx](int result) {
        if (safeThis == nullptr || result == 0) {
            return;
        }

        magda::ModTarget target{deviceId, paramIdx};

        if (result >= 1500 && result < 2000) {
            // Unlink from mod
            int modIndex = result - 1500;
            if (safeThis->onModUnlinked) {
                safeThis->onModUnlinked(modIndex, target);
            }
        } else if (result >= 2000 && result < 3000) {
            // Unlink from macro
            int macroIndex = result - 2000;
            if (safeThis->onMacroLinked) {
                safeThis->onMacroLinked(macroIndex, magda::MacroTarget{});
            }
        } else if (result >= 3000 && result < 4000) {
            // Link to mod at 50%
            int modIndex = result - 3000;
            if (safeThis->onModLinkedWithAmount) {
                safeThis->onModLinkedWithAmount(modIndex, target, 0.5f);
            }
        } else if (result >= 4000 && result < 5000) {
            // Link to macro
            int macroIndex = result - 4000;
            if (safeThis->onMacroLinked) {
                magda::MacroTarget macroTarget;
                macroTarget.deviceId = deviceId;
                macroTarget.paramIndex = paramIdx;
                safeThis->onMacroLinked(macroIndex, macroTarget);
            }
        }

        safeThis->repaint();
    });
}

// ============================================================================
// DragAndDropTarget
// ============================================================================

bool ParamSlotComponent::isInterestedInDragSource(const SourceDetails& details) {
    // Accept drags from mod and macro knobs
    auto desc = details.description.toString();
    return desc.startsWith("mod_drag:") || desc.startsWith("macro_drag:");
}

void ParamSlotComponent::itemDragEnter(const SourceDetails& /*details*/) {
    isDragOver_ = true;
    repaint();
}

void ParamSlotComponent::itemDragExit(const SourceDetails& /*details*/) {
    isDragOver_ = false;
    repaint();
}

void ParamSlotComponent::itemDropped(const SourceDetails& details) {
    isDragOver_ = false;

    auto desc = details.description.toString();

    // Handle mod drops: "mod_drag:trackId:topLevelDeviceId:modIndex"
    if (desc.startsWith("mod_drag:")) {
        auto parts = juce::StringArray::fromTokens(desc.substring(9), ":", "");
        if (parts.size() < 3) {
            return;
        }

        int modIndex = parts[2].getIntValue();

        // Create the link at 50% default amount
        magda::ModTarget target{deviceId_, paramIndex_};
        if (onModLinkedWithAmount) {
            onModLinkedWithAmount(modIndex, target, 0.5f);
        }

        // Auto-select this param so user can immediately edit the amount
        if (devicePath_.isValid()) {
            magda::SelectionManager::getInstance().selectParam(devicePath_, paramIndex_);
        }
    }
    // Handle macro drops: "macro_drag:trackId:topLevelDeviceId:macroIndex"
    else if (desc.startsWith("macro_drag:")) {
        auto parts = juce::StringArray::fromTokens(desc.substring(11), ":", "");
        if (parts.size() < 3) {
            return;
        }

        int macroIndex = parts[2].getIntValue();

        // Create the macro link
        magda::MacroTarget target;
        target.deviceId = deviceId_;
        target.paramIndex = paramIndex_;
        if (onMacroLinked) {
            onMacroLinked(macroIndex, target);
        }
    }

    repaint();
}

bool ParamSlotComponent::isInScopeOf(const magda::ChainNodePath& parentPath) const {
    // Check if this parameter is within the scope of the mod/macro parent

    // Must be on the same track
    if (devicePath_.trackId != parentPath.trackId) {
        return false;
    }

    // Determine if parent and device use legacy (topLevelDeviceId) or modern (steps) paths
    bool parentIsTopLevel = parentPath.topLevelDeviceId != magda::INVALID_DEVICE_ID;
    bool deviceIsTopLevel = devicePath_.topLevelDeviceId != magda::INVALID_DEVICE_ID;

    // Case 1: Parent is a top-level device
    if (parentIsTopLevel) {
        // Device must also be top-level and match exactly
        if (!deviceIsTopLevel) {
            return false;  // Parent is top-level, device is in rack/chain - no match
        }
        return devicePath_.topLevelDeviceId == parentPath.topLevelDeviceId;
    }

    // Case 2: Parent uses steps (rack/chain/device inside rack)
    if (parentPath.steps.empty()) {
        return false;
    }

    // Device must also use steps (not top-level)
    if (deviceIsTopLevel) {
        return false;  // Parent is in rack/chain, device is top-level - no match
    }

    // Both use steps - check scope based on parent type
    auto parentType = parentPath.getType();

    if (parentType == magda::ChainNodeType::Rack) {
        // Parent is a rack - parameter must be in a device inside that rack
        // devicePath should start with rack's path and have additional steps (chain -> device)

        // Must have more steps than parent (to be a descendant)
        if (devicePath_.steps.size() <= parentPath.steps.size()) {
            return false;
        }

        // Check if devicePath starts with parentPath
        for (size_t i = 0; i < parentPath.steps.size(); ++i) {
            if (devicePath_.steps[i].type != parentPath.steps[i].type ||
                devicePath_.steps[i].id != parentPath.steps[i].id) {
                return false;
            }
        }

        return true;
    } else if (parentType == magda::ChainNodeType::Device) {
        // Parent is a device - parameter must belong to that exact device
        // devicePath must match exactly

        if (devicePath_.steps.size() != parentPath.steps.size()) {
            return false;
        }

        for (size_t i = 0; i < parentPath.steps.size(); ++i) {
            if (devicePath_.steps[i].type != parentPath.steps[i].type ||
                devicePath_.steps[i].id != parentPath.steps[i].id) {
                return false;
            }
        }

        return true;
    }

    // Other types (Chain, etc.) - for now, don't support
    return false;
}

}  // namespace magda::daw::ui
