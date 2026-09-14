#include "OfflineRenderHelper.hpp"

#include <algorithm>
#include <utility>

#include "../audio/AudioBridge.hpp"
#include "../audio/plugins/InsertCapturePlugin.hpp"
#include "../audio/racks/InstrumentRackManager.hpp"
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

/// Rendered ahead of a non-realtime render and cut off again, so plugins settle.
constexpr double kPrerollSeconds = 2.0;

/**
 * @brief Cut the first @p seconds off @p file, rewriting it at its own format.
 *
 * Tracktion's renderer starts where it is told, so the preroll lands in the file.
 */
bool trimLeadingSeconds(const juce::File& file, double seconds) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader)
        return false;

    const auto samplesToSkip = static_cast<juce::int64>(seconds * reader->sampleRate);
    const auto samplesToKeep = reader->lengthInSamples - samplesToSkip;
    if (samplesToKeep <= 0)
        return false;

    const auto tempFile =
        file.getSiblingFile(file.getFileNameWithoutExtension() + "_tmp" + file.getFileExtension());

    std::unique_ptr<juce::AudioFormat> format;
    if (file.hasFileExtension(".flac"))
        format = std::make_unique<juce::FlacAudioFormat>();
    else
        format = std::make_unique<juce::WavAudioFormat>();

    std::unique_ptr<juce::OutputStream> outputStream =
        std::make_unique<juce::FileOutputStream>(tempFile);
    auto writerOptions = juce::AudioFormatWriterOptions()
                             .withSampleRate(reader->sampleRate)
                             .withNumChannels(static_cast<int>(reader->numChannels))
                             .withBitsPerSample(static_cast<int>(reader->bitsPerSample));
    auto writer = format->createWriterFor(outputStream, writerOptions);
    if (!writer)
        return false;

    writer->writeFromAudioReader(*reader, samplesToSkip, samplesToKeep);
    writer.reset();
    reader.reset();

    file.deleteFile();
    return tempFile.moveFileTo(file);
}

/** @brief The longest tail a plugin on the tracks in @p tracksToDo declares. */
tracktion::TimeDuration declaredTail(tracktion::Edit& edit, const juce::BigInteger& tracksToDo) {
    const auto allTracks = tracktion::getAllTracks(edit);

    juce::Array<tracktion::EditItemID> trackIds;
    for (auto index = 0; index < allTracks.size(); ++index)
        if (tracksToDo[index])
            trackIds.add(allTracks[index]->itemID);

    return tracktion::RenderOptions::findEndAllowance(edit, &trackIds, nullptr);
}

class TracktionOfflineRenderTask final : public OfflineRenderTask {
  public:
    TracktionOfflineRenderTask(TracktionEngineWrapper& engine, tracktion::Edit& edit,
                               OfflineRenderRequest request, const juce::BigInteger& tracksToDo)
        : request_(std::move(request)), params_(edit) {
        params_.destFile = request_.destination;
        auto& formats = engine.getEngine()->getAudioFileFormatManager();
        params_.audioFormat = request_.format == OfflineRenderFormat::Flac ? formats.getFlacFormat()
                                                                           : formats.getWavFormat();
        params_.bitDepth = request_.bitDepth;
        params_.ditheringEnabled =
            request_.dither.value_or(defaultOfflineRenderDither(request_.bitDepth)) !=
            OfflineRenderDither::None;
        params_.sampleRateForAudio = request_.sampleRate;
        params_.blockSizeForAudio = request_.blockSize;
        params_.shouldNormalise = request_.shouldNormalise;
        params_.normaliseToLevelDb = request_.normaliseToLevelDb;
        params_.useMasterPlugins = request_.useMasterPlugins;
        params_.usePlugins = request_.usePlugins;
        params_.realTimeRender = request_.realTimeRender;

        // Lead-in is kept out of the preroll rather than rendered on top of it.
        if (!request_.realTimeRender)
            trimSeconds_ = std::max(0.0, kPrerollSeconds - request_.leadInSeconds);

        const auto& tempo = edit.tempoSequence;
        const auto start =
            tempo.toTime(tracktion::BeatPosition::fromBeats(request_.range.start.value));
        const auto end = tempo.toTime(tracktion::BeatPosition::fromBeats(request_.range.end.value));
        params_.time =
            tracktion::TimeRange(start - tracktion::TimeDuration::fromSeconds(
                                             request_.realTimeRender ? 0.0 : kPrerollSeconds),
                                 end);
        params_.tracksToDo = tracksToDo;

        if (auto* bridge = engine.getAudioBridge())
            for (const auto clipId : request_.clipIds)
                if (auto* clip = bridge->getArrangementTeClip(clipId))
                    params_.allowedClips.add(clip);

        params_.endAllowance = request_.tailSeconds.has_value()
                                   ? tracktion::TimeDuration::fromSeconds(*request_.tailSeconds)
                                   : declaredTail(edit, tracksToDo);
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
        if (trimSeconds_ > 0.0 && !trimLeadingSeconds(request_.destination, trimSeconds_))
            return {false, "Render could not trim its preroll"};
        if (onProgress)
            onProgress(1.0f);
        return {true, {}};
    }

  private:
    OfflineRenderRequest request_;
    tracktion::Renderer::Parameters params_;
    double trimSeconds_ = 0.0;
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
        for (const auto& [plugin, wasEnabled] : bypassed_)
            if (plugin != nullptr)
                plugin->setEnabled(wasEnabled);

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

        if (!request.useTrackEffects && bridge != nullptr)
            for (const auto trackId : request.trackIds)
                if (auto* track = bridge->getAudioTrack(trackId))
                    bypassEffects(*track, bridge->getPluginManager().getInstrumentRackManager());

        return std::make_unique<TracktionOfflineRenderTask>(engine_, edit_, request, *tracksToDo);
    }

  private:
    /**
     * @brief Disable everything on @p track but its instrument, until the session ends.
     *
     * An external insert counts as the instrument, and the capture tap has to
     * keep playing the captured return (#1623).
     */
    void bypassEffects(tracktion::AudioTrack& track, InstrumentRackManager& racks) {
        for (auto* plugin : track.pluginList) {
            if (racks.isWrapperRack(plugin) ||
                dynamic_cast<tracktion::InsertPlugin*>(plugin) != nullptr ||
                dynamic_cast<InsertCapturePlugin*>(plugin) != nullptr)
                continue;

            bypassed_.emplace_back(plugin, plugin->isEnabled());
            plugin->setEnabled(false);
        }
    }

    TracktionEngineWrapper& engine_;
    tracktion::Edit& edit_;
    bool resumePlaybackWhenFinished_ = false;
    tracktion::TransportControl::ReallocationInhibitor inhibitor_;
    std::vector<std::pair<tracktion::Plugin::Ptr, bool>> bypassed_;
};

}  // namespace

std::unique_ptr<OfflineRenderSession> TracktionEngineWrapper::createOfflineRenderSession(
    bool resumePlaybackWhenFinished) {
    if (!currentEdit_ || !engine_)
        return nullptr;
    return std::make_unique<TracktionOfflineRenderSession>(*this, resumePlaybackWhenFinished);
}

}  // namespace magda
