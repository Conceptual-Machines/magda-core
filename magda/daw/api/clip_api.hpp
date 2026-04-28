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

    virtual ClipId createMidiClipBeats(TrackId trackId, double startBeats, double lengthBeats,
                                       ClipView view = ClipView::Arrangement) = 0;
    virtual void deleteClip(ClipId clipId) = 0;

    virtual void setClipName(ClipId clipId, const juce::String& name) = 0;
};

}  // namespace magda
