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
        updateFromSelectedClip();
    }
}

void ClipInspector::clipSelectionChanged(magda::ClipId clipId) {
    setSelectedClip(clipId);
}

}  // namespace magda::daw::ui
