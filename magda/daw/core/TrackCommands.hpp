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
    explicit CreateTrackCommand(TrackType type = TrackType::Audio,
                                const juce::String& name = juce::String(),
                                TrackId afterTrackId = INVALID_TRACK_ID);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override;

    TrackId getCreatedTrackId() const {
        return createdTrackId_;
    }

  private:
    TrackType type_;
    juce::String name_;
    TrackId afterTrackId_ = INVALID_TRACK_ID;
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

/**
 * @brief Command for adding a device to an existing track
 */
class AddDeviceToTrackCommand : public UndoableCommand {
  public:
    AddDeviceToTrackCommand(TrackId trackId, const DeviceInfo& device);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Add Device to Track";
    }

  private:
    TrackId trackId_;
    DeviceInfo device_;
    DeviceId createdDeviceId_ = INVALID_DEVICE_ID;
    bool executed_ = false;
};

/**
 * @brief Command for removing a device from a track (undoable)
 */
class RemoveDeviceFromTrackCommand : public UndoableCommand {
  public:
    RemoveDeviceFromTrackCommand(TrackId trackId, DeviceId deviceId);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Remove Device from Track";
    }

  private:
    TrackId trackId_;
    DeviceId deviceId_;
    DeviceInfo savedDevice_;
    int savedIndex_ = -1;
    bool executed_ = false;
};

/**
 * @brief Command for creating a new track with a device (single undo step)
 */
class CreateTrackWithDeviceCommand : public UndoableCommand {
  public:
    CreateTrackWithDeviceCommand(const juce::String& trackName, TrackType type,
                                 const DeviceInfo& device);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Create Track with Plugin";
    }

    TrackId getCreatedTrackId() const {
        return createdTrackId_;
    }

  private:
    juce::String trackName_;
    TrackType type_;
    DeviceInfo device_;
    TrackId createdTrackId_ = INVALID_TRACK_ID;
    DeviceId createdDeviceId_ = INVALID_DEVICE_ID;
    bool executed_ = false;
};

}  // namespace magda
