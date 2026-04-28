#pragma once

#include <vector>

#include "../core/ClipInfo.hpp"
#include "../core/ClipTypes.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

/// Abstract view onto ClipManager — what the agent layer reads and writes.
class ClipApi {
  public:
    virtual ~ClipApi() = default;

    virtual ClipInfo* getClip(ClipId clipId) = 0;
    virtual std::vector<ClipInfo> getArrangementClips() const = 0;
    virtual std::vector<ClipId> getClipsOnTrack(TrackId trackId) const = 0;

    virtual ClipId createMidiClipBeats(TrackId trackId, double startBeats, double lengthBeats,
                                       ClipView view = ClipView::Arrangement) = 0;
    virtual void deleteClip(ClipId clipId) = 0;

    virtual void setClipName(ClipId clipId, const juce::String& name) = 0;
    virtual void setGrooveTemplate(ClipId clipId, const juce::String& templateName) = 0;

    /**
     * @brief Cached transient times for an audio clip's source file.
     *
     * Looks the file path up in the audio thumbnail / transient cache.
     * @return Array of transient times in seconds, or nullptr if no
     *         transients have been detected for this file.
     */
    virtual const juce::Array<double>* getCachedTransients(const juce::String& filePath) const = 0;
};

}  // namespace magda
