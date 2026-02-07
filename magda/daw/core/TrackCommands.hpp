#pragma once

#include "ClipInfo.hpp"
#include "TrackManager.hpp"
#include "UndoManager.hpp"

namespace magda {

/**
 * @brief Command for creating a new track
 */
class CreateTrackCommand : public UndoableCommand {
  public:
    explicit CreateTrackCommand(TrackType type = TrackType::Audio);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override;

    TrackId getCreatedTrackId() const {
        return createdTrackId_;
    }

  private:
    TrackType type_;
    TrackId createdTrackId_ = INVALID_TRACK_ID;
    bool executed_ = false;
};

/**
 * @brief Command for deleting a track
 */
class DeleteTrackCommand : public UndoableCommand {
  public:
    explicit DeleteTrackCommand(TrackId trackId);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Delete Track";
    }

  private:
    TrackId trackId_;
    TrackInfo storedTrack_;
    std::vector<ClipInfo> storedClips_;
    bool executed_ = false;
};

/**
 * @brief Command for duplicating a track
 */
class DuplicateTrackCommand : public UndoableCommand {
  public:
    explicit DuplicateTrackCommand(TrackId sourceTrackId, bool duplicateContent = true);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return duplicateContent_ ? "Duplicate Track" : "Duplicate Track Without Content";
    }

    TrackId getDuplicatedTrackId() const {
        return duplicatedTrackId_;
    }

  private:
    TrackId sourceTrackId_;
    bool duplicateContent_;
    TrackId duplicatedTrackId_ = INVALID_TRACK_ID;
    bool executed_ = false;
};

}  // namespace magda
