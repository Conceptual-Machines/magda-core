#include "LeftPanel.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"

namespace magica {

LeftPanel::LeftPanel() {
    setName("Left Panel");

    // Setup collapse button
    collapseButton.setButtonText("<");
    collapseButton.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    collapseButton.setColour(juce::TextButton::buttonOnColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_HOVER));
    collapseButton.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    collapseButton.onClick = [this]() {
        if (onCollapse) {
            onCollapse();
        }
    };
    addAndMakeVisible(collapseButton);
}

LeftPanel::~LeftPanel() = default;

void LeftPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    // Draw a border on the right edge only
    g.setColour(DarkTheme::getBorderColour());
    g.drawLine(static_cast<float>(getWidth() - 1), 0.0f, static_cast<float>(getWidth() - 1),
               static_cast<float>(getHeight()), 1.0f);

    // Draw placeholder text in center area (below button)
    auto textArea = getLocalBounds().reduced(10).withTrimmedTop(30);
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText("Browser / Library", textArea, juce::Justification::centredTop);
}

void LeftPanel::resized() {
    // Collapse button in top-right corner
    collapseButton.setBounds(getWidth() - 24, 4, 20, 20);
}

}  // namespace magica
