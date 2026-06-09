#include "OfflineMixAnalysis.hpp"

#include <atomic>
#include <chrono>
#include <memory>

// clang-format off
#include <tracktion_engine/tracktion_engine.h>
// clang-format on

#include "../../core/TrackManager.hpp"
#include "../../engine/TracktionEngineWrapper.hpp"
#include "../AudioBridge.hpp"
#include "MixAnalysisInput.hpp"

namespace magda {
namespace daw::audio {

namespace tk = tracktion;

namespace {

// Load a rendered WAV into an in-memory buffer. Returns false on read failure.
bool loadWav(juce::AudioFormatManager& fm, const juce::File& file, juce::AudioBuffer<float>& out,
             double& sampleRateOut) {
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file.createInputStream()));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;
    sampleRateOut = reader->sampleRate;
    out.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples),
                false, true, false);
    reader->read(&out, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
    return true;
}

// Format a duration for the "time left" readout: "Xm Ys" past a minute, else "Xs".
juce::String formatSeconds(double seconds) {
    const int s = juce::jmax(0, juce::roundToInt(seconds));
    if (s >= 60)
        return juce::String(s / 60) + "m " + juce::String(s % 60) + "s";
    return juce::String(s) + "s";
}

// Run one offline render pass to outFile, blocking until done. tracksToDo empty
// => all tracks (the RenderTask path treats an empty bitset as "no track filter").
// onProgress is called with the pass's 0..1 render progress. Returns false on
// failure or cancellation.
bool renderPass(const tk::Renderer::Parameters& base, const juce::File& outFile,
                const juce::BigInteger& tracksToDo, bool useMasterPlugins,
                std::atomic<bool>& cancel, const std::function<void(float)>& onProgress) {
    tk::Renderer::Parameters params = base;
    params.destFile = outFile;
    params.tracksToDo = tracksToDo;
    params.useMasterPlugins = useMasterPlugins;

    std::atomic<float> progress{0.0f};
    tk::Renderer::RenderTask task("MixAnalysis", params, &progress, nullptr);

    for (;;) {
        if (cancel.load())
            return false;
        const auto status = task.runJob();
        if (onProgress)
            onProgress(progress.load());
        if (status == juce::ThreadPoolJob::jobHasFinished)
            return outFile.existsAsFile() && task.errorMessage.isEmpty();
        if (status == juce::ThreadPoolJob::jobNeedsRunningAgain) {
            juce::Thread::sleep(1);
            continue;
        }
        return false;  // error
    }
}

// Background driver: owns itself, deletes on completion. Setup runs in the ctor
// (message thread); the render passes + LLM call run in run() (background).
class AnalysisJob : public juce::Thread {
  public:
    AnalysisJob(TracktionEngineWrapper& engine, OfflineMixAnalysis::Request request,
                OfflineMixAnalysis::ProgressFn onProgress,
                OfflineMixAnalysis::CompletionFn onComplete,
                OfflineMixAnalysis::MeasuredFn onMeasured, OfflineMixAnalysis::CancelToken cancel)
        : juce::Thread("OfflineMixAnalysis"),
          engine_(engine),
          edit_(*engine.getEdit()),
          request_(std::move(request)),
          onProgress_(std::move(onProgress)),
          onComplete_(std::move(onComplete)),
          onMeasured_(std::move(onMeasured)),
          cancel_(std::move(cancel)),
          inhibitor_(edit_.getTransport()) {
        // Message-thread setup, mirroring the export path: TE asserts the play
        // context is not active during an offline render.
        auto& transport = edit_.getTransport();
        if (transport.isPlaying())
            transport.stop(false, false);
        tk::freePlaybackContextIfNotRecording(transport);

        for (auto* track : tk::getAudioTracks(edit_))
            for (auto* plugin : track->pluginList)
                if (!plugin->isEnabled())
                    plugin->setEnabled(true);

        if (auto* bridge = engine_.getAudioBridge())
            bridge->getPluginManager().prepareForRendering();

        // Block the transport for the render's lifetime: starting playback while
        // the render owns the edit corrupts the node graph (NodeRenderContext
        // asserts). Cleared in run()'s completion.
        engine_.setOfflineRenderActive(true);

        startThread();
    }

