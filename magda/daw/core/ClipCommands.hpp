#pragma once

#include "ClipInfo.hpp"
#include "ClipManager.hpp"
#include "CommandPattern.hpp"

namespace magda {

/**
 * @brief Command for splitting a clip at a given time
 *
 * Uses SnapshotCommand for complete state capture and reliable undo.
 * Creates a new clip (right half) and modifies the original (left half).
 */
class SplitClipCommand : public SnapshotCommand<ClipInfo> {
  public:
    SplitClipCommand(ClipId clipId, double splitTime);

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
    ClipId rightClipId_ = INVALID_CLIP_ID;
};

/**
 * @brief Command for moving a clip to a new time position
 *
 * Supports merging consecutive small moves into a single undo step.
 */
class MoveClipCommand : public SnapshotCommand<ClipInfo> {
  public:
    MoveClipCommand(ClipId clipId, double newStartTime);

    juce::String getDescription() const override {
        return "Move Clip";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  protected:
    ClipInfo captureState() override;
    void restoreState(const ClipInfo& state) override;
    void performAction() override;

  private:
    ClipId clipId_;
    double newStartTime_;
};

/**
 * @brief Command for moving a clip to a different track
 */
class MoveClipToTrackCommand : public SnapshotCommand<ClipInfo> {
  public:
    MoveClipToTrackCommand(ClipId clipId, TrackId newTrackId);

    juce::String getDescription() const override {
        return "Move Clip to Track";
    }

    bool canExecute() const override;

  protected:
    ClipInfo captureState() override;
    void restoreState(const ClipInfo& state) override;
    void performAction() override;
    bool validateState() const override;

  private:
    ClipId clipId_;
    TrackId newTrackId_;
};

/**
 * @brief Command for resizing a clip
 *
 * Supports merging consecutive resize operations.
 */
class ResizeClipCommand : public SnapshotCommand<ClipInfo> {
  public:
    ResizeClipCommand(ClipId clipId, double newLength, bool fromStart = false);

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
 * @brief State for CreateClipCommand - stores creation parameters
 */
struct CreateClipState {
    ClipId createdClipId = INVALID_CLIP_ID;
    bool wasCreated = false;
};

/**
 * @brief Command for creating a new clip
 *
 * For undo, deletes the created clip.
 */
class CreateClipCommand : public SnapshotCommand<CreateClipState> {
  public:
    CreateClipCommand(ClipType type, TrackId trackId, double startTime, double length,
                      const juce::String& audioFilePath = {},
                      ClipView view = ClipView::Arrangement);

    juce::String getDescription() const override {
        return type_ == ClipType::Audio ? "Create Audio Clip" : "Create MIDI Clip";
    }

    bool canExecute() const override;

    ClipId getCreatedClipId() const {
        return createdClipId_;
    }

  protected:
    CreateClipState captureState() override;
    void restoreState(const CreateClipState& state) override;
    void performAction() override;
    bool validateState() const override;

  private:
    ClipType type_;
    TrackId trackId_;
    double startTime_;
    double length_;
    juce::String audioFilePath_;
    ClipView view_;
    ClipId createdClipId_ = INVALID_CLIP_ID;
};

/**
 * @brief State for DuplicateClipCommand
 */
struct DuplicateClipState {
    ClipId duplicatedClipId = INVALID_CLIP_ID;
    bool wasDuplicated = false;
};

/**
 * @brief Command for duplicating a clip
 */
class DuplicateClipCommand : public SnapshotCommand<DuplicateClipState> {
  public:
    DuplicateClipCommand(ClipId sourceClipId, double startTime = -1.0,
                         TrackId targetTrackId = INVALID_TRACK_ID);

    juce::String getDescription() const override {
        return "Duplicate Clip";
    }

    bool canExecute() const override;

    ClipId getDuplicatedClipId() const {
        return duplicatedClipId_;
    }

  protected:
    DuplicateClipState captureState() override;
    void restoreState(const DuplicateClipState& state) override;
    void performAction() override;
    bool validateState() const override;

  private:
    ClipId sourceClipId_;
    double startTime_;       // -1 = use default (after source)
    TrackId targetTrackId_;  // INVALID = same track
    ClipId duplicatedClipId_ = INVALID_CLIP_ID;
};

}  // namespace magda
