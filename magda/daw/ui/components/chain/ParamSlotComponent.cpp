#include "ParamSlotComponent.hpp"

#include "ParamLinkMenu.hpp"
#include "ParamLinkResolver.hpp"
#include "ParamModulationPainter.hpp"
#include "ParamWidgetSetup.hpp"
#include "core/LinkModeManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

ParamSlotComponent::ParamSlotComponent(int paramIndex) : paramIndex_(paramIndex) {
    magda::LinkModeManager::getInstance().addListener(this);

    setInterceptsMouseClicks(true, true);

    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    valueSlider_.setRange(0.0, 1.0, 0.01);
    valueSlider_.setValue(0.5, juce::dontSendNotification);
    valueSlider_.setTextColour(juce::Colours::white);
    valueSlider_.setBackgroundColour(juce::Colours::transparentBlack);
    valueSlider_.onValueChanged = [this](double value) {
        if (onValueChanged) {
            onValueChanged(value);
        }
    };
    valueSlider_.onClicked = [this]() {
        if (devicePath_.isValid()) {
            magda::SelectionManager::getInstance().selectParam(devicePath_, paramIndex_);
        }
    };
    valueSlider_.onRightClicked = [this]() {
        showParamLinkMenu(this, buildLinkContext(),
                          {onModUnlinked, onModLinkedWithAmount, onMacroLinked,
                           onMacroLinkedWithAmount, onMacroUnlinked});
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

        const auto* existingLink = selectedMod.getLink(thisTarget);
        bool isLinked = existingLink != nullptr || (selectedMod.target.deviceId == deviceId_ &&
                                                    selectedMod.target.paramIndex == paramIndex_);

        float startAmount = 0.5f;
        if (!isLinked) {
            if (onModLinkedWithAmount) {
                onModLinkedWithAmount(selectedModIndex_, thisTarget, 0.5f);
            }
        } else {
            startAmount = existingLink ? existingLink->amount : selectedMod.amount;
        }
        valueSlider_.setShiftDragStartValue(startAmount);

        isModAmountDrag_ = true;
        modAmountDragModIndex_ = selectedModIndex_;

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

    valueSlider_.setRightClickEditsText(true);
    addAndMakeVisible(valueSlider_);

    setInterceptsMouseClicks(true, true);
}

ParamSlotComponent::~ParamSlotComponent() {
    stopTimer();

    if (amountLabel_.isOnDesktop()) {
        amountLabel_.removeFromDesktop();
    }
    magda::LinkModeManager::getInstance().removeListener(this);
}

// ============================================================================
// buildLinkContext
// ============================================================================

ParamLinkContext ParamSlotComponent::buildLinkContext() const {
    return {deviceId_,          paramIndex_,      devicePath_,          availableMods_,
            availableRackMods_, availableMacros_, availableRackMacros_, selectedModIndex_,
            selectedMacroIndex_};
}

// ============================================================================
// Link mode listener
// ============================================================================

void ParamSlotComponent::modLinkModeChanged(bool active, const magda::ModSelection& selection) {
    bool isInScope = isInScopeOf(devicePath_, selection.parentPath);

    isInLinkMode_ = active && isInScope;

    if (active && isInScope) {
        activeMod_ = selection;
        activeMacro_ = magda::MacroSelection{};
    } else {
        activeMod_ = magda::ModSelection{};
    }

    if (!active || !isInScope) {
        isLinkModeDrag_ = false;
        amountLabel_.setVisible(false);
        if (amountLabel_.isOnDesktop()) {
            amountLabel_.removeFromDesktop();
        }
    }

    valueSlider_.setInterceptsMouseClicks(!isInLinkMode_, !isInLinkMode_);

    if (isMouseOver()) {
        setMouseCursor(isInLinkMode_ ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
    }

    repaint();
}

void ParamSlotComponent::macroLinkModeChanged(bool active, const magda::MacroSelection& selection) {
    DBG("macroLinkModeChanged param=" << paramIndex_ << " active=" << (active ? 1 : 0)
                                      << " macroIndex=" << selection.macroIndex);

    bool isInScope = isInScopeOf(devicePath_, selection.parentPath);
    DBG("  isInScope=" << (isInScope ? 1 : 0));

    isInLinkMode_ = active && isInScope;

    if (active && isInScope) {
        activeMacro_ = selection;
        activeMod_ = magda::ModSelection{};
    } else {
        activeMacro_ = magda::MacroSelection{};
    }

    if (!active || !isInScope) {
        isLinkModeDrag_ = false;
        amountLabel_.setVisible(false);
        if (amountLabel_.isOnDesktop()) {
            amountLabel_.removeFromDesktop();
        }
    }

    valueSlider_.setInterceptsMouseClicks(!isInLinkMode_, !isInLinkMode_);

    if (isMouseOver()) {
        setMouseCursor(isInLinkMode_ ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
    }

    repaint();
}

// ============================================================================
// handleLinkModeClick / showLinkModeSlider / hideLinkModeSlider
// ============================================================================

void ParamSlotComponent::handleLinkModeClick() {
    if (!isInLinkMode_) {
        return;
    }

    const auto* modPtr = resolveModPtr(activeMod_, devicePath_, availableMods_, availableRackMods_);

    if (modPtr) {
        magda::ModTarget thisTarget{deviceId_, paramIndex_};

        const auto* existingLink = modPtr->getLink(thisTarget);
        bool isLinked = existingLink != nullptr || (modPtr->target.deviceId == deviceId_ &&
                                                    modPtr->target.paramIndex == paramIndex_);

        float initialAmount =
            isLinked ? (existingLink ? existingLink->amount : modPtr->amount) : 0.5f;

        if (!isLinked) {
            if (onModLinkedWithAmount) {
                onModLinkedWithAmount(activeMod_.modIndex, thisTarget, initialAmount);
            }
        }

        showLinkModeSlider(!isLinked, initialAmount);
    } else {
        const auto* macroPtr =
            resolveMacroPtr(activeMacro_, devicePath_, availableMacros_, availableRackMacros_);

        if (macroPtr) {
            magda::MacroTarget thisTarget{deviceId_, paramIndex_};

            const auto* existingLink = macroPtr->getLink(thisTarget);
            bool isLinked = existingLink != nullptr || (macroPtr->target.deviceId == deviceId_ &&
                                                        macroPtr->target.paramIndex == paramIndex_);

            float initialAmount = isLinked ? (existingLink ? existingLink->amount : 0.5f) : 0.5f;

            if (!isLinked) {
                if (onMacroLinkedWithAmount) {
                    onMacroLinkedWithAmount(activeMacro_.macroIndex, thisTarget, initialAmount);
                }
            }

            showLinkModeSlider(!isLinked, initialAmount);
        }
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

    auto accentColor = activeMod_.isValid() ? DarkTheme::getColour(DarkTheme::ACCENT_ORANGE)
                                            : DarkTheme::getColour(DarkTheme::ACCENT_PURPLE);
    linkModeSlider_->setColour(juce::Slider::thumbColourId, accentColor);
    linkModeSlider_->setColour(juce::Slider::trackColourId, accentColor.withAlpha(0.5f));

    linkModeSlider_->setValue(initialAmount * 100.0, juce::dontSendNotification);
    linkModeSlider_->setBounds(getLocalBounds().reduced(2));
    linkModeSlider_->toFront(true);
    linkModeSlider_->setVisible(true);
    DBG("  Slider NOW visible=" << (linkModeSlider_->isVisible() ? 1 : 0));
}

void ParamSlotComponent::hideLinkModeSlider() {
    if (linkModeSlider_) {
        DBG("HIDE SLIDER on param "
            << paramIndex_ << " (was visible=" << (linkModeSlider_->isVisible() ? 1 : 0) << ")");
        linkModeSlider_->setVisible(false);
    }
}

// ============================================================================
// Simple setters / getters
// ============================================================================

void ParamSlotComponent::setParamName(const juce::String& name) {
    nameLabel_.setText(name, juce::dontSendNotification);
}

void ParamSlotComponent::setParamValue(double value) {
    valueSlider_.setValue(value, juce::dontSendNotification);
}

bool ParamSlotComponent::isBeingDragged() const {
    return valueSlider_.isBeingDragged();
}

void ParamSlotComponent::setShowEmptyText(bool show) {
    valueSlider_.setShowEmptyText(show);
}

void ParamSlotComponent::setParameterInfo(const magda::ParameterInfo& info) {
    paramInfo_ = info;

    // Hide all widgets first
    valueSlider_.setVisible(false);
    if (discreteCombo_)
        discreteCombo_->setVisible(false);
    if (boolToggle_)
        boolToggle_->setVisible(false);

    if (info.scale == magda::ParameterScale::Boolean) {
        if (!boolToggle_) {
            boolToggle_ = std::make_unique<juce::ToggleButton>();
            addAndMakeVisible(*boolToggle_);
        }
        configureBoolToggle(*boolToggle_, info, onValueChanged);
        boolToggle_->setVisible(true);
    } else if (info.scale == magda::ParameterScale::Discrete && !info.choices.empty()) {
        if (!discreteCombo_) {
            discreteCombo_ = std::make_unique<juce::ComboBox>();
            addAndMakeVisible(*discreteCombo_);
        }
        configureDiscreteCombo(*discreteCombo_, info, onValueChanged);
        discreteCombo_->setVisible(true);
    } else {
        valueSlider_.setVisible(true);
        configureSliderFormatting(valueSlider_, info);
    }

    resized();
}

void ParamSlotComponent::setFonts(const juce::Font& labelFont, const juce::Font& valueFont) {
    nameLabel_.setFont(labelFont);
    valueSlider_.setFont(valueFont);
    valueSlider_.setTextColour(juce::Colours::white);
    valueSlider_.setBackgroundColour(juce::Colours::transparentBlack);
}

// ============================================================================
// Painting
// ============================================================================

void ParamSlotComponent::paint(juce::Graphics& /*g*/) {
    // Selection highlight is drawn in paintOverChildren()
}

void ParamSlotComponent::paintOverChildren(juce::Graphics& g) {
    // Disabled overlay
    if (!isEnabled()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).withAlpha(0.6f));
        g.fillRect(getLocalBounds());
        return;
    }

    // Draw link mode / drag-over / selection highlight
    if (isInLinkMode_) {
        auto color = activeMod_.isValid()
                         ? DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f)
                         : DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.15f);
        g.setColour(color);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    } else if (isDragOver_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    } else if (selected_) {
        g.setColour(juce::Colour(0xff888888));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 2.0f, 2.0f);
    }

    // Modulation indicator bars — delegated to free function
    ModulationPaintContext paintCtx;
    paintCtx.sliderBounds = valueSlider_.getBounds();
    paintCtx.cellBounds = getLocalBounds();
    paintCtx.currentParamValue = static_cast<float>(valueSlider_.getValue());
    paintCtx.isInLinkMode = isInLinkMode_;
    paintCtx.isLinkModeDrag = isLinkModeDrag_;
    paintCtx.linkModeDragCurrentAmount = linkModeDragCurrentAmount_;
    paintCtx.activeMod = activeMod_;
    paintCtx.activeMacro = activeMacro_;
    paintCtx.linkCtx = buildLinkContext();

    paintModulationIndicators(g, paintCtx);

    // Update timer state after painting (avoids const_cast)
    updateModTimerState();
}

