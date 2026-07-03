#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <vector>

#include "../core/UndoManager.hpp"
#include "TempoSequenceRippleMath.hpp"

namespace magda {

/**
 * @brief Ripples the global tempo, time-signature and pitch sequences alongside
 *        a time-range clip edit.
 *
 * Tempo / time-sig / pitch are edit-wide (not per-track), so this runs only for
 * all-tracks (global) ops and loop ops, mirroring RippleMarkersCommand. It sits
 * alongside the clip/automation/marker ripple commands in the same compound
 * operation.
 *
 * All shifting is beats-domain. Because moving a tempo event changes the
 * beats<->seconds map, every apply is wrapped in EditTimecodeRemapperSnapshot
 * (save before / remap after) so beat-anchored clips and automation keep their
 * bar/beat position under the new map instead of drifting off the grid. This is
 * the same pattern TempoLaneBridge uses when it rewrites the sequence.
 *
 * IMPORTANT: must run AFTER the clip-shifting commands in the compound op, so
 * the remapper snapshot sees clips at their final beats.
 */
class TempoSequenceRippleCommand : public UndoableCommand {
  public:
    using Mode = temporipple::Mode;  // Insert / Delete / Duplicate

    TempoSequenceRippleCommand(tracktion::Edit& edit, Mode mode, double startBeat, double endBeat);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override;

  private:
    struct TempoEvent {
        double beat;
        double bpm;
        float curve;
    };
    struct TimeSigEvent {
        double beat;
        int numerator;
        int denominator;
        bool triplets;
    };
    struct PitchEvent {
        double beat;
        int pitch;
    };
    struct Snapshot {
        std::vector<TempoEvent> tempos;
        std::vector<TimeSigEvent> timeSigs;
        std::vector<PitchEvent> pitches;
    };

    Snapshot readSequences() const;
    Snapshot rippled(const Snapshot&) const;
    void applySequences(const Snapshot&);

    tracktion::Edit& edit_;
    Mode mode_;
    double startBeat_;
    double endBeat_;

    Snapshot before_;       // pre-change state, captured on execute for undo/redo
    bool changed_ = false;  // false = no-op (nothing to ripple), skip apply/undo
};

}  // namespace magda
