#include "../ClipInspector.hpp"

namespace magda::daw::ui {

void ClipInspector::setSelectedClip(magda::ClipId clipId) {
    selectedClipId_ = clipId;
    updateFromSelectedClip();
}

void ClipInspector::clipsChanged() {
    updateFromSelectedClip();
}

void ClipInspector::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == selectedClipId_) {
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
                           (clipGainValue_ && clipGainValue_->isDragging()) ||
                           (clipPanValue_ && clipPanValue_->isDragging()) ||
                           (fadeInValue_ && fadeInValue_->isDragging()) ||
                           (fadeOutValue_ && fadeOutValue_->isDragging()) ||
                           (beatSensitivityValue_ && beatSensitivityValue_->isDragging()) ||
                           (transientSensitivityValue_ && transientSensitivityValue_->isDragging());
        if (anyDragging)
            return;

        updateFromSelectedClip();
    }
}

void ClipInspector::clipSelectionChanged(magda::ClipId clipId) {
    setSelectedClip(clipId);
}

}  // namespace magda::daw::ui
