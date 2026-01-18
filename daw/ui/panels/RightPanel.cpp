#include "RightPanel.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"

namespace magica {

RightPanel::RightPanel() {
    setName("Right Panel");

    // Setup collapse/expand button
    collapseButton.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    collapseButton.setColour(juce::TextButton::buttonOnColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_HOVER));
    collapseButton.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    collapseButton.onClick = [this]() {
        collapsed_ = !collapsed_;
        collapseButton.setButtonText(collapsed_ ? "<" : ">");
        if (onCollapseChanged) {
            onCollapseChanged(collapsed_);
        }
    };
    collapseButton.setButtonText(">");
    addAndMakeVisible(collapseButton);
}

RightPanel::~RightPanel() = default;

void RightPanel::setCollapsed(bool collapsed) {
    if (collapsed_ != collapsed) {
        collapsed_ = collapsed;
        collapseButton.setButtonText(collapsed_ ? "<" : ">");
        resized();
        repaint();
    }
}

void RightPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    // Draw a border on the left edge only
    g.setColour(DarkTheme::getBorderColour());
    g.drawLine(0.0f, 0.0f, 0.0f, static_cast<float>(getHeight()), 1.0f);

    // Only draw content if not collapsed
    if (!collapsed_) {
        auto textArea = getLocalBounds().reduced(10).withTrimmedTop(30);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText("Inspector / Properties", textArea, juce::Justification::centredTop);
    }
}

void RightPanel::resized() {
    if (collapsed_) {
        // Center the button vertically when collapsed
        collapseButton.setBounds(2, getHeight() / 2 - 10, 20, 20);
    } else {
        // Button in top-left corner when expanded
        collapseButton.setBounds(4, 4, 20, 20);
    }
}

}  // namespace magica
