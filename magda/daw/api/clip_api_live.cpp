#include "clip_api_live.hpp"

#include "../audio/AudioThumbnailManager.hpp"
#include "../core/ClipManager.hpp"

namespace magda {

ClipInfo* ClipApiLive::getClip(ClipId clipId) {
    return ClipManager::getInstance().getClip(clipId);
}

std::vector<ClipInfo> ClipApiLive::getArrangementClips() const {
    return ClipManager::getInstance().getArrangementClips();
}

std::vector<ClipId> ClipApiLive::getClipsOnTrack(TrackId trackId) const {
    return ClipManager::getInstance().getClipsOnTrack(trackId);
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

void ClipApiLive::setGrooveTemplate(ClipId clipId, const juce::String& templateName) {
    ClipManager::getInstance().setGrooveTemplate(clipId, templateName);
}

const juce::Array<double>* ClipApiLive::getCachedTransients(const juce::String& filePath) const {
    return AudioThumbnailManager::getInstance().getCachedTransients(filePath);
}

}  // namespace magda
