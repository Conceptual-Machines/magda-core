#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <unordered_set>
#include <vector>

#include "ClipInfo.hpp"
#include "ClipManager.hpp"
#include "CommandPattern.hpp"
#include "TimeTypes.hpp"

namespace magda {

// Forward declarations
class AudioEngine;
class TempoMap;

/**
 * @brief Inclusive-start, exclusive-end arrangement range in musical beats.
 *
 * Bounce operations keep this in the project's authoritative timeline unit
 * until the renderer needs seconds.
 */
struct BounceRange {
    double startBeats = 0.0;
    double endBeats = 0.0;

    bool isValid() const {
        return endBeats > startBeats;
    }
};

/**
 * @brief A bounce range resolved for Tracktion's seconds-based renderer.
 *
 * Placement remains beat-based; seconds are derived from a TempoMap only for
 * the renderer and external-insert capture pass.
 */
struct BounceRenderRange {
    double startBeats = 0.0;
    double endBeats = 0.0;
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    bool isValid() const {
        return endBeats > startBeats;
    }

    double lengthBeats() const {
        return endBeats - startBeats;
    }
};

/** Resolve the active time range or, when absent, the range of selected clips. */
BounceRange resolveBounceSelectionRange(const BounceRange& timeSelection,
                                        bool hasActiveTimeSelection,
                                        const std::vector<ClipInfo>& selectedClips);

/** Resolve a requested bounce range against a source clip and tempo map. */
BounceRenderRange resolveBounceRenderRange(const ClipInfo& clip, const BounceRange& requestedRange,
                                           const TempoMap* tempoMap, bool constrainToClip);

/**
 * @brief Replaces a MIDI clip range with a bounced audio file and restores it on undo.
 *
 * The helper owns the source-fragment bookkeeping needed when only the middle
 * of a clip is bounced, so it can be tested independently of offline rendering.
 */
class BounceInPlaceReplacement {
  public:
    void setOriginalClip(const ClipInfo& originalClip);

    const ClipInfo& getOriginalClip() const {
        return originalClip_;
    }

    ClipId getNewClipId() const {
        return newClipId_;
    }

    bool replace(ClipManager& clipManager, ClipId sourceClipId, const BounceRenderRange& range,
                 const juce::String& renderedFilePath, double projectBPM);
    void undo(ClipManager& clipManager);

  private:
    void restoreOriginalClip(ClipManager& clipManager);

    ClipInfo originalClip_;
    std::vector<ClipId> sourceFragmentIds_;
    ClipId newClipId_ = INVALID_CLIP_ID;
};

/**
 * @brief Command for splitting a clip at a given beat position
 *
 * Uses SnapshotCommand for complete state capture and reliable undo.
 * Creates a new clip (right half) and modifies the original (left half).
 */
class SplitClipCommand : public SnapshotCommand<ClipInfo> {
  public:
    SplitClipCommand(ClipId clipId, BeatPosition splitBeat, double tempo = 0.0);

    juce::String getDescription() const override {
        return "Split Clip";
    }

    bool canExecute() const override;

    // Get the ID of the right (new) clip created by the split
    ClipId getRightClipId() const {
        return rightClipId_;
    }

  protected:
    ClipInfo captureState() override;
    void restoreState(const ClipInfo& state) override;
    void performAction() override;
    bool validateState() const override;

  private:
    ClipId clipId_;
    double splitBeat_;
    double tempo_;
    ClipId rightClipId_ = INVALID_CLIP_ID;
};

/**
 * @brief Command for moving a clip to a new beat position
 *
 * Supports merging consecutive small moves into a single undo step.
 */
class MoveClipCommand : public ValidatedCommand {
  public:
    MoveClipCommand(ClipId clipId, BeatPosition newStartBeat, double tempo = 0.0);

    juce::String getDescription() const override {
        return "Move Clip";
    }

