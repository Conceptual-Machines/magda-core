#pragma once

#include <algorithm>

#include "TrackManager.hpp"
#include "UndoManager.hpp"

namespace magda {

/**
 * @brief The child that currently follows @p trackId in its parent group's
 * childIds list, or INVALID_TRACK_ID if it is the last child / not grouped.
 *
 * Captured before a reorder so undo can re-insert the track at the same spot
 * via moveChildWithinGroup(trackId, sibling).
 */
inline TrackId siblingAfterInGroup(TrackId trackId) {
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (!track || !track->hasParent())
        return INVALID_TRACK_ID;
    const auto* parent = TrackManager::getInstance().getTrack(track->parentId);
    if (!parent)
        return INVALID_TRACK_ID;
    const auto& children = parent->childIds;
    auto it = std::find(children.begin(), children.end(), trackId);
    if (it == children.end() || std::next(it) == children.end())
        return INVALID_TRACK_ID;
    return *std::next(it);
}

/**
 * @brief Command for setting track volume (supports merging for slider drags)
 */
class SetTrackVolumeCommand : public UndoableCommand {
  public:
    SetTrackVolumeCommand(TrackId trackId, float newVolume)
        : trackId_(trackId), newVolume_(newVolume) {
        auto* track = TrackManager::getInstance().getTrack(trackId);
        if (track)
            oldVolume_ = track->volume;
    }

    void execute() override {
        TrackManager::getInstance().setTrackVolume(trackId_, newVolume_);
    }
    void undo() override {
        TrackManager::getInstance().setTrackVolume(trackId_, oldVolume_);
    }
    juce::String getDescription() const override {
        return "Set Track Volume";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetTrackVolumeCommand*>(other))
            return o->trackId_ == trackId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newVolume_ = static_cast<const SetTrackVolumeCommand*>(other)->newVolume_;
    }

  private:
    TrackId trackId_;
    float oldVolume_ = 1.0f, newVolume_;
};

/**
 * @brief Command for setting track pan (supports merging for slider drags)
 */
class SetTrackPanCommand : public UndoableCommand {
  public:
    SetTrackPanCommand(TrackId trackId, float newPan) : trackId_(trackId), newPan_(newPan) {
        auto* track = TrackManager::getInstance().getTrack(trackId);
        if (track)
            oldPan_ = track->pan;
    }
    SetTrackPanCommand(TrackId trackId, float oldPan, float newPan)
        : trackId_(trackId), oldPan_(oldPan), newPan_(newPan) {}

    void execute() override {
        TrackManager::getInstance().setTrackPan(trackId_, newPan_);
    }
    void undo() override {
        TrackManager::getInstance().setTrackPan(trackId_, oldPan_);
    }
    juce::String getDescription() const override {
        return "Set Track Pan";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetTrackPanCommand*>(other))
            return o->trackId_ == trackId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newPan_ = static_cast<const SetTrackPanCommand*>(other)->newPan_;
    }

  private:
    TrackId trackId_;
    float oldPan_ = 0.0f, newPan_;
};

/**
 * @brief Command for setting track mute state
 */
class SetTrackMuteCommand : public UndoableCommand {
  public:
    SetTrackMuteCommand(TrackId trackId, bool newMuted) : trackId_(trackId), newMuted_(newMuted) {
        auto* track = TrackManager::getInstance().getTrack(trackId);
        if (track)
            oldMuted_ = track->muted;
    }

    void execute() override {
        TrackManager::getInstance().setTrackMuted(trackId_, newMuted_);
    }
    void undo() override {
        TrackManager::getInstance().setTrackMuted(trackId_, oldMuted_);
    }
    juce::String getDescription() const override {
        return "Set Track Mute";
    }

  private:
    TrackId trackId_;
    bool oldMuted_ = false, newMuted_;
};

/**
 * @brief Command for setting track solo state
 */
class SetTrackSoloCommand : public UndoableCommand {
  public:
    SetTrackSoloCommand(TrackId trackId, bool newSoloed)
        : trackId_(trackId), newSoloed_(newSoloed) {
        auto* track = TrackManager::getInstance().getTrack(trackId);
        if (track)
            oldSoloed_ = track->soloed;
    }

