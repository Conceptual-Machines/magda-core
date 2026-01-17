#include "TimelineFiller.hpp"

#include "../../themes/DarkTheme.hpp"

namespace magica {

TimelineFiller::TimelineFiller() {
    setSize(200, 80);  // Default size - will be resized by parent
}

void TimelineFiller::paint(juce::Graphics& g) {
    // Fill with timeline background color to match the timeline
    g.fillAll(DarkTheme::getColour(DarkTheme::TIMELINE_BACKGROUND));

    // Draw border to match timeline
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
}

}  // namespace magica
