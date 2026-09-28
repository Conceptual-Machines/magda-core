#include "EngineOfflineRender.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "../../audio/RenderFileMetadata.hpp"
#include "../../audio/plugin_manager/ExternalPluginState.hpp"
#include "../../audio/plugins/engine/EngineExternalDevice.hpp"
#include "../../core/AutomationManager.hpp"
#include "../../core/TrackManager.hpp"
#include "../../project/ProjectManager.hpp"
#include "EngineProject.hpp"
#include "EngineRuntimeFactory.hpp"
#include "EngineTrace.hpp"
#include "OfflineRenderModel.hpp"
#include "clip/ClipAudioSource.hpp"
#include "clip/ClipMidiSource.hpp"
#include "clip/ClipSnapshotFeed.hpp"
#include "clip/ClipVoicePool.hpp"
#include "exec/OfflineRender.hpp"
#include "exec/ParallelPlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RenderThreadPool.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "io/AudioFileSink.hpp"
#include "io/PrefetchThread.hpp"
#include "plan/PlanCompiler.hpp"

namespace magda::daw::engine_host {

namespace adapter = magda::daw::audio::engine_adapter;

namespace {

constexpr int kChannels = 2;

/// The share of the progress bar the render takes when a normalise pass follows it.
constexpr float kRenderShareWhenNormalising = 0.9f;

using BorrowedDevices = std::map<engine::DeviceKey, std::shared_ptr<engine::EngineDevice>>;

void report(const juce::String& what, const std::vector<std::string>& messages) {
    for (const auto& message : messages)
        juce::Logger::writeToLog("[engine] offline " + what + ": " + juce::String(message));
}

/** @brief The hosted plugin inside @p device, past the trace, or null. */
adapter::EngineExternalDevice* externalIn(engine::EngineDevice& device) {
    if (auto* tracing = dynamic_cast<TracingDevice*>(&device))
        return externalIn(tracing->wrapped());

    return dynamic_cast<adapter::EngineExternalDevice*>(&device);
}

std::optional<engine::DitherMode> ditherModeFor(std::optional<OfflineRenderDither> dither) {
    if (!dither.has_value())
        return std::nullopt;

    switch (*dither) {
        case OfflineRenderDither::None:
            return engine::DitherMode::none;
        case OfflineRenderDither::Tpdf:
            return engine::DitherMode::tpdf;
        case OfflineRenderDither::Shaped:
            return engine::DitherMode::shaped;
    }
    return std::nullopt;
}

engine::AudioFileSpec fileSpecFor(const OfflineRenderRequest& request,
                                  engine::AudioFileMetadata metadata) {
    return {.format = request.format == OfflineRenderFormat::Flac ? engine::AudioFileFormat::flac
                                                                  : engine::AudioFileFormat::wav,
            .bitDepth = request.bitDepth,
            .dither = ditherModeFor(request.dither),
            .metadata = std::move(metadata)};
}

/** @brief A live session's instance, lent to one render's store without being owned by it. */
class BorrowedDevice final : public engine::EngineDevice {
  public:
    explicit BorrowedDevice(std::shared_ptr<engine::EngineDevice> device)
        : device_(std::move(device)) {}

    /// Reset as well: a hosted plugin prepared again at the same rate keeps its
    /// buffers, and a render must not start inside live playback's tail or the last pass's.
    void prepare(const engine::RenderContext& context) override {
        device_->prepare(context);
        device_->reset();
    }
    void reset() override {
        device_->reset();
    }
    void setOfflineRender(bool offline) override {
        device_->setOfflineRender(offline);
    }
    void setMidiInputBoundBytes(int bytes) override {
        device_->setMidiInputBoundBytes(bytes);
    }
    void setMidiOutputBoundBytes(int bytes) override {
        device_->setMidiOutputBoundBytes(bytes);
    }
    bool forwardsMidiInput() const override {
        return device_->forwardsMidiInput();
    }
    int latencySamples() const override {
        return device_->latencySamples();
    }
    double tailSeconds() const override {
        return device_->tailSeconds();
    }
    void process(engine::DeviceBlock& block) override {
        device_->process(block);
    }

