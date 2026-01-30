#include "MacroEditorPanel.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr float kNameFontSize = 10.0f;
constexpr float kValueFontSize = 9.0f;
constexpr float kSecondaryFontSize = 8.0f;

constexpr double kValueSliderMin = 0.0;
constexpr double kValueSliderMax = 1.0;
constexpr double kValueSliderStep = 0.01;
constexpr double kValueSliderDefault = 0.5;

constexpr float kBackgroundBrightnessDelta = 0.03f;

constexpr int kOuterPadding = 4;
constexpr int kNameLabelHeight = 20;
constexpr int kNameLabelBottomGap = 4;
constexpr int kValueLabelHeight = 12;
constexpr int kValueSliderHeight = 20;
constexpr int kValueBottomGap = 8;
constexpr int kNameAreaHeight = kNameLabelHeight + kNameLabelBottomGap;
}  // namespace

MacroEditorPanel::MacroEditorPanel() {
    // Intercept mouse clicks to prevent propagation to parent
    setInterceptsMouseClicks(true, true);

    // Name label at top (editable)
    nameLabel_.setFont(FontManager::getInstance().getUIFontBold(kNameFontSize));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centred);
    nameLabel_.setText("No Macro Selected", juce::dontSendNotification);
    nameLabel_.setEditable(false, true);  // Single-click doesn't edit, double-click does
    nameLabel_.onTextChange = [this]() {
        if (onNameChanged) {
            onNameChanged(nameLabel_.getText());
        }
    };
    addAndMakeVisible(nameLabel_);

    // Value slider
    valueSlider_.setRange(kValueSliderMin, kValueSliderMax, kValueSliderStep);
    valueSlider_.setValue(kValueSliderDefault, juce::dontSendNotification);
    valueSlider_.setFont(FontManager::getInstance().getUIFont(kValueFontSize));
    valueSlider_.onValueChanged = [this](double value) {
        currentMacro_.value = static_cast<float>(value);
        if (onValueChanged) {
            onValueChanged(currentMacro_.value);
        }
    };
    addAndMakeVisible(valueSlider_);

    // Target label
    targetLabel_.setFont(FontManager::getInstance().getUIFont(kSecondaryFontSize));
    targetLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    targetLabel_.setJustificationType(juce::Justification::centred);
    targetLabel_.setText("No Target", juce::dontSendNotification);
    addAndMakeVisible(targetLabel_);
}

void MacroEditorPanel::setMacroInfo(const magda::MacroInfo& macro) {
    currentMacro_ = macro;
    updateFromMacro();
}

void MacroEditorPanel::setSelectedMacroIndex(int index) {
    selectedMacroIndex_ = index;
    if (index < 0) {
        nameLabel_.setText("No Macro Selected", juce::dontSendNotification);
        nameLabel_.setEditable(false, false);
        valueSlider_.setEnabled(false);
        targetLabel_.setText("No Target", juce::dontSendNotification);
    } else {
        nameLabel_.setEditable(false, true);  // Allow double-click editing
        valueSlider_.setEnabled(true);
    }
}

void MacroEditorPanel::updateFromMacro() {
    nameLabel_.setText(currentMacro_.name, juce::dontSendNotification);
    valueSlider_.setValue(currentMacro_.value, juce::dontSendNotification);

    if (currentMacro_.isLinked()) {
        targetLabel_.setText("Target: Device " + juce::String(currentMacro_.target.deviceId) +
                                 "\nParam " + juce::String(currentMacro_.target.paramIndex + 1),
                             juce::dontSendNotification);
    } else {
        targetLabel_.setText("No Target", juce::dontSendNotification);
    }
}

void MacroEditorPanel::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(kBackgroundBrightnessDelta));
    g.fillRect(getLocalBounds());

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds());

    // Section headers
    auto bounds = getLocalBounds().reduced(kOuterPadding);
    bounds.removeFromTop(kNameAreaHeight);  // Skip name label

    // "Value" label
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(kSecondaryFontSize));
    g.drawText("Value", bounds.removeFromTop(kValueLabelHeight), juce::Justification::centredLeft);
}

void MacroEditorPanel::resized() {
    auto bounds = getLocalBounds().reduced(kOuterPadding);

    // Name label at top
    nameLabel_.setBounds(bounds.removeFromTop(kNameLabelHeight));
    bounds.removeFromTop(kNameLabelBottomGap);

    // Value label area (painted) + slider
    bounds.removeFromTop(kValueLabelHeight);  // "Value" label
    valueSlider_.setBounds(bounds.removeFromTop(kValueSliderHeight));
    bounds.removeFromTop(kValueBottomGap);

    // Target info at bottom
    targetLabel_.setBounds(bounds);
}

void MacroEditorPanel::mouseDown(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

void MacroEditorPanel::mouseUp(const juce::MouseEvent& /*e*/) {
    // Consume mouse events to prevent propagation to parent
}

}  // namespace magda::daw::ui
