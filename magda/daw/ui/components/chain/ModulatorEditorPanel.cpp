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

    // Name label at top
    nameLabel_.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setText("No Mod Selected", juce::dontSendNotification);
    addAndMakeVisible(nameLabel_);

    // Waveform selector
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

    // Waveform display
    addAndMakeVisible(waveformDisplay_);

    // Phase offset slider (0° to 360°)
    phaseSlider_.setRange(0.0, 360.0, 1.0);
    phaseSlider_.setValue(0.0, juce::dontSendNotification);
    phaseSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    phaseSlider_.onValueChanged = [this](double value) {
        // Convert degrees (0-360) to normalized (0-1)
        float normalized = static_cast<float>(value / 360.0);
        currentMod_.phaseOffset = normalized;
        if (onPhaseOffsetChanged) {
            onPhaseOffsetChanged(normalized);
        }
    };
    addAndMakeVisible(phaseSlider_);

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

    // Target label
    targetLabel_.setFont(FontManager::getInstance().getUIFont(8.0f));
    targetLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    targetLabel_.setJustificationType(juce::Justification::centred);
    targetLabel_.setText("No Target", juce::dontSendNotification);
    addAndMakeVisible(targetLabel_);
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
        phaseSlider_.setEnabled(false);
        syncToggle_.setEnabled(false);
        syncDivisionCombo_.setEnabled(false);
        rateSlider_.setEnabled(false);
        triggerModeCombo_.setEnabled(false);
        advancedButton_->setEnabled(false);
        targetLabel_.setText("No Target", juce::dontSendNotification);
    } else {
        waveformCombo_.setEnabled(true);
        phaseSlider_.setEnabled(true);
        syncToggle_.setEnabled(true);
        syncDivisionCombo_.setEnabled(true);
        rateSlider_.setEnabled(true);
        triggerModeCombo_.setEnabled(true);
        advancedButton_->setEnabled(true);
    }
}

void ModulatorEditorPanel::updateFromMod() {
    nameLabel_.setText(currentMod_.name, juce::dontSendNotification);
    waveformCombo_.setSelectedId(static_cast<int>(currentMod_.waveform) + 1,
                                 juce::dontSendNotification);
    // Convert normalized (0-1) to degrees (0-360)
    phaseSlider_.setValue(currentMod_.phaseOffset * 360.0, juce::dontSendNotification);

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

    if (currentMod_.isLinked()) {
        targetLabel_.setText("Target: Device " + juce::String(currentMod_.target.deviceId) +
                                 "\nParam " + juce::String(currentMod_.target.paramIndex + 1),
                             juce::dontSendNotification);
    } else {
        targetLabel_.setText("No Target", juce::dontSendNotification);
    }
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

    // "Waveform" label
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("Waveform", bounds.removeFromTop(10), juce::Justification::centredLeft);

    bounds.removeFromTop(18 + 4);  // Skip waveform selector + gap
    bounds.removeFromTop(46 + 6);  // Skip waveform display + gap

    // "Phase" label
    g.drawText("Phase", bounds.removeFromTop(10), juce::Justification::centredLeft);

    bounds.removeFromTop(18 + 6);  // Skip phase slider + gap
    bounds.removeFromTop(18 + 6);  // Skip rate row + gap

    // "Trigger" label
    g.drawText("Trigger", bounds.removeFromTop(10), juce::Justification::centredLeft);
}

void ModulatorEditorPanel::resized() {
    auto bounds = getLocalBounds().reduced(6);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(6);

    // Waveform label area (painted) + selector
    bounds.removeFromTop(10);  // "Waveform" label
    waveformCombo_.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(4);

    // Waveform display (animated visualization)
    waveformDisplay_.setBounds(bounds.removeFromTop(46));
    bounds.removeFromTop(6);

    // Phase label area (painted) + slider
    bounds.removeFromTop(10);  // "Phase" label
    phaseSlider_.setBounds(bounds.removeFromTop(18));
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

    // Trigger row: [label painted] [combo] [... button]
    bounds.removeFromTop(10);  // "Trigger" label
    auto triggerRow = bounds.removeFromTop(18);

    // Advanced button on the right
    int advButtonWidth = 20;
    advancedButton_->setBounds(triggerRow.removeFromRight(advButtonWidth));
    triggerRow.removeFromRight(4);  // Small gap

    // Trigger combo takes remaining space
    triggerModeCombo_.setBounds(triggerRow);
    bounds.removeFromTop(8);

    // Target info at bottom
    targetLabel_.setBounds(bounds);
}

void ModulatorEditorPanel::mouseDown(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

void ModulatorEditorPanel::mouseUp(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

}  // namespace magda::daw::ui
