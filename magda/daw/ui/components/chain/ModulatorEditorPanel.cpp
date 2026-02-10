#include "ModulatorEditorPanel.hpp"

#include "BinaryData.h"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

ModulatorEditorPanel::ModulatorEditorPanel() {
    // Intercept mouse clicks to prevent propagation to parent
    setInterceptsMouseClicks(true, true);

    startTimer(33);  // 30 FPS for waveform animation

    // Name label at top
    nameLabel_.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setText("No Mod Selected", juce::dontSendNotification);
    addAndMakeVisible(nameLabel_);

    // Waveform selector (for LFO shapes - hidden when Custom/Curve)
    waveformCombo_.addItem("Sine", static_cast<int>(magda::LFOWaveform::Sine) + 1);
    waveformCombo_.addItem("Triangle", static_cast<int>(magda::LFOWaveform::Triangle) + 1);
    waveformCombo_.addItem("Square", static_cast<int>(magda::LFOWaveform::Square) + 1);
    waveformCombo_.addItem("Saw", static_cast<int>(magda::LFOWaveform::Saw) + 1);
    waveformCombo_.addItem("Reverse Saw", static_cast<int>(magda::LFOWaveform::ReverseSaw) + 1);
    waveformCombo_.setSelectedId(1, juce::dontSendNotification);
    waveformCombo_.setColour(juce::ComboBox::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    waveformCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    waveformCombo_.setColour(juce::ComboBox::outlineColourId,
                             DarkTheme::getColour(DarkTheme::BORDER));
    waveformCombo_.setJustificationType(juce::Justification::centredLeft);
    waveformCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    waveformCombo_.onChange = [this]() {
        int id = waveformCombo_.getSelectedId();
        if (id > 0 && onWaveformChanged) {
            onWaveformChanged(static_cast<magda::LFOWaveform>(id - 1));
        }
    };
    addAndMakeVisible(waveformCombo_);

    // Waveform display (for standard LFO shapes)
    addAndMakeVisible(waveformDisplay_);

    // Curve editor (for curve mode - bezier editing with integrated phase indicator)
    curveEditor_.setVisible(false);
    curveEditor_.setCurveColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    curveEditor_.onWaveformChanged = [this]() {
        // Curve points are stored directly in ModInfo by LFOCurveEditor
        // Sync external editor window if open
        if (curveEditorWindow_ && curveEditorWindow_->isVisible()) {
            curveEditorWindow_->getCurveEditor().setModInfo(curveEditor_.getModInfo());
        }
        // Notify parent (NodeComponent) to update MiniWaveformDisplay
        if (onCurveChanged) {
            onCurveChanged();
        }
        repaint();
    };
    curveEditor_.onDragPreview = [this]() {
        // Sync external editor during drag for fluid preview
        if (curveEditorWindow_ && curveEditorWindow_->isVisible()) {
            curveEditorWindow_->getCurveEditor().repaint();
        }
        // Notify parent for fluid MiniWaveformDisplay update
        if (onCurveChanged) {
            onCurveChanged();
        }
        repaint();
    };
    addChildComponent(curveEditor_);

    // Button to open external curve editor window
    curveEditorButton_ = std::make_unique<magda::SvgButton>("Edit Curve", BinaryData::curve_svg,
                                                            BinaryData::curve_svgSize);
    curveEditorButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    curveEditorButton_->setHoverColor(DarkTheme::getTextColour());
    curveEditorButton_->setActiveColor(DarkTheme::getColour(DarkTheme::BACKGROUND));
    curveEditorButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    curveEditorButton_->onClick = [this]() {
        if (!curveEditorWindow_) {
            auto* modInfo = const_cast<magda::ModInfo*>(liveModPtr_ ? liveModPtr_ : &currentMod_);
            curveEditorWindow_ = std::make_unique<LFOCurveEditorWindow>(
                modInfo,
                [this]() {
                    // Sync embedded editor when external editor changes
                    curveEditor_.setModInfo(curveEditor_.getModInfo());
                    if (onCurveChanged) {
                        onCurveChanged();
                    }
                    repaint();
                },
                [this]() {
                    // Sync embedded editor from ModInfo during external window drag
                    curveEditor_.syncFromModInfo();
                    // Notify parent for fluid MiniWaveformDisplay update during drag
                    if (onCurveChanged) {
                        onCurveChanged();
                    }
                    repaint();
                });

            // Wire up rate/sync callbacks from external editor
            curveEditorWindow_->onRateChanged = [this](float rate) {
                currentMod_.rate = rate;
                rateSlider_.setValue(rate, juce::dontSendNotification);
                if (onRateChanged) {
                    onRateChanged(rate);
                }
            };
            curveEditorWindow_->onTempoSyncChanged =
                [safeThis = juce::Component::SafePointer(this)](bool synced) {
                    if (!safeThis)
                        return;
                    safeThis->currentMod_.tempoSync = synced;
                    safeThis->syncToggle_.setToggleState(synced, juce::dontSendNotification);
                    safeThis->syncToggle_.setButtonText(synced ? "Sync" : "Free");
                    safeThis->rateSlider_.setVisible(!synced);
                    safeThis->syncDivisionCombo_.setVisible(synced);
                    if (safeThis->onTempoSyncChanged) {
                        safeThis->onTempoSyncChanged(synced);
                    }
                    if (safeThis)
                        safeThis->resized();
                };
            curveEditorWindow_->onSyncDivisionChanged = [this](magda::SyncDivision div) {
                currentMod_.syncDivision = div;
                syncDivisionCombo_.setSelectedId(static_cast<int>(div) + 100,
                                                 juce::dontSendNotification);
                if (onSyncDivisionChanged) {
                    onSyncDivisionChanged(div);
                }
            };
            curveEditorWindow_->onWindowClosed = [this]() { curveEditorButton_->setActive(false); };

            curveEditorButton_->setActive(true);
        } else if (curveEditorWindow_->isVisible()) {
            curveEditorWindow_->setVisible(false);
            curveEditorButton_->setActive(false);
        } else {
            curveEditorWindow_->setVisible(true);
            curveEditorWindow_->toFront(true);
            curveEditorButton_->setActive(true);
        }
    };
    addChildComponent(curveEditorButton_.get());

    // Curve preset selector (shown in curve mode below the name)
    curvePresetCombo_.addItem("Triangle", static_cast<int>(magda::CurvePreset::Triangle) + 1);
    curvePresetCombo_.addItem("Sine", static_cast<int>(magda::CurvePreset::Sine) + 1);
    curvePresetCombo_.addItem("Ramp Up", static_cast<int>(magda::CurvePreset::RampUp) + 1);
    curvePresetCombo_.addItem("Ramp Down", static_cast<int>(magda::CurvePreset::RampDown) + 1);
    curvePresetCombo_.addItem("S-Curve", static_cast<int>(magda::CurvePreset::SCurve) + 1);
    curvePresetCombo_.addItem("Exp", static_cast<int>(magda::CurvePreset::Exponential) + 1);
    curvePresetCombo_.addItem("Log", static_cast<int>(magda::CurvePreset::Logarithmic) + 1);
    curvePresetCombo_.setTextWhenNothingSelected("Preset");
    curvePresetCombo_.setColour(juce::ComboBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    curvePresetCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    curvePresetCombo_.setColour(juce::ComboBox::outlineColourId,
                                DarkTheme::getColour(DarkTheme::BORDER));
    curvePresetCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    curvePresetCombo_.onChange = [this]() {
        int id = curvePresetCombo_.getSelectedId();
        if (id > 0) {
            auto preset = static_cast<magda::CurvePreset>(id - 1);
            curveEditor_.loadPreset(preset);
            // Sync external editor if open
            if (curveEditorWindow_ && curveEditorWindow_->isVisible()) {
                curveEditorWindow_->getCurveEditor().setModInfo(curveEditor_.getModInfo());
            }
            if (onCurveChanged) {
                onCurveChanged();
            }
        }
    };
    addChildComponent(curvePresetCombo_);

    // Save preset button (shown in curve mode next to preset combo)
    savePresetButton_ = std::make_unique<magda::SvgButton>("Save Preset", BinaryData::save_svg,
                                                           BinaryData::save_svgSize);
    savePresetButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    savePresetButton_->setHoverColor(DarkTheme::getTextColour());
    savePresetButton_->onClick = []() {
        // TODO: Show save preset dialog
    };
    addChildComponent(savePresetButton_.get());

    // Sync toggle button (small square button style)
    syncToggle_.setButtonText("Free");
    syncToggle_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    syncToggle_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    syncToggle_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    syncToggle_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    syncToggle_.setClickingTogglesState(true);
    syncToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    syncToggle_.onClick = [safeThis = juce::Component::SafePointer(this)]() {
        if (!safeThis)
            return;
        bool synced = safeThis->syncToggle_.getToggleState();
        safeThis->currentMod_.tempoSync = synced;
        // Update button text
        safeThis->syncToggle_.setButtonText(synced ? "Sync" : "Free");
        // Show/hide appropriate control
        safeThis->rateSlider_.setVisible(!synced);
        safeThis->syncDivisionCombo_.setVisible(synced);
        if (safeThis->onTempoSyncChanged) {
            safeThis->onTempoSyncChanged(synced);
        }
        if (safeThis)
            safeThis->resized();  // Re-layout
    };
    addAndMakeVisible(syncToggle_);

    // Sync division combo box
    syncDivisionCombo_.addItem("1 Bar", static_cast<int>(magda::SyncDivision::Whole) + 100);
    syncDivisionCombo_.addItem("1/2", static_cast<int>(magda::SyncDivision::Half) + 100);
    syncDivisionCombo_.addItem("1/4", static_cast<int>(magda::SyncDivision::Quarter) + 100);
    syncDivisionCombo_.addItem("1/8", static_cast<int>(magda::SyncDivision::Eighth) + 100);
    syncDivisionCombo_.addItem("1/16", static_cast<int>(magda::SyncDivision::Sixteenth) + 100);
    syncDivisionCombo_.addItem("1/32", static_cast<int>(magda::SyncDivision::ThirtySecond) + 100);
    syncDivisionCombo_.addItem("1/2.", static_cast<int>(magda::SyncDivision::DottedHalf) + 100);
    syncDivisionCombo_.addItem("1/4.", static_cast<int>(magda::SyncDivision::DottedQuarter) + 100);
    syncDivisionCombo_.addItem("1/8.", static_cast<int>(magda::SyncDivision::DottedEighth) + 100);
    syncDivisionCombo_.addItem("1/2T", static_cast<int>(magda::SyncDivision::TripletHalf) + 100);
    syncDivisionCombo_.addItem("1/4T", static_cast<int>(magda::SyncDivision::TripletQuarter) + 100);
    syncDivisionCombo_.addItem("1/8T", static_cast<int>(magda::SyncDivision::TripletEighth) + 100);
    syncDivisionCombo_.setSelectedId(static_cast<int>(magda::SyncDivision::Quarter) + 100,
                                     juce::dontSendNotification);
    syncDivisionCombo_.setColour(juce::ComboBox::backgroundColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    syncDivisionCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    syncDivisionCombo_.setColour(juce::ComboBox::outlineColourId,
                                 DarkTheme::getColour(DarkTheme::BORDER));
    syncDivisionCombo_.setJustificationType(juce::Justification::centredLeft);
    syncDivisionCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    syncDivisionCombo_.onChange = [this]() {
        int id = syncDivisionCombo_.getSelectedId();
        if (id >= 100) {
            auto division = static_cast<magda::SyncDivision>(id - 100);
            currentMod_.syncDivision = division;
            if (onSyncDivisionChanged) {
                onSyncDivisionChanged(division);
            }
        }
    };
    addChildComponent(syncDivisionCombo_);  // Hidden by default (shown when sync enabled)

    // Rate slider
    rateSlider_.setRange(0.01, 20.0, 0.01);
    rateSlider_.setValue(1.0, juce::dontSendNotification);
    rateSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    rateSlider_.onValueChanged = [this](double value) {
        currentMod_.rate = static_cast<float>(value);
        if (onRateChanged) {
            onRateChanged(currentMod_.rate);
        }
    };
    addAndMakeVisible(rateSlider_);

    // Trigger mode combo box
    triggerModeCombo_.addItem("Free", static_cast<int>(magda::LFOTriggerMode::Free) + 1);
    triggerModeCombo_.addItem("Transport", static_cast<int>(magda::LFOTriggerMode::Transport) + 1);
    triggerModeCombo_.addItem("MIDI", static_cast<int>(magda::LFOTriggerMode::MIDI) + 1);
    triggerModeCombo_.addItem("Audio", static_cast<int>(magda::LFOTriggerMode::Audio) + 1);
    triggerModeCombo_.setSelectedId(static_cast<int>(magda::LFOTriggerMode::Free) + 1,
                                    juce::dontSendNotification);
    triggerModeCombo_.setColour(juce::ComboBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    triggerModeCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    triggerModeCombo_.setColour(juce::ComboBox::outlineColourId,
                                DarkTheme::getColour(DarkTheme::BORDER));
    triggerModeCombo_.setJustificationType(juce::Justification::centredLeft);
    triggerModeCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    triggerModeCombo_.onChange = [this]() {
        int id = triggerModeCombo_.getSelectedId();
        if (id > 0) {
            auto mode = static_cast<magda::LFOTriggerMode>(id - 1);
            currentMod_.triggerMode = mode;
            if (onTriggerModeChanged) {
                onTriggerModeChanged(mode);
            }
        }
    };
    addAndMakeVisible(triggerModeCombo_);

    // Advanced settings button
    advancedButton_ = std::make_unique<magda::SvgButton>("Advanced", BinaryData::settings_nobg_svg,
                                                         BinaryData::settings_nobg_svgSize);
    advancedButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    advancedButton_->setHoverColor(DarkTheme::getTextColour());
    advancedButton_->onClick = []() {
        // TODO: Show advanced trigger settings popup
    };
    addAndMakeVisible(advancedButton_.get());
}

ModulatorEditorPanel::~ModulatorEditorPanel() {
    stopTimer();
}

void ModulatorEditorPanel::setModInfo(const magda::ModInfo& mod, const magda::ModInfo* liveMod) {
    currentMod_ = mod;
    liveModPtr_ = liveMod;
    // Use live mod pointer if available (for animation), otherwise use local copy
    waveformDisplay_.setModInfo(liveMod ? liveMod : &currentMod_);
    updateFromMod();
}

void ModulatorEditorPanel::setSelectedModIndex(int index) {
    selectedModIndex_ = index;
    if (index < 0) {
        nameLabel_.setText("No Mod Selected", juce::dontSendNotification);
        waveformCombo_.setEnabled(false);
        syncToggle_.setEnabled(false);
        syncDivisionCombo_.setEnabled(false);
        rateSlider_.setEnabled(false);
        triggerModeCombo_.setEnabled(false);
        advancedButton_->setEnabled(false);
    } else {
        waveformCombo_.setEnabled(true);
        syncToggle_.setEnabled(true);
        syncDivisionCombo_.setEnabled(true);
        rateSlider_.setEnabled(true);
        triggerModeCombo_.setEnabled(true);
        advancedButton_->setEnabled(true);
    }
}

void ModulatorEditorPanel::updateFromMod() {
    nameLabel_.setText(currentMod_.name, juce::dontSendNotification);

    // Check if this is a Custom (Curve) waveform
    isCurveMode_ = (currentMod_.waveform == magda::LFOWaveform::Custom);

    // Show/hide appropriate controls based on curve mode
    waveformCombo_.setVisible(!isCurveMode_);

    // In curve mode, show the curve editor, edit button, preset selector, and save button
    curveEditor_.setVisible(isCurveMode_);
    curveEditorButton_->setVisible(isCurveMode_);
    curvePresetCombo_.setVisible(isCurveMode_);
    savePresetButton_->setVisible(isCurveMode_);
    waveformDisplay_.setVisible(!isCurveMode_);

    if (isCurveMode_) {
        // Pass ModInfo to curve editor for loading/saving curve points
        auto* modInfo = const_cast<magda::ModInfo*>(liveModPtr_ ? liveModPtr_ : &currentMod_);
        curveEditor_.setModInfo(modInfo);
    } else {
        // LFO mode - show waveform shape
        waveformCombo_.setSelectedId(static_cast<int>(currentMod_.waveform) + 1,
                                     juce::dontSendNotification);
    }

    // Tempo sync controls
    syncToggle_.setToggleState(currentMod_.tempoSync, juce::dontSendNotification);
    syncToggle_.setButtonText(currentMod_.tempoSync ? "Sync" : "Free");
    syncDivisionCombo_.setSelectedId(static_cast<int>(currentMod_.syncDivision) + 100,
                                     juce::dontSendNotification);
    rateSlider_.setValue(currentMod_.rate, juce::dontSendNotification);

    // Show/hide rate vs division based on sync state
    rateSlider_.setVisible(!currentMod_.tempoSync);
    syncDivisionCombo_.setVisible(currentMod_.tempoSync);

    // Trigger mode
    triggerModeCombo_.setSelectedId(static_cast<int>(currentMod_.triggerMode) + 1,
                                    juce::dontSendNotification);

    // Update layout since curve/LFO mode affects component positions
    resized();
}

void ModulatorEditorPanel::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f));
    g.fillRect(getLocalBounds());

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds());

    // Section headers
    auto bounds = getLocalBounds().reduced(6);
    bounds.removeFromTop(18 + 6);  // Skip name label + gap

    // Skip the area below name - different for curve vs LFO mode
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    if (isCurveMode_) {
        bounds.removeFromTop(18 + 4);  // Skip preset combo + gap
    } else {
        // "Waveform" label (only shown for LFO mode)
        g.drawText("Waveform", bounds.removeFromTop(10), juce::Justification::centredLeft);
        bounds.removeFromTop(18 + 4);  // Skip waveform selector + gap
    }
    int displayHeight = isCurveMode_ ? 70 : 46;
    bounds.removeFromTop(displayHeight + 6);  // Skip waveform/curve display + gap
    bounds.removeFromTop(18 + 8);             // Skip rate row + gap

    // "Trigger" label
    g.drawText("Trigger", bounds.removeFromTop(12), juce::Justification::centredLeft);

    // Skip to trigger row for monitor dot
    auto triggerRow = bounds.removeFromTop(18);
    // Layout: [dropdown] [monitor dot] [advanced button]
    int advButtonWidth = 20;
    int dotDiameter = 8;
    triggerRow.removeFromRight(advButtonWidth);  // Skip advanced button
    triggerRow.removeFromRight(4);               // Skip gap before advanced
    auto dotArea = triggerRow.removeFromRight(dotDiameter);
    triggerRow.removeFromRight(4);  // Skip gap before dot

    // Draw trigger indicator dot
    constexpr float dotRadius = 3.0f;
    auto dotBounds =
        juce::Rectangle<float>(static_cast<float>(dotArea.getX()), dotArea.getCentreY() - dotRadius,
                               dotRadius * 2, dotRadius * 2);

    // Use trigger counter to detect triggers across frame boundaries.
    // The triggered bool is only true for one 60fps tick — the 30fps paint
    // misses ~50% of them. The counter never misses.
    const magda::ModInfo* mod = liveModPtr_ ? liveModPtr_ : &currentMod_;
    if (mod->triggerCount != lastSeenTriggerCount_) {
        lastSeenTriggerCount_ = mod->triggerCount;
        triggerHoldFrames_ = 4;  // Show for ~130ms at 30fps
    }

    if (triggerHoldFrames_ > 0) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillEllipse(dotBounds);
    } else {
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
        g.drawEllipse(dotBounds, 1.0f);
    }
}

