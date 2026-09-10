#include "EngineHost.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

#include "../../audio/plugin_manager/ExternalPluginState.hpp"
#include "../../audio/plugins/engine/EngineDeviceFactory.hpp"
#include "../../core/AutomationManager.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/TempoMap.hpp"
#include "../../core/TrackManager.hpp"
#include "../../project/ProjectManager.hpp"
#include "EngineProject.hpp"
#include "EngineRuntimeFactory.hpp"
#include "EngineTrace.hpp"
#include "ExternalPluginLoader.hpp"
#include "LiveMidiQueue.hpp"
#include "LiveMidiSources.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "clip/ClipVoicePool.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "io/LiveInput.hpp"
#include "io/PrefetchThread.hpp"
#include "plan/PlanCompiler.hpp"

namespace magda::daw::engine_host {

namespace adapter = magda::daw::audio::engine_adapter;

namespace {

/// Stereo, like everything else in the engine: the model has no mono tracks,
/// and a narrow device is a declared width rather than a smaller buffer.
constexpr int kChannels = 2;

/// 30 fps, which is what the fork's own metering timer runs at.
constexpr int kMeterIntervalMs = 33;

/// Live MIDI sources the callback has room for. One per enabled input plus one
/// per track ever auditioned; a project past this loses the sources beyond it
/// rather than allocating for them on the audio thread.
constexpr int kMaxLiveMidiSources = 64;

/// What one queued event costs a MidiBuffer: a sample position and a length in
/// front of its three bytes.
constexpr int kQueuedEventBytes = 9;

/// Anything a publish could not honour, named by the half that reported it.
void report(const juce::String& what, const std::vector<std::string>& messages) {
    for (const auto& message : messages)
        juce::Logger::writeToLog("[engine] " + what + ": " + juce::String(message));
}

/**
 * @brief The engine's tempo map as the app's conversion facade (#2579).
 *
 * Holds the map by reference rather than by value: magda::TempoMap is
 * non-movable, so this is a member of the object that owns the map, and every
 * refresh of it is seen here.
 */
class TempoMapView final : public magda::TempoMap {
  public:
    explicit TempoMapView(const engine::TempoMap& map) : map_(map) {}

    double beatToTime(double beat) const override {
        return map_.beatToTime(beat);
    }
    double timeToBeat(double seconds) const override {
        return map_.timeToBeat(seconds);
    }
    double bpmAt(double beat) const override {
        return map_.bpmAt(beat);
    }

