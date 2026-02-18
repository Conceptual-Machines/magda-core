#include "LinkableTextSlider.hpp"

#include "ParamLinkResolver.hpp"
#include "core/LinkModeManager.hpp"
#include "ui/components/chain/ParamLinkMenu.hpp"
#include "ui/components/chain/ParamModulationPainter.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

LinkableTextSlider::LinkableTextSlider(TextSlider::Format format) : slider_(format) {
    magda::LinkModeManager::getInstance().addListener(this);

    setInterceptsMouseClicks(true, true);

    slider_.onValueChanged = [this](double value) {
        if (onValueChanged) {
            onValueChanged(value);
        }
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
    slider_.onShiftDragStart = [this](float /*startValue*/) {
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
        slider_.setShiftDragStartValue(startAmount);

        isModAmountDrag_ = true;
        modAmountDragModIndex_ = selectedModIndex_;

        int percent = static_cast<int>(startAmount * 100);
        amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);
        amountLabel_.setBounds(getLocalBounds().withHeight(14).translated(0, -16));
        amountLabel_.setVisible(true);
    };

    slider_.onShiftDrag = [this](float newAmount) {
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

    slider_.onShiftDragEnd = [this]() {
        isModAmountDrag_ = false;
        modAmountDragModIndex_ = -1;
        amountLabel_.setVisible(false);
    };

    slider_.onShiftClicked = [this]() {
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

    slider_.setRightClickEditsText(true);
    addAndMakeVisible(slider_);
}

LinkableTextSlider::~LinkableTextSlider() {
    if (amountLabel_.isOnDesktop()) {
        amountLabel_.removeFromDesktop();
    }
    magda::LinkModeManager::getInstance().removeListener(this);
}

// ============================================================================
// TextSlider forwarding
// ============================================================================

void LinkableTextSlider::setValue(double value, juce::NotificationType notification) {
    slider_.setValue(value, notification);
}

double LinkableTextSlider::getValue() const {
    return slider_.getValue();
}

void LinkableTextSlider::setRange(double min, double max, double step) {
    slider_.setRange(min, max, step);
}

void LinkableTextSlider::setValueFormatter(std::function<juce::String(double)> formatter) {
    slider_.setValueFormatter(std::move(formatter));
}

void LinkableTextSlider::setValueParser(std::function<double(const juce::String&)> parser) {
    slider_.setValueParser(std::move(parser));
}

void LinkableTextSlider::setRightClickEditsText(bool shouldEdit) {
    slider_.setRightClickEditsText(shouldEdit);
}

void LinkableTextSlider::setFont(const juce::Font& font) {
    slider_.setFont(font);
}

void LinkableTextSlider::setTextColour(const juce::Colour& colour) {
    slider_.setTextColour(colour);
}

void LinkableTextSlider::setBackgroundColour(const juce::Colour& colour) {
    slider_.setBackgroundColour(colour);
}

TextSlider& LinkableTextSlider::getSlider() {
    return slider_;
}

bool LinkableTextSlider::isBeingDragged() const {
    return slider_.isBeingDragged();
}

// ============================================================================
// Linking context setters
// ============================================================================

void LinkableTextSlider::setLinkContext(magda::DeviceId deviceId, int paramIndex,
                                        const magda::ChainNodePath& devicePath) {
    deviceId_ = deviceId;
    paramIndex_ = paramIndex;
    devicePath_ = devicePath;
}

void LinkableTextSlider::setAvailableMods(const magda::ModArray* mods) {
    availableMods_ = mods;
}

void LinkableTextSlider::setAvailableMacros(const magda::MacroArray* macros) {
    availableMacros_ = macros;
}

void LinkableTextSlider::setAvailableRackMods(const magda::ModArray* rackMods) {
    availableRackMods_ = rackMods;
}

void LinkableTextSlider::setAvailableRackMacros(const magda::MacroArray* rackMacros) {
    availableRackMacros_ = rackMacros;
}

void LinkableTextSlider::setSelectedModIndex(int modIndex) {
    selectedModIndex_ = modIndex;
    repaint();
}

void LinkableTextSlider::setSelectedMacroIndex(int macroIndex) {
    selectedMacroIndex_ = macroIndex;
    repaint();
}

// ============================================================================
// Link mode listener
// ============================================================================

void LinkableTextSlider::modLinkModeChanged(bool active, const magda::ModSelection& selection) {
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

    slider_.setInterceptsMouseClicks(!isInLinkMode_, !isInLinkMode_);

    if (isMouseOver()) {
        setMouseCursor(isInLinkMode_ ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
    }

    repaint();
}

void LinkableTextSlider::macroLinkModeChanged(bool active, const magda::MacroSelection& selection) {
    bool isInScope = isInScopeOf(devicePath_, selection.parentPath);

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

    slider_.setInterceptsMouseClicks(!isInLinkMode_, !isInLinkMode_);

    if (isMouseOver()) {
        setMouseCursor(isInLinkMode_ ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
    }

    repaint();
}

// ============================================================================
// Layout & painting
// ============================================================================

void LinkableTextSlider::resized() {
    slider_.setBounds(getLocalBounds());
}

void LinkableTextSlider::paintOverChildren(juce::Graphics& g) {
    if (isInLinkMode_) {
        auto color = activeMod_.isValid()
                         ? DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.15f)
                         : DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).withAlpha(0.15f);
        g.setColour(color);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);
    }

    // Modulation indicator bars
    ModulationPaintContext paintCtx;
    paintCtx.sliderBounds = slider_.getBounds();
    paintCtx.cellBounds = getLocalBounds();
    paintCtx.currentParamValue = static_cast<float>(slider_.getNormalizedValue());
    paintCtx.isInLinkMode = isInLinkMode_;
    paintCtx.isLinkModeDrag = isLinkModeDrag_;
    paintCtx.linkModeDragCurrentAmount = linkModeDragCurrentAmount_;
    paintCtx.activeMod = activeMod_;
    paintCtx.activeMacro = activeMacro_;
    paintCtx.linkCtx = buildLinkContext();

    paintModulationIndicators(g, paintCtx);

    updateModTimerState();
}

// ============================================================================
// Mouse handling
// ============================================================================

void LinkableTextSlider::mouseEnter(const juce::MouseEvent& /*e*/) {
    if (isInLinkMode_) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
}

void LinkableTextSlider::mouseExit(const juce::MouseEvent& /*e*/) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void LinkableTextSlider::mouseDown(const juce::MouseEvent& e) {
    // Right-click: show link/unlink context menu
    if (e.mods.isPopupMenu() && deviceId_ != magda::INVALID_DEVICE_ID) {
        showParamLinkMenu(this, buildLinkContext(),
                          {onModUnlinked, onModLinkedWithAmount, nullptr, onMacroLinkedWithAmount,
                           onMacroUnlinked});
        return;
    }

    if (!isInLinkMode_ || !e.mods.isLeftButtonDown()) {
        return;
    }

    // Mod link mode
    if (activeMod_.isValid()) {
        const auto* modPtr =
            resolveModPtr(activeMod_, devicePath_, availableMods_, availableRackMods_);

        float initialAmount = 0.0f;
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

        float initialAmount = 0.0f;
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

void LinkableTextSlider::mouseDrag(const juce::MouseEvent& e) {
    if (!isLinkModeDrag_) {
        return;
    }

    int deltaY = linkModeDragStartY_ - e.getPosition().y;
    float sensitivity = 0.005f;
    float newAmount = juce::jlimit(-1.0f, 1.0f, linkModeDragStartAmount_ + (deltaY * sensitivity));

    linkModeDragCurrentAmount_ = newAmount;

    int percent = static_cast<int>(newAmount * 100);
    amountLabel_.setText(juce::String(percent) + "%", juce::dontSendNotification);

    // Resolve mod/macro and dispatch amount change
    const auto* modPtr = resolveModPtr(activeMod_, devicePath_, availableMods_, availableRackMods_);

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
}

void LinkableTextSlider::mouseUp(const juce::MouseEvent& /*e*/) {
    if (isLinkModeDrag_) {
        isLinkModeDrag_ = false;
        amountLabel_.setVisible(false);

        if (amountLabel_.isOnDesktop()) {
            amountLabel_.removeFromDesktop();
        }

        repaint();
    }
}

// ============================================================================
// Modulation display
// ============================================================================

ParamLinkContext LinkableTextSlider::buildLinkContext() const {
    return {deviceId_,          paramIndex_,      devicePath_,          availableMods_,
            availableRackMods_, availableMacros_, availableRackMacros_, selectedModIndex_,
            selectedMacroIndex_};
}

void LinkableTextSlider::timerCallback() {
    repaint();
}

void LinkableTextSlider::updateModTimerState() {
    if (hasActiveLinks(buildLinkContext())) {
        if (!isTimerRunning()) {
            startTimer(33);  // ~30 FPS
        }
    } else {
        stopTimer();
    }
}

}  // namespace magda::daw::ui