    void execute() override;
    void undo() override;

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    double newStartBeat_;
    double tempo_;
    std::vector<ClipInfo> arrangementSnapshot_;
};

/**
 * @brief Command for moving a session clip to a different slot (track + scene index)
 *
 * Used for drag-and-drop in the session view clip grid.
 */
class MoveSessionClipCommand : public ValidatedCommand {
  public:
    MoveSessionClipCommand(ClipId clipId, TrackId targetTrackId, int targetSceneIndex);

    juce::String getDescription() const override {
        return "Move Session Clip";
    }

    bool canExecute() const override;
    void execute() override;
    void undo() override;

  private:
    ClipId clipId_;
    TrackId targetTrackId_;
    int targetSceneIndex_;
    TrackId originalTrackId_ = INVALID_TRACK_ID;
    int originalSceneIndex_ = -1;
};

/**
 * @brief Command for moving a clip to a different track
 */
class MoveClipToTrackCommand : public ValidatedCommand {
  public:
    MoveClipToTrackCommand(ClipId clipId, TrackId newTrackId);

    juce::String getDescription() const override {
        return "Move Clip to Track";
    }

    bool canExecute() const override;
    void execute() override;
    void undo() override;

  private:
    ClipId clipId_;
    TrackId newTrackId_;
    std::vector<ClipInfo> arrangementSnapshot_;
};

/**
 * @brief Command for resizing a clip to a new beat length
 *
 * Supports merging consecutive resize operations.
 */
class ResizeClipCommand : public SnapshotCommand<ClipInfo> {
  public:
    ResizeClipCommand(ClipId clipId, BeatDuration newLength, bool fromStart = false,
                      double tempo = 0.0);

    juce::String getDescription() const override {
        return "Resize Clip";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  protected:
    ClipInfo captureState() override;
    void restoreState(const ClipInfo& state) override;
    void performAction() override;

  private:
    ClipId clipId_;
    double newLengthBeats_;
    bool fromStart_;
    double tempo_;
};

/**
 * @brief Command for deleting a clip
 *
 * Stores the full clip info for restoration on undo.
 */
class DeleteClipCommand : public SnapshotCommand<ClipInfo> {
  public:
    explicit DeleteClipCommand(ClipId clipId);

    juce::String getDescription() const override {
        return "Delete Clip";
    }

  protected:
    ClipInfo captureState() override;
    void restoreState(const ClipInfo& state) override;
    void performAction() override;
    bool validateState() const override;

  private:
    ClipId clipId_;
};

/**
 * @brief Command for creating a new clip
 *
 * For undo, deletes the created clip.
 */
class CreateClipCommand : public ValidatedCommand {
  public:
    CreateClipCommand(ClipType type, TrackId trackId, BeatPosition startBeat,
                      BeatDuration lengthBeats, const juce::String& audioFilePath = {},
                      ClipView view = ClipView::Arrangement, double tempo = 0.0,
                      ClipOverlapPolicy overlapPolicy = ClipOverlapPolicy::PreserveExisting);

    juce::String getDescription() const override {
        return type_ == ClipType::Audio ? "Create Audio Clip" : "Create MIDI Clip";
    }

    bool canExecute() const override;
    void execute() override;
    void undo() override;

    ClipId getCreatedClipId() const {
        return createdClipId_;
    }

  private:
    ClipType type_;
    TrackId trackId_;
    double startBeat_;
    double lengthBeats_;
    juce::String audioFilePath_;
    ClipView view_;
    double tempo_;
    ClipOverlapPolicy overlapPolicy_;
    ClipId createdClipId_ = INVALID_CLIP_ID;
    std::vector<ClipInfo> arrangementSnapshot_;
};

/**
 * @brief Command for duplicating a clip
 */
class DuplicateClipCommand : public ValidatedCommand {
  public:
    explicit DuplicateClipCommand(ClipId sourceClipId, bool asGhost = false);
    DuplicateClipCommand(ClipId sourceClipId, BeatPosition startBeat,
                         TrackId targetTrackId = INVALID_TRACK_ID, double tempo = 0.0,
                         int targetSceneIndex = -1, bool asGhost = false);
    static std::unique_ptr<DuplicateClipCommand> forSessionSlot(
        ClipId sourceClipId, TrackId targetTrackId = INVALID_TRACK_ID, int targetSceneIndex = -1);

