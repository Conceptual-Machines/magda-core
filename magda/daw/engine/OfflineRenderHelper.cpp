#include "OfflineRenderHelper.hpp"

#include "../audio/AudioBridge.hpp"
#include "TracktionEngineWrapper.hpp"

namespace magda {

void prepareEditForOfflineRender(tracktion::Edit& edit) {
    auto& transport = edit.getTransport();
    if (transport.isPlaying())
        transport.stop(false, false);

    tracktion::freePlaybackContextIfNotRecording(transport);

    // Nothing here may touch plugin enablement (#1880). Every engine plugin's
    // enabled flag already mirrors TrackManager::isDeviceEffectivelyEnabled,
    // so powered-off devices must stay off through the render and stay off
    // afterwards.
}

namespace {

void preparePluginsForOfflineRender(TracktionEngineWrapper& engine) {
    if (auto* bridge = engine.getAudioBridge())
        bridge->getPluginManager().prepareForRendering();
}

void restorePluginsAfterOfflineRender(TracktionEngineWrapper& engine) {
    if (auto* bridge = engine.getAudioBridge())
        bridge->getPluginManager().restoreAfterRendering();
}

class TracktionOfflineRenderTask final : public OfflineRenderTask {
  public:
    TracktionOfflineRenderTask(TracktionEngineWrapper& engine, const OfflineRenderRequest& request)
        : engine_(engine),
          edit_(*engine.getEdit()),
          request_(request),
          params_(edit_),
          inhibitor_(edit_.getTransport()) {
        auto& transport = edit_.getTransport();
        wasPlaying_ = transport.isPlaying();
        prepareEditForOfflineRender(edit_);
        preparePluginsForOfflineRender(engine_);
        engine_.setOfflineRenderActive(true);

        params_.destFile = request_.destination;
        auto& formats = engine_.getEngine()->getAudioFileFormatManager();
        params_.audioFormat = request_.format == OfflineRenderFormat::Flac ? formats.getFlacFormat()
                                                                           : formats.getWavFormat();
        params_.bitDepth = request_.bitDepth;
        params_.sampleRateForAudio = request_.sampleRate;
        params_.blockSizeForAudio = request_.blockSize;
        params_.shouldNormalise = request_.shouldNormalise;
        params_.normaliseToLevelDb = request_.normaliseToLevelDb;
        params_.useMasterPlugins = request_.useMasterPlugins;
        params_.usePlugins = request_.usePlugins;
        params_.checkNodesForAudio = request_.checkNodesForAudio;
        params_.realTimeRender = request_.realTimeRender;
        params_.time =
            tracktion::TimeRange(tracktion::TimePosition::fromSeconds(request_.startSeconds),
                                 tracktion::TimePosition::fromSeconds(request_.endSeconds));
        params_.endAllowance = tracktion::TimeDuration::fromSeconds(request_.endAllowanceSeconds);

        const auto allTracks = tracktion::getAllTracks(edit_);
        auto* bridge = engine_.getAudioBridge();
        if (!request_.trackIds.empty()) {
            for (const auto trackId : request_.trackIds) {
                if (bridge == nullptr)
                    break;
                if (auto* track = bridge->getAudioTrack(trackId)) {
                    const int index = allTracks.indexOf(track);
                    if (index >= 0)
                        params_.tracksToDo.setBit(index);
                }
            }
        } else if (!request_.excludedTrackIds.empty()) {
            for (int index = 0; index < allTracks.size(); ++index)
                params_.tracksToDo.setBit(index);
            if (bridge != nullptr) {
                for (const auto trackId : request_.excludedTrackIds) {
                    if (auto* track = bridge->getAudioTrack(trackId)) {
                        const int index = allTracks.indexOf(track);
                        if (index >= 0)
                            params_.tracksToDo.clearBit(index);
                    }
                }
            }
        }

        if (bridge != nullptr) {
            for (const auto clipId : request_.clipIds)
                if (auto* clip = bridge->getArrangementTeClip(clipId))
                    params_.allowedClips.add(clip);
        }
    }

    ~TracktionOfflineRenderTask() override {
        restorePluginsAfterOfflineRender(engine_);
        edit_.getTransport().ensureContextAllocated();
        engine_.setOfflineRenderActive(false);
        if (request_.resumePlaybackAfterRender && wasPlaying_)
            edit_.getTransport().play(false);
    }

    OfflineRenderResult run(const std::function<bool()>& shouldCancel,
                            const std::function<void(float)>& onProgress) override {
        std::atomic<float> progress{0.0f};
        tracktion::Renderer::RenderTask task("MAGDA Offline Render", params_, &progress, nullptr);

        for (;;) {
            if (shouldCancel && shouldCancel())
                return {false, "Cancelled"};

            const auto status = task.runJob();
            if (onProgress)
                onProgress(progress.load());
            if (status == juce::ThreadPoolJob::jobHasFinished)
                break;
            if (status != juce::ThreadPoolJob::jobNeedsRunningAgain)
                return {false, task.errorMessage.isNotEmpty() ? task.errorMessage
                                                              : juce::String("Render failed")};
            juce::Thread::sleep(1);
        }

        if (task.errorMessage.isNotEmpty())
            return {false, task.errorMessage};
        if (!request_.destination.existsAsFile() || request_.destination.getSize() <= 0)
            return {false, "Render did not create an output file"};
        if (onProgress)
            onProgress(1.0f);
        return {true, {}};
    }

  private:
    TracktionEngineWrapper& engine_;
    tracktion::Edit& edit_;
    OfflineRenderRequest request_;
    tracktion::Renderer::Parameters params_;
    tracktion::TransportControl::ReallocationInhibitor inhibitor_;
    bool wasPlaying_ = false;
};

}  // namespace

std::unique_ptr<OfflineRenderTask> TracktionEngineWrapper::createOfflineRenderTask(
    const OfflineRenderRequest& request) {
    if (!currentEdit_ || !engine_ || request.destination == juce::File() ||
        request.endSeconds <= request.startSeconds)
        return nullptr;
    return std::make_unique<TracktionOfflineRenderTask>(*this, request);
}

}  // namespace magda
