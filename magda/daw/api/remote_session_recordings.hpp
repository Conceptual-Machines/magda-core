#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "remote_jobs.hpp"

namespace magda {
class MagdaApi;

namespace remote {

/** Coordinates long-running Session recording controls with the shared job registry. */
class RemoteSessionRecordings final : public std::enable_shared_from_this<RemoteSessionRecordings> {
  public:
    struct Result {
        std::optional<RemoteJobDto> job;
        std::optional<Error> error;
        bool modelChanged = false;
    };

    RemoteSessionRecordings(MagdaApi& api, std::shared_ptr<RemoteJobManager> jobs,
                            std::shared_ptr<std::atomic<Revision>> revision);
    ~RemoteSessionRecordings();

    Result beginSlot(TrackId trackId, SceneId sceneId, int sceneIndex,
                     const RequestContext& context);
    Result stopSlot(const juce::String& jobId, const RequestContext& context);
    Result beginPerformance(const RequestContext& context);
    Result stopPerformance(const juce::String& jobId, const RequestContext& context);
    void poll();
    void shutdown();

  private:
    enum class Kind { Slot, Performance };
    struct Active {
        juce::String jobId;
        juce::String ownerClientId;
        Kind kind = Kind::Slot;
        TrackId trackId = INVALID_TRACK_ID;
        SceneId sceneId = INVALID_SCENE_ID;
        int sceneIndex = -1;
        std::vector<ClipId> arrangementBefore;
        bool stopping = false;
    };

    void pollSoon();
    void releaseSlot(TrackId trackId, int sceneIndex, bool discard);
    void releasePerformance(const juce::String& jobId);
    static juce::var slotResult(const Active& active, ClipId clipId);
    juce::var performanceResult(const Active& active);
    Active* owned(const juce::String& jobId, const RequestContext& context, Error& error);
    std::optional<RemoteJobDto> job(const Active& active, const RequestContext& context,
                                    Error& error);

    MagdaApi& api_;
    std::shared_ptr<RemoteJobManager> jobs_;
    std::shared_ptr<std::atomic<Revision>> revision_;
    std::vector<Active> active_;
    bool shutdown_ = false;
};

}  // namespace remote
}  // namespace magda
