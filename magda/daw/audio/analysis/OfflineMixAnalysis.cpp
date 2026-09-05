#include "OfflineMixAnalysis.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <chrono>
#include <memory>

#include "../../core/TrackManager.hpp"
#include "../../core/UserAlert.hpp"
#include "../../engine/AudioEngine.hpp"
#include "MixAnalysisInput.hpp"

namespace magda {
namespace daw::audio {

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

bool runOnMessageThreadBlocking(std::function<void()> operation) {
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    if (messageManager == nullptr || messageManager->isThisTheMessageThread()) {
        operation();
        return true;
    }

    enum class State { Queued, Running, Cancelled, Finished, Failed };
    struct SharedOperation {
        explicit SharedOperation(std::function<void()> op) : operation(std::move(op)) {}
        std::function<void()> operation;
        std::atomic<State> state{State::Queued};
        juce::WaitableEvent completed;
    };

    auto shared = std::make_shared<SharedOperation>(std::move(operation));
    if (!juce::MessageManager::callAsync([shared]() {
            auto expected = State::Queued;
            if (!shared->state.compare_exchange_strong(expected, State::Running)) {
                shared->completed.signal();
                return;
            }
            // A throw here runs on the message thread and must not escape:
            // uncaught, it would propagate into JUCE's dispatch loop and
            // take the whole app down instead of just failing this render.
            try {
                shared->operation();
                shared->state.store(State::Finished);
            } catch (const std::exception& e) {
                juce::Logger::writeToLog(juce::String("[runOnMessageThreadBlocking] ") + e.what());
                shared->state.store(State::Failed);
            } catch (...) {
                juce::Logger::writeToLog("[runOnMessageThreadBlocking] unknown exception");
                shared->state.store(State::Failed);
            }
            shared->completed.signal();
        }))
        return false;

    constexpr int kMessageThreadTimeoutMs = 5000;
    if (shared->completed.wait(kMessageThreadTimeoutMs))
        return shared->state.load() == State::Finished;

    auto expected = State::Queued;
    if (shared->state.compare_exchange_strong(expected, State::Cancelled))
        return false;

    // The callback started before the queue timeout. Its operations are limited
    // to render-session/task construction or destruction, so let that bounded
    // critical section finish to keep its captured objects alive. The shutdown
    // deadlock case is the Queued state handled above.
    shared->completed.wait();
    return shared->state.load() == State::Finished;
}

class MessageThreadRenderSession {
  public:
    explicit MessageThreadRenderSession(AudioEngine& engine) : engine_(engine) {}

    ~MessageThreadRenderSession() {
        try {
            close();
        } catch (const std::exception& e) {
            juce::Logger::writeToLog(juce::String("[MessageThreadRenderSession] ") + e.what());
            const juce::String message = juce::String("Mix analysis cleanup failed: ") + e.what();
            juce::MessageManager::callAsync([message] { magda::notifyUserAlert(message); });
        } catch (...) {
            juce::Logger::writeToLog("[MessageThreadRenderSession] unknown exception");
            juce::MessageManager::callAsync(
                [] { magda::notifyUserAlert("Mix analysis cleanup failed"); });
        }
    }

    bool open() {
        return runOnMessageThreadBlocking(
                   [this]() { session_ = engine_.createOfflineRenderSession(false); }) &&
               session_ != nullptr;
    }

    std::unique_ptr<OfflineRenderTask> createTask(const OfflineRenderRequest& request) {
        std::unique_ptr<OfflineRenderTask> task;
        if (!runOnMessageThreadBlocking(
                [this, &request, &task]() { task = session_->createTask(request); }))
            return nullptr;
        return task;
    }

    void close() {
        if (!session_)
            return;
        if (!runOnMessageThreadBlocking([this]() { session_.reset(); }))
            session_.reset();
    }

