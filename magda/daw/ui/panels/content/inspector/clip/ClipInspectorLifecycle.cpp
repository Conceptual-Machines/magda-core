#include "../../../../themes/DarkTheme.hpp"
#include "../ClipInspector.hpp"

namespace magda::daw::ui {

void ClipInspector::onActivated() {
    magda::ClipManager::getInstance().addListener(this);
}

void ClipInspector::onDeactivated() {
    magda::ClipManager::getInstance().removeListener(this);
}

void ClipInspector::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());

    // Segmented chip behind the view|type indicator icons in the name row.
    if (clipTypeIcon_ && clipTypeIcon_->isVisible() && !viewTypeChipBounds_.isEmpty()) {
        auto chip = viewTypeChipBounds_.toFloat();
        g.setColour(juce::Colour(0xff2A2A2A));
        g.fillRoundedRectangle(chip, 4.0f);
        g.setColour(juce::Colour(0xff555555));
        g.drawRoundedRectangle(chip.reduced(0.5f), 4.0f, 1.0f);
        if (clipViewIcon_ && clipViewIcon_->isVisible()) {
            g.drawLine(chip.getCentreX(), chip.getY() + 4.0f, chip.getCentreX(),
                       chip.getBottom() - 4.0f, 1.0f);
        }
    }
}

}  // namespace magda::daw::ui