  private:
    std::shared_ptr<engine::EngineDevice> device_;
};

/**
 * @brief The runtime objects behind one render's plan.
 *
 * A device the live session holds is lent; anything else is built for the
 * render. Sources read the render's own clip feed, never the live one.
 */
class OfflineRuntimeFactory final : public engine::RuntimeStateFactory {
  public:
    OfflineRuntimeFactory(const OfflineRenderHost& host, BorrowedDevices& borrowed)
        : host_(host), borrowed_(borrowed) {}

    void setModel(const OfflineRenderModel& model) {
        devices_.clear();
        for (const auto& [key, device] : adapter::devicesIn(model.tracks, model.master))
            devices_.emplace(key, *device);
    }

    void attach(engine::ClipSnapshotFeed& clips, engine::ClipStreamFeed& streams) {
        clips_ = &clips;
        streams_ = &streams;
    }

    bool lendsFromLive(engine::DeviceKey key) const {
        return host_.liveDevice(key) != nullptr;
    }

    /// A plugin loaded for this render because the live session had none.
    void holdLoaded(engine::DeviceKey key, std::unique_ptr<engine::EngineDevice> device) {
        loaded_[key] = std::move(device);
    }

    std::unique_ptr<engine::EngineDevice> createDevice(engine::DeviceKey key) override {
        if (auto live = host_.liveDevice(key))
            return lend(key, std::move(live));

        if (auto loaded = loaded_.extract(key); !loaded.empty())
            return std::move(loaded.mapped());

        const auto model = devices_.find(key);
        if (model == devices_.end() || adapter::isExternalDevice(model->second))
            return nullptr;

        return adapter::createEngineDevice(model->second, /*offlineRender=*/true);
    }

    std::unique_ptr<engine::EngineAudioSource> createClipAudioSource(TrackId trackId) override {
        return std::make_unique<engine::ClipAudioSource>(trackId, *clips_, *streams_);
    }

    std::unique_ptr<engine::EngineMidiSource> createClipMidiSource(TrackId trackId) override {
        return std::make_unique<engine::ClipMidiSource>(trackId, *clips_);
    }

    /// The range a hardware insert's recording has to cover, and what it plays at.
    void setInsertWindow(const engine::CaptureWindow& window,
                         const engine::RenderContext& context) {
        insertWindow_ = window;
        context_ = context;
    }

    std::unique_ptr<engine::EngineInsert> createInsert(engine::DeviceKey key) override {
        return host_.insertPlayback(key, insertWindow_, context_);
    }

  private:
    /// A hosted plugin's parameter edits wait for the render's blocks while it has one.
    std::unique_ptr<engine::EngineDevice> lend(engine::DeviceKey key,
                                               std::shared_ptr<engine::EngineDevice> live) {
        live->setOfflineRender(true);
        if (auto* external = externalIn(*live))
            external->setRendered(true);

        borrowed_[key] = live;
        return std::make_unique<BorrowedDevice>(std::move(live));
    }

    const OfflineRenderHost& host_;
    BorrowedDevices& borrowed_;
    std::map<engine::DeviceKey, DeviceInfo> devices_;
    std::map<engine::DeviceKey, std::unique_ptr<engine::EngineDevice>> loaded_;
    engine::CaptureWindow insertWindow_;
    engine::RenderContext context_;
    engine::ClipSnapshotFeed* clips_ = nullptr;
    engine::ClipStreamFeed* streams_ = nullptr;
};

/** @brief Hands each block on and counts it, which is what progress is measured in. */
class CountingSink final : public engine::OfflineRenderSink {
  public:
    explicit CountingSink(engine::OfflineRenderSink& target) : target_(target) {}

    void write(const juce::AudioBuffer<float>& block, int numSamples) override {
        target_.write(block, numSamples);
        samples_ += numSamples;
    }

