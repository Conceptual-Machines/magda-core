#include "PresetBrowserContent.hpp"

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

PresetBrowserContent::PresetBrowserContent() {
    setName("Preset Browser");

    // Setup title
    titleLabel_.setText("Presets", juce::dontSendNotification);
    titleLabel_.setFont(FontManager::getInstance().getUIFont(14.0f));
    titleLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
    addAndMakeVisible(titleLabel_);

    // Setup search box
    searchBox_.setTextToShowWhenEmpty("Search presets...", ActiveTheme::getSecondaryTextColour());
    searchBox_.setColour(juce::TextEditor::backgroundColourId,
                         ActiveTheme::getColour(ActiveTheme::BUTTON_NORMAL));
    searchBox_.setColour(juce::TextEditor::textColourId, ActiveTheme::getTextColour());
    searchBox_.setColour(juce::TextEditor::outlineColourId, ActiveTheme::getBorderColour());
    addAndMakeVisible(searchBox_);
}

void PresetBrowserContent::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getPanelBackgroundColour());

    // Placeholder content area
    auto contentArea = getLocalBounds().reduced(10).withTrimmedTop(70);
    g.setColour(ActiveTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText("Preset browser will appear here", contentArea, juce::Justification::centredTop);
}

void PresetBrowserContent::resized() {
    auto bounds = getLocalBounds().reduced(10);

    titleLabel_.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);  // Spacing
    searchBox_.setBounds(bounds.removeFromTop(28));
}

void PresetBrowserContent::onActivated() {
    // Could refresh presets here
}

void PresetBrowserContent::onDeactivated() {
    // Could save selection here
}

}  // namespace magda::daw::ui