  private:
    const engine::TempoMap& map_;
};

}  // namespace

/**
 * @brief The session, the callback that drives it, and the model it follows.
 *
 * One class rather than three because the three cannot be separated: what the
 * callback renders is what the model last published, and both are bounded by
 * the device's own start and stop.
 */
struct EngineHost::Impl final : private juce::AudioIODeviceCallback,
                                private juce::AsyncUpdater,
                                private juce::Timer,
                                private TrackManagerListener,
                                private ClipManagerListener,
                                private ProjectManagerListener {
    Impl()
        : loader_([this](engine::DeviceKey key) { return modelDevice(key); },
                  [this](engine::DeviceKey key, const DeviceInfo& resolved,
                         const std::vector<RestoredParameter>& restored) {
                      applyLoadedDevice(key, resolved, restored);
                  }) {
        factory_.loadExternalsWith(loader_);

        // A tap read late loses nothing, since the peak is held until
        // something takes it, so this is how smooth a meter looks.
        startTimer(kMeterIntervalMs);

        if (!EngineTrace::enabled())
            return;

        // Both ends of the race into one stream (#2568): the publishes below
        // write to it on this thread and the devices write to it on the audio
        // thread, and reading them in order is the whole point.
        factory_.traceInto(trace_);
        EngineTrace::print("MIDI trace on. Publishes and what reached each device, in order.");
    }

    ~Impl() override {
        stopTimer();
        detach();
    }

    /// The meters, and the audio thread's side of the trace.
    void timerCallback() override {
        publishMeters();

        for (const auto& line : trace_.drain())
            EngineTrace::print(line);

        traceDroppedLiveMidi();
    }

    /// Live MIDI that never reached the callback. Only when the count moved,
    /// since a line per tick would bury the notes that did arrive.
    void traceDroppedLiveMidi() {
        const auto dropped = queue_.oversized() + queue_.overflowed() +
                             droppedEvents_.load(std::memory_order_relaxed);
        if (!EngineTrace::enabled() || dropped == tracedDrops_)
            return;

        tracedDrops_ = dropped;
        EngineTrace::print("live midi dropped: " + juce::String(queue_.oversized()) +
                           " too long, " + juce::String(queue_.overflowed()) + " overflowed, " +
                           juce::String(droppedEvents_.load(std::memory_order_relaxed)) +
                           " in the callback");
    }

    /// What every track's output tap has held since the last tick (#2570).
    /// Walked off the model, and the one reader: LevelTap::read is
    /// destructive.
    void publishMeters() {
        if (session_ == nullptr || meters_ == nullptr)
            return;

        for (const auto& track : TrackManager::getInstance().getTracks())
            publishMeter(track.id);

        publishMeter(MASTER_TRACK_ID);
    }

    void publishMeter(TrackId trackId) {
        auto* tap = session_->meterTap(engine::trackMeterKey(trackId));
        if (tap == nullptr)
            return;

        const auto levels = tap->read();

        // A meter reading nothing and a meter nothing reads look identical from
        // a still mixer, so the trace says which (#2570).
        if (EngineTrace::enabled() && levels.loudest() > 0.0f)
            EngineTrace::print("meter: track " + juce::String(trackId) + " peak " +
                               juce::String(levels.loudest(), 4));

        meters_(trackId, levels.peak[0], levels.peak[1]);
    }

    /// Where the transport was when the model moved, in the same stream as the
    /// notes, so an edit can be placed against the block that sounded one.
    void traceEdit(EngineTrace::Kind kind) {
        if (EngineTrace::enabled())
            trace_.write({.kind = kind, .beat = positionBeats()});
    }

    double positionBeats() const {
        return session_ != nullptr ? session_->positionBeats() : 0.0;
    }

    /**
     * @brief What the plan asked for and what it got.
     *
     * The other half of whether this path is live: a trace with no notes in it
     * means one of a device nothing could build, a callback that never ran, or
     * an engine that was never chosen, and those look identical from silence.
     */
    void tracePlan(const engine::RenderPlan& plan) {
        if (!EngineTrace::enabled())
            return;

        const auto devices =
            std::ranges::count(plan.ops, engine::OpKind::Device, &engine::PlanOp::kind);
        const auto midi =
            std::ranges::count(plan.ops, engine::OpKind::ClipMidi, &engine::PlanOp::kind);
        const auto inputs =
            std::ranges::count(plan.ops, engine::OpKind::MidiInput, &engine::PlanOp::kind);

        EngineTrace::print(
            "plan: " + juce::String(devices) + " device ops (" +
            juce::String(static_cast<int>(unbuilt_.size())) + " unbuilt, " +
            juce::String(static_cast<int>(factory_.loadingExternals())) + " loading), " +
            juce::String(midi) + " clip-midi ops, " + juce::String(inputs) + " midi-input ops, " +
            juce::String(rendered_.load(std::memory_order_relaxed)) + " callbacks so far");
    }

    void start(juce::AudioDeviceManager& devices) {
        if (devices_ != nullptr)
            return;

        devices_ = &devices;
        TrackManager::getInstance().addListener(this);
        ClipManager::getInstance().addListener(this);
        ProjectManager::getInstance().addListener(this);
        devices_->addAudioCallback(this);
    }

    void detach() {
        if (devices_ == nullptr)
            return;

        // The callback first: what follows destroys the session it renders
        // through, and removeAudioCallback returns only once the audio thread
        // is out of here.
        devices_->removeAudioCallback(this);
        ProjectManager::getInstance().removeListener(this);
        ClipManager::getInstance().removeListener(this);
        TrackManager::getInstance().removeListener(this);
        devices_ = nullptr;

        cancelPendingUpdate();
        session_.reset();
        voiceThread_.reset();
        voices_.reset();
    }

    // ===== Publishing, all on the message thread =====

    /// The four things a plan publish is: the topology, the values resolved
    /// against it, the IDs the model still holds, and the context they render
    /// under.
    void publishPlan() {
        const auto& tracks = TrackManager::getInstance().getTracks();
        const auto* master = TrackManager::getInstance().getTrack(MASTER_TRACK_ID);
        if (session_ == nullptr || master == nullptr)
            return;

        // Before the plan binds them, so an "all" route resolves to the inputs
        // this machine has now rather than to whichever of them has already
        // played a note.
        sources_.registerAvailableDevices();
        factory_.setModel(tracks, *master);

        auto plan = std::make_shared<const engine::RenderPlan>(
            engine::compileRenderPlan(tracks, *master, {.auditionMidi = true}));
        report("plan", plan->diagnostics);

        engine::PlanValues values;
        report("values", resolveValues(*plan, tracks, *master, values));

        const auto ids = engine::collectRuntimeStateIds(tracks, *master);
        const auto result = session_->publish(plan, context_, ids, std::move(values));
        report("publish", result.messages);

        if (!result.published)
            return;

        livePlan_ = std::move(plan);
        traceEdit(EngineTrace::Kind::Swap);
        tracePlan(*livePlan_);
        reportUnbuiltDevices();
    }

    /// A mixer move: the same values against the plan already playing. It
    /// escalates itself into a structural publish when a link edit changed the
    /// parameter table's shape, which is the one thing values cannot carry.
    void publishValues() {
        const auto& tracks = TrackManager::getInstance().getTracks();
        const auto* master = TrackManager::getInstance().getTrack(MASTER_TRACK_ID);
        if (session_ == nullptr || livePlan_ == nullptr || master == nullptr)
            return;

        engine::PlanValues values;
        report("values", resolveValues(*livePlan_, tracks, *master, values));
        report("values", session_->publishValues(std::move(values)).messages);
    }

    /// What every track plays, resolved against the tempo the transport is
    /// published with: a snapshot compiled against a different map would place
    /// every clip at the seconds that map gave it.
    void publishClips() {
        if (session_ == nullptr)
            return;

        const auto& tracks = TrackManager::getInstance().getTracks();
        auto snapshot = std::make_shared<const engine::ClipSnapshot>(
            engine::compileClipSnapshot(clipLanesFor(tracks), clipSources(), tempoMap()));
        report("clips", snapshot->diagnostics);

        traceEdit(EngineTrace::Kind::Publish);
        session_->publishClips(std::move(snapshot));
    }

    void publishTransport() {
        if (session_ == nullptr)
            return;

        session_->publishTransport(
            {.tempo = tempoMap(), .loop = loop_, .click = click_, .request = request_});
    }

    /// A play, a stop or a locate. The generation is what makes a snapshot a
    /// request rather than a description, so it is bumped here and nowhere
    /// else.
    void publishRequest(const engine::TransportRequest& request) {
        request_ = request;
        request_.generation = ++generation_;
        publishTransport();
    }

    // ===== Following the model =====

    /// Nothing the store holds outlives the project it was built for: track
    /// and device ids restart at 1 in the next one, so its keys would collide
    /// with devices that are already gone (#2572).
    void projectTeardown() override {
        factory_.forgetBuiltDevices();
    }

    void tracksChanged() override {
        wantPlan();
    }
    void trackDevicesChanged(TrackId) override {
        wantPlan();
    }
    void deviceAdded(const ChainNodePath&, const DeviceInfo&) override {
        wantPlan();
    }
    void trackPropertyChanged(int) override {
        wantValues();
    }
    void masterChannelChanged() override {
        wantValues();
    }
    void devicePropertyChanged(const ChainNodePath&) override {
        wantValues();
    }
    void deviceParameterChanged(const ChainNodePath&, int, float) override {
        wantValues();
    }
    void deviceModifiersChanged(TrackId) override {
        wantValues();
    }
    void clipsChanged() override {
        wantClips();
    }
    void clipPropertyChanged(ClipId) override {
        wantClips();
    }

    void wantPlan() {
        plan_.store(true, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
    void wantValues() {
        values_.store(true, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
    void wantClips() {
        clips_.store(true, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }

    /// Everything the model has asked for since the last one. A plan publish
    /// resolves values on its way through, so the two are alternatives; clips
    /// travel on their own and are published beside either.
    void handleAsyncUpdate() override {
        const engine::RenderContext wanted{.sampleRate = rate_.load(),
                                           .maxBlockSize = blockSize_.load(),
                                           .numChannels = kChannels};
        if (wanted.sampleRate <= 0.0 || wanted.maxBlockSize <= 0)
            return;

        if (session_ == nullptr || wanted != context_) {
            rebuild(wanted);
            return;
        }

        // All three taken before any of them runs, so a plan publish clears the
        // values it already resolved rather than leaving them to be published
        // again at whatever the next edit turns out to be.
        const auto plan = plan_.exchange(false);
        const auto values = values_.exchange(false);
        const auto clips = clips_.exchange(false);

        if (plan)
            publishPlan();
        else if (values)
            publishValues();

        if (clips)
            publishClips();
    }

    /**
     * @brief Build the session the device just described, and fill it.
     *
     * A sample rate or block size change means every prepared object is
     * prepared for the wrong one, and the pool's readers are cued in the wrong
     * samples, so the session is remade rather than adjusted. Off the device
     * for the length of it: everything here allocates, and the session being
     * destroyed is the one the callback renders through.
     */
    void rebuild(const engine::RenderContext& context) {
        devices_->removeAudioCallback(this);

        session_.reset();
        voiceThread_.reset();
        voices_.reset();
        livePlan_.reset();

        context_ = context;
        scratch_.setSize(kChannels, context.maxBlockSize);

        // What a plugin is created at. Everything the store held has gone with
        // the session, so the publish below asks for each of them again.
        loader_.setContext(context);

        voices_ = std::make_unique<engine::ClipVoicePool>(files_, reader_, context);
        voiceThread_ = std::make_unique<engine::ClipVoiceThread>(*voices_);

        // No render pool: every block renders on the audio thread alone, which
        // is the same executor with one thread instead of many. Spreading a
        // block across realtime workers is the next question this can be asked,
        // and not one to answer in the same change that first made a sound.
        session_ = std::make_unique<engine::EngineSession>(factory_, nullptr, voices_.get());
        factory_.attach(session_->clipFeed(), voices_->feed(), session_->launchHandleFeed(),
                        session_->liveInputs(), sources_);

        session_->liveInputs().prepare(inputChannels_.load(std::memory_order_relaxed),
                                       context.maxBlockSize);
        prepareLiveMidi();

        plan_.store(false, std::memory_order_relaxed);
        values_.store(false, std::memory_order_relaxed);
        clips_.store(false, std::memory_order_relaxed);

        publishTransport();
        publishPlan();
        publishClips();

        devices_->addAudioCallback(this);
    }

    // ===== The device =====

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        // Only what the device is. Building a session allocates and publishing
        // one waits, and both belong to the publishing thread.
        rate_.store(device->getCurrentSampleRate());
        blockSize_.store(device->getCurrentBufferSizeSamples());
        inputChannels_.store(device->getActiveInputChannels().countNumberOfSetBits());
        triggerAsyncUpdate();

        if (EngineTrace::enabled())
            EngineTrace::print(
                "device \"" + device->getName() + "\" at " +
                juce::String(device->getCurrentSampleRate(), 0) + " Hz, " +
                juce::String(device->getCurrentBufferSizeSamples()) + " samples, " +
                juce::String(device->getActiveOutputChannels().countNumberOfSetBits()) +
                " outputs");
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* output,
                                          int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        for (auto channel = 0; channel < numOutputChannels; ++channel)
            juce::FloatVectorOperations::clear(output[channel], numSamples);

        rendered_.fetch_add(1, std::memory_order_relaxed);

        // Read without a guard because there is nothing to guard against:
        // rebuild() takes this callback off the device before it touches the
        // session, and removeAudioCallback returns only once this has left.
        if (session_ == nullptr || numSamples <= 0)
            return;

        const auto outputs = std::min(numOutputChannels, kChannels);
        const auto streams = collectLiveMidi();

        // In pieces no longer than the plan was prepared for. A driver handing
        // over more than the block size it declared is rare and legal, and
        // rendering it in one go is a buffer overrun rather than a wrong sound.
        for (auto done = 0; done < numSamples;) {
            const auto piece = std::min(numSamples - done, scratch_.getNumSamples());
            juce::AudioBuffer<float> block(scratch_.getArrayOfWritePointers(), kChannels, piece);

            // The callback's live MIDI belongs to its first piece alone: every
            // event is stamped at offset 0, so handing the streams to a second
            // piece would sound each note again.
            session_->process(piece, block,
                              done == 0 ? engine::LiveInputBlock{{}, streams}
                                        : engine::LiveInputBlock{});

            for (auto channel = 0; channel < outputs; ++channel)
                juce::FloatVectorOperations::copy(output[channel] + done,
                                                  scratch_.getReadPointer(channel), piece);
            done += piece;
        }
    }

    /// Room for every source the callback may see. Off the device, from
    /// rebuild(), because it allocates.
    void prepareLiveMidi() {
        midiBySource_.resize(kMaxLiveMidiSources);
        for (auto& buffer : midiBySource_)
            buffer.ensureSize(static_cast<std::size_t>(engine::kMaxMidiBytesPerPort));

        streams_.reserve(kMaxLiveMidiSources);
    }

    /**
     * @brief The queue's events, per source, as the span the session reads.
     *
     * Audio thread, once per callback. Every event lands at offset 0: placing
     * one where it was played is part of #2553's monitor round trip, and until
     * then this is up to a block of jitter.
     */
    std::span<const engine::LiveMidiStream> collectLiveMidi() {
        for (auto& buffer : midiBySource_)
            buffer.clear();

        streams_.clear();

        queue_.drain([this](const LiveMidiQueue::Event& event) {
            const auto index = static_cast<std::size_t>(event.source - 1);
            if (event.source < 1 || index >= midiBySource_.size()) {
                droppedEvents_.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            auto& buffer = midiBySource_[index];
            if (static_cast<int>(buffer.data.size()) + kQueuedEventBytes >
                engine::kMaxMidiBytesPerPort) {
                droppedEvents_.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            buffer.addEvent(event.bytes, event.size, 0);
        });

        for (std::size_t i = 0; i < midiBySource_.size(); ++i)
            if (!midiBySource_[i].isEmpty())
                streams_.push_back(
                    {static_cast<engine::LiveMidiSourceId>(i + 1), &midiBySource_[i]});

        return streams_;
    }

    // ===== Odds and ends =====

    const engine::TempoMap& tempoMap() const {
        return map_;
    }

    /** @brief Bake the map again, after anything it is baked from moves. */
    void refreshTempoMap() {
        map_ = tempoMapAt(bpm_, numerator_, denominator_);
    }

    std::vector<std::string> resolveValues(const engine::RenderPlan& plan,
                                           const std::vector<TrackInfo>& tracks,
                                           const TrackInfo& master,
                                           engine::PlanValues& values) const {
        const auto& automation = AutomationManager::getInstance();
        return engine::resolvePlanValues(plan, tracks, master, values, automation.getLanes(),
                                         automation.getClips());
    }

    /**
     * @brief The model's own DeviceInfo at @p key, or null.
     *
     * Read when it is asked for rather than from the publish's copy: a plugin
     * load takes seconds, and what the model holds when one finishes is what
     * its state has to be restored onto. Per track because TrackManager hands
     * out mutable tracks but not a mutable project.
     */
    DeviceInfo* modelDevice(engine::DeviceKey key) {
        DeviceInfo* found = nullptr;

        TrackManager::getInstance().forEachTrackIncludingMaster([&found, key](TrackInfo& track) {
            if (found != nullptr)
                return;

            const auto devices = adapter::devicesIn(track);
            if (const auto device = devices.find(key); device != devices.end())
                found = device->second;
        });

        return found;
    }

    /**
     * @brief Write what a plugin turned out to be back into the model.
     *
     * The engine never corrects the model itself, so this is where an external
     * plugin's real bus widths, MIDI capability and role arrive, and the plan
     * compiles port widths and MIDI edges from exactly those. The parameters
     * come with them because a chunk is allowed to disagree with the array a
     * project saved, and everything downstream reads the model.
     */
    void applyLoadedDevice(engine::DeviceKey key, const DeviceInfo& resolved,
                           const std::vector<RestoredParameter>& restored) {
        auto* device = modelDevice(key);
        if (device == nullptr)
            return;

        *device = resolved;
        applyRestoredParameters(*device, restored);

        // Only the main FX chain has a path to tell: findDevicePath searches
        // that segment alone, and a DeviceId is section-local, so a post-FX id
        // would find whichever unrelated device holds the same number there.
        if (key.segment == ChainSegment::Fx)
            if (const auto path = TrackManager::getInstance().findDevicePath(key.deviceId);
                path.isValid())
                TrackManager::getInstance().notifyDevicePropertyChanged(path);

        // Last, so the plan that binds the loader's instance is compiled from
        // the model as corrected above.
        wantPlan();
    }

    /// Devices the model names and no catalog could build. Named rather than
    /// counted, and only when the set changes: a line per publish would bury
    /// everything else.
    void reportUnbuiltDevices() {
        if (factory_.unbuilt() == unbuilt_)
            return;

        unbuilt_ = factory_.unbuilt();
        for (const auto& name : unbuilt_)
            juce::Logger::writeToLog("[engine] nothing to build \"" + name + "\" with");
    }

    EngineFileReaders files_;
    engine::PrefetchThread reader_;

    /// Who is playing, and what they played. The registry is the message and
    /// MIDI threads'; the queue is how one reaches the other side. Before the
    /// factory, which holds the registry for the length of a publish.
    LiveMidiSources sources_;
    LiveMidiQueue queue_;

    /// One buffer per source id, indexed by id - 1, and the streams over the
    /// ones a callback found anything in. Both sized in rebuild().
    std::vector<juce::MidiBuffer> midiBySource_;
    std::vector<engine::LiveMidiStream> streams_;

    /// Events the callback could not place: a source past kMaxLiveMidiSources,
    /// or a port already holding its whole budget.
    std::atomic<std::uint32_t> droppedEvents_{0};
    std::uint32_t tracedDrops_ = 0;

    EngineRuntimeFactory factory_;

    /// After the factory, so it is destroyed first: a load still in flight is
    /// gated on the assignment table this owns, and a table that has gone is a
    /// load nobody is waiting for.
    ExternalPluginLoader loader_;

    juce::AudioDeviceManager* devices_ = nullptr;
    engine::RenderContext context_{};
    juce::AudioBuffer<float> scratch_;

    // Declared so that destruction unwinds inwards: the session lets go of the
    // pool before the thread servicing it stops, and the thread stops before
    // the pool it is inside goes away.
    std::unique_ptr<engine::ClipVoicePool> voices_;
    std::unique_ptr<engine::ClipVoiceThread> voiceThread_;
    std::unique_ptr<engine::EngineSession> session_;

    /// The plan the session is rendering, kept so a mixer move can resolve
    /// values against it without compiling another.
    std::shared_ptr<const engine::RenderPlan> livePlan_;

    double bpm_ = 120.0;
    int numerator_ = 4;
    int denominator_ = 4;

    /// The three above, baked. Cached rather than rebuilt per read: everything
    /// published with a tempo reads it, and so does the app through @ref view_.
    engine::TempoMap map_ = tempoMapAt(bpm_, numerator_, denominator_);
    TempoMapView view_{map_};

    engine::LoopRange loop_;
    engine::ClickSettings click_;
    engine::TransportRequest request_;
    std::uint64_t generation_ = 0;

    std::atomic<double> rate_{0.0};
    std::atomic<int> blockSize_{0};
    std::atomic<int> inputChannels_{0};
    std::atomic<bool> plan_{false};
    std::atomic<bool> values_{false};
    std::atomic<bool> clips_{false};

    /// Callbacks this host has rendered. Only ever read by the trace, and the
    /// one number that separates "nothing sounded" from "nothing ran".
    std::atomic<std::uint64_t> rendered_{0};

    std::vector<juce::String> unbuilt_;
    EngineTrace trace_;

    /// Where the levels go. Null until a caller asks (#2570).
    EngineHost::MeterSink meters_;
};

EngineHost::EngineHost() : impl_(std::make_unique<Impl>()) {}

EngineHost::~EngineHost() = default;

void EngineHost::start(juce::AudioDeviceManager& devices) {
    impl_->start(devices);
}

void EngineHost::setPluginServices(juce::AudioPluginFormatManager& formats,
                                   const juce::KnownPluginList& knownPlugins) {
    impl_->loader_.setServices(&formats, &knownPlugins);
}

void EngineHost::meterInto(MeterSink sink) {
    impl_->meters_ = std::move(sink);
}

void EngineHost::stop() {
    impl_->detach();
}

void EngineHost::audition(TrackId trackId, const juce::MidiMessage& message) {
    impl_->queue_.push(impl_->sources_.auditionSourceFor(trackId), message);
}

void EngineHost::pushMidi(const juce::String& deviceId, const juce::MidiMessage& message) {
    impl_->queue_.push(impl_->sources_.sourceFor(deviceId), message);
}

void EngineHost::registerLiveMidiSource(const juce::String& deviceId) {
    impl_->sources_.sourceFor(deviceId);
}

void EngineHost::play() {
    impl_->publishRequest({.playing = true, .locate = false});
}

void EngineHost::stopPlaying() {
    impl_->publishRequest({.playing = false, .locate = false});
}

void EngineHost::locateSeconds(double seconds) {
    impl_->publishRequest({.playing = impl_->request_.playing,
                           .locate = true,
                           .positionBeat = impl_->tempoMap().timeToBeat(seconds)});
}

bool EngineHost::isPlaying() const {
    return impl_->request_.playing;
}

double EngineHost::positionBeats() const {
    return impl_->positionBeats();
}

double EngineHost::positionSeconds() const {
    return impl_->tempoMap().beatToTime(positionBeats());
}

void EngineHost::setTempo(double bpm) {
    impl_->bpm_ = bpm;
    impl_->refreshTempoMap();
    impl_->publishTransport();

    // The snapshot's seconds were derived through the map that just changed, so
    // it is compiled again rather than left placing clips at the old tempo.
    impl_->publishClips();
}

double EngineHost::tempo() const {
    return impl_->bpm_;
}

void EngineHost::setTimeSignature(int numerator, int denominator) {
    impl_->numerator_ = numerator;
    impl_->denominator_ = denominator;
    impl_->refreshTempoMap();
    impl_->publishTransport();
}

void EngineHost::getTimeSignature(int& numerator, int& denominator) const {
    numerator = impl_->numerator_;
    denominator = impl_->denominator_;
}

void EngineHost::setLoop(bool enabled, double startBeat, double endBeat) {
    impl_->loop_ = {.enabled = enabled, .startBeat = startBeat, .endBeat = endBeat};
    impl_->publishTransport();
}

void EngineHost::setMetronomeEnabled(bool enabled) {
    impl_->click_.enabled = enabled;
    impl_->publishTransport();
}

bool EngineHost::isMetronomeEnabled() const {
    return impl_->click_.enabled;
}

EngineHost::LoopState EngineHost::loop() const {
    return {.enabled = impl_->loop_.enabled,
            .startBeat = impl_->loop_.startBeat,
            .endBeat = impl_->loop_.endBeat};
}

const magda::TempoMap* EngineHost::tempoMap() const {
    return &impl_->view_;
}

}  // namespace magda::daw::engine_host
