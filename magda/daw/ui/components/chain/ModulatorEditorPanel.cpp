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

    // Start timer for trigger indicator animation
    startTimer(33);  // 30 FPS

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

    // Curve preset selector (for Custom waveform - hidden when LFO shapes)
    curvePresetCombo_.addItem("Triangle", static_cast<int>(magda::CurvePreset::Triangle) + 1);
    curvePresetCombo_.addItem("Sine", static_cast<int>(magda::CurvePreset::Sine) + 1);
    curvePresetCombo_.addItem("Ramp Up", static_cast<int>(magda::CurvePreset::RampUp) + 1);
    curvePresetCombo_.addItem("Ramp Down", static_cast<int>(magda::CurvePreset::RampDown) + 1);
    curvePresetCombo_.addItem("S-Curve", static_cast<int>(magda::CurvePreset::SCurve) + 1);
    curvePresetCombo_.addItem("Custom", static_cast<int>(magda::CurvePreset::Custom) + 1);
    curvePresetCombo_.setSelectedId(static_cast<int>(magda::CurvePreset::Triangle) + 1,
                                    juce::dontSendNotification);
    curvePresetCombo_.setColour(juce::ComboBox::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    curvePresetCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    curvePresetCombo_.setColour(juce::ComboBox::outlineColourId,
                                DarkTheme::getColour(DarkTheme::BORDER));
    curvePresetCombo_.setJustificationType(juce::Justification::centredLeft);
    curvePresetCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    curvePresetCombo_.onChange = [this]() {
        int id = curvePresetCombo_.getSelectedId();
        if (id > 0 && onCurvePresetChanged) {
            onCurvePresetChanged(static_cast<magda::CurvePreset>(id - 1));
        }
    };
    addChildComponent(curvePresetCombo_);  // Hidden by default

    // Waveform display
    addAndMakeVisible(waveformDisplay_);

    // Curve editor (initially hidden)
    curveEditor_.setVisible(false);
    curveEditor_.onWaveformChanged = [this]() {
        // TODO: Update ModInfo with custom waveform data
        repaint();
    };
    addChildComponent(curveEditor_);

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
    syncToggle_.onClick = [this]() {
        bool synced = syncToggle_.getToggleState();
        currentMod_.tempoSync = synced;
        // Update button text
        syncToggle_.setButtonText(synced ? "Sync" : "Free");
        // Show/hide appropriate control
        rateSlider_.setVisible(!synced);
        syncDivisionCombo_.setVisible(synced);
        if (onTempoSyncChanged) {
            onTempoSyncChanged(synced);
        }
        resized();  // Re-layout
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
    advancedButton_->onClick = [this]() {
        // TODO: Show advanced trigger settings popup
    };
    addAndMakeVisible(advancedButton_.get());
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

    // Determine if we should show curve editor (only for custom preset with editable points)
    bool showCurveEditor = isCurveMode_ && (currentMod_.curvePreset == magda::CurvePreset::Custom);

    // Show/hide appropriate selector based on curve mode
    waveformCombo_.setVisible(!isCurveMode_);
    curvePresetCombo_.setVisible(isCurveMode_);

    // Show curve editor only for custom preset, otherwise show waveform display
    // (waveformDisplay_ now correctly renders curve presets via generateWaveformForMod)
    curveEditor_.setVisible(showCurveEditor);
    waveformDisplay_.setVisible(!showCurveEditor);

    if (isCurveMode_) {
        // Curve mode - show curve preset
        curvePresetCombo_.setSelectedId(static_cast<int>(currentMod_.curvePreset) + 1,
                                        juce::dontSendNotification);
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

    // "Waveform" or "Preset" label depending on mode
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText(isCurveMode_ ? "Preset" : "Waveform", bounds.removeFromTop(10),
               juce::Justification::centredLeft);

    bounds.removeFromTop(18 + 4);  // Skip waveform selector + gap
    bounds.removeFromTop(46 + 6);  // Skip waveform display + gap
    bounds.removeFromTop(18 + 6);  // Skip rate row + gap

    // "Trigger" label
    g.drawText("Trigger", bounds.removeFromTop(10), juce::Justification::centredLeft);

    // Skip to trigger row for monitor dot
    auto triggerRow = bounds.removeFromTop(18);
    // Layout: [dropdown] [gap] [monitor dot] [gap] [advanced button]
    // Advanced button width: 20, gap: 4, monitor dot: 6
    int advButtonWidth = 20;
    int dotDiameter = 6;
    triggerRow.removeFromRight(advButtonWidth);  // Skip advanced button
    triggerRow.removeFromRight(4);               // Skip gap before advanced
    auto dotArea = triggerRow.removeFromRight(dotDiameter);
    triggerRow.removeFromRight(4);  // Skip gap before dot

    // Draw trigger indicator dot
    const float dotRadius = 3.0f;
    auto dotBounds =
        juce::Rectangle<float>(static_cast<float>(dotArea.getX()), dotArea.getCentreY() - dotRadius,
                               dotRadius * 2, dotRadius * 2);

    // Use live mod pointer for real-time trigger state
    const magda::ModInfo* mod = liveModPtr_ ? liveModPtr_ : &currentMod_;
    if (mod->triggered) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillEllipse(dotBounds);
    } else {
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
        g.drawEllipse(dotBounds, 1.0f);
    }
}

void ModulatorEditorPanel::resized() {
    auto bounds = getLocalBounds().reduced(6);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(6);

    // Waveform/Preset label area (painted) + selector
    bounds.removeFromTop(10);  // "Waveform" or "Preset" label
    auto selectorRow = bounds.removeFromTop(18);
    waveformCombo_.setBounds(selectorRow);
    curvePresetCombo_.setBounds(selectorRow);  // Same position, shown alternately
    bounds.removeFromTop(4);

    // Waveform display or curve editor (same area)
    auto waveformArea = bounds.removeFromTop(46);
    waveformDisplay_.setBounds(waveformArea);
    curveEditor_.setBounds(waveformArea);
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
    bounds.removeFromTop(6);

    // Trigger row: [dropdown] [gap] [monitor dot] [gap] [advanced button]
    bounds.removeFromTop(10);  // "Trigger" label
    auto triggerRow = bounds.removeFromTop(18);

    // Advanced button on the right
    int advButtonWidth = 20;
    advancedButton_->setBounds(triggerRow.removeFromRight(advButtonWidth));
    triggerRow.removeFromRight(4);  // Gap before advanced

    // Leave space for monitor dot (painted in paint())
    int dotDiameter = 6;
    triggerRow.removeFromRight(dotDiameter);  // Monitor dot space
    triggerRow.removeFromRight(4);            // Gap before dot

    // Trigger combo takes remaining space
    triggerModeCombo_.setBounds(triggerRow);
}

void ModulatorEditorPanel::mouseDown(const juce::MouseEvent& e) {
    // Check if click is on the curve editor area when in curve mode
    if (isCurveMode_ && curveEditor_.getBounds().contains(e.getPosition())) {
        if (onOpenCurveEditor) {
            onOpenCurveEditor();
        }
    }
    // Consume mouse events to prevent propagation to parent
}

void ModulatorEditorPanel::mouseUp(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

void ModulatorEditorPanel::timerCallback() {
    // Repaint for trigger indicator animation
    repaint();
}

}  // namespace magda::daw::ui