void ParamSlotComponent::resized() {
    auto bounds = getLocalBounds();

    int labelHeight = juce::jmin(12, getHeight() / 3);
    nameLabel_.setBounds(bounds.removeFromTop(labelHeight));

    if (discreteCombo_ && discreteCombo_->isVisible()) {
        discreteCombo_->setBounds(bounds);
    } else if (boolToggle_ && boolToggle_->isVisible()) {
        auto toggleBounds = bounds.withSizeKeepingCentre(bounds.getWidth(), 20);
        boolToggle_->setBounds(toggleBounds);
    } else {
        valueSlider_.setBounds(bounds);
    }
}

// ============================================================================
// Mouse handling
// ============================================================================

void ParamSlotComponent::mouseEnter(const juce::MouseEvent& /*e*/) {
    if (isInLinkMode_) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
}

void ParamSlotComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ParamSlotComponent::mouseDown(const juce::MouseEvent& e) {
    // Right-click — let value controls handle their own, otherwise show link menu
    if (e.mods.isPopupMenu()) {
        if (valueSlider_.isVisible() && valueSlider_.getBounds().contains(e.getPosition())) {
            return;
        }
        if (discreteCombo_ && discreteCombo_->isVisible() &&
            discreteCombo_->getBounds().contains(e.getPosition())) {
            return;
        }
        if (boolToggle_ && boolToggle_->isVisible() &&
            boolToggle_->getBounds().contains(e.getPosition())) {
            return;
        }
        showParamLinkMenu(this, buildLinkContext(),
                          {onModUnlinked, onModLinkedWithAmount, onMacroLinked,
                           onMacroLinkedWithAmount, onMacroUnlinked});
        return;
    }

    // Link mode: prepare for drag to set amount/value
    if (isInLinkMode_ && e.mods.isLeftButtonDown()) {
        // Mod link mode
        if (activeMod_.isValid()) {
            const auto* modPtr =
                resolveModPtr(activeMod_, devicePath_, availableMods_, availableRackMods_);

            float initialAmount = 0.5f;
            bool isLinked = false;

            if (modPtr) {
                magda::ModTarget thisTarget{deviceId_, paramIndex_};
                const auto* existingLink = modPtr->getLink(thisTarget);
                isLinked = existingLink != nullptr || (modPtr->target.deviceId == deviceId_ &&
                                                       modPtr->target.paramIndex == paramIndex_);
                if (isLinked) {
                    initialAmount = existingLink ? existingLink->amount : modPtr->amount;
                }
            }

            isLinkModeDrag_ = true;
            linkModeDragStartAmount_ = initialAmount;
            linkModeDragCurrentAmount_ = initialAmount;
            linkModeDragStartY_ = e.getMouseDownY();

            int percent = static_cast<int>(initialAmount * 100);
            amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);
            amountLabel_.setColour(juce::Label::backgroundColourId,
                                   DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.95f));

            if (!amountLabel_.isOnDesktop()) {
                amountLabel_.addToDesktop(juce::ComponentPeer::windowIsTemporary |
                                          juce::ComponentPeer::windowIgnoresMouseClicks);
            }

            auto screenBounds = getScreenBounds();
            amountLabel_.setBounds(screenBounds.getX(), screenBounds.getY() - 22,
                                   screenBounds.getWidth(), 20);
            amountLabel_.setVisible(true);
            amountLabel_.toFront(true);
            repaint();
            return;
        }

        // Macro link mode
        if (activeMacro_.isValid()) {
            const auto* macroPtr =
                resolveMacroPtr(activeMacro_, devicePath_, availableMacros_, availableRackMacros_);

            float initialAmount = 0.5f;
            bool isLinked = false;

            if (macroPtr) {
                magda::MacroTarget thisTarget{deviceId_, paramIndex_};
                const auto* existingLink = macroPtr->getLink(thisTarget);
                isLinked = existingLink != nullptr;
                if (isLinked) {
                    initialAmount = existingLink->amount;
                }
            }

            isLinkModeDrag_ = true;
            linkModeDragStartAmount_ = initialAmount;
            linkModeDragCurrentAmount_ = initialAmount;
            linkModeDragStartY_ = e.getMouseDownY();

            int percent = static_cast<int>(initialAmount * 100);
            amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);
            amountLabel_.setColour(juce::Label::backgroundColourId,
                                   DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.95f));

            if (!amountLabel_.isOnDesktop()) {
                amountLabel_.addToDesktop(juce::ComponentPeer::windowIsTemporary |
                                          juce::ComponentPeer::windowIgnoresMouseClicks);
            }

            auto screenBounds = getScreenBounds();
            amountLabel_.setBounds(screenBounds.getX(), screenBounds.getY() - 22,
                                   screenBounds.getWidth(), 20);
            amountLabel_.setVisible(true);
            amountLabel_.toFront(true);
            repaint();
            return;
        }
    }

    // Regular click on label area: select param
    if (e.mods.isLeftButtonDown() && !e.mods.isShiftDown() &&
        !valueSlider_.getBounds().contains(e.getPosition())) {
        if (devicePath_.isValid()) {
            magda::SelectionManager::getInstance().selectParam(devicePath_, paramIndex_);
        }
    }
}

