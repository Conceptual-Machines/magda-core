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

    /** ID of the clip that notes were last written to (or auto-created). -1 if none. */
    int getCurrentClipId() const {
        return currentClipId_;
    }

    /** Whether execute() auto-created a fresh clip (vs. writing into a seeded one). */
    bool didAutoCreateClip() const {
        return autoCreatedClip_;
    }

    /**
     * Seed the executor with a clip that a prior step (e.g. the command agent's
     * clip.new) just created. The executor will write notes into that clip
     * instead of auto-creating a new one. Pass -1 to clear.
     *
     * We deliberately do NOT inherit the UI's selected clip: the music agent
     * should never silently fill a user-selected clip — it should always
     * produce a new clip unless explicitly handed one.
     */
    void setSeedClipId(int clipId) {
        seedClipId_ = clipId;
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
    int seedClipId_ = -1;
    bool autoCreatedClip_ = false;
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
