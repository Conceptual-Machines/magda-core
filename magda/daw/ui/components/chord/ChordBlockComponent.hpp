// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "music/ChordTypes.hpp"
#include "ui/components/chord/ChordFunction.hpp"

namespace magda::daw::ui {

/**
 * @brief Draggable chord tile: the name, its degree under it, and a dot for its function.
 *
 * Dragging a tile writes the chord as a temp MIDI file, which the piano roll and the chord
 * lane accept as a drop.
 */
class ChordBlockComponent : public juce::Component {
  public:
    explicit ChordBlockComponent(magda::music::Chord chord);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    /** Called on click (not drag). */
    std::function<void(const magda::music::Chord&)> onClicked;
    /** Called on mouse release (for stopping preview). */
    std::function<void()> onReleased;

    const magda::music::Chord& getChord() const {
        return chord_;
    }

    /** Optional degree label shown below the chord name (e.g. "IV", "vi") */
    void setDegreeLabel(const juce::String& degree) {
        degree_ = degree;
    }
    /** The dot in the corner; None draws no dot. */
    void setFunction(chord_function::HarmonicFunction function) {
        function_ = function;
    }
    /** Lit as the chord that is sounding now. */
    void setHighlighted(bool highlighted) {
        highlighted_ = highlighted;
        repaint();
    }

  private:
    magda::music::Chord chord_;
    juce::String degree_;
    chord_function::HarmonicFunction function_ = chord_function::HarmonicFunction::None;
    bool highlighted_ = false;
    bool dragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordBlockComponent)
};

}  // namespace magda::daw::ui