void ModulatorEditorPanel::resized() {
    auto bounds = getLocalBounds().reduced(6);

    // Name label at top with curve edit button on right (in curve mode)
    auto headerRow = bounds.removeFromTop(18);
    if (isCurveMode_) {
        int editButtonWidth = 18;
        curveEditorButton_->setBounds(headerRow.removeFromRight(editButtonWidth));
        headerRow.removeFromRight(4);  // Gap
    }
    nameLabel_.setBounds(headerRow);
    bounds.removeFromTop(6);

    if (isCurveMode_) {
        // Curve mode: show preset selector + save button below name
        auto presetRow = bounds.removeFromTop(18);
        int saveButtonWidth = 18;
        savePresetButton_->setBounds(presetRow.removeFromRight(saveButtonWidth));
        presetRow.removeFromRight(4);  // Gap
        curvePresetCombo_.setBounds(presetRow);
        bounds.removeFromTop(4);
    } else {
        // LFO mode: show waveform label + selector
        bounds.removeFromTop(10);  // "Waveform" label
        waveformCombo_.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(4);
    }

    // Waveform display or curve editor (same area)
    // Give more height to curve editor since it needs space for editing
    int displayHeight = isCurveMode_ ? 70 : 46;
    auto waveformArea = bounds.removeFromTop(displayHeight);
    waveformDisplay_.setBounds(waveformArea);
    // Expand curve editor bounds by its padding so the curve content fills the visual area
    // while dots can extend into the padding without clipping
    curveEditor_.setBounds(waveformArea.expanded(curveEditor_.getPadding()));
    bounds.removeFromTop(6);

    // Rate row: [Sync button] [Rate slider/division combo]
    auto rateRow = bounds.removeFromTop(18);

    // Sync toggle (small square button)
    int syncToggleWidth = 32;
    syncToggle_.setBounds(rateRow.removeFromLeft(syncToggleWidth));
    rateRow.removeFromLeft(4);  // Small gap

    // Rate slider or division combo takes remaining space (same position, shown alternately)
    rateSlider_.setBounds(rateRow);
    syncDivisionCombo_.setBounds(rateRow);
    bounds.removeFromTop(8);

    // Trigger row: [dropdown] [monitor dot] [advanced button]
    bounds.removeFromTop(12);  // "Trigger" label
    auto triggerRow = bounds.removeFromTop(18);

    // Advanced button on the right
    int advButtonWidth = 20;
    advancedButton_->setBounds(triggerRow.removeFromRight(advButtonWidth));
    triggerRow.removeFromRight(4);  // Gap before advanced

    // Leave space for monitor dot (painted in paint())
    int dotDiameter = 8;
    triggerRow.removeFromRight(dotDiameter);  // Monitor dot space
    triggerRow.removeFromRight(4);            // Gap before dot

    // Trigger combo takes remaining space
    triggerModeCombo_.setBounds(triggerRow);
}

void ModulatorEditorPanel::mouseDown(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

void ModulatorEditorPanel::mouseUp(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

void ModulatorEditorPanel::timerCallback() {
    if (triggerHoldFrames_ > 0)
        triggerHoldFrames_--;
    repaint();
}

}  // namespace magda::daw::ui
