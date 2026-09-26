#pragma once

#include "session_api.hpp"

namespace magda {

/// Forwards SessionApi calls to the project, clip, track, and engine owners.
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
    bool isSlotRecordArmed(TrackId trackId, int sceneIndex) const override;
    bool isSlotRecording(TrackId trackId, int sceneIndex) const override;
    SessionSceneState captureSceneState() const override;
    void restoreSceneState(const SessionSceneState& state) override;
    SceneId createScene(int index, const juce::String& name, std::uint32_t colourArgb) override;
    bool updateScene(SceneId sceneId, const juce::String& name, std::uint32_t colourArgb) override;
    bool moveScene(SceneId sceneId, int toIndex) override;
    SceneId duplicateScene(SceneId sceneId, bool copyClips) override;
    bool deleteScene(SceneId sceneId, PopulatedScenePolicy policy,
                     SceneId destinationSceneId) override;
};

}  // namespace magda
