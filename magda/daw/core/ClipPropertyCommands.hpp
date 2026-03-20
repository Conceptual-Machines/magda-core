#pragma once

#include "ClipManager.hpp"
#include "UndoManager.hpp"

namespace magda {

/**
 * @brief Command for setting clip name
 */
class SetClipNameCommand : public UndoableCommand {
  public:
    SetClipNameCommand(ClipId clipId, const juce::String& newName)
        : clipId_(clipId), newName_(newName) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldName_ = clip->name;
    }

    void execute() override {
        ClipManager::getInstance().setClipName(clipId_, newName_);
    }
    void undo() override {
        ClipManager::getInstance().setClipName(clipId_, oldName_);
    }
    juce::String getDescription() const override {
        return "Set Clip Name";
    }

  private:
    ClipId clipId_;
    juce::String oldName_, newName_;
};

/**
 * @brief Command for setting clip offset (supports merging)
 */
class SetClipOffsetCommand : public UndoableCommand {
  public:
    SetClipOffsetCommand(ClipId clipId, double newOffset) : clipId_(clipId), newOffset_(newOffset) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldOffset_ = (clip->type == ClipType::MIDI) ? clip->midiOffset : clip->offset;
    }

    void execute() override {
        ClipManager::getInstance().setOffset(clipId_, newOffset_);
    }
    void undo() override {
        ClipManager::getInstance().setOffset(clipId_, oldOffset_);
    }
    juce::String getDescription() const override {
        return "Set Clip Offset";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetClipOffsetCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newOffset_ = static_cast<const SetClipOffsetCommand*>(other)->newOffset_;
    }

  private:
    ClipId clipId_;
    double oldOffset_ = 0.0, newOffset_;
};

/**
 * @brief Command for setting clip pitch change (supports merging)
 */
class SetClipPitchCommand : public UndoableCommand {
  public:
    SetClipPitchCommand(ClipId clipId, float newPitch) : clipId_(clipId), newPitch_(newPitch) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldPitch_ = clip->pitchChange;
    }

    void execute() override {
        ClipManager::getInstance().setPitchChange(clipId_, newPitch_);
    }
    void undo() override {
        ClipManager::getInstance().setPitchChange(clipId_, oldPitch_);
    }
    juce::String getDescription() const override {
        return "Set Clip Pitch";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetClipPitchCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newPitch_ = static_cast<const SetClipPitchCommand*>(other)->newPitch_;
    }

  private:
    ClipId clipId_;
    float oldPitch_ = 0.0f, newPitch_;
};

/**
 * @brief Command for setting clip speed ratio (supports merging)
 */
class SetClipSpeedRatioCommand : public UndoableCommand {
  public:
    SetClipSpeedRatioCommand(ClipId clipId, double newRatio)
        : clipId_(clipId), newRatio_(newRatio) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldRatio_ = clip->speedRatio;
    }

    void execute() override {
        ClipManager::getInstance().setSpeedRatio(clipId_, newRatio_);
    }
    void undo() override {
        ClipManager::getInstance().setSpeedRatio(clipId_, oldRatio_);
    }
    juce::String getDescription() const override {
        return "Set Clip Speed Ratio";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetClipSpeedRatioCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newRatio_ = static_cast<const SetClipSpeedRatioCommand*>(other)->newRatio_;
    }

  private:
    ClipId clipId_;
    double oldRatio_ = 1.0, newRatio_;
};

/**
 * @brief Command for setting clip time stretch mode
 */
class SetClipStretchModeCommand : public UndoableCommand {
  public:
    SetClipStretchModeCommand(ClipId clipId, int newMode) : clipId_(clipId), newMode_(newMode) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldMode_ = clip->timeStretchMode;
    }

    void execute() override {
        ClipManager::getInstance().setTimeStretchMode(clipId_, newMode_);
    }
    void undo() override {
        ClipManager::getInstance().setTimeStretchMode(clipId_, oldMode_);
    }
    juce::String getDescription() const override {
        return "Set Clip Stretch Mode";
    }

  private:
    ClipId clipId_;
    int oldMode_ = 0, newMode_;
};

/**
 * @brief Command for setting clip volume in dB (supports merging)
 */
class SetClipVolumeDBCommand : public UndoableCommand {
  public:
    SetClipVolumeDBCommand(ClipId clipId, float newDB) : clipId_(clipId), newDB_(newDB) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldDB_ = clip->volumeDB;
    }

    void execute() override {
        ClipManager::getInstance().setClipVolumeDB(clipId_, newDB_);
    }
    void undo() override {
        ClipManager::getInstance().setClipVolumeDB(clipId_, oldDB_);
    }
    juce::String getDescription() const override {
        return "Set Clip Volume";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetClipVolumeDBCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newDB_ = static_cast<const SetClipVolumeDBCommand*>(other)->newDB_;
    }

  private:
    ClipId clipId_;
    float oldDB_ = 0.0f, newDB_;
};

