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
    // so powered-off devices must stay off through the render and afterwards.
}

std::optional<juce::BigInteger> resolveOfflineRenderTrackFilter(
    const OfflineRenderRequest& request, int numTracks,
    const std::function<int(TrackId)>& indexForTrack) {
    juce::BigInteger result;

    if (!request.trackIds.empty()) {
        for (const auto trackId : request.trackIds) {
            const int index = indexForTrack(trackId);
            if (index >= 0 && index < numTracks)
                result.setBit(index);
        }
        if (result.isZero())
            return std::nullopt;
        return result;
    }

    if (!request.excludedTrackIds.empty()) {
        for (int index = 0; index < numTracks; ++index)
            result.setBit(index);

        int resolvedExclusions = 0;
        for (const auto trackId : request.excludedTrackIds) {
            const int index = indexForTrack(trackId);
            if (index >= 0 && index < numTracks) {
                result.clearBit(index);
                ++resolvedExclusions;
            }
        }
        if (resolvedExclusions == 0 || result.isZero())
            return std::nullopt;
    }

    return result;
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
    TracktionOfflineRenderTask(TracktionEngineWrapper& engine, tracktion::Edit& edit,
                               const OfflineRenderRequest& request,
                               const juce::BigInteger& tracksToDo)
        : request_(request), params_(edit) {
        params_.destFile = request_.destination;
        auto& formats = engine.getEngine()->getAudioFileFormatManager();
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
            tracktion::TimeRange(tracktion::TimePosition::fromSeconds(request_.range.start.seconds),
                                 tracktion::TimePosition::fromSeconds(request_.range.end.seconds));
        params_.endAllowance =
            tracktion::TimeDuration::fromSeconds(request_.range.endAllowance.seconds);
        params_.tracksToDo = tracksToDo;

        if (auto* bridge = engine.getAudioBridge())
            for (const auto clipId : request_.clipIds)
                if (auto* clip = bridge->getArrangementTeClip(clipId))
                    params_.allowedClips.add(clip);
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
    OfflineRenderRequest request_;
    tracktion::Renderer::Parameters params_;
};

class TracktionOfflineRenderSession final : public OfflineRenderSession {
  public:
    TracktionOfflineRenderSession(TracktionEngineWrapper& engine, bool resumePlaybackWhenFinished)
        : engine_(engine),
          edit_(*engine.getEdit()),
          resumePlaybackWhenFinished_(resumePlaybackWhenFinished),
          inhibitor_(edit_.getTransport()) {
        prepareEditForOfflineRender(edit_);
        preparePluginsForOfflineRender(engine_);
        engine_.setOfflineRenderActive(true);
    }

    ~TracktionOfflineRenderSession() override {
        restorePluginsAfterOfflineRender(engine_);
        edit_.getTransport().ensureContextAllocated();
        engine_.setOfflineRenderActive(false);
        if (resumePlaybackWhenFinished_)
            edit_.getTransport().play(false);
    }

    std::unique_ptr<OfflineRenderTask> createTask(const OfflineRenderRequest& request) override {
        if (request.destination == juce::File() || !request.range.isValid())
            return nullptr;

        const auto allTracks = tracktion::getAllTracks(edit_);
        auto* bridge = engine_.getAudioBridge();
        const auto tracksToDo = resolveOfflineRenderTrackFilter(
            request, allTracks.size(), [bridge, &allTracks](TrackId trackId) {
                if (bridge == nullptr)
                    return -1;
                auto* track = bridge->getAudioTrack(trackId);
                return track != nullptr ? allTracks.indexOf(track) : -1;
            });
        if (!tracksToDo)
            return nullptr;

        if (!request.clipIds.empty()) {
            if (bridge == nullptr)
                return nullptr;
            bool resolvedClip = false;
            for (const auto clipId : request.clipIds)
                resolvedClip = resolvedClip || bridge->getArrangementTeClip(clipId) != nullptr;
            if (!resolvedClip)
                return nullptr;
        }

        return std::make_unique<TracktionOfflineRenderTask>(engine_, edit_, request, *tracksToDo);
    }

  private:
    TracktionEngineWrapper& engine_;
    tracktion::Edit& edit_;
    bool resumePlaybackWhenFinished_ = false;
    tracktion::TransportControl::ReallocationInhibitor inhibitor_;
};

}  // namespace

std::unique_ptr<OfflineRenderSession> TracktionEngineWrapper::createOfflineRenderSession(
    bool resumePlaybackWhenFinished) {
    if (!currentEdit_ || !engine_)
        return nullptr;
    return std::make_unique<TracktionOfflineRenderSession>(*this, resumePlaybackWhenFinished);
}

}  // namespace magda
