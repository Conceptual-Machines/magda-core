#pragma once

#include <functional>

#include "SvgButton.hpp"
#include "core/TypeIds.hpp"

namespace magda {

// Chord-track audition control: a single chord-glyph button that folds the
// chord track's mute, solo and input-monitor into one 3-state axis.
//
//   Silent  - muted, monitor Off          (off glyph, surface chip)
//   Audible - unmuted, unsoloed, monitor In (on glyph, cyan chip)
//   Solo    - unmuted, soloed, monitor In   (on glyph, amber chip)
//
// Left-click cycles Silent -> Audible -> Solo; right-click opens a dropdown that
// spells out the states. Monitor rides along with audibility: a chord track
// never records, so the monitor's Auto (monitor-while-armed) mode is dropped and
// monitoring is simply on whenever the track is audible.
//
// The control is stateless itself: it reads the track's flags from TrackManager
// and writes changes through the undo system, so it stays in sync no matter what
// surface (track header, inspector, chain header) drives it.
class ChordAuditionControl : public SvgButton {
  public:
    enum class State { Silent, Audible, Solo };

    ChordAuditionControl();

    // Supplies the chord track this control acts on (INVALID_TRACK_ID if none).
    std::function<TrackId()> getTrackId;

    // Re-read the track's flags and repaint the glyph/chip to match.
    void refresh();

    void mouseDown(const juce::MouseEvent& e) override;

  private:
    void applyState(State target);
    void showStateMenu();
    void updateVisual(State state);
    static State stateForFlags(bool muted, bool soloed);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAuditionControl)
};

}  // namespace magda