void ParamSlotComponent::mouseDrag(const juce::MouseEvent& e) {
    if (isLinkModeDrag_) {
        int deltaY = linkModeDragStartY_ - e.getPosition().y;
        float sensitivity = 0.005f;
        float newAmount =
            juce::jlimit(0.0f, 1.0f, linkModeDragStartAmount_ + (deltaY * sensitivity));

        linkModeDragCurrentAmount_ = newAmount;

        int percent = static_cast<int>(newAmount * 100);
        amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);

        // Resolve mod/macro and dispatch amount change
        const auto* modPtr =
            resolveModPtr(activeMod_, devicePath_, availableMods_, availableRackMods_);

        if (modPtr) {
            magda::ModTarget thisTarget{deviceId_, paramIndex_};

            const auto* existingLink = modPtr->getLink(thisTarget);
            bool isLinked = existingLink != nullptr || (modPtr->target.deviceId == deviceId_ &&
                                                        modPtr->target.paramIndex == paramIndex_);

            if (isLinked) {
                if (onModAmountChanged) {
                    onModAmountChanged(activeMod_.modIndex, thisTarget, newAmount);
                }
            } else {
                if (onModLinkedWithAmount) {
                    onModLinkedWithAmount(activeMod_.modIndex, thisTarget, newAmount);
                }
            }
            repaint();
        } else if (activeMacro_.isValid()) {
            const auto* macroPtr =
                resolveMacroPtr(activeMacro_, devicePath_, availableMacros_, availableRackMacros_);
            if (macroPtr) {
                magda::MacroTarget thisTarget{deviceId_, paramIndex_};

                const auto* existingLink = macroPtr->getLink(thisTarget);
                bool isLinked = existingLink != nullptr;

                if (isLinked) {
                    if (onMacroAmountChanged) {
                        onMacroAmountChanged(activeMacro_.macroIndex, thisTarget, newAmount);
                    }
                } else {
                    if (onMacroLinkedWithAmount) {
                        onMacroLinkedWithAmount(activeMacro_.macroIndex, thisTarget, newAmount);
                    }
                }
                repaint();
            }
        }
        return;
    }
}

