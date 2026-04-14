#pragma once

#include <juce_core/juce_core.h>

#include <unordered_set>
#include <vector>

#include "../daw/core/ClipTypes.hpp"
#include "../daw/core/TypeIds.hpp"
#include "compact_parser.hpp"

namespace magda {

/**
 * @brief Executes IR instructions against TrackManager/ClipManager.
 *
 * Skips the DSL text round-trip: compact LLM output → IR → direct API calls.
 * Must be called on the message thread (same as DSL Interpreter).
 */
class CompactExecutor {
  public:
    /**
     * @brief Execute a list of IR instructions.
     * @return true if all instructions succeeded
     */
    bool execute(const std::vector<Instruction>& instructions);

    juce::String getError() const {
        return error_;
    }
    juce::String getResults() const {
        return results_.joinIntoString("\n");
    }

  private:
    bool executeTrack(const TrackOp& op);
    bool executeDel(const DelOp& op);
    bool executeMute(const MuteOp& op);
    bool executeSolo(const SoloOp& op);
    bool executeSet(const SetOp& op);
    bool executeClip(const ClipOp& op);
    bool executeFx(const FxOp& op);
    bool executeSelect(const SelectOp& op);
    bool executeArp(const ArpOp& op);
    bool executeChord(const ChordOp& op);
    bool executeNote(const NoteOp& op);

    /** Auto-create a MIDI clip on the current track when NOTE/CHORD/ARP lack clip context. */
    bool autoCreateClip();

    /** Apply SET key=value props to a track. */
    void applySetProps(int trackId, const juce::StringPairArray& props);

    /** Resolve a TrackRef to an internal track ID. Returns -1 on failure. */
    int resolveTrackRef(const TrackRef& ref);

    /** Find track by name, returns internal ID or -1. */
    int findTrackByName(const juce::String& name) const;

    /** Convert 1-based bar number to time in seconds. */
    double barsToTime(double bar) const;

    /** Convert bar count to duration in seconds. */
    double barsToLength(double bars) const;

    int currentTrackId_ = -1;
    int currentClipId_ = -1;
    juce::String error_;
    juce::StringArray results_;

    // Active selection from SELECT — consumed by subsequent instructions
    std::unordered_set<TrackId> selectedTracks_;
    std::unordered_set<ClipId> selectedClips_;
    bool hasActiveSelection() const {
        return !selectedTracks_.empty() || !selectedClips_.empty();
    }
    void clearActiveSelection() {
        selectedTracks_.clear();
        selectedClips_.clear();
    }
};

}  // namespace magda
