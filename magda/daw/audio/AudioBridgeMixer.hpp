#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

class TrackController;

class AudioBridgeMixer {
  public:
    AudioBridgeMixer(te::Edit& edit, TrackController& trackController);

    void setTrackVolume(TrackId trackId, float volume);
    float getTrackVolume(TrackId trackId) const;

    void setTrackPan(TrackId trackId, float pan);
    float getTrackPan(TrackId trackId) const;

    void setMasterVolume(float volume);
    float getMasterVolume() const;

    void setMasterPan(float pan);
    float getMasterPan() const;

  private:
    te::Edit& edit_;
    TrackController& trackController_;
};

}  // namespace magda
