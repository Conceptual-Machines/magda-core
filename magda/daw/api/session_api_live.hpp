#pragma once

#include "session_api.hpp"

namespace magda {

/// Forwards SessionApi calls to ClipManager + TrackManager singletons.
/// State queries beyond getActiveClipOnTrack would require
/// SessionClipScheduler access, which isn't a singleton — deferred until
/// the scheduler is properly accessible.
class SessionApiLive : public SessionApi {
  public:
    void launchClip(ClipId clipId) override;
    void stopClip(ClipId clipId) override;
    void stopTrack(TrackId trackId) override;
    void stopAll() override;
    void launchScene(int sceneIndex) override;
    ClipId getActiveClipOnTrack(TrackId trackId) const override;
    ClipId getClipInSlot(TrackId trackId, int sceneIndex) const override;
    SessionClipPlayState getClipPlayState(ClipId clipId) const override;
};

}  // namespace magda
