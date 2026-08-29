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
    explicit CreateTrackCommand(TrackType type = TrackType::Media,
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
    /// One deleted track, with everything needed to put it back as it was.
    struct DeletedTrack {
        TrackInfo track;
        /// Where it stood -- in the project and among its group's children --
        /// so undo puts it back there rather than at the end of either.
        TrackRestorePosition position;
        std::vector<ClipInfo> clips;
    };

    /// @p trackId and everything under it, each with where it stood and what
    /// it held, ordered by their places in the project.
    static std::vector<DeletedTrack> collectSubtree(TrackId trackId);

    TrackId trackId_;
    /// The whole subtree, in the order it stood in the project.
    ///
    /// Deleting a group deletes its children with it, so storing only the track
    /// the user named restored the group alone: its tracks, their devices and
    /// their clips were gone for good, and the restored group listed children
    /// that no longer existed (#2229).
    std::vector<DeletedTrack> storedTracks_;
    /// What the deletion cleared on the tracks that outlived it: their sends
    /// into the subtree, their inputs listening to it, their sidechains on it.
    /// None of that is inside the subtree, so restoring the subtree alone left
    /// a project that had permanently lost them (#2229).
    ExternalTrackRouting storedRouting_;
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
    /// What the first run actually made, ids and position included.
    ///
    /// A redo restores this rather than duplicating again. Duplicating again
    /// allocates a fresh TrackId and fresh device, rack and chain ids, so an
    /// undo followed by a redo would orphan every link, automation lane and
    /// alias made against the first duplicate -- the same reason a paste
    /// replays what it materialised and a wrap reuses its rack's id (#2229).
    TrackInfo storedTrack_;
    std::vector<ClipInfo> storedClips_;
    TrackRestorePosition storedPosition_;
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
    /// What the first execute actually produced, ids included.
    ///
    /// A redo replays this rather than re-copying the template: the template is
    /// re-keyed on the way in, so re-deriving it would give every pasted device
    /// a fresh id and orphan the links, automation lanes and aliases made
    /// against the first paste (#2221).
    std::vector<ChainElement> materialised_;
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
    /// Where each wrapped element stood, ascending.
    ///
    /// A selection need not be contiguous. Remembering only the lowest index and
    /// reinserting everything from there closed the gaps, so wrapping the first
    /// and third of three and undoing reordered the chain (#2221).
    std::vector<int> sourceIndices_;
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

/**
 * @brief Add a device anywhere in the chain tree, addressed by path.
 *
 * The track-level `AddDeviceToTrackCommand` above cannot reach into a rack, and
 * a `(trackId, rackId, chainId)` triple stops at one level of nesting. This
 * takes the parent path — track-level for the main FX chain, or a chain path at
 * any depth — so anything the model can express is reachable.
 */
class AddDeviceByPathCommand : public UndoableCommand {
  public:
    AddDeviceByPathCommand(const ChainNodePath& parentPath, const DeviceInfo& device,
                           int insertIndex = -1);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Add Device";
    }

    DeviceId getCreatedDeviceId() const {
        return createdDeviceId_;
    }

    /** Path of the device this command created, invalid until it has run. */
    const ChainNodePath& getCreatedDevicePath() const {
        return createdDevicePath_;
    }

  private:
    ChainNodePath parentPath_;
    DeviceInfo device_;
    int insertIndex_ = -1;
    DeviceId createdDeviceId_ = INVALID_DEVICE_ID;
    ChainNodePath createdDevicePath_;
    bool executed_ = false;
};

/**
 * @brief Remove a device addressed by path, restoring it in place on undo.
 *
 * Captures the plugin's live state before removal so undo restores the device
 * as it sounded, not as it was first added.
 */
class RemoveDeviceByPathCommand : public UndoableCommand {
  public:
    explicit RemoveDeviceByPathCommand(const ChainNodePath& devicePath);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Remove Device";
    }

    bool didRemove() const {
        return executed_;
    }

  private:
    ChainNodePath devicePath_;
    ChainNodePath parentPath_;
    DeviceInfo savedDevice_;
    int savedIndex_ = -1;
    bool executed_ = false;
};

/**
 * @brief Remove a rack addressed by path, restoring the whole subtree on undo.
 *
 * The chain view used to call `removeRackFromChainByPath()` straight off the
 * model, so deleting a rack -- with every device, nested rack, macro and mod it
 * held -- could not be undone at all. It was the one operation the structural
 * matrix had to exclude by name rather than assert (#2232).
 *
 * Captures the live plugin state of every device beneath the rack first, so
 * undo restores them as they sounded, and restores under the ids they had so
 * the links naming them still resolve.
 */
class RemoveRackByPathCommand : public UndoableCommand {
  public:
    explicit RemoveRackByPathCommand(const ChainNodePath& rackPath);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Remove Rack";
    }

    bool didRemove() const {
        return executed_;
    }

  private:
    ChainNodePath rackPath_;
    ChainNodePath parentPath_;
    RackInfo savedRack_;
    int savedIndex_ = -1;
    bool executed_ = false;
};

/**
 * @brief Remove one chain from a rack, restoring it in place on undo.
 *
 * Same gap as the rack above, one level down: the chain row's X went straight
 * to `removeChainByPath()`, and a chain carries devices (#2232).
 */
class RemoveChainByPathCommand : public UndoableCommand {
  public:
    explicit RemoveChainByPathCommand(const ChainNodePath& chainPath);

    void execute() override;
    void undo() override;
    juce::String getDescription() const override {
        return "Remove Chain";
    }

    bool didRemove() const {
        return executed_;
    }

  private:
    ChainNodePath chainPath_;
    ChainNodePath rackPath_;
    ChainInfo savedChain_;
    int savedIndex_ = -1;
    bool executed_ = false;
};

}  // namespace magda