  private:
    AudioEngine& engine_;
    std::unique_ptr<OfflineRenderSession> session_;
};

// Run one offline render pass to outFile, blocking until done.
bool renderPass(MessageThreadRenderSession& session, OfflineRenderRequest request,
                const juce::File& outFile, std::optional<TrackId> trackId, bool useMasterPlugins,
                std::atomic<bool>& cancel, const std::function<void(float)>& onProgress) {
    request.destination = outFile;
    request.useMasterPlugins = useMasterPlugins;
    if (trackId)
        request.trackIds = {*trackId};

    auto task = session.createTask(request);
    if (!task)
        return false;

    return task->run([&cancel]() { return cancel.load(); }, onProgress).success;
}

// Background driver: owns itself, deletes on completion. Setup runs in the ctor
// (message thread); the render passes + measurement run in run() (background).
class AnalysisJob : public juce::Thread {
  public:
    AnalysisJob(AudioEngine& engine, OfflineMixAnalysis::Request request,
                OfflineMixAnalysis::ProgressFn onProgress,
                OfflineMixAnalysis::CompletionFn onComplete,
                OfflineMixAnalysis::MeasuredFn onMeasured, OfflineMixAnalysis::CancelToken cancel)
        : juce::Thread("OfflineMixAnalysis"),
          engine_(engine),
          request_(std::move(request)),
          onProgress_(std::move(onProgress)),
          onComplete_(std::move(onComplete)),
          onMeasured_(std::move(onMeasured)),
          cancel_(std::move(cancel)) {
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

    OfflineMixAnalysis::Result doWork() {
        OfflineMixAnalysis::Result err;

        // Render at a reduced sample rate to speed up the N-pass deep render.
        // Mix measurements (loudness, dynamics, stereo, tonal balance) don't need
        // full bandwidth; 22.05 kHz keeps musically relevant HF (Nyquist ~11 kHz)
        // while roughly halving render + measurement cost per pass. This is the
        // cheap lever -- the structural win is a single-pass tap render.
        constexpr double kAnalysisSampleRate = 22050.0;
        const double sampleRate = kAnalysisSampleRate;

        const auto* tempoMap = engine_.tempoMap();
        if (tempoMap == nullptr) {
            err.hasError = true;
            err.error = "The audio engine does not provide a tempo map.";
            return err;
        }

        // Resolve the musical range first; convert only at the render boundary.
        BeatRange musicalRange{{0.0}, {engine_.getEditLengthBeats().value}};
        if (request_.range == OfflineMixAnalysis::RangeMode::LoopRange) {
            const auto loop = engine_.getLoopRegionBeats();
            if (loop.isValid())
                musicalRange = loop;
        }

        OfflineRenderRequest base;
        base.bitDepth = 24;
        base.sampleRate = sampleRate;
        base.usePlugins = true;
        base.range = {{tempoMap->beatToTime(musicalRange.start.value)},
                      {tempoMap->beatToTime(musicalRange.end.value)},
                      {2.0}};

        MessageThreadRenderSession renderSession(engine_);
        if (!renderSession.open()) {
            err.hasError = true;
            err.error = "Could not prepare the edit for offline analysis.";
            return err;
        }

        const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        const bool deep = request_.depth == OfflineMixAnalysis::Depth::Deep;

        // Resolve the track set up front (Deep) so the total pass count -- and
        // therefore the progress estimate -- is known before rendering starts:
        // one master pass plus one pass per track.
        std::vector<TrackId> trackIds;
        if (deep) {
            if (request_.trackSet.empty()) {
                for (const auto& track : TrackManager::getInstance().getTracks())
                    if (track.type != TrackType::Chord)
                        trackIds.push_back(track.id);
            } else {
                for (auto id : request_.trackSet)
                    if (TrackManager::getInstance().getTrack(id) != nullptr)
                        trackIds.push_back(id);
            }
        }
        const int totalPasses = deep ? 1 + static_cast<int>(trackIds.size()) : 1;

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
        if (!renderPass(renderSession, base, masterFile, std::nullopt, true, *cancel_,
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

        MixAnalysisData measurements;

        if (request_.depth == OfflineMixAnalysis::Depth::Shallow) {
            postProgress("Measuring mix...");
            auto mix = MixAnalysisInput::fingerprint(masterBuf, masterSr, "Mix", "master");
            measurements.master = mix;
            measurements.tracks.push_back(std::move(mix));
        } else {
            // Buffers must outlive the build() call; unique_ptr keeps Source
            // pointers stable as the vector grows.
            std::vector<std::unique_ptr<juce::AudioBuffer<float>>> stemBufs;
            std::vector<MixAnalysisInput::Source> sources;
            std::vector<magda::TrackId> sourceTrackIds;  // aligned with sources for post-build tags
            stemBufs.reserve(trackIds.size());
            sources.reserve(trackIds.size());
            sourceTrackIds.reserve(trackIds.size());

            const int total = static_cast<int>(trackIds.size());
            int skipped = 0;
            for (int i = 0; i < total; ++i) {
                if (cancel_->load()) {
                    err.hasError = true;
                    err.error = "Cancelled.";
                    return err;
                }
                const auto trackId = trackIds[static_cast<size_t>(i)];
                const auto* track = TrackManager::getInstance().getTrack(trackId);
                if (track == nullptr) {
                    ++skipped;
                    continue;
                }
                passesDone = 1 + i;  // master pass + tracks finished so far

                // Name the current pass so the user sees track-by-track progress.
                // Reset lastPct so the new label posts immediately (not throttled).
                phaseLabel = "Rendering: " + track->name + "  (" + juce::String(i + 1) + "/" +
                             juce::String(total) + ")";
                lastPct = -1.0;

                auto stemFile = tempDir.getNonexistentChildFile("magda_mix_stem", ".wav");
                // Stems are pre-master (useMasterPlugins=false) so each is comparable.
                if (!renderPass(renderSession, base, stemFile, trackId, false, *cancel_,
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
                src.name = track->name;
                src.audio = buf.get();
                stemBufs.push_back(std::move(buf));
                sources.push_back(std::move(src));
                sourceTrackIds.push_back(trackId);
            }

            if (sources.empty()) {
                err.hasError = true;
                err.error = "No tracks could be rendered for analysis.";
                return err;
            }

            postProgress("Analysing tracks...");
            MixAnalysisInput::Options opts;
            opts.numSegments = request_.numSegments;
            measurements = MixAnalysisInput::build(masterSr, sources, &masterBuf, {}, opts);

            // Annotate each track with its type (audio/MIDI) + effect chain. build()
            // preserves source order, so measurements.tracks[i] matches sourceTrackIds[i].
            auto& tmgr = magda::TrackManager::getInstance();
            for (size_t i = 0; i < measurements.tracks.size() && i < sourceTrackIds.size(); ++i) {
                const auto tid = sourceTrackIds[i];
                if (tid == magda::INVALID_TRACK_ID)
                    continue;
                measurements.tracks[i].role = tmgr.getPrimaryInstrument(tid) ? "MIDI" : "audio";
                measurements.tracks[i].chain = tmgr.getChainSummary(tid);
            }

            if (skipped > 0)
                postProgress(juce::String(skipped) + " track(s) skipped (render/read failed).");
        }

        // Song-level context.
        if (request_.bpm > 0.0f)
            measurements.bpm = request_.bpm;
        measurements.genre = request_.genre;

        // Agent interpretation is an upper-layer concern. This DAW service only
        // renders and measures, then delivers the neutral data model.
        if (onMeasured_) {
            auto cb = onMeasured_;
            juce::MessageManager::callAsync([cb, measurements = std::move(measurements)]() mutable {
                cb(std::move(measurements));
            });
        }
        return {};
    }

    AudioEngine& engine_;
    OfflineMixAnalysis::Request request_;
    OfflineMixAnalysis::ProgressFn onProgress_;
    OfflineMixAnalysis::CompletionFn onComplete_;
    OfflineMixAnalysis::MeasuredFn onMeasured_;
    OfflineMixAnalysis::CancelToken cancel_;
};

}  // namespace

OfflineMixAnalysis::CancelToken OfflineMixAnalysis::start(AudioEngine& engine, Request request,
                                                          ProgressFn onProgress,
                                                          CompletionFn onComplete,
                                                          MeasuredFn onMeasured) {
    if (!engine.hasActiveEdit()) {
        Result r;
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
