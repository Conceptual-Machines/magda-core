#pragma once

#include <juce_core/juce_core.h>

#include "ClipInfo.hpp"
#include "ClipManager.hpp"
#include "CommandPattern.hpp"

namespace magda {

// Forward declarations
class TracktionEngineWrapper;

/**
 * @brief Command for splitting a clip at a given time
 *
 * Uses SnapshotCommand for complete state capture and reliable undo.
 * Creates a new clip (right half) and modifies the original (left half).
 */
class SplitClipCommand : public SnapshotCommand<ClipInfo> {
  public:
    SplitClipCommand(ClipId clipId, double splitTime, double tempo = 120.0);

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
    double splitTime_;
    double tempo_;
    ClipId rightClipId_ = INVALID_CLIP_ID;
};

/**
 * @brief Command for moving a clip to a new time position
 *
 * Supports merging consecutive small moves into a single undo step.
 */
class MoveClipCommand : public ValidatedCommand {
  public:
    MoveClipCommand(ClipId clipId, double newStartTime);

    juce::String getDescription() const override {
        return "Move Clip";
    }

    void execute() override;
    void undo() override;

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    double newStartTime_;
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
 * @brief Command for resizing a clip
 *
 * Supports merging consecutive resize operations.
 */
class ResizeClipCommand : public SnapshotCommand<ClipInfo> {
  public:
    ResizeClipCommand(ClipId clipId, double newLength, bool fromStart = false,
                      double tempo = 120.0);

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
    double newLength_;
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
    CreateClipCommand(ClipType type, TrackId trackId, double startTime, double length,
                      const juce::String& audioFilePath = {},
                      ClipView view = ClipView::Arrangement);

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
    double startTime_;
    double length_;
    juce::String audioFilePath_;
    ClipView view_;
    ClipId createdClipId_ = INVALID_CLIP_ID;
    std::vector<ClipInfo> arrangementSnapshot_;
};

/**
 * @brief Command for duplicating a clip
 */
class DuplicateClipCommand : public ValidatedCommand {
  public:
    DuplicateClipCommand(ClipId sourceClipId, double startTime = -1.0,
                         TrackId targetTrackId = INVALID_TRACK_ID, double tempo = 0.0);

    juce::String getDescription() const override {
        return "Duplicate Clip";
    }

    bool canExecute() const override;
    void execute() override;
    void undo() override;

    ClipId getDuplicatedClipId() const {
        return duplicatedClipId_;
    }

  private:
    ClipId sourceClipId_;
    double startTime_;       // -1 = use default (after source)
    TrackId targetTrackId_;  // INVALID = same track
    double tempo_;           // BPM for beat field sync (0 = skip)
    ClipId duplicatedClipId_ = INVALID_CLIP_ID;
    std::vector<ClipInfo> arrangementSnapshot_;
};

/**
 * @brief Command for pasting clips from clipboard
 */
class PasteClipCommand : public ValidatedCommand {
  public:
    PasteClipCommand(double pasteTime, TrackId targetTrackId = INVALID_TRACK_ID);

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
    double pasteTime_;
    TrackId targetTrackId_;
    std::vector<ClipId> pastedClipIds_;
    std::vector<ClipInfo> arrangementSnapshot_;
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
    RenderClipCommand(ClipId clipId, TracktionEngineWrapper* engine);

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
    TracktionEngineWrapper* engine_;
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
                               const std::vector<TrackId>& trackIds,
                               TracktionEngineWrapper* engine);

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
    TracktionEngineWrapper* engine_;
    std::vector<RenderTrackState> trackStates_;
    std::vector<ClipId> newClipIds_;
    bool success_ = false;
};

/**
 * @brief Command for ripple-deleting a time selection
 *
 * Removes content within the time range and shifts subsequent clips left.
 * Uses full arrangement snapshot for reliable undo.
 */
class RippleDeleteTimeSelectionCommand : public UndoableCommand {
  public:
    RippleDeleteTimeSelectionCommand(double startTime, double endTime,
                                     const std::vector<TrackId>& trackIds);

    juce::String getDescription() const override {
        return "Ripple Delete Time Selection";
    }

    void execute() override;
    void undo() override;

  private:
    double startTime_;
    double endTime_;
    std::vector<TrackId> trackIds_;
    std::vector<ClipInfo> snapshot_;  // Full arrangement clips snapshot for undo
    bool executed_ = false;
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
                               const std::vector<TrackId>& trackIds);

    juce::String getDescription() const override {
        return "Delete Time Selection";
    }

    void execute() override;
    void undo() override;

  private:
    double startTime_;
    double endTime_;
    std::vector<TrackId> trackIds_;
    std::vector<ClipInfo> snapshot_;
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
    BounceInPlaceCommand(ClipId clipId, TracktionEngineWrapper* engine);

    juce::String getDescription() const override {
        return "Bounce In Place";
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
    TracktionEngineWrapper* engine_;
    ClipInfo originalClipSnapshot_;
    ClipId newClipId_ = INVALID_CLIP_ID;
    juce::File renderedFile_;
    bool success_ = false;
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
    BounceToNewTrackCommand(ClipId clipId, TracktionEngineWrapper* engine);

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
    TracktionEngineWrapper* engine_;
    ClipId newClipId_ = INVALID_CLIP_ID;
    TrackId newTrackId_ = INVALID_TRACK_ID;
    juce::File renderedFile_;
    bool success_ = false;
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