/**
 * @brief Command for setting clip gain in dB (supports merging)
 */
class SetClipGainDBCommand : public UndoableCommand {
  public:
    SetClipGainDBCommand(ClipId clipId, float newDB) : clipId_(clipId), newDB_(newDB) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldDB_ = clip->gainDB;
    }

    void execute() override {
        ClipManager::getInstance().setClipGainDB(clipId_, newDB_);
    }
    void undo() override {
        ClipManager::getInstance().setClipGainDB(clipId_, oldDB_);
    }
    juce::String getDescription() const override {
        return "Set Clip Gain";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetClipGainDBCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newDB_ = static_cast<const SetClipGainDBCommand*>(other)->newDB_;
    }

  private:
    ClipId clipId_;
    float oldDB_ = 0.0f, newDB_;
};

/**
 * @brief Command for setting clip pan (supports merging)
 */
class SetClipPanCommand : public UndoableCommand {
  public:
    SetClipPanCommand(ClipId clipId, float newPan) : clipId_(clipId), newPan_(newPan) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldPan_ = clip->pan;
    }

    void execute() override {
        ClipManager::getInstance().setClipPan(clipId_, newPan_);
    }
    void undo() override {
        ClipManager::getInstance().setClipPan(clipId_, oldPan_);
    }
    juce::String getDescription() const override {
        return "Set Clip Pan";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetClipPanCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newPan_ = static_cast<const SetClipPanCommand*>(other)->newPan_;
    }

  private:
    ClipId clipId_;
    float oldPan_ = 0.0f, newPan_;
};

/**
 * @brief Command for setting clip reversed state
 */
class SetClipReversedCommand : public UndoableCommand {
  public:
    SetClipReversedCommand(ClipId clipId, bool newReversed)
        : clipId_(clipId), newReversed_(newReversed) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldReversed_ = clip->isReversed;
    }

    void execute() override {
        ClipManager::getInstance().setIsReversed(clipId_, newReversed_);
    }
    void undo() override {
        ClipManager::getInstance().setIsReversed(clipId_, oldReversed_);
    }
    juce::String getDescription() const override {
        return "Set Clip Reversed";
    }

  private:
    ClipId clipId_;
    bool oldReversed_ = false, newReversed_;
};

/**
 * @brief Command for setting clip fade in duration (supports merging)
 */
class SetClipFadeInCommand : public UndoableCommand {
  public:
    SetClipFadeInCommand(ClipId clipId, double newFadeIn) : clipId_(clipId), newFadeIn_(newFadeIn) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldFadeIn_ = clip->fadeIn;
    }

    void execute() override {
        ClipManager::getInstance().setFadeIn(clipId_, newFadeIn_);
    }
    void undo() override {
        ClipManager::getInstance().setFadeIn(clipId_, oldFadeIn_);
    }
    juce::String getDescription() const override {
        return "Set Clip Fade In";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetClipFadeInCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newFadeIn_ = static_cast<const SetClipFadeInCommand*>(other)->newFadeIn_;
    }

  private:
    ClipId clipId_;
    double oldFadeIn_ = 0.0, newFadeIn_;
};

/**
 * @brief Command for setting clip fade out duration (supports merging)
 */
class SetClipFadeOutCommand : public UndoableCommand {
  public:
    SetClipFadeOutCommand(ClipId clipId, double newFadeOut)
        : clipId_(clipId), newFadeOut_(newFadeOut) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldFadeOut_ = clip->fadeOut;
    }

    void execute() override {
        ClipManager::getInstance().setFadeOut(clipId_, newFadeOut_);
    }
    void undo() override {
        ClipManager::getInstance().setFadeOut(clipId_, oldFadeOut_);
    }
    juce::String getDescription() const override {
        return "Set Clip Fade Out";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetClipFadeOutCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        newFadeOut_ = static_cast<const SetClipFadeOutCommand*>(other)->newFadeOut_;
    }

  private:
    ClipId clipId_;
    double oldFadeOut_ = 0.0, newFadeOut_;
};

/**
 * @brief Command for setting clip length in beats (supports merging)
 */
