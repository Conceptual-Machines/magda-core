#include "LinkableTextSlider.hpp"

#include "core/LinkModeManager.hpp"
#include "ui/components/chain/params/ParamLinkMenu.hpp"
#include "ui/components/chain/params/ParamLinkResolver.hpp"
#include "ui/components/chain/params/ParamModulationPainter.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

juce::String linkPathString(const magda::ChainNodePath& path) {
    return path.isValid() ? path.toString() : juce::String("<invalid>");
}

juce::String yesNo(bool value) {
    return value ? "yes" : "no";
}

}  // namespace

// ============================================================================
// Construction / Destruction
// ============================================================================

LinkableTextSlider::LinkableTextSlider(TextSlider::Format format) : slider_(format) {
    magda::LinkModeManager::getInstance().addListener(this);
    magda::MidiLearnCoordinator::getInstance().addListener(this);
    magda::BindingRegistry::getInstance().addListener(this);
    magda::ControllerRegistry::getInstance().addListener(this);

    setInterceptsMouseClicks(true, true);
    slider_.setShowFillIndicator(false);

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
        magda::ControlTarget thisTarget =
            magda::ControlTarget::pluginParam(devicePath_, paramIndex_);

        const auto* existingLink = selectedMod.getLink(thisTarget);
        bool isLinked = existingLink != nullptr;

        float startAmount = 0.5f;
        if (!isLinked) {
            if (onModLinkedWithAmount) {
                onModLinkedWithAmount(selectedModIndex_, thisTarget, 0.5f);
            }
        } else if (existingLink) {
            startAmount = existingLink->amount;
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
        magda::ControlTarget thisTarget =
            magda::ControlTarget::pluginParam(devicePath_, paramIndex_);
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
        magda::ControlTarget thisTarget =
            magda::ControlTarget::pluginParam(devicePath_, paramIndex_);

        const auto* existingLink = selectedMod.getLink(thisTarget);
        bool isLinked = existingLink != nullptr;

        if (!isLinked && onModLinkedWithAmount) {
            onModLinkedWithAmount(selectedModIndex_, thisTarget, 0.5f);
            repaint();
        }
    };

    slider_.setRightClickEditsText(false);
    slider_.onRightClicked = [this]() {
        if (deviceId_ != magda::INVALID_DEVICE_ID) {
            showParamLinkMenu(this, buildLinkContext(),
                              {.onModUnlinked = onModUnlinked,
                               .onRackModUnlinked = onRackModUnlinked,
                               .onTrackModUnlinked = onTrackModUnlinked,
                               .onModLinkedWithAmount = onModLinkedWithAmount,
                               .onMacroLinked = onMacroLinked,
                               .onMacroLinkedWithAmount = onMacroLinkedWithAmount,
                               .onMacroUnlinked = onMacroUnlinked,
                               .onRackMacroLinked = onRackMacroLinked,
                               .onTrackMacroLinked = onTrackMacroLinked,
                               .onRackMacroUnlinked = onRackMacroUnlinked,
                               .onTrackMacroUnlinked = onTrackMacroUnlinked,
                               .onShowAutomationLane = onShowAutomationLane,
                               .onMidiLearn = onMidiLearn,
                               .onMidiClear = onMidiClear});
        }
    };
    // Default MIDI Learn wiring: delegate to MidiLearnCoordinator singleton
    onMidiLearn = [](magda::ChainNodePath path, int paramIdx, juce::String paramName) {
        magda::MidiLearnCoordinator::getInstance().beginLearn(
            magda::ControlTarget::pluginParam(path, paramIdx), paramName);
    };
    onMidiClear = [](magda::ChainNodePath path, int paramIdx) {
        magda::MidiLearnCoordinator::getInstance().clearMappings(
            magda::ControlTarget::pluginParam(path, paramIdx));
    };

    addAndMakeVisible(slider_);
}

