#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "../../themes/DarkTheme.hpp"

namespace magda {

/**
 * @brief Clickable rectangle showing a colour, used for track/clip colour selection
 */
class ColourSwatch : public juce::Component {
  public:
    ColourSwatch() {
        setInterceptsMouseClicks(true, false);
    }

    void setColour(juce::Colour c) {
        colour_ = c;
        hasColour_ = true;
        repaint();
    }

    void clearColour() {
        hasColour_ = false;
        repaint();
    }

    bool hasColour() const {
        return hasColour_;
    }

    juce::Colour getColour() const {
        return colour_;
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        if (hasColour_) {
            // Show the derived swatch (same hue, normalized lightness/chroma)
            // rather than the raw stored colour, so the picker matches what
            // actually renders on the track header/clips.
            const auto swatchColour = deriveTrackSwatch(colour_);
            g.setColour(swatchColour);
            g.fillRoundedRectangle(bounds, 3.0f);
            g.setColour(swatchColour.brighter(0.3f));
            g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        } else {
            // No colour — draw an empty outlined rectangle
            g.setColour(juce::Colour(0xFF666666));
            g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        }
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (onColourClicked)
            onColourClicked();
    }

    std::function<void()> onColourClicked;

  private:
    juce::Colour colour_{0xFF5588AA};
    bool hasColour_ = true;
};

}  // namespace magda