    void execute() override {
        TrackManager::getInstance().setTrackSoloed(trackId_, newSoloed_);
    }
    void undo() override {
        TrackManager::getInstance().setTrackSoloed(trackId_, oldSoloed_);
    }
    juce::String getDescription() const override {
        return "Set Track Solo";
    }

  private:
    TrackId trackId_;
    bool oldSoloed_ = false, newSoloed_;
};

/**
 * @brief Command for setting track input monitor mode
 */
class SetTrackInputMonitorCommand : public UndoableCommand {
  public:
    SetTrackInputMonitorCommand(TrackId trackId, InputMonitorMode newMode)
        : trackId_(trackId), newMode_(newMode) {
        auto* track = TrackManager::getInstance().getTrack(trackId);
        if (track)
            oldMode_ = track->inputMonitor;
    }

    void execute() override {
        TrackManager::getInstance().setTrackInputMonitor(trackId_, newMode_);
    }
    void undo() override {
        TrackManager::getInstance().setTrackInputMonitor(trackId_, oldMode_);
    }
    juce::String getDescription() const override {
        return "Set Track Input Monitor";
    }

  private:
    TrackId trackId_;
    InputMonitorMode oldMode_ = InputMonitorMode::Off, newMode_;
};

/**
 * @brief Command for moving a track to a 1-based position among its siblings.
 *
 * Undo restores the track's original sibling position (captured at construction),
 * which round-trips a stable reorder.
 */
class MoveTrackCommand : public UndoableCommand {
  public:
    MoveTrackCommand(TrackId trackId, int newPosition)
        : trackId_(trackId), newPosition_(newPosition) {
        oldPosition_ = TrackManager::getInstance().getTrackSiblingPosition(trackId);
    }

    void execute() override {
        TrackManager::getInstance().moveTrackToPosition(trackId_, newPosition_);
    }
    void undo() override {
        if (oldPosition_ > 0)
            TrackManager::getInstance().moveTrackToPosition(trackId_, oldPosition_);
    }
    juce::String getDescription() const override {
        return "Move Track";
    }

  private:
    TrackId trackId_;
    int newPosition_;
    int oldPosition_ = 0;
};

/**
 * @brief Command for grouping tracks. Undo dissolves the created group.
 *
 * Faithful for the common case of grouping contiguous tracks; ungroup returns
 * the children to the parent level at the group's position. Redo re-groups the
 * same (still-existing) tracks.
 */
class GroupTracksCommand : public UndoableCommand {
  public:
    GroupTracksCommand(std::vector<TrackId> trackIds, juce::String name)
        : trackIds_(std::move(trackIds)), name_(std::move(name)) {}

    void execute() override {
        groupId_ = TrackManager::getInstance().groupTracks(trackIds_, name_);
    }
    void undo() override {
        if (groupId_ != INVALID_TRACK_ID)
            TrackManager::getInstance().ungroupTrack(groupId_);
    }
    juce::String getDescription() const override {
        return "Group Tracks";
    }
    TrackId getCreatedGroupId() const {
        return groupId_;
    }

  private:
    std::vector<TrackId> trackIds_;
    juce::String name_;
    TrackId groupId_ = INVALID_TRACK_ID;
};

/**
 * @brief Command for ungrouping a group track. Undo re-groups the children.
 */
class UngroupTrackCommand : public UndoableCommand {
  public:
    explicit UngroupTrackCommand(TrackId groupId) : ungroupTarget_(groupId) {
        if (const auto* group = TrackManager::getInstance().getTrack(groupId)) {
            name_ = group->name;
            children_ = group->childIds;
        }
    }

    void execute() override {
        TrackManager::getInstance().ungroupTrack(ungroupTarget_);
    }
    void undo() override {
        if (children_.size() >= 2) {
            // Re-group the children; track the new group so redo ungroups it.
            ungroupTarget_ = TrackManager::getInstance().groupTracks(children_, name_);
        }
    }
    juce::String getDescription() const override {
        return "Ungroup Track";
    }

    /// The children captured before ungrouping (for callers that select them).
    const std::vector<TrackId>& getChildren() const {
        return children_;
    }

