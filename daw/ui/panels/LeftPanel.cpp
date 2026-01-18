#include "LeftPanel.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"

namespace magica {

LeftPanel::LeftPanel() {
    setName("Left Panel");

    // Setup collapse/expand button
    collapseButton.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    collapseButton.setColour(juce::TextButton::buttonOnColourId,
                             DarkTheme::getColour(DarkTheme::BUTTON_HOVER));
    collapseButton.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    collapseButton.onClick = [this]() {
        collapsed_ = !collapsed_;
        collapseButton.setButtonText(collapsed_ ? ">" : "<");
        if (onCollapseChanged) {
            onCollapseChanged(collapsed_);
        }
    };
    collapseButton.setButtonText("<");
    addAndMakeVisible(collapseButton);
}

LeftPanel::~LeftPanel() = default;

void LeftPanel::setCollapsed(bool collapsed) {
    if (collapsed_ != collapsed) {
        collapsed_ = collapsed;
        collapseButton.setButtonText(collapsed_ ? ">" : "<");
        resized();
        repaint();
    }
}

void LeftPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    // Draw a border on the right edge only
    g.setColour(DarkTheme::getBorderColour());
    g.drawLine(static_cast<float>(getWidth() - 1), 0.0f, static_cast<float>(getWidth() - 1),
               static_cast<float>(getHeight()), 1.0f);

    // Only draw content if not collapsed
    if (!collapsed_) {
        auto textArea = getLocalBounds().reduced(10).withTrimmedTop(30);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText("Browser / Library", textArea, juce::Justification::centredTop);
    }
}

void LeftPanel::resized() {
    if (collapsed_) {
        // Center the button vertically when collapsed
        collapseButton.setBounds(2, getHeight() / 2 - 10, 20, 20);
    } else {
        // Button in top-right corner when expanded
        collapseButton.setBounds(getWidth() - 24, 4, 20, 20);
    }
}

}  // namespace magica