    juce::String getDescription() const override {
        return asGhost_ ? "Duplicate Clip as Ghost" : "Duplicate Clip";
    }

    bool canExecute() const override;
    void execute() override;
    void undo() override;

    ClipId getDuplicatedClipId() const {
        return duplicatedClipId_;
    }
    ClipId getSourceClipId() const {
        return sourceClipId_;
    }

  private:
    ClipId sourceClipId_;
    bool hasExplicitStartBeat_ = false;
    double startBeat_ = 0.0;
    TrackId targetTrackId_;           // INVALID = same track
    double tempo_;                    // BPM for derived seconds cache (0 = project tempo)
    int targetSceneIndex_;            // -1 = keep/unplaced; session clips only
    bool asGhost_ = false;            // copy joins the source's link group (ghost clip)
    bool sourceWasUnlinked_ = false;  // execute created the source's link group
    ClipId duplicatedClipId_ = INVALID_CLIP_ID;
};

std::vector<std::unique_ptr<DuplicateClipCommand>> createArrangementBlockDuplicateCommands(
    const std::unordered_set<ClipId>& clipIds, double tempo, bool asGhost = false);

/**
 * @brief Command for detaching a ghost clip from its link group
 */
class MakeClipUniqueCommand : public ValidatedCommand {
  public:
    explicit MakeClipUniqueCommand(ClipId clipId);

    juce::String getDescription() const override {
        return "Make Clip Unique";
    }

    bool canExecute() const override;
    void execute() override;
    void undo() override;

  private:
    ClipId clipId_;
    int prevLinkGroupId_ = 0;
};

/**
 * @brief Command for pasting clips from clipboard
 */
class PasteClipCommand : public ValidatedCommand {
  public:
    PasteClipCommand(BeatPosition pasteBeat, TrackId targetTrackId = INVALID_TRACK_ID,
                     ClipView targetView = ClipView::Arrangement, int targetSceneIndex = -1,
                     double tempo = 0.0);

    juce::String getDescription() const override {
        return "Paste Clip";
    }

    bool canExecute() const override;
    void execute() override;
    void undo() override;

    const std::vector<ClipId>& getPastedClipIds() const {
        return pastedClipIds_;
    }

  private:
    double pasteBeat_;
    TrackId targetTrackId_;
    ClipView targetView_;
    int targetSceneIndex_;
    double tempo_;
    std::vector<ClipId> pastedClipIds_;
    std::vector<ClipInfo> arrangementSnapshot_;
    std::vector<ClipInfo> sessionSnapshot_;
};

/**
 * @brief State for JoinClipsCommand - stores both clip snapshots
 */
struct JoinClipsState {
    ClipInfo leftClip;
    ClipInfo rightClip;
};

/**
 * @brief Command for joining two adjacent clips into one
 *
 * Merges the right clip into the left clip and deletes the right clip.
 * This is the inverse of split.
 */
class JoinClipsCommand : public SnapshotCommand<JoinClipsState> {
  public:
    JoinClipsCommand(ClipId leftClipId, ClipId rightClipId, double tempo = 120.0);

    juce::String getDescription() const override {
        return "Join Clips";
    }

    bool canExecute() const override;

  protected:
    JoinClipsState captureState() override;
    void restoreState(const JoinClipsState& state) override;
    void performAction() override;
    bool validateState() const override;

  private:
    ClipId leftClipId_;
    ClipId rightClipId_;
    double tempo_;
};

/**
 * @brief Command for stretching a clip (time-stretch)
 *
 * Since stretch operations modify the clip directly during drag (for live preview),
 * this command takes the before-state saved at drag start. The clip is already in
 * its final state when execute() is called, so performAction is a no-op.
 * Undo restores the full ClipInfo snapshot from before the stretch began.
 */
class StretchClipCommand : public UndoableCommand {
  public:
    StretchClipCommand(ClipId clipId, const ClipInfo& beforeState);