void ParamSlotComponent::mouseUp(const juce::MouseEvent& /*e*/) {
    if (isLinkModeDrag_) {
        isLinkModeDrag_ = false;
        amountLabel_.setVisible(false);

        if (amountLabel_.isOnDesktop()) {
            amountLabel_.removeFromDesktop();
        }

        repaint();
        return;
    }
}

// ============================================================================
// DragAndDropTarget
// ============================================================================

bool ParamSlotComponent::isInterestedInDragSource(const SourceDetails& details) {
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

    if (desc.startsWith("mod_drag:")) {
        auto parts = juce::StringArray::fromTokens(desc.substring(9), ":", "");
        if (parts.size() < 3) {
            return;
        }

        int modIndex = parts[2].getIntValue();
        magda::ModTarget target{deviceId_, paramIndex_};
        if (onModLinkedWithAmount) {
            onModLinkedWithAmount(modIndex, target, 0.5f);
        }

        if (devicePath_.isValid()) {
            magda::SelectionManager::getInstance().selectParam(devicePath_, paramIndex_);
        }
    } else if (desc.startsWith("macro_drag:")) {
        auto parts = juce::StringArray::fromTokens(desc.substring(11), ":", "");
        if (parts.size() < 3) {
            return;
        }

        int macroIndex = parts[2].getIntValue();
        magda::MacroTarget target;
        target.deviceId = deviceId_;
        target.paramIndex = paramIndex_;
        if (onMacroLinked) {
            onMacroLinked(macroIndex, target);
        }
    }

    repaint();
}

// ============================================================================
// Timer
// ============================================================================

void ParamSlotComponent::timerCallback() {
    repaint();
}

void ParamSlotComponent::updateModTimerState() {
    if (hasActiveLinks(buildLinkContext())) {
        if (!isTimerRunning()) {
            startTimer(33);  // ~30 FPS
        }
    } else {
        stopTimer();
    }
}

}  // namespace magda::daw::ui
