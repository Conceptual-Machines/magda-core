#pragma once

#include "clip_api.hpp"

namespace magda {

/// Forwards every ClipApi call to ClipManager::getInstance().
class ClipApiLive : public ClipApi {
  public:
    ClipInfo* getClip(ClipId clipId) override;
    std::vector<ClipInfo> getArrangementClips() const override;
    std::vector<ClipId> getClipsOnTrack(TrackId trackId) const override;

    ClipId createMidiClipBeats(TrackId trackId, double startBeats, double lengthBeats,
                               ClipView view) override;
    void deleteClip(ClipId clipId) override;

    void setClipName(ClipId clipId, const juce::String& name) override;
    void setGrooveTemplate(ClipId clipId, const juce::String& templateName) override;

    const juce::Array<double>* getCachedTransients(const juce::String& filePath) const override;
};

}  // namespace magda