  private:
    TrackId ungroupTarget_;
    juce::String name_;
    std::vector<TrackId> children_;
};

/**
 * @brief Command for moving a track to a flat index in the track list.
 *
 * Mirrors TrackManager::moveTrack (0-based flat-vector reorder, used by the
 * drag-reorder gesture). Captures the track's index before the move and
 * restores it on undo.
 */
class MoveTrackToIndexCommand : public UndoableCommand {
  public:
    MoveTrackToIndexCommand(TrackId trackId, int newIndex)
        : trackId_(trackId), newIndex_(newIndex) {}

    void execute() override {
        auto& tm = TrackManager::getInstance();
        oldIndex_ = tm.getTrackIndex(trackId_);
        tm.moveTrack(trackId_, newIndex_);
    }
    void undo() override {
        if (oldIndex_ >= 0)
            TrackManager::getInstance().moveTrack(trackId_, oldIndex_);
    }
    juce::String getDescription() const override {
        return "Move Track";
    }

  private:
    TrackId trackId_;
    int newIndex_;
    int oldIndex_ = -1;
};

/**
 * @brief Command for reordering a track within its parent group's children.
 *
 * Mirrors TrackManager::moveChildWithinGroup. Captures the following sibling so
 * undo re-inserts the track at its original position.
 */
class MoveChildWithinGroupCommand : public UndoableCommand {
  public:
    MoveChildWithinGroupCommand(TrackId childId, TrackId beforeChildId)
        : childId_(childId), beforeChildId_(beforeChildId) {}

    void execute() override {
        oldBeforeChildId_ = siblingAfterInGroup(childId_);
        TrackManager::getInstance().moveChildWithinGroup(childId_, beforeChildId_);
    }
    void undo() override {
        TrackManager::getInstance().moveChildWithinGroup(childId_, oldBeforeChildId_);
    }
    juce::String getDescription() const override {
        return "Move Track in Group";
    }

  private:
    TrackId childId_;
    TrackId beforeChildId_;
    TrackId oldBeforeChildId_ = INVALID_TRACK_ID;
};

/**
 * @brief Command for adding a track to a group. Undo restores the track's
 * previous parent (and position within it) or returns it to the top level,
 * along with its audio output routing.
 */
class AddTrackToGroupCommand : public UndoableCommand {
  public:
    AddTrackToGroupCommand(TrackId trackId, TrackId groupId)
        : trackId_(trackId), groupId_(groupId) {}

    void execute() override {
        auto& tm = TrackManager::getInstance();
        const auto* track = tm.getTrack(trackId_);
        oldParentId_ = track ? track->parentId : INVALID_TRACK_ID;
        oldBeforeChildId_ = siblingAfterInGroup(trackId_);
        oldAudioOutput_ = track ? track->audioOutputDevice : juce::String();
        tm.addTrackToGroup(trackId_, groupId_);
    }
    void undo() override {
        auto& tm = TrackManager::getInstance();
        tm.removeTrackFromGroup(trackId_);
        if (oldParentId_ != INVALID_TRACK_ID) {
            tm.addTrackToGroup(trackId_, oldParentId_);
            tm.moveChildWithinGroup(trackId_, oldBeforeChildId_);
        }
        tm.setTrackAudioOutput(trackId_, oldAudioOutput_);
    }
    juce::String getDescription() const override {
        return "Add Track to Group";
    }

  private:
    TrackId trackId_;
    TrackId groupId_;
    TrackId oldParentId_ = INVALID_TRACK_ID;
    TrackId oldBeforeChildId_ = INVALID_TRACK_ID;
    juce::String oldAudioOutput_;
};

/**
 * @brief Command for removing a track from its group. Undo re-adds it to its
 * previous parent at its original position and restores its audio output.
 */
class RemoveTrackFromGroupCommand : public UndoableCommand {
  public:
    explicit RemoveTrackFromGroupCommand(TrackId trackId) : trackId_(trackId) {}