    std::int64_t samples() const {
        return samples_;
    }

  private:
    engine::OfflineRenderSink& target_;
    std::int64_t samples_ = 0;
};

/** @brief Hands each block on and keeps the loudest sample, for normalising. */
class PeakSink final : public engine::OfflineRenderSink {
  public:
    explicit PeakSink(engine::OfflineRenderSink& target) : target_(target) {}

    void write(const juce::AudioBuffer<float>& block, int numSamples) override {
        for (auto channel = 0; channel < block.getNumChannels(); ++channel)
            peak_ = std::max(peak_, block.getMagnitude(channel, 0, numSamples));

        target_.write(block, numSamples);
    }

    float peak() const {
        return peak_;
    }

  private:
    engine::OfflineRenderSink& target_;
    float peak_ = 0.0f;
};

std::int64_t samplesFor(double seconds, double sampleRate) {
    return seconds > 0.0 ? static_cast<std::int64_t>(std::llround(seconds * sampleRate)) : 0;
}

void writeSilence(engine::AudioFileSink& sink, std::int64_t samples, int blockSize) {
    juce::AudioBuffer<float> silence(kChannels, blockSize);
    silence.clear();

    for (; samples > 0; samples -= blockSize)
        sink.write(silence, static_cast<int>(std::min<std::int64_t>(samples, blockSize)));
}

/**
 * @brief Write @p source into @p target at @p gain, a block at a time.
 *
 * The render went to float first, so the gain lands ahead of the target's
 * dither rather than on a grid that was already rounded.
 */
bool copyScaled(const juce::File& source, engine::AudioFileSink& target, float gain,
                int blockSize) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (reader == nullptr)
        return false;

    juce::AudioBuffer<float> block(kChannels, blockSize);
    for (juce::int64 position = 0; position < reader->lengthInSamples; position += blockSize) {
        const auto numSamples =
            static_cast<int>(std::min<juce::int64>(blockSize, reader->lengthInSamples - position));
        reader->read(&block, 0, numSamples, position, true, true);
        block.applyGain(0, numSamples, gain);
        target.write(block, numSamples);
    }

    return !target.failed();
}

/**
 * @brief Everything one render owns: the model it read, the plan, and what binds it.
 *
 * Declared so destruction unwinds inwards: the executor lets go of the store's
 * objects, the store's sources let go of the feeds, and the pool goes before
 * the reader it fills from.
 */
struct OfflineRuntime {
    OfflineRuntime(const OfflineRenderRequest& renderRequest, const OfflineRenderHost& host,
                   BorrowedDevices& borrowed)
        : request(renderRequest),
          context{.sampleRate = request.sampleRate,
                  .maxBlockSize = request.blockSize,
                  .numChannels = kChannels},
          tempo(host.renderTempo()),
          metadata(renderFileMetadata(ProjectManager::getInstance().getCurrentProjectInfo(), 0.0,
                                      "MAGDA offline render")),
          services(host.pluginServices()),
          factory(host, borrowed) {
        services.context = context;
    }

    /// Why the render cannot run, or empty. Message thread.
    juce::String prepare(OfflineRenderModel model) {
        if (auto failure = loadPluginsTheLiveSessionLacks(model); failure.isNotEmpty())
            return failure;

        compileClips(model);
        compilePlan(model);
        if (auto failure = bind(model); failure.isNotEmpty())
            return failure;

        tailSeconds = request.tailSeconds.value_or(declaredTail());
        return {};
    }

    OfflineRenderRequest request;
    engine::RenderContext context;
    double tailSeconds = 0.0;
    engine::TempoMap tempo;
    engine::AudioFileMetadata metadata;
    adapter::ExternalPluginServices services;

    EngineFileReaders files;
    engine::PrefetchThread reader{false};
    std::unique_ptr<engine::ClipVoicePool> voices;
    engine::ClipSnapshotFeed clips;

    OfflineRuntimeFactory factory;
    engine::RuntimeStateStore store{factory};