class SetClipLengthBeatsCommand : public UndoableCommand {
  public:
    SetClipLengthBeatsCommand(ClipId clipId, double newBeats, double bpm)
        : clipId_(clipId), newBeats_(newBeats), bpm_(bpm) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldBeats_ = clip->lengthBeats;
    }

    void execute() override {
        ClipManager::getInstance().setLengthBeats(clipId_, newBeats_, bpm_);
    }
    void undo() override {
        ClipManager::getInstance().setLengthBeats(clipId_, oldBeats_, bpm_);
    }
    juce::String getDescription() const override {
        return "Set Clip Length";
    }

    bool canMergeWith(const UndoableCommand* other) const override {
        if (auto* o = dynamic_cast<const SetClipLengthBeatsCommand*>(other))
            return o->clipId_ == clipId_;
        return false;
    }
    void mergeWith(const UndoableCommand* other) override {
        auto* o = static_cast<const SetClipLengthBeatsCommand*>(other);
        newBeats_ = o->newBeats_;
        bpm_ = o->bpm_;
    }

  private:
    ClipId clipId_;
    double oldBeats_ = 0.0, newBeats_;
    double bpm_;
};

/**
 * @brief Command for setting clip fade-in type
 */
class SetClipFadeInTypeCommand : public UndoableCommand {
  public:
    SetClipFadeInTypeCommand(ClipId clipId, int newType) : clipId_(clipId), newType_(newType) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldType_ = clip->fadeInType;
    }

    void execute() override {
        ClipManager::getInstance().setFadeInType(clipId_, newType_);
    }
    void undo() override {
        ClipManager::getInstance().setFadeInType(clipId_, oldType_);
    }
    juce::String getDescription() const override {
        return "Set Clip Fade In Type";
    }

  private:
    ClipId clipId_;
    int oldType_ = 1, newType_;
};

/**
 * @brief Command for setting clip fade-out type
 */
class SetClipFadeOutTypeCommand : public UndoableCommand {
  public:
    SetClipFadeOutTypeCommand(ClipId clipId, int newType) : clipId_(clipId), newType_(newType) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldType_ = clip->fadeOutType;
    }

    void execute() override {
        ClipManager::getInstance().setFadeOutType(clipId_, newType_);
    }
    void undo() override {
        ClipManager::getInstance().setFadeOutType(clipId_, oldType_);
    }
    juce::String getDescription() const override {
        return "Set Clip Fade Out Type";
    }

  private:
    ClipId clipId_;
    int oldType_ = 1, newType_;
};

/**
 * @brief Command for setting clip fade-in behaviour
 */
class SetClipFadeInBehaviourCommand : public UndoableCommand {
  public:
    SetClipFadeInBehaviourCommand(ClipId clipId, int newBehaviour)
        : clipId_(clipId), newBehaviour_(newBehaviour) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldBehaviour_ = clip->fadeInBehaviour;
    }

    void execute() override {
        ClipManager::getInstance().setFadeInBehaviour(clipId_, newBehaviour_);
    }
    void undo() override {
        ClipManager::getInstance().setFadeInBehaviour(clipId_, oldBehaviour_);
    }
    juce::String getDescription() const override {
        return "Set Clip Fade In Behaviour";
    }

  private:
    ClipId clipId_;
    int oldBehaviour_ = 0, newBehaviour_;
};

/**
 * @brief Command for setting clip fade-out behaviour
 */
class SetClipFadeOutBehaviourCommand : public UndoableCommand {
  public:
    SetClipFadeOutBehaviourCommand(ClipId clipId, int newBehaviour)
        : clipId_(clipId), newBehaviour_(newBehaviour) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldBehaviour_ = clip->fadeOutBehaviour;
    }

    void execute() override {
        ClipManager::getInstance().setFadeOutBehaviour(clipId_, newBehaviour_);
    }
    void undo() override {
        ClipManager::getInstance().setFadeOutBehaviour(clipId_, oldBehaviour_);
    }
    juce::String getDescription() const override {
        return "Set Clip Fade Out Behaviour";
    }

  private:
    ClipId clipId_;
    int oldBehaviour_ = 0, newBehaviour_;
};

/**
 * @brief Command for setting clip colour
 */
class SetClipColourCommand : public UndoableCommand {
  public:
    SetClipColourCommand(ClipId clipId, juce::Colour newColour)
        : clipId_(clipId), newColour_(newColour) {
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip)
            oldColour_ = clip->colour;
    }

    void execute() override {
        ClipManager::getInstance().setClipColour(clipId_, newColour_);
    }
    void undo() override {
        ClipManager::getInstance().setClipColour(clipId_, oldColour_);
    }
    juce::String getDescription() const override {
        return "Set Clip Colour";
    }

  private:
    ClipId clipId_;
    juce::Colour oldColour_, newColour_;
};

}  // namespace magda
