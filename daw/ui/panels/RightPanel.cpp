#include "RightPanel.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"

namespace magica {

RightPanel::RightPanel() {
    setName("Right Panel");

    // Setup collapse button
    collapseButton.setButtonText(">");
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

RightPanel::~RightPanel() = default;

void RightPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    // Draw a border on the left edge only
    g.setColour(DarkTheme::getBorderColour());
    g.drawLine(0.0f, 0.0f, 0.0f, static_cast<float>(getHeight()), 1.0f);

    // Draw placeholder text in center area (below button)
    auto textArea = getLocalBounds().reduced(10).withTrimmedTop(30);
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText("Inspector / Properties", textArea, juce::Justification::centredTop);
}

void RightPanel::resized() {
    // Collapse button in top-left corner
    collapseButton.setBounds(4, 4, 20, 20);
}

}  // namespace magica
