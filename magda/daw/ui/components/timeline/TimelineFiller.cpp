#include "TimelineFiller.hpp"

#include "../../themes/ActiveTheme.hpp"

namespace magda {

TimelineFiller::TimelineFiller() {
    setSize(200, 80);  // Default size - will be resized by parent
}

void TimelineFiller::paint(juce::Graphics& g) {
    // Fill with timeline background color to match the timeline
    g.fillAll(ActiveTheme::getColour(ActiveTheme::TIMELINE_BACKGROUND));

    // Draw border to match timeline
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
}

}  // namespace magda
