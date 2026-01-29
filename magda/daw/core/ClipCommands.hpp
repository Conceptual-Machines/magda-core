#pragma once

#include "ClipInfo.hpp"
#include "ClipManager.hpp"
#include "UndoManager.hpp"

namespace magda {

/**
 * @brief Command for splitting a clip at a given time
 *
 * Creates a new clip (right half) and modifies the original (left half).
 * Undo restores the original clip to its full length and removes the new clip.
 */
class SplitClipCommand : public UndoableCommand {
  public:
    SplitClipCommand(ClipId clipId, double splitTime);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Split Clip";
    }

  private:
    ClipId originalClipId_;
    double splitTime_;

    // Stored state for undo
    juce::String originalName_;
    double originalLength_;
    ClipId createdClipId_ = INVALID_CLIP_ID;
    bool executed_ = false;
};

/**
 * @brief Command for moving a clip to a new time position
 *
 * Supports merging consecutive small moves into a single undo step.
 */
class MoveClipCommand : public UndoableCommand {
  public:
    MoveClipCommand(ClipId clipId, double newStartTime);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move Clip";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    double oldStartTime_;
    double newStartTime_;
    bool executed_ = false;
};

/**
 * @brief Command for moving a clip to a different track
 */
class MoveClipToTrackCommand : public UndoableCommand {
  public:
    MoveClipToTrackCommand(ClipId clipId, TrackId newTrackId);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move Clip to Track";
    }

  private:
    ClipId clipId_;
    TrackId oldTrackId_;
    TrackId newTrackId_;
    bool executed_ = false;
};

/**
 * @brief Command for resizing a clip
 *
 * Supports merging consecutive resize operations.
 */
class ResizeClipCommand : public UndoableCommand {
  public:
    ResizeClipCommand(ClipId clipId, double newLength, bool fromStart = false);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Resize Clip";
    }

    bool canMergeWith(const UndoableCommand* other) const override;
    void mergeWith(const UndoableCommand* other) override;

  private:
    ClipId clipId_;
    double oldStartTime_;
    double oldLength_;
    double newStartTime_;
    double newLength_;
    bool fromStart_;
    bool executed_ = false;
};

/**
 * @brief Command for deleting a clip
 *
 * Stores the full clip info for restoration on undo.
 */
class DeleteClipCommand : public UndoableCommand {
  public:
    explicit DeleteClipCommand(ClipId clipId);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete Clip";
    }

  private:
    ClipId clipId_;
    ClipInfo storedClip_;
    bool executed_ = false;
};

/**
 * @brief Command for creating a new clip
 *
 * For undo, simply deletes the created clip.
 */
class CreateClipCommand : public UndoableCommand {
  public:
    CreateClipCommand(ClipType type, TrackId trackId, double startTime, double length,
                      const juce::String& audioFilePath = {},
                      ClipView view = ClipView::Arrangement);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return type_ == ClipType::Audio ? "Create Audio Clip" : "Create MIDI Clip";
    }

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
    bool executed_ = false;
};

/**
 * @brief Command for duplicating a clip
 */
class DuplicateClipCommand : public UndoableCommand {
  public:
    DuplicateClipCommand(ClipId sourceClipId, double startTime = -1.0,
                         TrackId targetTrackId = INVALID_TRACK_ID);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Duplicate Clip";
    }

    ClipId getDuplicatedClipId() const {
        return duplicatedClipId_;
    }

  private:
    ClipId sourceClipId_;
    double startTime_;       // -1 = use default (after source)
    TrackId targetTrackId_;  // INVALID = same track
    ClipId duplicatedClipId_ = INVALID_CLIP_ID;
    bool executed_ = false;
};

}  // namespace magda
