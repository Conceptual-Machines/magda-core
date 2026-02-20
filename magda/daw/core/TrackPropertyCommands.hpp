#pragma once

#include "TrackManager.hpp"
#include "UndoManager.hpp"

namespace magda {

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

}  // namespace magda
