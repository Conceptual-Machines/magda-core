#include "EmptyContent.hpp"

#include "../../themes/ActiveTheme.hpp"

namespace magda::daw::ui {

EmptyContent::EmptyContent() {
    setName("Empty");
}

void EmptyContent::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getPanelBackgroundColour());
}

void EmptyContent::resized() {
    // Nothing to layout
}

}  // namespace magda::daw::ui
