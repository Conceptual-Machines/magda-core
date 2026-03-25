#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "music/ChordTypes.hpp"

namespace magda::daw::ui {

/**
 * @brief Small draggable block showing a chord name
 *
 * Matches the visual style used in the piano roll chord row:
 * ACCENT_BLUE at 20% alpha, 3px rounded corners, TEXT_PRIMARY text.
 *
 * Dragging a chord block creates a drag description containing
 * the chord data, which the piano roll can accept as a drop target
 * to insert MIDI notes.
 */
class ChordBlockComponent : public juce::Component {
  public:
    explicit ChordBlockComponent(const magda::music::Chord& chord);

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

  private:
    magda::music::Chord chord_;
    juce::String degree_;
    bool dragging_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordBlockComponent)
};

}  // namespace magda::daw::ui
