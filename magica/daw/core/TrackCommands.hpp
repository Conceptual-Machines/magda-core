#pragma once

#include "TrackManager.hpp"
#include "UndoManager.hpp"

namespace magica {

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
    bool executed_ = false;
};

/**
 * @brief Command for duplicating a track
 */
class DuplicateTrackCommand : public UndoableCommand {
  public:
    explicit DuplicateTrackCommand(TrackId sourceTrackId);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Duplicate Track";
    }

    TrackId getDuplicatedTrackId() const {
        return duplicatedTrackId_;
    }

  private:
    TrackId sourceTrackId_;
    TrackId duplicatedTrackId_ = INVALID_TRACK_ID;
    bool executed_ = false;
};

}  // namespace magica
