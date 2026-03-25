#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Inline UI for the Chord Engine plugin.
 *
 * Displays the currently detected chord name.
 */
class ChordEngineUI : public juce::Component {
  public:
    ChordEngineUI() = default;
    ~ChordEngineUI() override = default;

    void setChordName(const juce::String& name) {
        if (chordName_ != name) {
            chordName_ = name;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        auto chordArea = getLocalBounds().reduced(2);

        // Background
        g.setColour(DarkTheme::getBackgroundColour().brighter(0.08f));
        g.fillRoundedRectangle(chordArea.toFloat(), 4.0f);
        g.setColour(DarkTheme::getBorderColour());
        g.drawRoundedRectangle(chordArea.toFloat(), 4.0f, 1.0f);

        if (chordName_.isEmpty()) {
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.4f));
            g.setFont(FontManager::getInstance().getUIFont(13.0f));
            g.drawText("Play a chord...", chordArea, juce::Justification::centred);
        } else {
            g.setColour(DarkTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getUIFont(18.0f).boldened());
            g.drawText(chordName_, chordArea, juce::Justification::centred);
        }
    }

    void resized() override {}

  private:
    juce::String chordName_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordEngineUI)
};

}  // namespace magda::daw::ui