    void execute() override {
        auto& tm = TrackManager::getInstance();
        const auto* track = tm.getTrack(trackId_);
        oldParentId_ = track ? track->parentId : INVALID_TRACK_ID;
        oldBeforeChildId_ = siblingAfterInGroup(trackId_);
        oldAudioOutput_ = track ? track->audioOutputDevice : juce::String();
        tm.removeTrackFromGroup(trackId_);
    }
    void undo() override {
        if (oldParentId_ == INVALID_TRACK_ID)
            return;
        auto& tm = TrackManager::getInstance();
        tm.addTrackToGroup(trackId_, oldParentId_);
        tm.moveChildWithinGroup(trackId_, oldBeforeChildId_);
        tm.setTrackAudioOutput(trackId_, oldAudioOutput_);
    }
    juce::String getDescription() const override {
        return "Remove Track from Group";
    }

  private:
    TrackId trackId_;
    TrackId oldParentId_ = INVALID_TRACK_ID;
    TrackId oldBeforeChildId_ = INVALID_TRACK_ID;
    juce::String oldAudioOutput_;
};

/**
 * @brief Command for setting track name
 */
class SetTrackNameCommand : public UndoableCommand {
  public:
    SetTrackNameCommand(TrackId trackId, const juce::String& newName)
        : trackId_(trackId), newName_(newName) {
        auto* track = TrackManager::getInstance().getTrack(trackId);
        if (track)
            oldName_ = track->name;
    }

    void execute() override {
        TrackManager::getInstance().setTrackName(trackId_, newName_);
    }
    void undo() override {
        TrackManager::getInstance().setTrackName(trackId_, oldName_);
    }
    juce::String getDescription() const override {
        return "Set Track Name";
    }

  private:
    TrackId trackId_;
    juce::String oldName_, newName_;
};

/**
 * @brief Command for setting send level (supports merging for slider drags)
 */
class SetSendLevelCommand : public UndoableCommand {
  public:
    SetSendLevelCommand(TrackId trackId, int busIndex, float newLevel)
        : trackId_(trackId), busIndex_(busIndex), newLevel_(newLevel) {
        auto* track = TrackManager::getInstance().getTrack(trackId);
        if (track) {
            for (const auto& send : track->sends) {
                if (send.busIndex == busIndex) {
                    oldLevel_ = send.level;
                    break;
                }
            }
        }
    }

    void execute() override {
        TrackManager::getInstance().setSendLevel(trackId_, busIndex_, newLevel_);
    }
    void undo() override {
        TrackManager::getInstance().setSendLevel(trackId_, busIndex_, oldLevel_);
    }
    juce::String getDescription() const override {
        return "Set Send Level";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetSendLevelCommand*>(other))
            return o->trackId_ == trackId_ && o->busIndex_ == busIndex_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newLevel_ = static_cast<const SetSendLevelCommand*>(other)->newLevel_;
    }

  private:
    TrackId trackId_;
    int busIndex_;
    float oldLevel_ = 1.0f, newLevel_;
};

/**
 * @brief Command for adding a send from one track to another (aux bus).
 */
class AddSendCommand : public UndoableCommand {
  public:
    AddSendCommand(TrackId sourceTrackId, TrackId destTrackId)
        : sourceTrackId_(sourceTrackId), destTrackId_(destTrackId) {}

    void execute() override {
        auto& tm = TrackManager::getInstance();
        tm.addSend(sourceTrackId_, destTrackId_);
        // Capture the bus index that addSend assigned so undo can remove exactly
        // this send (addSend auto-allocates the dest's aux bus index).
        busIndex_ = -1;
        if (const auto* source = tm.getTrack(sourceTrackId_)) {
            for (const auto& send : source->sends) {
                if (send.destTrackId == destTrackId_) {
                    busIndex_ = send.busIndex;
                    break;
                }
            }
        }
    }
    void undo() override {
        if (busIndex_ >= 0)
            TrackManager::getInstance().removeSend(sourceTrackId_, busIndex_);
    }
    juce::String getDescription() const override {
        return "Add Send";
    }

  private:
    TrackId sourceTrackId_;
    TrackId destTrackId_;
    int busIndex_ = -1;
};

/**
 * @brief Command for removing a send. Restores level on undo.
 */
