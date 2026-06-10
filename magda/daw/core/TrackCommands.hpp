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
    explicit DuplicateTrackCommand(TrackId sourceTrackId, bool duplicateContent = true,
                                   bool duplicateDevices = true);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        if (duplicateContent_ && duplicateDevices_)
            return "Duplicate Track";
        if (!duplicateContent_ && duplicateDevices_)
            return "Duplicate Track Without Content";
        if (duplicateContent_ && !duplicateDevices_)
            return "Duplicate Track Content Only";
        return "Duplicate Track (Empty)";
    }

    TrackId getDuplicatedTrackId() const {
        return duplicatedTrackId_;
    }

  private:
    TrackId sourceTrackId_;
    bool duplicateContent_;
    bool duplicateDevices_;
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

    DeviceId getCreatedDeviceId() const {
        return createdDeviceId_;
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
 * @brief Command for moving a chain element within/between track and rack chains.
 */
class MoveChainElementCommand : public UndoableCommand {
  public:
    MoveChainElementCommand(const ChainNodePath& sourceElementPath,
                            const ChainNodePath& destinationChainPath, int insertIndex);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move Chain Element";
    }

    bool didMove() const {
        return executed_;
    }

  private:
    ChainNodePath buildMovedPath(const ChainNodePath& destinationChainPath) const;

    ChainNodePath sourceElementPath_;
    ChainNodePath destinationChainPath_;
    ChainNodePath undoChainPath_;
    ChainNodePath movedElementPath_;
    int insertIndex_ = 0;
    int undoIndex_ = -1;
    ChainStepType sourceType_ = ChainStepType::Device;
    int sourceId_ = INVALID_DEVICE_ID;
    bool executed_ = false;
};

/**
 * @brief Command for moving multiple chain elements as one undoable step.
 */
class MoveChainElementsCommand : public UndoableCommand {
  public:
    MoveChainElementsCommand(std::vector<ChainNodePath> sourceElementPaths,
                             const ChainNodePath& destinationChainPath, int insertIndex);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Move Chain Elements";
    }

    bool didMove() const {
        return executed_;
    }

  private:
    struct MovedElementRecord {
        ChainNodePath originalPath;
        ChainNodePath originalParentPath;
        int originalIndex = -1;
        ChainStepType type = ChainStepType::Device;
        int id = INVALID_DEVICE_ID;
    };

    std::vector<ChainNodePath> sourceElementPaths_;
    ChainNodePath destinationChainPath_;
    int insertIndex_ = 0;
    std::vector<std::unique_ptr<MoveChainElementCommand>> commands_;
    std::vector<MovedElementRecord> movedElements_;
    bool executed_ = false;
};

class PasteChainElementsCommand : public UndoableCommand {
  public:
    PasteChainElementsCommand(const ChainNodePath& destinationChainPath,
                              std::vector<ChainElement> elements, int insertIndex);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Paste Chain Elements";
    }

    bool didPaste() const {
        return executed_;
    }

  private:
    ChainNodePath destinationChainPath_;
    std::vector<ChainElement> templateElements_;
    std::vector<ChainNodePath> insertedPaths_;
    int insertIndex_ = 0;
    bool executed_ = false;
};

class WrapChainElementsInRackCommand : public UndoableCommand {
  public:
    explicit WrapChainElementsInRackCommand(std::vector<ChainNodePath> sourceElementPaths,
                                            juce::String rackName = "Rack");

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Add Devices to New Rack";
    }

    bool didWrap() const {
        return executed_;
    }

  private:
    std::vector<ChainNodePath> sourceElementPaths_;
    juce::String rackName_;
    ChainNodePath sourceChainPath_;
    RackId rackId_ = INVALID_RACK_ID;
    ChainId chainId_ = INVALID_CHAIN_ID;
    int sourceIndex_ = -1;
    bool executed_ = false;
};

class SetMacroNameCommand : public UndoableCommand {
  public:
    SetMacroNameCommand(const ChainNodePath& path, int macroIndex, const juce::String& newName);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Rename Macro";
    }

  private:
    void applyName(const juce::String& name);

    ChainNodePath path_;
    int macroIndex_ = -1;
    juce::String oldName_;
    juce::String newName_;
    bool valid_ = false;
};

class SetModNameCommand : public UndoableCommand {
  public:
    SetModNameCommand(const ChainNodePath& path, int modIndex, const juce::String& newName);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Rename Modulator";
    }

  private:
    void applyName(const juce::String& name);

    ChainNodePath path_;
    int modIndex_ = -1;
    juce::String oldName_;
    juce::String newName_;
    bool valid_ = false;
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
