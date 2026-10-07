#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

namespace magda::daw::ui::device_shell {

/** @brief The rows a v1 shell paints around its body; an empty rectangle is not drawn. */
struct ShellRows {
    juce::Rectangle<int> headerSeparatorLeft, headerSeparatorRight;
    juce::Rectangle<int> idRow, sideStrip, footer;
    juce::Rectangle<int> footerSeparator, footerInfo, midiLed;
};

/** @brief Frame, header band, ID row, side strip and footer of a device or rack shell. */
void paintFrame(juce::Graphics& g, juce::Rectangle<int> bounds, int headerHeight,
                const ShellRows& rows, const juce::String& footerInfo, bool midiLedLit);

/** @brief Footer info text: "mono" or "stereo", then the interface rate when one is running. */
juce::String audioInfoText(int outputChannels);

/** @brief The side strip's delta solo: a text glyph styled like the device icon buttons. */
void styleDeltaButton(juce::TextButton& delta);

/** @brief Holds the footer MIDI LED lit for a few frames after the activity counter moves. */
struct MidiLed {
    uint32_t lastCounter = 0;
    int framesLeft = 0;

    /** Returns true when the LED changed state and needs a repaint. */
    bool update(uint32_t counter) {
        if (counter != lastCounter) {
            lastCounter = counter;
            framesLeft = 4;
            return true;
        }
        if (framesLeft == 0)
            return false;
        --framesLeft;
        return framesLeft == 0;
    }

    bool isLit() const {
        return framesLeft > 0;
    }
};

}  // namespace magda::daw::ui::device_shell
