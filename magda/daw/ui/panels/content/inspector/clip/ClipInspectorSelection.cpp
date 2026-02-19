#include "../ClipInspector.hpp"

namespace magda::daw::ui {

void ClipInspector::setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds) {
    selectedClipIds_ = clipIds;
    updateFromSelectedClip();
}

void ClipInspector::setSelectedClip(magda::ClipId clipId) {
    if (clipId == magda::INVALID_CLIP_ID)
        setSelectedClips({});
    else
        setSelectedClips({clipId});
}

void ClipInspector::clipsChanged() {
    updateFromSelectedClip();
}

void ClipInspector::clipPropertyChanged(magda::ClipId clipId) {
    if (selectedClipIds_.count(clipId) == 0)
        return;

    // When a draggable control triggers a value change, the round-trip is:
    //   onValueChange → ClipManager::set*() → notifyClipPropertyChanged → here
    // Calling the full updateFromSelectedClip() (which calls resized()) mid-drag
    // disrupts the drag interaction.  Skip the update for value-only changes.
    bool anyDragging = (clipStartValue_ && clipStartValue_->isDragging()) ||
                       (clipEndValue_ && clipEndValue_->isDragging()) ||
                       (clipContentOffsetValue_ && clipContentOffsetValue_->isDragging()) ||
                       (clipLoopStartValue_ && clipLoopStartValue_->isDragging()) ||
                       (clipLoopEndValue_ && clipLoopEndValue_->isDragging()) ||
                       (clipLoopPhaseValue_ && clipLoopPhaseValue_->isDragging()) ||
                       (clipStretchValue_ && clipStretchValue_->isDragging()) ||
                       (clipBeatsLengthValue_ && clipBeatsLengthValue_->isDragging()) ||
                       (pitchChangeValue_ && pitchChangeValue_->isDragging()) ||
                       (transposeValue_ && transposeValue_->isDragging()) ||
                       (clipVolumeValue_ && clipVolumeValue_->isDragging()) ||
                       (clipPanValue_ && clipPanValue_->isDragging()) ||
                       (clipGainValue_ && clipGainValue_->isDragging()) ||
                       (fadeInValue_ && fadeInValue_->isDragging()) ||
                       (fadeOutValue_ && fadeOutValue_->isDragging()) ||
                       (beatSensitivityValue_ && beatSensitivityValue_->isDragging()) ||
                       (transientSensitivityValue_ && transientSensitivityValue_->isDragging());
    if (anyDragging) {
        // Still update stretch mode combo during drag — pitchChange affects effective mode
        auto pid = primaryClipId();
        const auto* clip = magda::ClipManager::getInstance().getClip(pid);
        if (clip && clip->type == magda::ClipType::Audio) {
            int effectiveMode = clip->timeStretchMode;
            bool isAnalog = clip->isAnalogPitchActive();
            if (!isAnalog && effectiveMode == 0 &&
                (clip->autoTempo || clip->warpEnabled || std::abs(clip->speedRatio - 1.0) > 0.001 ||
                 std::abs(clip->pitchChange) > 0.001f)) {
                effectiveMode = 4;
            }
            stretchModeCombo_.setSelectedId(effectiveMode + 1, juce::dontSendNotification);
        }
        return;
    }

    updateFromSelectedClip();
}

void ClipInspector::clipSelectionChanged(magda::ClipId clipId) {
    setSelectedClip(clipId);
}

}  // namespace magda::daw::ui