class RemoveSendCommand : public UndoableCommand {
  public:
    RemoveSendCommand(TrackId sourceTrackId, int busIndex)
        : sourceTrackId_(sourceTrackId), busIndex_(busIndex) {
        if (const auto* source = TrackManager::getInstance().getTrack(sourceTrackId)) {
            for (const auto& send : source->sends) {
                if (send.busIndex == busIndex) {
                    saved_ = send;
                    hasSaved_ = true;
                    break;
                }
            }
        }
    }

    void execute() override {
        TrackManager::getInstance().removeSend(sourceTrackId_, busIndex_);
    }
    void undo() override {
        if (!hasSaved_)
            return;
        auto& tm = TrackManager::getInstance();
        tm.addSend(sourceTrackId_, saved_.destTrackId);
        tm.setSendLevel(sourceTrackId_, saved_.busIndex, saved_.level);
    }
    juce::String getDescription() const override {
        return "Remove Send";
    }

  private:
    TrackId sourceTrackId_;
    int busIndex_;
    SendInfo saved_;
    bool hasSaved_ = false;
};

/**
 * @brief Command for setting master volume (supports merging for slider drags)
 */
class SetMasterVolumeCommand : public UndoableCommand {
  public:
    explicit SetMasterVolumeCommand(float newVolume) : newVolume_(newVolume) {
        oldVolume_ = TrackManager::getInstance().getMasterChannel().volume;
    }

    void execute() override {
        TrackManager::getInstance().setMasterVolume(newVolume_);
    }
    void undo() override {
        TrackManager::getInstance().setMasterVolume(oldVolume_);
    }
    juce::String getDescription() const override {
        return "Set Master Volume";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        return dynamic_cast<const SetMasterVolumeCommand*>(other) != nullptr;
    }
    void mergeWith(const UndoableCommand* other) override {
        newVolume_ = static_cast<const SetMasterVolumeCommand*>(other)->newVolume_;
    }

  private:
    float oldVolume_ = 1.0f, newVolume_;
};

/**
 * @brief Command for setting master pan (supports merging for slider drags)
 */
class SetMasterPanCommand : public UndoableCommand {
  public:
    explicit SetMasterPanCommand(float newPan) : newPan_(newPan) {
        oldPan_ = TrackManager::getInstance().getMasterChannel().pan;
    }
    SetMasterPanCommand(float oldPan, float newPan) : oldPan_(oldPan), newPan_(newPan) {}

    void execute() override {
        TrackManager::getInstance().setMasterPan(newPan_);
    }
    void undo() override {
        TrackManager::getInstance().setMasterPan(oldPan_);
    }
    juce::String getDescription() const override {
        return "Set Master Pan";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        return dynamic_cast<const SetMasterPanCommand*>(other) != nullptr;
    }
    void mergeWith(const UndoableCommand* other) override {
        newPan_ = static_cast<const SetMasterPanCommand*>(other)->newPan_;
    }

  private:
    float oldPan_ = 0.0f, newPan_;
};

/**
 * @brief Command for setting master mute state
 */
class SetMasterMuteCommand : public UndoableCommand {
  public:
    explicit SetMasterMuteCommand(bool newMuted) : newMuted_(newMuted) {
        oldMuted_ = TrackManager::getInstance().getMasterChannel().muted;
    }

    void execute() override {
        TrackManager::getInstance().setMasterMuted(newMuted_);
    }
    void undo() override {
        TrackManager::getInstance().setMasterMuted(oldMuted_);
    }
    juce::String getDescription() const override {
        return "Set Master Mute";
    }

  private:
    bool oldMuted_ = false, newMuted_;
};
/**
 * @brief Command for setting track colour
 */
class SetTrackColourCommand : public UndoableCommand {
  public:
    SetTrackColourCommand(TrackId trackId, juce::Colour newColour)
        : trackId_(trackId), newColour_(newColour) {
        auto* track = TrackManager::getInstance().getTrack(trackId);
        if (track)
            oldColour_ = track->colour;
    }

    void execute() override {
        TrackManager::getInstance().setTrackColour(trackId_, newColour_);
    }
    void undo() override {
        TrackManager::getInstance().setTrackColour(trackId_, oldColour_);
    }
    juce::String getDescription() const override {
        return "Set Track Colour";
    }

  private:
    TrackId trackId_;
    juce::Colour oldColour_, newColour_;
};

}  // namespace magda