    ~AnalysisJob() override {
        cancel_->store(true);
        stopThread(8000);
    }

  private:
    void run() override {
        auto result = doWork();

        // Restore + deliver + self-destruct on the message thread.
        juce::MessageManager::callAsync([this, result = std::move(result)]() mutable {
            if (auto* bridge = engine_.getAudioBridge())
                bridge->getPluginManager().restoreAfterRendering();
            // The ctor freed the live playback context for the render
            // (freePlaybackContextIfNotRecording); rebuild it so live monitoring
            // and metering (the master VU etc.) resume instead of staying dead.
            edit_.getTransport().ensureContextAllocated();
            engine_.setOfflineRenderActive(false);  // re-allow playback
            if (onComplete_)
                onComplete_(std::move(result));
            delete this;
        });
    }

    // Progress is posted with a COPY of the callback (not `this`) so a queued
    // progress message is safe even if the job has already self-destructed.
    void postProgress(const juce::String& message) {
        auto cb = onProgress_;
        juce::MessageManager::callAsync([cb, message]() {
            if (cb)
                cb(message);
        });
    }

    MixAnalysisAgent::Result doWork() {
        MixAnalysisAgent::Result err;

        // Render at a reduced sample rate to speed up the N-pass deep render.
        // Mix measurements (loudness, dynamics, stereo, tonal balance) don't need
        // full bandwidth; 22.05 kHz keeps musically relevant HF (Nyquist ~11 kHz)
        // while roughly halving render + measurement cost per pass. This is the
        // cheap lever -- the structural win is a single-pass tap render.
        constexpr double kAnalysisSampleRate = 22050.0;
        const double sampleRate = kAnalysisSampleRate;

        // Resolve the render range.
        tk::TimeRange range;
        if (request_.range == OfflineMixAnalysis::RangeMode::LoopRange)
            range = edit_.getTransport().getLoopRange();
        if (range.getLength() <= tk::TimeDuration())
            range = tk::TimeRange(tk::TimePosition(), tk::TimePosition() + edit_.getLength());

        // Shared render parameters (mirrors the export settings).
        tk::Renderer::Parameters base(edit_);
        base.audioFormat = engine_.getEngine()->getAudioFileFormatManager().getWavFormat();
        base.bitDepth = 24;
        base.sampleRateForAudio = sampleRate;
        base.blockSizeForAudio = 512;
        base.shouldNormalise = false;
        base.usePlugins = true;
        base.checkNodesForAudio = false;
        base.realTimeRender = false;
        base.time = range;
        base.endAllowance = tk::TimeDuration::fromSeconds(2.0);  // let reverbs/delays decay

        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        const bool deep = request_.depth == OfflineMixAnalysis::Depth::Deep;

        // Resolve the track set up front (Deep) so the total pass count -- and
        // therefore the progress estimate -- is known before rendering starts:
        // one master pass plus one pass per track.
        std::vector<tk::AudioTrack*> teTracks;
        if (deep) {
            if (request_.trackSet.empty()) {
                for (auto* track : tk::getAudioTracks(edit_))
                    teTracks.push_back(track);
            } else if (auto* bridge = engine_.getAudioBridge()) {
                for (auto id : request_.trackSet)
                    if (auto* track = bridge->getAudioTrack(id))
                        teTracks.push_back(track);
            }
        }
        const int totalPasses = deep ? 1 + static_cast<int>(teTracks.size()) : 1;

        // Progress + ETA across all passes. Each pass reports its own 0..1 render
        // progress; we fold that into an overall fraction and extrapolate the time
        // left from elapsed wall-clock. Throttled so the chat is not flooded.
        const auto startTime = std::chrono::steady_clock::now();
        int passesDone = 0;
        double lastPct = -1.0;
        auto lastPost = startTime;
        juce::String phaseLabel = "Rendering mix...";  // set per pass (which track / the master)
        auto reportProgress = [&](float passFraction) {
            const double overall =
                totalPasses > 0
                    ? (passesDone + juce::jlimit(0.0f, 1.0f, passFraction)) / totalPasses
                    : 0.0;
            const auto now = std::chrono::steady_clock::now();
            const double pct = overall * 100.0;
            const double sinceMs =
                std::chrono::duration<double, std::milli>(now - lastPost).count();
            if (pct - lastPct < 1.0 && sinceMs < 250.0)
                return;  // throttle
            lastPct = pct;
            lastPost = now;
            juce::String msg;
            msg << phaseLabel << "  " << juce::roundToInt(pct) << "%";
            if (overall > 0.05) {
                const double elapsed = std::chrono::duration<double>(now - startTime).count();
                msg << "  (~" << formatSeconds(elapsed * (1.0 - overall) / overall) << " left)";
            }
            postProgress(msg);
        };

        // --- master / sum pass (both depths need it) ---
        phaseLabel = deep ? "Rendering mix (master)..." : "Rendering mix...";
        postProgress(phaseLabel);
        auto masterFile = tempDir.getNonexistentChildFile("magda_mix_master", ".wav");
        if (!renderPass(base, masterFile, {} /* all tracks */, /*useMasterPlugins*/ true, *cancel_,
                        reportProgress)) {
            masterFile.deleteFile();
            err.hasError = true;
            err.error = cancel_->load() ? "Cancelled." : "Mix render failed.";
            return err;
        }
        ++passesDone;

        juce::AudioBuffer<float> masterBuf;
        double masterSr = sampleRate;
        const bool masterOk = loadWav(fm, masterFile, masterBuf, masterSr);
        masterFile.deleteFile();
        if (!masterOk) {
            err.hasError = true;
            err.error = "Could not read the rendered mix.";
            return err;
        }

        MixAnalysisAgent::Input input;

        if (request_.depth == OfflineMixAnalysis::Depth::Shallow) {
            postProgress("Measuring mix...");
            auto mix = MixAnalysisInput::fingerprint(masterBuf, masterSr, "Mix", "master");
            input.master = mix;
            input.tracks.push_back(std::move(mix));  // agent requires a non-empty track set
        } else {
            const auto allTracks = tk::getAllTracks(edit_);

            // Buffers must outlive the build() call; unique_ptr keeps Source
            // pointers stable as the vector grows.
            std::vector<std::unique_ptr<juce::AudioBuffer<float>>> stemBufs;
            std::vector<MixAnalysisInput::Source> sources;
            std::vector<magda::TrackId> sourceTrackIds;  // aligned with sources for post-build tags
            stemBufs.reserve(teTracks.size());
            sources.reserve(teTracks.size());
            sourceTrackIds.reserve(teTracks.size());

            auto* bridge = engine_.getAudioBridge();

            const int total = static_cast<int>(teTracks.size());
            int skipped = 0;
            for (int i = 0; i < total; ++i) {
                if (cancel_->load()) {
                    err.hasError = true;
                    err.error = "Cancelled.";
                    return err;
                }
                auto* track = teTracks[static_cast<size_t>(i)];
                passesDone = 1 + i;  // master pass + tracks finished so far

                // Name the current pass so the user sees track-by-track progress.
                // Reset lastPct so the new label posts immediately (not throttled).
                phaseLabel = "Rendering: " + track->getName() + "  (" + juce::String(i + 1) + "/" +
                             juce::String(total) + ")";
                lastPct = -1.0;

                const int bit = allTracks.indexOf(track);
                if (bit < 0) {
                    ++skipped;
                    continue;
                }
                juce::BigInteger bits;
                bits.setBit(bit);

                auto stemFile = tempDir.getNonexistentChildFile("magda_mix_stem", ".wav");
                // Stems are pre-master (useMasterPlugins=false) so each is comparable.
                if (!renderPass(base, stemFile, bits, /*useMasterPlugins*/ false, *cancel_,
                                reportProgress)) {
                    stemFile.deleteFile();
                    ++skipped;
                    continue;
                }

                auto buf = std::make_unique<juce::AudioBuffer<float>>();
                double stemSr = sampleRate;
                const bool ok = loadWav(fm, stemFile, *buf, stemSr);
                stemFile.deleteFile();
                if (!ok) {
                    ++skipped;
                    continue;
                }

                MixAnalysisInput::Source src;
                src.name = track->getName();
                src.audio = buf.get();
                stemBufs.push_back(std::move(buf));
                sources.push_back(std::move(src));
                sourceTrackIds.push_back(bridge ? bridge->getTrackIdForTeTrack(track->itemID)
                                                : magda::INVALID_TRACK_ID);
            }

            if (sources.empty()) {
                err.hasError = true;
                err.error = "No tracks could be rendered for analysis.";
                return err;
            }

            postProgress("Analysing tracks...");
            MixAnalysisInput::Options opts;
            opts.numSegments = request_.numSegments;
            input = MixAnalysisInput::build(masterSr, sources, &masterBuf, {}, opts);

            // Annotate each track with its type (audio/MIDI) + effect chain. build()
            // preserves source order, so input.tracks[i] matches sourceTrackIds[i].
            auto& tmgr = magda::TrackManager::getInstance();
            for (size_t i = 0; i < input.tracks.size() && i < sourceTrackIds.size(); ++i) {
                const auto tid = sourceTrackIds[i];
                if (tid == magda::INVALID_TRACK_ID)
                    continue;
                input.tracks[i].role = tmgr.getPrimaryInstrument(tid) ? "MIDI" : "audio";
                input.tracks[i].chain = tmgr.getChainSummary(tid);
            }

            if (skipped > 0)
                postProgress(juce::String(skipped) + " track(s) skipped (render/read failed).");
        }

        // Song-level context.
        if (request_.bpm > 0.0f)
            input.bpm = request_.bpm;
        input.genre = request_.genre;
        input.question = request_.question;

        // Deliver the measured data before the agent step. Measure-only callers
        // (the mix-analysis modal) stop here; the LLM is opt-in.
        if (onMeasured_) {
            auto cb = onMeasured_;
            auto snapshot = input;  // copy: input is still needed for the agent below
            juce::MessageManager::callAsync(
                [cb, snapshot = std::move(snapshot)]() mutable { cb(std::move(snapshot)); });
        }
        if (request_.skipAgent)
            return {};  // clean, no-error Result; the measured Input went via onMeasured_

        postProgress("Asking the mix analyst...");
        MixAnalysisAgent agent;
        return agent.generate(input);
    }