    std::shared_ptr<const engine::RenderPlan> plan;
    engine::PlanValues values;

    // Not realtime: a bounce runs beside playback and must not take its cores from it.
    engine::RenderThreadPool renderPool{engine::RenderThreadPool::workersForThisMachine(),
                                        /*realtime=*/false};
    engine::ParallelPlanExecutor executor{&renderPool};

  private:
    static engine::RenderPlan compile(const OfflineRenderModel& model) {
        return engine::compileRenderPlan(model.tracks, model.master, {.deviceMeters = false});
    }

    /**
     * @brief Open every hosted plugin the plan uses and the live session does not hold.
     *
     * Blocking, and before the final compile: an instance is allowed to correct
     * the bus widths and role the plan is compiled from.
     */
    juce::String loadPluginsTheLiveSessionLacks(OfflineRenderModel& model) {
        std::set<engine::DeviceKey> planned;
        for (const auto& op : compile(model).ops)
            if (op.kind == engine::OpKind::Device)
                planned.insert(op.key.deviceKey());

        for (auto& [key, device] : adapter::devicesIn(model.tracks, model.master)) {
            if (!planned.contains(key) || !adapter::isExternalDevice(*device) ||
                factory.lendsFromLive(key))
                continue;

            auto made = adapter::createEngineExternalDevice(*device, services, true);
            if (made.device == nullptr)
                return "Could not load " + device->name + ": " + made.failure;

            if (made.resolvedDevice.has_value())
                *device = *made.resolvedDevice;
            applyRestoredParameters(*device, made.restoredParameters);
            factory.holdLoaded(key, std::move(made.device));
        }

        return {};
    }

    void compileClips(const OfflineRenderModel& model) {
        auto snapshot = std::make_shared<const engine::ClipSnapshot>(
            engine::compileClipSnapshot(model.lanes, clipSources(), tempo));
        report("clips", snapshot->diagnostics);

        voices = std::make_unique<engine::ClipVoicePool>(files, reader, context);
        voices->setSnapshot(snapshot);
        clips.publish(std::move(snapshot));
    }

    void compilePlan(const OfflineRenderModel& model) {
        plan = std::make_shared<const engine::RenderPlan>(compile(model));
        report("plan", plan->diagnostics);
    }

    /// The longest finite tail a device in the plan declares.
    double declaredTail() const {
        auto longest = 0.0;
        for (const auto& op : plan->ops)
            if (op.kind == engine::OpKind::Device)
                if (const auto device = store.device(op.key.deviceKey()))
                    if (const auto tail = device->tailSeconds(); std::isfinite(tail))
                        longest = std::max(longest, tail);

        return longest;
    }

    juce::String bind(const OfflineRenderModel& model) {
        factory.setModel(model);
        factory.attach(clips, voices->feed());
        factory.setInsertWindow({.startSeconds = tempo.beatToTime(request.range.start.value),
                                 .endSeconds = tempo.beatToTime(request.range.end.value)},
                                context);

        report("values", engine::resolvePlanValues(*plan, model.tracks, model.master, values,
                                                   model.automation,
                                                   AutomationManager::getInstance().getClips()));

        // Nothing here binds hardware, so an input op with no source is this
        // render's correct answer rather than something to report (#2628).
        auto bindings = store.realise(*plan, context);
        bindings.liveSession = false;

        // A return with nothing to play would render the hardware as silence, which the
        // file cannot tell from a quiet insert: refused instead (#2279).
        for (const auto& op : plan->ops)
            if (op.kind == engine::OpKind::InsertReturn &&
                !bindings.inserts.contains(op.key.deviceKey()))
                return "A hardware insert has no recording of its return to render from";

        const auto messages = executor.prepare(*plan, bindings, context, nullptr, &values);
        report("prepare", messages);

        if (executor.isPrepared())
            return {};

        return messages.empty() ? juce::String("The render plan could not be prepared")
                                : juce::String(messages.front());
    }
};

/** @brief One render of one request, run on whichever thread the caller picks. */
class EngineOfflineRenderTask final : public OfflineRenderTask {
  public:
    EngineOfflineRenderTask(std::unique_ptr<OfflineRuntime> runtime, juce::String failure)
        : runtime_(std::move(runtime)), failure_(std::move(failure)) {}

