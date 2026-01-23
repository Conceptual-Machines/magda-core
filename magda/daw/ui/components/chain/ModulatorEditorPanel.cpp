#include "ModulatorEditorPanel.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

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

    // Type selector
    typeSelector_.addItem("LFO", static_cast<int>(magda::ModType::LFO) + 1);
    typeSelector_.addItem("Envelope", static_cast<int>(magda::ModType::Envelope) + 1);
    typeSelector_.addItem("Random", static_cast<int>(magda::ModType::Random) + 1);
    typeSelector_.addItem("Follower", static_cast<int>(magda::ModType::Follower) + 1);
    typeSelector_.setSelectedId(1, juce::dontSendNotification);
    typeSelector_.setColour(juce::ComboBox::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    typeSelector_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    typeSelector_.setColour(juce::ComboBox::outlineColourId,
                            DarkTheme::getColour(DarkTheme::BORDER));
    typeSelector_.onChange = [this]() {
        int id = typeSelector_.getSelectedId();
        if (id > 0 && onTypeChanged) {
            onTypeChanged(static_cast<magda::ModType>(id - 1));
        }
    };
    addAndMakeVisible(typeSelector_);

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
    waveformCombo_.onChange = [this]() {
        int id = waveformCombo_.getSelectedId();
        if (id > 0 && onWaveformChanged) {
            onWaveformChanged(static_cast<magda::LFOWaveform>(id - 1));
        }
    };
    addAndMakeVisible(waveformCombo_);

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

    // Target label
    targetLabel_.setFont(FontManager::getInstance().getUIFont(8.0f));
    targetLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    targetLabel_.setJustificationType(juce::Justification::centred);
    targetLabel_.setText("No Target", juce::dontSendNotification);
    addAndMakeVisible(targetLabel_);
}

void ModulatorEditorPanel::setModInfo(const magda::ModInfo& mod) {
    currentMod_ = mod;
    updateFromMod();
}

void ModulatorEditorPanel::setSelectedModIndex(int index) {
    selectedModIndex_ = index;
    if (index < 0) {
        nameLabel_.setText("No Mod Selected", juce::dontSendNotification);
        typeSelector_.setEnabled(false);
        waveformCombo_.setEnabled(false);
        rateSlider_.setEnabled(false);
        targetLabel_.setText("No Target", juce::dontSendNotification);
    } else {
        typeSelector_.setEnabled(true);
        waveformCombo_.setEnabled(true);
        rateSlider_.setEnabled(true);
    }
}

void ModulatorEditorPanel::updateFromMod() {
    nameLabel_.setText(currentMod_.name, juce::dontSendNotification);
    typeSelector_.setSelectedId(static_cast<int>(currentMod_.type) + 1, juce::dontSendNotification);
    waveformCombo_.setSelectedId(static_cast<int>(currentMod_.waveform) + 1,
                                 juce::dontSendNotification);
    rateSlider_.setValue(currentMod_.rate, juce::dontSendNotification);

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
    auto bounds = getLocalBounds().reduced(4);
    bounds.removeFromTop(24);  // Skip name label

    // "Type" label
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("Type", bounds.removeFromTop(12), juce::Justification::centredLeft);

    bounds.removeFromTop(22);  // Skip type selector

    // "Waveform" label
    g.drawText("Waveform", bounds.removeFromTop(12), juce::Justification::centredLeft);

    bounds.removeFromTop(22);  // Skip waveform selector

    // "Rate" label
    g.drawText("Rate", bounds.removeFromTop(12), juce::Justification::centredLeft);
}

void ModulatorEditorPanel::resized() {
    auto bounds = getLocalBounds().reduced(4);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(4);

    // Type label area (painted) + selector
    bounds.removeFromTop(12);  // "Type" label
    typeSelector_.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(4);

    // Waveform label area (painted) + selector
    bounds.removeFromTop(12);  // "Waveform" label
    waveformCombo_.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(4);

    // Rate label area (painted) + slider
    bounds.removeFromTop(12);  // "Rate" label
    rateSlider_.setBounds(bounds.removeFromTop(20));
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