LinkableTextSlider::~LinkableTextSlider() {
    if (amountLabel_.isOnDesktop()) {
        amountLabel_.removeFromDesktop();
    }
    if (isInMidiLearnMode_) {
        magda::MidiLearnCoordinator::getInstance().cancelLearn();
    }
    magda::MidiLearnCoordinator::getInstance().removeListener(this);
    magda::BindingRegistry::getInstance().removeListener(this);
    magda::ControllerRegistry::getInstance().removeListener(this);
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

void LinkableTextSlider::setSkewForCentre(double centreValue) {
    slider_.setSkewForCentre(centreValue);
}

void LinkableTextSlider::setValueFormatter(std::function<juce::String(double)> formatter) {
    slider_.setValueFormatter(std::move(formatter));
}

void LinkableTextSlider::setValueParser(std::function<double(const juce::String&)> parser) {
    slider_.setValueParser(std::move(parser));
}

void LinkableTextSlider::setParameterInfo(const magda::ParameterInfo& info) {
    slider_.setParameterInfo(info);
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

void LinkableTextSlider::setOrientation(TextSlider::Orientation orientation) {
    slider_.setOrientation(orientation);
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

void LinkableTextSlider::setParamIndex(int paramIndex) {
    paramIndex_ = paramIndex;
}

void LinkableTextSlider::setLinkContext(magda::DeviceId deviceId, int paramIndex,
                                        const magda::ChainNodePath& devicePath) {
    deviceId_ = deviceId;
    paramIndex_ = paramIndex;
    devicePath_ = devicePath;
    linkOwnerPath_ = devicePath;
    refreshMidiBindingState();

    // Wire the underlying TextSlider's automation target so the purple
    // "automated" tint paints when a lane exists for this param, and so
    // drag gestures trigger the touch/override bookkeeping on the lane.
    magda::AutomationTarget target;
    target.kind = magda::ControlTarget::Kind::PluginParam;
    target.devicePath.trackId = devicePath.trackId;
    target.devicePath = devicePath;
    target.paramIndex = paramIndex;
    if (target.isValid())
        slider_.setAutomationTarget(target);
    else
        slider_.clearAutomationTarget();
}

void LinkableTextSlider::setLinkOwnerPath(const magda::ChainNodePath& ownerPath) {
    linkOwnerPath_ = ownerPath;
    DBG("[LinkableSlider] set owner param=" << paramIndex_
                                            << " owner=" << linkPathString(linkOwnerPath_)
                                            << " target=" << linkPathString(devicePath_));
    refreshLinkModeState();
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

void LinkableTextSlider::setAvailableTrackMods(const magda::ModArray* trackMods) {
    availableTrackMods_ = trackMods;
}

void LinkableTextSlider::setAvailableTrackMacros(const magda::MacroArray* trackMacros) {
    availableTrackMacros_ = trackMacros;
}

void LinkableTextSlider::setSelectedModIndex(int modIndex) {
    selectedModIndex_ = modIndex;
    repaint();
}

void LinkableTextSlider::setSelectedMacroIndex(int macroIndex) {
    selectedMacroIndex_ = macroIndex;
    repaint();
}

void LinkableTextSlider::refreshLinkModeState() {
    auto& manager = magda::LinkModeManager::getInstance();
    if (manager.getLinkModeType() == magda::LinkModeType::Mod) {
        modLinkModeChanged(true, manager.getModInLinkMode());
    } else if (manager.getLinkModeType() == magda::LinkModeType::Macro) {
        macroLinkModeChanged(true, manager.getMacroInLinkMode());
    } else {
        modLinkModeChanged(false, {});
        macroLinkModeChanged(false, {});
    }
}

// ============================================================================
// Link mode listener
// ============================================================================

void LinkableTextSlider::modLinkModeChanged(bool active, const magda::ModSelection& selection) {
    bool isInScope = isInScopeOf(linkOwnerPath_, selection.parentPath);

    isInLinkMode_ = active && isInScope;
    DBG("[LinkableSlider] mod mode active="
        << yesNo(active) << " param=" << paramIndex_ << " owner=" << linkPathString(linkOwnerPath_)
        << " target=" << linkPathString(devicePath_)
        << " selection=" << linkPathString(selection.parentPath) << " index=" << selection.modIndex
        << " inScope=" << yesNo(isInScope) << " final=" << yesNo(isInLinkMode_));

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
    bool isInScope = isInScopeOf(linkOwnerPath_, selection.parentPath);

    isInLinkMode_ = active && isInScope;
    DBG("[LinkableSlider] macro mode active="
        << yesNo(active) << " param=" << paramIndex_ << " owner=" << linkPathString(linkOwnerPath_)
        << " target=" << linkPathString(devicePath_) << " selection="
        << linkPathString(selection.parentPath) << " index=" << selection.macroIndex
        << " inScope=" << yesNo(isInScope) << " final=" << yesNo(isInLinkMode_));

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
// MidiLearnCoordinatorListener
// ============================================================================

void LinkableTextSlider::midiLearnStateChanged(const magda::ChainNodePath& path, int paramIndex,
                                               magda::ControlTarget::Kind owner, bool learning) {
    const auto wantedOwner =
        isModRate_ ? magda::ControlTarget::Kind::ModParam : magda::ControlTarget::Kind::PluginParam;
    if (owner != wantedOwner)
        return;
    const int wantedIndex = isModRate_ ? modParamIndex_ : paramIndex_;
    bool isMe = (path == devicePath_ && paramIndex == wantedIndex);
    isInMidiLearnMode_ = learning && isMe;
    repaint();
}

void LinkableTextSlider::midiLearnCompleted(const magda::ChainNodePath& path, int paramIndex,
                                            magda::ControlTarget::Kind owner,
                                            const magda::Binding&) {
    const auto wantedOwner =
        isModRate_ ? magda::ControlTarget::Kind::ModParam : magda::ControlTarget::Kind::PluginParam;
    if (owner != wantedOwner)
        return;
    const int wantedIndex = isModRate_ ? modParamIndex_ : paramIndex_;
    if (path == devicePath_ && paramIndex == wantedIndex)
        refreshMidiBindingState();
}

void LinkableTextSlider::midiLearnCleared(const magda::ChainNodePath& path, int paramIndex,
                                          magda::ControlTarget::Kind owner, int) {
    const auto wantedOwner =
        isModRate_ ? magda::ControlTarget::Kind::ModParam : magda::ControlTarget::Kind::PluginParam;
    if (owner != wantedOwner)
        return;
    const int wantedIndex = isModRate_ ? modParamIndex_ : paramIndex_;
    if (path == devicePath_ && paramIndex == wantedIndex)
        refreshMidiBindingState();
}

void LinkableTextSlider::bindingRegistryChanged(magda::BindingScope) {
    refreshMidiBindingState();
}

void LinkableTextSlider::refreshMidiBindingState() {
    const auto target = isModRate_
                            ? magda::ControlTarget::modParam(devicePath_, modId_, modParamIndex_)
                            : magda::ControlTarget::pluginParam(devicePath_, paramIndex_);
    const bool newState = magda::BindingRegistry::getInstance().hasActiveBindingFor(target);
    if (newState != hasMidiBinding_) {
        hasMidiBinding_ = newState;
        repaint();
    }
}

void LinkableTextSlider::setModRateContext(const magda::ChainNodePath& path, magda::ModId modId,
                                           int modParamIndex) {
    isModRate_ = true;
    devicePath_ = path;
    modId_ = modId;
    modParamIndex_ = modParamIndex;
    refreshMidiBindingState();
}

void LinkableTextSlider::clearModRateContext() {
    isModRate_ = false;
    modId_ = magda::INVALID_MOD_ID;
    modParamIndex_ = 0;
    refreshMidiBindingState();
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

    // Persistent MIDI-mapped badge: a small dot at the top-right corner.
    // Learn-mode pulse overrides when both are set. Same colour as the
    // device-header binding dot — the override case is communicated by the
    // corresponding macro's automap dot greying out, not by a different
    // colour here.
    if (hasMidiBinding_ && !isInMidiLearnMode_) {
        constexpr float dotSize = 5.0f;
        constexpr float margin = 3.0f;
        auto r = getLocalBounds().toFloat();
        juce::Rectangle<float> dot(r.getRight() - margin - dotSize, r.getY() + margin, dotSize,
                                   dotSize);
        g.setColour(juce::Colour(0xFFFF6B35).withAlpha(0.85f));
        g.fillEllipse(dot);
    }

    // MIDI Learn pulsing border
    if (isInMidiLearnMode_) {
        float phase =
            std::fmod(static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.003), 1.0f);
        // 0.7 + 0.3*sin keeps alpha in [0.4, 1.0]; 0.4 + 0.6*sin went negative.
        float alpha = 0.7f + 0.3f * std::sin(phase * juce::MathConstants<float>::twoPi);
        g.setColour(juce::Colour(0xFFFF6B35).withAlpha(alpha));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 2.0f, 2.0f);
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
                          {.onModUnlinked = onModUnlinked,
                           .onRackModUnlinked = onRackModUnlinked,
                           .onTrackModUnlinked = onTrackModUnlinked,
                           .onModLinkedWithAmount = onModLinkedWithAmount,
                           .onMacroLinked = onMacroLinked,
                           .onMacroLinkedWithAmount = onMacroLinkedWithAmount,
                           .onMacroUnlinked = onMacroUnlinked,
                           .onRackMacroLinked = onRackMacroLinked,
                           .onTrackMacroLinked = onTrackMacroLinked,
                           .onRackMacroUnlinked = onRackMacroUnlinked,
                           .onTrackMacroUnlinked = onTrackMacroUnlinked,
                           .onShowAutomationLane = onShowAutomationLane,
                           .onMidiLearn = onMidiLearn,
                           .onMidiClear = onMidiClear});
        return;
    }

    if (!isInLinkMode_ || !e.mods.isLeftButtonDown()) {
        if (e.mods.isLeftButtonDown()) {
            DBG("[LinkableSlider] mouseDown ignored param="
                << paramIndex_ << " inLinkMode=" << yesNo(isInLinkMode_) << " activeMod="
                << yesNo(activeMod_.isValid()) << " activeMacro=" << yesNo(activeMacro_.isValid())
                << " owner=" << linkPathString(linkOwnerPath_)
                << " target=" << linkPathString(devicePath_));
        }
        return;
    }

    // Mod link mode
    if (activeMod_.isValid()) {
        const auto* modPtr = resolveModPtr(activeMod_, linkOwnerPath_, availableMods_,
                                           availableRackMods_, availableTrackMods_);
        DBG("[LinkableSlider] mod click param=" << paramIndex_
                                                << " modIndex=" << activeMod_.modIndex
                                                << " resolved=" << yesNo(modPtr != nullptr)
                                                << " owner=" << linkPathString(linkOwnerPath_)
                                                << " target=" << linkPathString(devicePath_));

        float initialAmount = 0.0f;
        if (modPtr) {
            magda::ControlTarget thisTarget =
                magda::ControlTarget::pluginParam(devicePath_, paramIndex_);
            if (const auto* existingLink = modPtr->getLink(thisTarget)) {
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
        const auto* macroPtr = resolveMacroPtr(activeMacro_, linkOwnerPath_, availableMacros_,
                                               availableRackMacros_, availableTrackMacros_);
        DBG("[LinkableSlider] macro click param=" << paramIndex_
                                                  << " macroIndex=" << activeMacro_.macroIndex
                                                  << " resolved=" << yesNo(macroPtr != nullptr)
                                                  << " owner=" << linkPathString(linkOwnerPath_)
                                                  << " target=" << linkPathString(devicePath_));

        float initialAmount = 0.0f;
        bool isLinked = false;

        if (macroPtr) {
            magda::ControlTarget thisTarget =
                magda::ControlTarget::pluginParam(devicePath_, paramIndex_);
            const auto* existingLink = macroPtr->getLink(thisTarget);
            isLinked = existingLink != nullptr;
            DBG("[LinkableSlider] macro click link param=" << paramIndex_
                                                           << " linked=" << yesNo(isLinked));
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
    const auto* modPtr = resolveModPtr(activeMod_, linkOwnerPath_, availableMods_,
                                       availableRackMods_, availableTrackMods_);

    if (modPtr) {
        magda::ControlTarget thisTarget =
            magda::ControlTarget::pluginParam(devicePath_, paramIndex_);

        const auto* existingLink = modPtr->getLink(thisTarget);
        bool isLinked = existingLink != nullptr;

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
        const auto* macroPtr = resolveMacroPtr(activeMacro_, linkOwnerPath_, availableMacros_,
                                               availableRackMacros_, availableTrackMacros_);
        if (macroPtr) {
            magda::ControlTarget thisTarget =
                magda::ControlTarget::pluginParam(devicePath_, paramIndex_);

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
        constexpr float kDefaultLinkAmount = 0.3f;
        const bool noDragHappened = linkModeDragStartAmount_ == linkModeDragCurrentAmount_;
        if (noDragHappened) {
            magda::ControlTarget target =
                magda::ControlTarget::pluginParam(devicePath_, paramIndex_);
            if (activeMod_.isValid()) {
                const auto* modPtr = resolveModPtr(activeMod_, linkOwnerPath_, availableMods_,
                                                   availableRackMods_, availableTrackMods_);
                if (modPtr && !modPtr->getLink(target) && onModLinkedWithAmount)
                    onModLinkedWithAmount(activeMod_.modIndex, target, kDefaultLinkAmount);
            } else if (activeMacro_.isValid()) {
                const auto* macroPtr =
                    resolveMacroPtr(activeMacro_, linkOwnerPath_, availableMacros_,
                                    availableRackMacros_, availableTrackMacros_);
                if (macroPtr && !macroPtr->getLink(target) && onMacroLinkedWithAmount)
                    onMacroLinkedWithAmount(activeMacro_.macroIndex, target, kDefaultLinkAmount);
            }
        }

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
    return {deviceId_,
            paramIndex_,
            devicePath_,
            availableMods_,
            availableRackMods_,
            availableMacros_,
            availableRackMacros_,
            availableTrackMods_,
            availableTrackMacros_,
            selectedModIndex_,
            selectedMacroIndex_};
}

void LinkableTextSlider::timerCallback() {
    repaint();
}

void LinkableTextSlider::updateModTimerState() {
    if (hasActiveLinks(buildLinkContext()) || isInMidiLearnMode_) {
        if (!isTimerRunning()) {
            startTimer(33);  // ~30 FPS
        }
    } else {
        stopTimer();
    }
}

}  // namespace magda::daw::ui