    juce::String getDescription() const override {
        return "Stretch Clip";
    }

    void execute() override;
    void undo() override;

  private:
    ClipId clipId_;
    ClipInfo beforeState_;
    ClipInfo afterState_;
};

/**
 * @brief Command for adjusting fade in/out durations via drag handles
 *
 * Since fade operations modify the clip directly during drag (for live preview),
 * this command takes the before-state saved at drag start. The clip is already in
 * its final state when execute() is called, so performAction is a no-op.
 * Undo restores the full ClipInfo snapshot from before the fade drag began.
 */
class SetFadeCommand : public UndoableCommand {
  public:
    SetFadeCommand(ClipId clipId, const ClipInfo& beforeState);

    juce::String getDescription() const override {
        return "Adjust Fade";
    }

    void execute() override;
    void undo() override;

  private:
    ClipId clipId_;
    ClipInfo beforeState_;
    ClipInfo afterState_;
};

/**
 * @brief Command for creating/resizing/removing a crossfade between two clips
 *
 * A crossfade is the overlap region between two adjacent audio clips (#1499);
 * this command moves both joint edges to the target region via
 * ClipManager::setCrossfadeRegionBeats. Both clips' before-states are captured
 * at construction, so construct it AFTER restoring any live-drag preview.
 * startBeat == endBeat butts the joint (removes the crossfade).
 */
class SetCrossfadeCommand : public UndoableCommand {
  public:
    SetCrossfadeCommand(ClipId leftId, ClipId rightId, double startBeat, double endBeat,
                        double tempo = 0.0);

    juce::String getDescription() const override {
        return "Adjust Crossfade";
    }

    void execute() override;
    void undo() override;

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetCrossfadeCommand*>(other))
            return o->leftId_ == leftId_ && o->rightId_ == rightId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        auto* o = static_cast<const SetCrossfadeCommand*>(other);
        startBeat_ = o->startBeat_;
        endBeat_ = o->endBeat_;
        tempo_ = o->tempo_;
    }

  private:
    ClipId leftId_;
    ClipId rightId_;
    double startBeat_;
    double endBeat_;
    double tempo_;
    ClipInfo leftBefore_;
    ClipInfo rightBefore_;
    bool captured_ = false;
};

/**
 * @brief Command for adjusting clip volume via drag handle
 *
 * Since volume operations modify the clip directly during drag (for live preview),
 * this command takes the before-state saved at drag start. The clip is already in
 * its final state when execute() is called, so performAction is a no-op.
 * Undo restores the full ClipInfo snapshot from before the volume drag began.
 */
class SetVolumeCommand : public UndoableCommand {
  public:
    SetVolumeCommand(ClipId clipId, const ClipInfo& beforeState);

    juce::String getDescription() const override {
        return "Adjust Volume";
    }

    void execute() override;
    void undo() override;

  private:
    ClipId clipId_;
    ClipInfo beforeState_;
    ClipInfo afterState_;
};

/**
 * @brief Command for rendering a clip to a new audio file with all processing baked in
 *
 * Renders speed, pitch, warp, fades, gain, offset/trim into a new WAV file.
 * Replaces the original clip with a clean clip referencing the rendered file.
 * Does NOT include track or master plugins.
 */
class RenderClipCommand : public UndoableCommand {
  public:
    RenderClipCommand(ClipId clipId, AudioEngine* engine);

    juce::String getDescription() const override {
        return "Render Clip";
    }

    void execute() override;
    void undo() override;

    bool wasSuccessful() const {
        return success_;
    }

    ClipId getNewClipId() const {
        return newClipId_;
    }

  private:
    ClipId clipId_;
    AudioEngine* engine_;
    ClipInfo originalClipSnapshot_;
    ClipId newClipId_ = INVALID_CLIP_ID;
    juce::File renderedFile_;
    bool success_ = false;
};

/**
 * @brief Per-track state for RenderTimeSelectionCommand undo
 */
struct RenderTrackState {
    TrackId trackId = INVALID_TRACK_ID;
    std::vector<ClipInfo> originalClips;
    ClipId newClipId = INVALID_CLIP_ID;
    juce::File renderedFile;
};