    OfflineRenderResult run(const std::function<bool()>& shouldCancel,
                            const std::function<void(float)>& onProgress) override {
        if (failure_.isNotEmpty())
            return {false, failure_};

        cancel_ = shouldCancel;
        progress_ = onProgress;

        const auto result = runtime_->request.shouldNormalise ? renderNormalised() : renderDirect();
        if (result.success && progress_)
            progress_(1.0f);

        return result;
    }

  private:
    OfflineRenderResult renderDirect() {
        auto sink = openDestination();
        if (sink == nullptr)
            return {false, "Could not open " + runtime_->request.destination.getFullPathName()};

        writeLeadIn(*sink);

        auto rendered = render(*sink, 1.0f);
        if (!rendered.success)
            return rendered;

        return finish(*sink);
    }

    OfflineRenderResult renderNormalised() {
        const auto& request = runtime_->request;
        const juce::TemporaryFile pass(request.destination.withFileExtension("wav"));

        auto floatSink = engine::AudioFileSink::create(pass.getFile(),
                                                       {.format = engine::AudioFileFormat::wav,
                                                        .bitDepth = 32,
                                                        .dither = engine::DitherMode::none},
                                                       runtime_->context);
        if (floatSink == nullptr)
            return {false, "Could not open a file to normalise into"};

        PeakSink peak(*floatSink);
        auto rendered = render(peak, kRenderShareWhenNormalising);
        if (!rendered.success)
            return rendered;
        if (!floatSink->close())
            return {false, "Could not write the render to normalise"};

        auto sink = openDestination();
        if (sink == nullptr)
            return {false, "Could not open " + request.destination.getFullPathName()};

        writeLeadIn(*sink);

        const auto gain =
            peak.peak() > 0.0f
                ? juce::Decibels::decibelsToGain(request.normaliseToLevelDb) / peak.peak()
                : 1.0f;
        if (!copyScaled(pass.getFile(), *sink, gain, runtime_->context.maxBlockSize))
            return {false, "Could not write the normalised render"};

        return finish(*sink);
    }

    std::unique_ptr<engine::AudioFileSink> openDestination() const {
        const auto& request = runtime_->request;
        auto metadata = runtime_->metadata;
        metadata.tempo = runtime_->tempo.bpmAt(request.range.start.value);
        metadata.beats = request.range.end.value - request.range.start.value;
        const auto signature = runtime_->tempo.barsAndBeatsAt(request.range.start.value);
        metadata.numerator = signature.numerator;
        metadata.denominator = signature.denominator;
        metadata.oneShot = request.oneShot;
        if (request.leadInSeconds > 0.0) {
            metadata.tempo.reset();
            metadata.beats.reset();
        }
        return engine::AudioFileSink::create(runtime_->request.destination,
                                             fileSpecFor(request, std::move(metadata)),
                                             runtime_->context);
    }

    void writeLeadIn(engine::AudioFileSink& sink) const {
        writeSilence(sink,
                     samplesFor(runtime_->request.leadInSeconds, runtime_->context.sampleRate),
                     runtime_->context.maxBlockSize);
    }

