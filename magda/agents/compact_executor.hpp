#pragma once

#include <juce_core/juce_core.h>

#include <vector>

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
    bool executeArp(const ArpOp& op);
    bool executeChord(const ChordOp& op);
    bool executeNote(const NoteOp& op);

    /** Resolve a TrackRef to an internal track ID. Returns -1 on failure. */
    int resolveTrackRef(const TrackRef& ref);

    /** Find track by name, returns internal ID or -1. */
    int findTrackByName(const juce::String& name) const;

    /** Convert 1-based bar number to time in seconds. */
    double barsToTime(double bar) const;

    int currentTrackId_ = -1;
    int currentClipId_ = -1;
    juce::String error_;
    juce::StringArray results_;
};

}  // namespace magda