/**
 * @brief Command for rendering all audio within a time selection range per-track
 *
 * Renders all overlapping clips on each track within the selection to a single
 * clean clip per track. Replaces the originals (standard "consolidate" behavior).
 * Does NOT include track or master plugins.
 */
class RenderTimeSelectionCommand : public UndoableCommand {
  public:
    RenderTimeSelectionCommand(double startTime, double endTime,
                               const std::vector<TrackId>& trackIds, AudioEngine* engine);

    juce::String getDescription() const override {
        return "Render Time Selection";
    }

    void execute() override;
    void undo() override;

    bool wasSuccessful() const {
        return success_;
    }

    const std::vector<ClipId>& getNewClipIds() const {
        return newClipIds_;
    }

  private:
    double startTime_;
    double endTime_;
    std::vector<TrackId> trackIds_;
    AudioEngine* engine_;
    std::vector<RenderTrackState> trackStates_;
    std::vector<ClipId> newClipIds_;
    bool success_ = false;
};

/**
 * @brief Command for deleting content within a time selection (no ripple)
 *
 * Removes/trims clips that overlap the time range but does NOT shift
 * subsequent clips left. Uses full arrangement snapshot for reliable undo.
 */
class DeleteTimeSelectionCommand : public UndoableCommand {
  public:
    DeleteTimeSelectionCommand(double startTime, double endTime,
                               const std::vector<TrackId>& trackIds, double tempo = 120.0);

    juce::String getDescription() const override {
        return "Delete Time Selection";
    }

    void execute() override;
    void undo() override;

  private:
    double startTime_;
    double endTime_;
    std::vector<TrackId> trackIds_;
    double tempo_;
    std::vector<ClipInfo> snapshot_;
    bool executed_ = false;
};

/**
 * @brief Command for inserting empty time (ripple insert), beats-native
 *
 * Opens a gap of `durationBeats` at `insertBeat`, shifting every later clip on
 * affected tracks right to make room. Clips spanning the insert point are split
 * (or, if looped, grown). All timeline placement is beat-domain, so the shift is
 * a pure beat delta via setBeatPlacement. Uses a full arrangement snapshot for
 * reliable undo. Empty trackIds means all tracks.
 */
class InsertTimeCommand : public UndoableCommand {
  public:
    InsertTimeCommand(double insertBeat, double durationBeats, const std::vector<TrackId>& trackIds,
                      double tempo = 120.0);

    juce::String getDescription() const override {
        return "Insert Time";
    }

    void execute() override;
    void undo() override;

  private:
    double insertBeat_;
    double durationBeats_;
    std::vector<TrackId> trackIds_;
    double tempo_;
    std::vector<ClipInfo> snapshot_;  // Full arrangement clips snapshot for undo
    bool executed_ = false;
};

/**
 * @brief Command for splitting every clip that crosses a beat, beats-native
 *
 * Splits all arrangement clips on affected tracks that strictly span
 * `splitBeat`, leaving a clean cut at that beat. Clips already starting or
 * ending exactly on the beat are left untouched. Empty trackIds means all
 * tracks. Uses a full arrangement snapshot for reliable undo.
 */
class SplitClipsAtBeatCommand : public UndoableCommand {
  public:
    SplitClipsAtBeatCommand(double splitBeat, const std::vector<TrackId>& trackIds,
                            double tempo = 120.0);

    juce::String getDescription() const override {
        return "Split Clips at Beat";
    }

    void execute() override;
    void undo() override;

    // IDs of the right-hand clips created by the splits (valid after execute()).
    const std::vector<ClipId>& getCreatedClipIds() const {
        return createdClipIds_;
    }

  private:
    double splitBeat_;
    std::vector<TrackId> trackIds_;
    double tempo_;
    std::vector<ClipInfo> snapshot_;  // Full arrangement clips snapshot for undo
    std::vector<ClipId> createdClipIds_;
    bool executed_ = false;
};