    TracktionEngineWrapper& engine_;
    tk::Edit& edit_;
    OfflineMixAnalysis::Request request_;
    OfflineMixAnalysis::ProgressFn onProgress_;
    OfflineMixAnalysis::CompletionFn onComplete_;
    OfflineMixAnalysis::MeasuredFn onMeasured_;
    tk::TransportControl::ReallocationInhibitor inhibitor_;  // held for the render's lifetime
    OfflineMixAnalysis::CancelToken cancel_;
};

}  // namespace

OfflineMixAnalysis::CancelToken OfflineMixAnalysis::start(TracktionEngineWrapper& engine,
                                                          Request request, ProgressFn onProgress,
                                                          CompletionFn onComplete,
                                                          MeasuredFn onMeasured) {
    if (engine.getEdit() == nullptr) {
        MixAnalysisAgent::Result r;
        r.hasError = true;
        r.error = "No active edit to analyse.";
        if (onComplete)
            onComplete(std::move(r));
        return nullptr;
    }

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    // Self-owning: deletes itself on the message thread when the work completes.
    [[maybe_unused]] auto* job =
        new AnalysisJob(engine, std::move(request), std::move(onProgress), std::move(onComplete),
                        std::move(onMeasured), cancel);
    return cancel;
}

}  // namespace daw::audio
}  // namespace magda
