#include "clip_api_live.hpp"

#include "../core/ClipManager.hpp"

namespace magda {

ClipInfo* ClipApiLive::getClip(ClipId clipId) {
    return ClipManager::getInstance().getClip(clipId);
}

std::vector<ClipInfo> ClipApiLive::getArrangementClips() const {
    return ClipManager::getInstance().getArrangementClips();
}

ClipId ClipApiLive::createMidiClipBeats(TrackId trackId, double startBeats, double lengthBeats,
                                        ClipView view) {
    return ClipManager::getInstance().createMidiClipBeats(trackId, startBeats, lengthBeats, view);
}

void ClipApiLive::deleteClip(ClipId clipId) {
    ClipManager::getInstance().deleteClip(clipId);
}

void ClipApiLive::setClipName(ClipId clipId, const juce::String& name) {
    ClipManager::getInstance().setClipName(clipId, name);
}

}  // namespace magda