/**
 * @brief Command for ripple-deleting a beat range, beats-native
 *
 * Removes all content in [startBeat, endBeat] on affected tracks and shifts
 * every later clip left by the range length to close the gap. The inverse of
 * InsertTimeCommand. Clips spanning a boundary are split/trimmed; looped clips
 * shrink instead of splitting. Empty trackIds means all tracks. Uses a full
 * arrangement snapshot for reliable undo.
 */
class RippleDeleteRangeCommand : public UndoableCommand {
  public:
    RippleDeleteRangeCommand(double startBeat, double endBeat, const std::vector<TrackId>& trackIds,
                             double tempo = 120.0);

    juce::String getDescription() const override {
        return "Delete Time Range";
    }

    void execute() override;
    void undo() override;

  private:
    double startBeat_;
    double endBeat_;
    std::vector<TrackId> trackIds_;
    double tempo_;
    std::vector<ClipInfo> snapshot_;  // Full arrangement clips snapshot for undo
    bool executed_ = false;
};

/**
 * @brief Command for bouncing a MIDI clip in place (synth only, no FX)
 *
 * Renders the clip through just the instrument plugin (bypassing all FX)
 * and replaces the MIDI clip with the resulting audio clip on the same track.
 */
class BounceInPlaceCommand : public UndoableCommand {
  public:
    BounceInPlaceCommand(ClipId clipId, AudioEngine* engine, BounceRange range = {});

    juce::String getDescription() const override {
        return "Bounce In Place";
    }

    void execute() override;
    void undo() override;

    bool wasSuccessful() const {
        return success_;
    }

    ClipId getNewClipId() const {
        return replacement_.getNewClipId();
    }

  private:
    ClipId clipId_;
    AudioEngine* engine_;
    BounceRange range_;
    BounceInPlaceReplacement replacement_;
    juce::File renderedFile_;
    bool success_ = false;
    juce::String errorMessage_;

  public:
    // Non-empty when the bounce failed for a reason worth showing the user
    // (e.g. the bounces directory or disk is not writable). Empty on success
    // or plain user cancellation. The UI caller surfaces this.
    juce::String getErrorMessage() const {
        return errorMessage_;
    }
};

/**
 * @brief Command for bouncing a clip to a new audio track (full signal chain)
 *
 * Renders the clip through all plugins (synth + FX) and places the resulting
 * audio clip on a new Audio track inserted after the source track.
 * The original clip remains untouched.
 */
class BounceToNewTrackCommand : public UndoableCommand {
  public:
    BounceToNewTrackCommand(ClipId clipId, AudioEngine* engine, BounceRange range = {});

    juce::String getDescription() const override {
        return "Bounce To New Track";
    }

    void execute() override;
    void undo() override;

    bool wasSuccessful() const {
        return success_;
    }

    ClipId getNewClipId() const {
        return newClipId_;
    }

  private:
    ClipId clipId_;
    AudioEngine* engine_;
    BounceRange range_;
    ClipId newClipId_ = INVALID_CLIP_ID;
    TrackId newTrackId_ = INVALID_TRACK_ID;
    juce::File renderedFile_;
    bool success_ = false;
    juce::String errorMessage_;

  public:
    // Non-empty when the bounce failed for a reason worth showing the user
    // (e.g. the bounces directory or disk is not writable). Empty on success
    // or plain user cancellation. The UI caller surfaces this.
    juce::String getErrorMessage() const {
        return errorMessage_;
    }
};

/**
 * @brief Command for flattening a MIDI clip (expand loops/offsets into a flat note list)
 *
 * Looped: expands loop cycles across the clip length, applies midiOffset phase.
 * Non-looped: applies midiTrimOffset, clips notes to boundaries.
 * Result is a non-looped clip with all offsets reset to 0.
 */
class FlattenMidiClipCommand : public UndoableCommand {
  public:
    explicit FlattenMidiClipCommand(ClipId clipId);

    juce::String getDescription() const override {
        return "Flatten MIDI Clip";
    }

    void execute() override;
    void undo() override;

