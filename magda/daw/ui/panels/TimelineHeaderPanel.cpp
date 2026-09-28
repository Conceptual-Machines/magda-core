#include "TimelineHeaderPanel.hpp"

#include "../components/timeline/TimelineComponent.hpp"
#include "../themes/ActiveTheme.hpp"
#include "../themes/FontManager.hpp"

namespace magda {

TimelineHeaderPanel::TimelineHeaderPanel() {
    // Create timeline component
    timeline = std::make_unique<TimelineComponent>();
    addAndMakeVisible(*timeline);

    // Set height to match timeline height for visual continuity
    setSize(800, 80);
}

TimelineHeaderPanel::~TimelineHeaderPanel() = default;

void TimelineHeaderPanel::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Left section divider
    int leftDivider = leftPanelWidth;
    // Right section divider
    int rightDivider = getWidth() - rightPanelWidth;

    // Fill left section with left panel background color
    auto leftSection = bounds.removeFromLeft(leftDivider);
    g.setColour(ActiveTheme::getPanelBackgroundColour());
    g.fillRect(leftSection);

    // Fill right section with right panel background color
    auto rightSection = bounds.removeFromRight(rightPanelWidth);
    g.setColour(ActiveTheme::getPanelBackgroundColour());
    g.fillRect(rightSection);

    // Fill center section with timeline background color
    g.setColour(ActiveTheme::getColour(ActiveTheme::TIMELINE_BACKGROUND));
    g.fillRect(bounds);

    // Draw section dividers
    g.setColour(ActiveTheme::getBorderColour());
    g.drawVerticalLine(leftDivider, 0, getHeight());
    g.drawVerticalLine(rightDivider, 0, getHeight());

    // Draw top border
    g.drawHorizontalLine(0, 0, getWidth());

    // Draw bottom border for the entire panel
    g.drawHorizontalLine(getHeight() - 1, 0, getWidth());

    // Draw subtle inner borders for the side panels to match the actual panels
    g.setColour(ActiveTheme::getBorderColour().withAlpha(0.5f));

    // Left panel inner border
    g.drawRect(0, 0, leftDivider, getHeight(), 1);

    // Right panel inner border
    g.drawRect(rightDivider, 0, rightPanelWidth, getHeight(), 1);

    // Draw section labels
    g.setColour(ActiveTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(10.0f));

    // Left section label
    g.drawText("TRACKS", 8, 0, leftPanelWidth - 16, getHeight(), juce::Justification::centredLeft);

    // Center section label
    g.drawText("TIMELINE", leftPanelWidth + 8, 0, rightDivider - leftPanelWidth - 16, getHeight(),
               juce::Justification::centredLeft);

    // Right section label
    g.drawText("MIXER", rightDivider + 8, 0, rightPanelWidth - 16, getHeight(),
               juce::Justification::centredLeft);
}

void TimelineHeaderPanel::resized() {
    // Left section divider
    int leftDivider = leftPanelWidth;
    // Right section divider
    int rightDivider = getWidth() - rightPanelWidth;

    // Position timeline in center section
    auto centerBounds =
        juce::Rectangle<int>(leftDivider, 0, rightDivider - leftDivider, getHeight());
    timeline->setBounds(centerBounds);
}

void TimelineHeaderPanel::setLayoutSizes(int leftWidth, int rightWidth) {
    leftPanelWidth = leftWidth;
    rightPanelWidth = rightWidth;
    resized();
    repaint();
}

}  // namespace magda
