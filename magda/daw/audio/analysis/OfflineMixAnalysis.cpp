#include "OfflineMixAnalysis.hpp"

#include <atomic>
#include <memory>

// clang-format off
#include <tracktion_engine/tracktion_engine.h>
// clang-format on

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

// Run one offline render pass to outFile, blocking until done. tracksToDo empty
// => all tracks (the RenderTask path treats an empty bitset as "no track filter").
// Returns false on failure or cancellation.
bool renderPass(const tk::Renderer::Parameters& base, const juce::File& outFile,
                const juce::BigInteger& tracksToDo, bool useMasterPlugins,
                std::atomic<bool>& cancel) {
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
                OfflineMixAnalysis::CompletionFn onComplete)
        : juce::Thread("OfflineMixAnalysis"),
          engine_(engine),
          edit_(*engine.getEdit()),
          request_(std::move(request)),
          onProgress_(std::move(onProgress)),
          onComplete_(std::move(onComplete)),
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

        startThread();
    }

    ~AnalysisJob() override {
        cancel_.store(true);
        stopThread(8000);
    }

  private:
    void run() override {
        auto result = doWork();

        // Restore + deliver + self-destruct on the message thread.
        juce::MessageManager::callAsync([this, result = std::move(result)]() mutable {
            if (auto* bridge = engine_.getAudioBridge())
                bridge->getPluginManager().restoreAfterRendering();
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

        double sampleRate = engine_.getEngine()->getDeviceManager().getSampleRate();
        if (sampleRate <= 0.0)
            sampleRate = 44100.0;

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

        // --- master / sum pass (both depths need it) ---
        postProgress("Rendering mix...");
        auto masterFile = tempDir.getNonexistentChildFile("magda_mix_master", ".wav");
        if (!renderPass(base, masterFile, {} /* all tracks */, /*useMasterPlugins*/ true,
                        cancel_)) {
            masterFile.deleteFile();
            err.hasError = true;
            err.error = cancel_.load() ? "Cancelled." : "Mix render failed.";
            return err;
        }

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
            // Resolve the track set to TE tracks.
            std::vector<tk::AudioTrack*> teTracks;
            if (request_.trackSet.empty()) {
                for (auto* track : tk::getAudioTracks(edit_))
                    teTracks.push_back(track);
            } else if (auto* bridge = engine_.getAudioBridge()) {
                for (auto id : request_.trackSet)
                    if (auto* track = bridge->getAudioTrack(id))
                        teTracks.push_back(track);
            }

            const auto allTracks = tk::getAllTracks(edit_);

            // Buffers must outlive the build() call; unique_ptr keeps Source
            // pointers stable as the vector grows.
            std::vector<std::unique_ptr<juce::AudioBuffer<float>>> stemBufs;
            std::vector<MixAnalysisInput::Source> sources;
            stemBufs.reserve(teTracks.size());
            sources.reserve(teTracks.size());

            const int total = static_cast<int>(teTracks.size());
            int skipped = 0;
            for (int i = 0; i < total; ++i) {
                if (cancel_.load()) {
                    err.hasError = true;
                    err.error = "Cancelled.";
                    return err;
                }
                auto* track = teTracks[static_cast<size_t>(i)];
                postProgress("Rendering track " + juce::String(i + 1) + "/" + juce::String(total) +
                             "...");

                const int bit = allTracks.indexOf(track);
                if (bit < 0) {
                    ++skipped;
                    continue;
                }
                juce::BigInteger bits;
                bits.setBit(bit);

                auto stemFile = tempDir.getNonexistentChildFile("magda_mix_stem", ".wav");
                // Stems are pre-master (useMasterPlugins=false) so each is comparable.
                if (!renderPass(base, stemFile, bits, /*useMasterPlugins*/ false, cancel_)) {
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

            if (skipped > 0)
                postProgress(juce::String(skipped) + " track(s) skipped (render/read failed).");
        }

        // Song-level context.
        if (request_.bpm > 0.0f)
            input.bpm = request_.bpm;
        input.genre = request_.genre;
        input.question = request_.question;

        postProgress("Asking the mix analyst...");
        MixAnalysisAgent agent;
        return agent.generate(input);
    }

    TracktionEngineWrapper& engine_;
    tk::Edit& edit_;
    OfflineMixAnalysis::Request request_;
    OfflineMixAnalysis::ProgressFn onProgress_;
    OfflineMixAnalysis::CompletionFn onComplete_;
    tk::TransportControl::ReallocationInhibitor inhibitor_;  // held for the render's lifetime
    std::atomic<bool> cancel_{false};
};

}  // namespace

void OfflineMixAnalysis::start(TracktionEngineWrapper& engine, Request request,
                               ProgressFn onProgress, CompletionFn onComplete) {
    if (engine.getEdit() == nullptr) {
        MixAnalysisAgent::Result r;
        r.hasError = true;
        r.error = "No active edit to analyse.";
        if (onComplete)
            onComplete(std::move(r));
        return;
    }

    // Self-owning: deletes itself on the message thread when the work completes.
    [[maybe_unused]] auto* job =
        new AnalysisJob(engine, std::move(request), std::move(onProgress), std::move(onComplete));
}

}  // namespace daw::audio
}  // namespace magda