    OfflineRenderResult render(engine::OfflineRenderSink& target, float progressShare) {
        auto& runtime = *runtime_;
        const auto& request = runtime.request;

        CountingSink counted(target);
        const auto expected = std::max<std::int64_t>(
            1, samplesFor(runtime.tempo.beatToTime(request.range.end.value) -
                              runtime.tempo.beatToTime(request.range.start.value) +
                              runtime.tailSeconds,
                          runtime.context.sampleRate));
        const auto started = juce::Time::getMillisecondCounterHiRes();

        const auto keepGoing = [&] {
            if (cancel_ && cancel_())
                return false;

            if (progress_)
                progress_(progressShare * static_cast<float>(counted.samples()) /
                          static_cast<float>(expected));

            if (request.realTimeRender)
                waitForRealTime(started, counted.samples());

            return true;
        };

        const auto result =
            engine::renderOffline(runtime.executor, runtime.values, runtime.context, runtime.tempo,
                                  {.startBeat = request.range.start.value,
                                   .endBeat = request.range.end.value,
                                   .tailSeconds = runtime.tailSeconds,
                                   .blockSize = request.blockSize},
                                  counted, runtime.voices.get(), &runtime.clips, {}, keepGoing);

        if (result.refused)
            return {false, "The engine refused the render plan"};
        if (result.cancelled)
            return {false, "Cancelled"};

        return {true, {}};
    }

    /// Hold the render back to the wall clock, for plugins that misbehave faster than real time.
    void waitForRealTime(double startedMs, std::int64_t renderedSamples) const {
        const auto dueMs = startedMs + 1000.0 * static_cast<double>(renderedSamples) /
                                           runtime_->context.sampleRate;
        const auto aheadMs = dueMs - juce::Time::getMillisecondCounterHiRes();
        if (aheadMs >= 1.0)
            juce::Thread::sleep(static_cast<int>(aheadMs));
    }

    static OfflineRenderResult finish(engine::AudioFileSink& sink) {
        if (!sink.close())
            return {false, "Could not write the rendered file"};

        return {true, {}};
    }

    std::unique_ptr<OfflineRuntime> runtime_;
    juce::String failure_;
    std::function<bool()> cancel_;
    std::function<void(float)> progress_;
};

/**
 * @brief The live render suspended, for as long as this is open.
 *
 * Every live instance a task borrowed is handed back prepared for the live
 * context before the host republishes.
 */
class EngineOfflineRenderSession final : public OfflineRenderSession {
  public:
    EngineOfflineRenderSession(OfflineRenderHost& host, bool resumePlaybackWhenFinished)
        : host_(host), resume_(resumePlaybackWhenFinished) {
        host_.beginOfflineRender();
    }

    ~EngineOfflineRenderSession() override {
        returnBorrowedDevices();
        host_.endOfflineRender(resume_);
    }

    std::unique_ptr<OfflineRenderTask> createTask(const OfflineRenderRequest& request) override {
        if (request.destination == juce::File() || !request.range.isValid() ||
            request.sampleRate <= 0.0 || request.blockSize <= 0)
            return nullptr;

        auto model = readModel(request);
        if (!model.has_value())
            return nullptr;

        auto runtime = std::make_unique<OfflineRuntime>(request, host_, borrowed_);
        auto failure = runtime->prepare(std::move(*model));
        return std::make_unique<EngineOfflineRenderTask>(std::move(runtime), std::move(failure));
    }

  private:
    static std::optional<OfflineRenderModel> readModel(const OfflineRenderRequest& request) {
        const auto& tracks = TrackManager::getInstance();
        const auto* master = tracks.getTrack(MASTER_TRACK_ID);
        if (master == nullptr)
            return std::nullopt;

        return narrowForRender({.tracks = tracks.getTracks(),
                                .master = *master,
                                .lanes = clipLanesFor(tracks.getTracks()),
                                .automation = AutomationManager::getInstance().getLanes()},
                               request);
    }

    void returnBorrowedDevices() {
        const auto live = host_.liveContext();

        for (const auto& [key, device] : borrowed_) {
            device->setOfflineRender(false);
            if (live.has_value())
                device->prepare(*live);
            device->reset();
        }
    }

    OfflineRenderHost& host_;
    bool resume_ = false;
    BorrowedDevices borrowed_;
};

}  // namespace

std::unique_ptr<OfflineRenderSession> createEngineOfflineRenderSession(
    OfflineRenderHost& host, bool resumePlaybackWhenFinished) {
    return std::make_unique<EngineOfflineRenderSession>(host, resumePlaybackWhenFinished);
}

}  // namespace magda::daw::engine_host