  private:
    ClipId clipId_;
    ClipInfo beforeSnapshot_;
    bool executed_ = false;
};

/**
 * @brief Flatten a stack of overlapping MIDI clips into one clip (#2003)
 *
 * Commits what you hear: every clip in the stack is unrolled (loops expanded,
 * offsets applied) and the notes that actually play are gathered into a single
 * clip spanning the whole stack. Which notes those are follows the overlap
 * preference — under the default the covered ones are dropped, under "play
 * both" everything is kept — so flattening never changes the sound.
 *
 * MIDI only. Two overlapping audio clips cannot be merged without rendering
 * them, which is what Bounce is for.
 */
class FlattenClipStackCommand : public UndoableCommand {
  public:
    explicit FlattenClipStackCommand(ClipId anchorClipId);

    juce::String getDescription() const override {
        return "Flatten Clips";
    }

    void execute() override;
    void undo() override;

    /// Every MIDI clip overlapping `anchorClipId` on its track, the anchor
    /// included, walked transitively so a chain of overlaps flattens in one go.
    /// Empty when there is nothing to merge or when an audio clip is involved.
    static std::vector<ClipId> collectStack(ClipId anchorClipId);

  private:
    ClipId anchorId_;
    std::vector<ClipInfo> arrangementSnapshot_;
    bool executed_ = false;
};

/**
 * @brief Post-hoc command for undoing a session-to-arrangement recording pass.
 *
 * The SessionRecorder creates arrangement clips during recording. When it
 * finishes, it pushes this command so the entire recording pass can be
 * undone/redone as a single operation.
 */
class RecordSessionToArrangementCommand : public UndoableCommand {
  public:
    explicit RecordSessionToArrangementCommand(const std::vector<ClipInfo>& preRecordSnapshot);

    juce::String getDescription() const override {
        return "Record Session to Arrangement";
    }

    void execute() override;
    void undo() override;

  private:
    std::vector<ClipInfo> preRecordSnapshot_;  // Arrangement state before recording
    std::vector<ClipInfo>
        postRecordSnapshot_;  // Arrangement state after recording (captured on first execute)
    bool snapshotCaptured_ = false;
};

// ============================================================================
// Slice Utilities
// ============================================================================

class AudioBridge;

/**
 * @brief Split a clip at multiple sorted ascending times as one undo step.
 *
 * Wraps the splits in a compound operation so a single undo restores
 * the original clip.  Caller must disable warp before calling if the
 * clip has warp enabled (splitClip's linear offset formula requires it).
 */
void sliceClipAtTimes(ClipId clipId, const std::vector<double>& splitTimes, double tempo);

/**
 * @brief Slice an audio clip at its warp marker positions.
 *
 * Disables warp, converts each marker's sourceTime to a linear timeline
 * position, and calls sliceClipAtTimes.
 */
void sliceClipAtWarpMarkers(ClipId clipId, double tempo, AudioBridge* bridge);

/**
 * @brief Slice a clip at regular grid intervals.
 *
 * @param gridInterval  Grid spacing in timeline seconds.
 *
 * Disables warp if enabled, then splits at each grid line inside the clip.
 */
void sliceClipAtGrid(ClipId clipId, double gridInterval, double tempo, AudioBridge* bridge);

/**
 * @brief Create a DrumGrid track from an audio clip's warp markers.
 *
 * Each warp marker boundary becomes a pad in a new DrumGridPlugin.
 * A MIDI clip is created with notes that trigger each pad in sequence
 * to reproduce the original pattern.
 */
void sliceWarpMarkersToDrumGrid(ClipId clipId, double tempo, AudioBridge* bridge);

/**
 * @brief Create a DrumGrid track from an audio clip sliced at grid intervals.
 *
 * Each grid-aligned region becomes a pad in a new DrumGridPlugin.
 * A MIDI clip is created with notes that trigger each pad in sequence
 * to reproduce the original pattern.
 */
void sliceAtGridToDrumGrid(ClipId clipId, double gridInterval, double tempo, AudioBridge* bridge);

}  // namespace magda
