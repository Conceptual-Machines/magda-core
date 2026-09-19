#include "EngineHost.hpp"

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "../../audio/DeviceParameterDisplayTextProvider.hpp"
#include "../../audio/DeviceParameterList.hpp"
#include "../../audio/midi/RecordingNoteQueue.hpp"
#include "../../audio/plugin_manager/ExternalPluginState.hpp"
#include "../../audio/plugins/engine/ControlExecutor.hpp"
#include "../../audio/plugins/engine/DeviceControl.hpp"
#include "../../audio/plugins/engine/EngineDeviceFactory.hpp"
#include "../../audio/plugins/engine/EngineExternalDevice.hpp"
#include "../../audio/plugins/engine/EngineMagdaDevice.hpp"
#include "../../core/AddressedParameters.hpp"
#include "../../core/AutomationManager.hpp"
#include "../../core/ChainWalk.hpp"
#include "../../core/ClipManager.hpp"
#include "../../core/DrumGridPads.hpp"
#include "../../core/OpenProjectAddressing.hpp"
#include "../../core/TempoMap.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/controllers/BindingRegistry.hpp"
#include "../../project/ProjectManager.hpp"
#include "EngineOfflineRender.hpp"
#include "EngineProject.hpp"
#include "EngineRuntimeFactory.hpp"
#include "EngineTrace.hpp"
#include "ExternalPluginLoader.hpp"
#include "HardwareInputMap.hpp"
#include "LiveMidiCollector.hpp"
#include "LiveMidiQueue.hpp"
#include "LiveMidiRouting.hpp"
#include "LiveMidiSources.hpp"
#include "SessionArrangementCapture.hpp"
#include "SlotLauncher.hpp"
#include "TrackFreeze.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "clip/ClipVoicePool.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "io/LiveInput.hpp"
#include "io/MidiTakeRecorder.hpp"
#include "io/PrefetchThread.hpp"
#include "io/RecordThread.hpp"
#include "io/TakeRecorder.hpp"
#include "plan/PlanCompiler.hpp"
#include "trace/PlaybackTrace.hpp"

namespace magda::daw::engine_host {

namespace adapter = magda::daw::audio::engine_adapter;

namespace {

/// Stereo, like everything else in the engine: the model has no mono tracks,
/// and a narrow device is a declared width rather than a smaller buffer.
constexpr int kChannels = 2;

/// 30 fps, which is what the fork's own metering timer runs at.
constexpr int kMeterIntervalMs = 33;

/// Per live MIDI pass. Fixed before the audio thread sees the tap.
constexpr std::size_t kRecordingPreviewNotes = 2048;

/// About 23 minutes at 48 kHz with the tap's default 1,024 samples per peak.
constexpr std::size_t kRecordingPreviewPeaks = 65536;

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

/**
 * @brief The model's own DeviceInfo at @p key, or null.
 *
 * Read when it is asked for rather than from a publish's copy: a plugin load
 * takes seconds, and what the model holds when one finishes is what its state
 * has to be restored onto. Per track because TrackManager hands out mutable
 * tracks but not a mutable project.
 *
 * A free function rather than a member, so that an asynchronous operation
 * writing a model carries nothing but its own request (PluginAssignments.hpp).
 */
DeviceInfo* modelDeviceAt(engine::DeviceKey key) {
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
 * @brief The keys for the device at @p devicePath and any device on its pads.
 *
 * Pad devices have no path of their own, so callers address a Drum Grid by
 * the grid's path (#2207). Looked up through devicesIn(), the walk the
 * publish uses, so both agree which chain section a device is in.
 */
std::vector<engine::DeviceKey> keysOfDevices(const std::set<const DeviceInfo*>& wanted) {
    std::vector<engine::DeviceKey> found;

    TrackManager::getInstance().forEachTrackIncludingMaster([&found, &wanted](TrackInfo& track) {
        for (const auto& [key, device] : adapter::devicesIn(track))
            if (wanted.contains(device))
                found.push_back(key);
    });

    return found;
}

std::vector<engine::DeviceKey> keysOfDeviceAt(const ChainNodePath& devicePath) {
    const auto* target = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
    if (target == nullptr)
        return {};

    std::set<const DeviceInfo*> subtree{target};
    if (target->pads)
        for (const auto& pad : target->pads->chains)
            chain_walk::forEachDevice(pad.elements, devicePath, chain_walk::Pads::Enter,
                                      [&subtree](const DeviceInfo& device, const ChainNodePath&) {
                                          subtree.insert(&device);
                                      });

    return keysOfDevices(subtree);
}

/// The one device @p devicePath names, without the pads a capture also takes:
/// a pad's editor is its own, and not what a click on the grid asked for.
std::optional<engine::DeviceKey> keyOfDeviceAt(const ChainNodePath& devicePath) {
    const auto* target = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
    if (target == nullptr)
        return std::nullopt;

    const auto keys = keysOfDevices({target});
    if (keys.empty())
        return std::nullopt;

    return keys.front();
}

/**
 * @brief The EngineExternalDevice inside @p device, or null if it is not one.
 *
 * Unwraps TracingDevice first, so turning the trace on does not change the
 * answer (#2568).
 */
adapter::EngineExternalDevice* externalIn(engine::EngineDevice& device) {
    if (auto* tracing = dynamic_cast<TracingDevice*>(&device))
        return externalIn(tracing->wrapped());

    return dynamic_cast<adapter::EngineExternalDevice*>(&device);
}

/// @brief The MAGDA device inside @p device, or null if it is not one (#2585).
/// Unwraps the trace the same way externalIn does, and for the same reason.
audio::MagdaDevice* magdaIn(engine::EngineDevice& device) {
    if (auto* tracing = dynamic_cast<TracingDevice*>(&device))
        return magdaIn(tracing->wrapped());

    auto* hosted = dynamic_cast<adapter::EngineMagdaDevice*>(&device);
    return hosted != nullptr ? &hosted->device() : nullptr;
}

/// The racks a project holds, by the id a rack meter is keyed with (#2649).
///
/// A Drum Grid's pad rack is not one: it is addressed by the grid's own path
/// rather than by a rack step, the compiler gives it no meter, and the grid
/// meters its pads itself (#2211).
std::set<RackId> modelRacks(const std::vector<TrackInfo>& tracks, const TrackInfo& master) {
    std::set<RackId> racks;

    const auto collect = [&racks](const TrackInfo& track) {
        chain_walk::forEachRack(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id),
                                chain_walk::Pads::Enter,
                                [&racks](const RackInfo& rack, const ChainNodePath& rackPath) {
                                    if (rackPath.getType() == ChainNodeType::Rack)
                                        racks.insert(rack.id);
                                });
    };

    for (const auto& track : tracks)
        collect(track);

    collect(master);
    return racks;
}

/**
 * @brief The external devices the live session holds (#2581).
 *
 * Owned by the host and held weakly by the plane, which is what makes a
 * capture outliving the project answer "the runtime is gone" rather than
 * reach for a session that has been destroyed.
 */
class SessionDevices final : public adapter::DeviceRegistry {
  public:
    using LiveSession = std::function<const engine::EngineSession*()>;

    explicit SessionDevices(LiveSession session) : session_(std::move(session)) {}

    std::shared_ptr<adapter::EngineExternalDevice> find(engine::DeviceKey key) const override {
        const auto* session = session_ ? session_() : nullptr;
        if (session == nullptr)
            return nullptr;

        auto held = session->device(key);
        if (held == nullptr)
            return nullptr;

        auto* external = externalIn(*held);
        if (external == nullptr)
            return nullptr;

        // Aliased onto the store's own lease, so what holds the instance open
        // is the thing that owns it rather than a second count beside it.
        return {std::move(held), external};
    }

  private:
    LiveSession session_;
};

/// One of @p key's outstanding edits has completed.
void settlePendingEdit(std::map<std::pair<engine::DeviceKey, int>, int>& pending,
                       const std::pair<engine::DeviceKey, int>& key) {
    const auto at = pending.find(key);
    if (at != pending.end() && --at->second <= 0)
        pending.erase(at);
}

}  // namespace

/**
 * @brief The session, the callback that drives it, and the model it follows.
 *
 * One class rather than three because the three cannot be separated: what the
 * callback renders is what the model last published, and both are bounded by
 * the device's own start and stop.
 */
/// Whether an edit could have changed what the plan is, as opposed to what it
/// is worth. Only a compile can answer it; this is which edits are worth
/// asking about (#2592).
enum class Shape { Unchanged, MayHaveMoved };

struct EngineHost::Impl final : private juce::AudioIODeviceCallback,
                                private juce::AsyncUpdater,
                                private juce::Timer,
                                private TrackManagerListener,
                                private AutomationManagerListener,
                                private ClipManagerListener,
                                private ProjectManagerListener,
                                private BindingRegistryListener,
                                public OfflineRenderHost,
                                public LaunchHost {
    Impl()
        : loader_([this](engine::DeviceKey key) { return modelDevice(key); },
                  [this](engine::DeviceKey key, const DeviceInfo& resolved,
                         const std::vector<RestoredParameter>& restored) {
                      applyLoadedDevice(key, resolved, restored);
                  }) {
        factory_.loadExternalsWith(loader_);

        // Captures nothing: the model is a singleton and the request guards
        // the key, so a project that closed first is a no-op.
        loader_.onPluginEdit(
            [this](engine::DeviceKey key, adapter::EngineExternalDevice::Observation observation) {
                observePluginParameter(key, observation);
            });

        // A device that applied edits wakes the same drain (#2651).
        loader_.onWoken([this] { plane_.settleParameterEdits(); });

        // A tap read late loses nothing, since the peak is held until
        // something takes it, so this is how smooth a meter looks.
        startTimer(kMeterIntervalMs);

        if (!EngineTrace::enabled())
            return;

        // Both ends of the race into one stream (#2568): the publishes below
        // write to it on this thread and the devices write to it on the audio
        // thread, and reading them in order is the whole point.
        factory_.traceInto(trace_);
        engine::setPlaybackTraceSink(&Impl::onPlaybackTrace, this);
        EngineTrace::print("MIDI trace on. Publishes and what reached each device, in order.");
    }

    ~Impl() override {
        engine::setPlaybackTraceSink(nullptr, nullptr);
        stopTimer();
        detach();
    }

    /// Audio thread: a slot's pass wrap or a voice's read window, into the
    /// same stream as the notes (#2674).
    static void onPlaybackTrace(const engine::PlaybackTraceEntry& entry, void* context) {
        auto* self = static_cast<Impl*>(context);
        self->trace_.write({.kind = entry.kind == engine::PlaybackTraceEntry::Kind::PassWrap
                                        ? EngineTrace::Kind::PassWrap
                                        : EngineTrace::Kind::VoiceWindow,
                            .beat = entry.beat,
                            .clip = entry.clip,
                            .a = entry.a,
                            .b = entry.b,
                            .c = entry.c,
                            .d = entry.d});
    }

    /// The meters, and the audio thread's side of the trace.
    void timerCallback() override {
        reconcileFinishedSessionTakes();
        if (sessionCapture_.update())
            publishClips();
        if (sessionCapture_.armed())
            sessionPunchPending_.store(false, std::memory_order_release);
        publishMeters();
        publishDeviceMeters();
        publishRackMeters();
        publishPadTriggers();

        for (const auto& line : trace_.drain())
            EngineTrace::print(line);

        traceDroppedLiveMidi();
    }

    /// Live MIDI that never reached the callback. Only when the count moved,
    /// since a line per tick would bury the notes that did arrive.
    void traceDroppedLiveMidi() {
        const auto dropped = queue_.oversized() + queue_.overflowed() + collector_.dropped();
        if (!EngineTrace::enabled() || dropped == tracedDrops_)
            return;

        tracedDrops_ = dropped;
        EngineTrace::print("live midi dropped: " + juce::String(queue_.oversized()) +
                           " too long, " + juce::String(queue_.overflowed()) + " overflowed, " +
                           juce::String(collector_.dropped()) + " in the callback");
    }

    /// What every track's output tap has held since the last tick (#2570).
    /// Walked off the model, and the one reader: LevelTap::read is
    /// destructive.
    void publishMeters() {
        if (session_ == nullptr || meters_ == nullptr)
            return;

        for (const auto& track : TrackManager::getInstance().getTracks())
            publishMeter(track.id, track.monitorsInput());

        publishMeter(MASTER_TRACK_ID, false);
    }

    void publishMeter(TrackId trackId, bool monitorsInput) {
        auto* tap = session_->meterTap(engine::trackMeterKey(trackId));
        auto* inputTap = session_->meterTap(engine::liveInputMeterKey(trackId));
        if (tap == nullptr && inputTap == nullptr)
            return;

        auto levels = tap != nullptr ? tap->read() : engine::LevelTap::Levels{};

        // A monitored input shows at its own level, ahead of the fader (#2553).
        // Drained either way, so switching monitoring on shows now rather than
        // the loudest input since.
        if (inputTap != nullptr) {
            const auto input = inputTap->read();
            if (monitorsInput)
                for (std::size_t channel = 0; channel < levels.peak.size(); ++channel)
                    levels.peak[channel] = std::max(levels.peak[channel], input.peak[channel]);
        }

        // A meter reading nothing and a meter nothing reads look identical from
        // a still mixer, so the trace says which (#2570).
        if (EngineTrace::enabled() && levels.loudest() > 0.0f)
            EngineTrace::print("meter: track " + juce::String(trackId) + " peak " +
                               juce::String(levels.loudest(), 4));

        meters_(trackId, levels.peak[0], levels.peak[1]);
    }

    /// What every device slot's tap has held since the last tick (#2570).
    ///
    /// Silence first, for every slot the model has: a slot the plan bound no
    /// tap for -- a bypassed device, one whose plugin is still loading -- would
    /// otherwise hold its last peak for as long as the project is open. Then
    /// the taps that exist report over the top, in the same tick, so nothing
    /// draws the zero.
    void publishDeviceMeters() {
        if (session_ == nullptr || deviceMeters_ == nullptr)
            return;

        for (const auto& slot : devicePaths_)
            deviceMeters_->setDevicePeak(slot.second, {});

        session_->forEachDeviceMeter([this](engine::DeviceKey key, engine::LevelTap& tap) {
            const auto slot = devicePaths_.find(key);
            if (slot == devicePaths_.end())
                return;

            const auto levels = tap.read();
            deviceMeters_->setDevicePeak(slot->second,
                                         {.peakL = levels.peak[0], .peakR = levels.peak[1]});
        });
    }

    /// What every rack's own tap has held since the last tick (#2649). Silence
    /// first, for the same reason the slots get it: a bypassed rack is not in
    /// the plan at all, so nothing would report over its last peak.
    void publishRackMeters() {
        if (session_ == nullptr || deviceMeters_ == nullptr)
            return;

        for (const auto rackId : rackIds_)
            deviceMeters_->setRackPeak(rackId, {});

        session_->forEachRackMeter([this](RackId rackId, engine::LevelTap& tap) {
            if (!rackIds_.contains(rackId))
                return;

            const auto levels = tap.read();
            deviceMeters_->setRackPeak(rackId, {.peakL = levels.peak[0], .peakR = levels.peak[1]});
        });
    }

    /// The notes each pad's gate passed since the last tick, as the grid notes
    /// that started them (#2669).
    void publishPadTriggers() {
        if (session_ == nullptr || deviceMeters_ == nullptr)
            return;

        session_->forEachNoteOnTap([this](const engine::OpKey& key, engine::NoteOnTap& tap) {
            const auto started = tap.take();
            if (started.none())
                return;

            const auto grid = devicePaths_.find(key.deviceKey());
            if (grid == devicePaths_.end())
                return;

            const auto* pads = TrackManager::getInstance().getPads(grid->second);
            if (pads == nullptr)
                return;

            const auto pad = std::ranges::find(pads->chains, key.chainId, &ChainInfo::id);
            if (pad == pads->chains.end())
                return;

            for (const auto note : padNotesPlayed(*pad, started))
                deviceMeters_->startPadNote(grid->second, note);
        });
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
        AutomationManager::getInstance().addListener(this);
        ClipManager::getInstance().addListener(this);
        ProjectManager::getInstance().addListener(this);
        BindingRegistry::getInstance().addListener(this);
        devices_->addAudioCallback(this);
    }

    void detach() {
        if (devices_ == nullptr)
            return;

        // The callback first: what follows destroys the session it renders
        // through, and removeAudioCallback returns only once the audio thread
        // is out of here.
        devices_->removeAudioCallback(this);
        stopRecording(true, false);
        BindingRegistry::getInstance().removeListener(this);
        ProjectManager::getInstance().removeListener(this);
        ClipManager::getInstance().removeListener(this);
        AutomationManager::getInstance().removeListener(this);
        TrackManager::getInstance().removeListener(this);
        devices_ = nullptr;

        cancelPendingUpdate();
        sessionCapture_.reset();
        session_.reset();
        voiceThread_.reset();
        voices_.reset();
    }

    // ===== Publishing, all on the message thread =====

    /// The four things a plan publish is: the topology, the values resolved
    /// against it, the IDs the model still holds, and the context they render
    /// under.
    /// The model as an engine plan. One call, so the compile options cannot
    /// differ between the publish and the comparison below.
    std::shared_ptr<const engine::RenderPlan> compilePlan(const std::vector<TrackInfo>& tracks,
                                                          const TrackInfo& master) const {
        engine::CompileOptions options{.auditionMidi = true};
        options.hardwareOutputs = hardwareOutputs_;
        return std::make_shared<const engine::RenderPlan>(
            engine::compileRenderPlan(tracks, master, options));
    }

    bool publishPlan(std::shared_ptr<const engine::RenderPlan> plan = nullptr) {
        const auto& model = TrackManager::getInstance().getTracks();
        const auto* master = TrackManager::getInstance().getTrack(MASTER_TRACK_ID);
        if (session_ == nullptr || master == nullptr)
            return false;

        // Before the plan binds them, so an "all" route resolves to the inputs
        // this machine has now rather than to whichever of them has already
        // played a note.
        sources_.registerAvailableDevices();
        factory_.setModel(model, *master);

        // Off the same reading of the model as the devices the plan is about to
        // bind, so a slot's meter and the slot the UI draws cannot come from
        // two walks that disagree (#2570). Only a structural edit moves a
        // device, which is what gets here.
        devicePaths_ = adapter::devicePathsIn(model, *master);
        rackIds_ = modelRacks(model, *master);

        const auto tracks = playedTracks();

        // Recording follows the new model before that model can reach an
        // audio block. A newly armed key is ignored by the old epoch; a
        // removed or rerouted key is closed before the new epoch/routing is
        // visible, so no block can fall between model and recorder state.
        reconcileRecording();

        // Before the swap, so the new plan's first block renders against
        // routing resolved from the same reading of the model (#2592).
        publishRouting(tracks);

        if (plan == nullptr)
            plan = compilePlan(tracks, *master);
        report("plan", plan->diagnostics);

        // Values and ids off the model rather than what plays: a frozen chain's
        // lanes stay addressed and its devices keep their state.
        engine::PlanValues values;
        report("values", resolveValues(*plan, model, *master, values));

        const auto ids = engine::collectRuntimeStateIds(model, *master);
        const auto result = session_->publish(plan, context_, ids, std::move(values));
        report("publish", result.messages);

        if (!result.published)
            return false;

        livePlan_ = std::move(plan);
        publishedHardwareOutputs_ = hardwareOutputs_;
        publishedTakeKeys_ = ids.takes;
        harvestClosedTakes();
        traceEdit(EngineTrace::Kind::Swap);
        tracePlan(*livePlan_);
        reportUnbuiltDevices();
        forgetPluginDriverState();
        markRenderedDevices();
        return true;
    }

    /**
     * @brief Tell each plugin whether a block will reach it (#2651).
     *
     * A bypassed device is not in the plan, and nothing is rendered while the
     * audio device is stopped; the plane applies what those hold from the
     * control side, so it is asked to settle after every change.
     */
    void markRenderedDevices() {
        if (session_ == nullptr)
            return;

        std::set<engine::DeviceKey> planned;
        if (livePlan_ != nullptr)
            for (const auto& op : livePlan_->ops)
                if (op.kind == engine::OpKind::Device)
                    planned.insert(op.key.deviceKey());

        const auto running = audioRunning_.load(std::memory_order_acquire);
        for (const auto key : factory_.externalKeys())
            if (auto* external = externalDeviceFor(key))
                external->setRendered(running && planned.contains(key));

        plane_.settleParameterEdits();
    }

    /// A mixer move: the same values against the plan already playing. It
    /// escalates itself into a structural publish when a link edit changed the
    /// parameter table's shape, which is the one thing values cannot carry.
    void publishValues() {
        const auto tracks = playedTracks();
        const auto* master = TrackManager::getInstance().getTrack(MASTER_TRACK_ID);
        if (session_ == nullptr || livePlan_ == nullptr || master == nullptr)
            return;

        // A property edit can be one values cannot carry: a bypass takes the
        // device out of the plan, a route moves an edge, a monitor switch
        // decides whether an input op exists at all. Compiling and comparing
        // is the exact question; a list of properties would be one more thing
        // to keep in step with the compiler (#2592).
        if (shape_.exchange(false, std::memory_order_relaxed)) {
            auto compiled = compilePlan(tracks, *master);
            if (engine::planFingerprint(*compiled) != engine::planFingerprint(*livePlan_)) {
                publishPlan(std::move(compiled));
                return;
            }
        }

        // Arming is permission for an existing input op to feed a take. With
        // Monitor In it changes no topology, but the epoch still needs the
        // new TakeKey set before the next block. This follows the topology
        // check above so Monitor Auto can first add its input op.
        const auto ids =
            engine::collectRuntimeStateIds(TrackManager::getInstance().getTracks(), *master);
        if (ids.takes != publishedTakeKeys_) {
            publishPlan(livePlan_);
            return;
        }

        reconcileRecording();

        engine::PlanValues values;
        report("values",
               resolveValues(*livePlan_, TrackManager::getInstance().getTracks(), *master, values));
        report("values", session_->publishValues(std::move(values)).messages);

        // A monitor or route change arrives as a track property, off the same
        // reading of the model as the values above.
        publishRouting(tracks);

        // A lane drawn on a parameter is a values publish, and it is what makes
        // that slot addressed (#2633).
        forgetPluginDriverState();
    }

    /**
     * @brief Publish what every track hears of the live input (#2592, #2553).
     *
     * One snapshot per reading of the model. The store keeps its inputs across
     * a recompile, and they read this.
     */
    void publishRouting(const std::vector<TrackInfo>& tracks) {
        if (session_ == nullptr)
            return;

        if (auto routing = routing_.resolve(tracks, liveAudioFor(tracks)))
            session_->liveInputs().publishRouting(std::move(routing));
    }

    /// The callback channels each track's hardware audio input names. A name
    /// this interface does not have reads silence, and is reported once.
    std::vector<engine::TrackLiveAudio> liveAudioFor(const std::vector<TrackInfo>& tracks) {
        std::vector<engine::TrackLiveAudio> audio;
        std::set<juce::String> unresolved;

        for (const auto& track : tracks) {
            const auto& input = track.audioInputDevice;
            if (input.isEmpty() || input.startsWith("track:"))
                continue;

            const auto found = hardwareInputs_.find(input);
            if (found != hardwareInputs_.end()) {
                audio.push_back({.trackId = track.id, .channels = found->second});
                continue;
            }

            if (!unresolvedInputs_.contains(input))
                juce::Logger::writeToLog("[engine] audio input \"" + input + "\" on track " +
                                         juce::String(track.id) +
                                         " is not on this interface, it renders silence");
            unresolved.insert(input);
        }

        unresolvedInputs_ = std::move(unresolved);
        std::ranges::sort(audio, {}, &engine::TrackLiveAudio::trackId);
        return audio;
    }

    /// A device plugged in or unplugged since the last structural publish.
    /// Without this the snapshot a route resolves against is the one the last
    /// plan was compiled with, and a track selecting a newly connected device
    /// would resolve to a source nothing pushes under.
    void refreshLiveMidiDevices() {
        sources_.registerAvailableDevices();
        reconcileRecording();
        publishRouting(playedTracks());
    }

    // ===== Arrangement input recording (#2553) =====

    struct ActiveMidiRoute {
        juce::String input;
        std::vector<engine::LiveMidiSourceId> sources;
        std::optional<int> sessionScene;

        bool operator==(const ActiveMidiRoute&) const = default;
    };

    struct ActiveAudioRoute {
        juce::String input;
        std::vector<int> channels;
        std::uint64_t deviceGeneration = 0;
        std::optional<int> sessionScene;

        bool operator==(const ActiveAudioRoute&) const = default;
    };

    struct ClosingTake {
        bool createClip = true;
        bool releaseSessionOnSuccess = false;
        std::optional<int> pendingSessionScene;
    };

    int planRecordingAdjustmentSamples() const {
        return session_ == nullptr ? 0 : std::max(0, session_->latencySamples());
    }

    int boundedRecordingAdjustment(std::int64_t samples) const {
        const auto limit = static_cast<std::int64_t>(std::clamp(
            rate_.load() * 3.0, 0.0, static_cast<double>(std::numeric_limits<int>::max())));
        return static_cast<int>(std::clamp(samples, -limit, limit));
    }

    int midiRecordingAdjustmentSamples() const {
        const auto sum = static_cast<std::int64_t>(
                             deviceOutputAdjustmentSamples_.load(std::memory_order_relaxed)) +
                         planRecordingAdjustmentSamples();
        return boundedRecordingAdjustment(sum);
    }

    int audioRecordingAdjustmentSamples() const {
        const auto sum = static_cast<std::int64_t>(
                             deviceRecordingAdjustmentSamples_.load(std::memory_order_relaxed)) +
                         planRecordingAdjustmentSamples();
        return boundedRecordingAdjustment(sum);
    }

    bool hasActiveAudioTakes() const {
        return std::ranges::any_of(audioRecordingRoutes_, [this](const auto& entry) {
            return !closingAudioTakes_.contains(entry.first);
        });
    }

    bool hasActiveMidiTakes() const {
        return std::ranges::any_of(recordingRoutes_, [this](const auto& entry) {
            return !closingMidiTakes_.contains(entry.first);
        });
    }

    bool hasClosingTake(TrackId trackId) const {
        return closingAudioTakes_.contains(trackId) || closingMidiTakes_.contains(trackId);
    }

    bool frozen(TrackId trackId) const {
        return std::ranges::any_of(
            frozen_, [trackId](const FrozenTrack& track) { return track.trackId == trackId; });
    }

    std::optional<std::vector<engine::LiveMidiSourceId>> recordingSources(
        const TrackInfo& track, const std::vector<engine::LiveMidiSourceId>& devices) {
        if (!track.recordArmed || !track.takesExternalInput() || frozen(track.id) ||
            track.midiInputDevice.isEmpty() || track.midiInputDevice.startsWith("track:"))
            return std::nullopt;

        if (track.midiInputDevice == "all") {
            if (devices.empty())
                return std::nullopt;
            return devices;
        }

        const auto source = sources_.resolveRoute(track.midiInputDevice);
        if (source == LiveMidiSources::kNoSource ||
            std::ranges::find(devices, source) == devices.end())
            return std::nullopt;

        return std::vector<engine::LiveMidiSourceId>{source};
    }

    std::optional<std::vector<int>> recordingChannels(const TrackInfo& track) const {
        if (!track.recordArmed || !track.takesExternalInput() || frozen(track.id) ||
            track.audioInputDevice.isEmpty() || track.audioInputDevice.startsWith("track:"))
            return std::nullopt;

        const auto input = hardwareInputs_.find(track.audioInputDevice);
        if (input == hardwareInputs_.end() || input->second.empty())
            return std::nullopt;
        return input->second;
    }

    bool previewTakeActive(const engine::TakeKey& key) const {
        return key.material == engine::RecordMaterial::audio
                   ? audioRecordingRoutes_.contains(key.trackId) &&
                         !closingAudioTakes_.contains(key.trackId)
                   : recordingRoutes_.contains(key.trackId) &&
                         !closingMidiTakes_.contains(key.trackId);
    }

    void erasePreview(const engine::TakeKey& key) {
        previewReadings_.erase(key);
        const auto preview = recordingPreviews_.find(key.trackId);
        if (preview != recordingPreviews_.end() &&
            preview->second.isAudioRecording == (key.material == engine::RecordMaterial::audio))
            recordingPreviews_.erase(preview);
    }

    void startMidiTake(const TrackInfo& track, std::vector<engine::LiveMidiSourceId> sources,
                       std::optional<int> sessionScene = {}) {
        if (session_ == nullptr)
            return;

        const auto adjustment = midiRecordingAdjustmentSamples();
        recordingRoutes_[track.id] = ActiveMidiRoute{
            .input = track.midiInputDevice, .sources = sources, .sessionScene = sessionScene};
        const engine::TakeKey key{track.id, engine::RecordMaterial::midi};
        erasePreview(key);
        session_->startTake(
            key,
            {.target =
                 sessionScene ? engine::RecordTarget::slot : engine::RecordTarget::arrangement,
             .scene = sessionScene.value_or(-1),
             .maxNotes = kRecordingPreviewNotes},
            [this, sources = std::move(sources), adjustment, sessionScene,
             trackId = track.id](engine::RecordTap& tap) mutable {
                engine::MidiTakeRecorderSettings settings;
                settings.sources = std::move(sources);
                settings.latencySamples = adjustment;
                if (sessionScene)
                    settings.slot = session_->slotRunTarget({trackId, *sessionScene});
                auto recorder = std::make_unique<engine::MidiTakeRecorder>(session_->liveInputs(),
                                                                           tap, settings);
                recordThread_.add(recorder->stream());
                return recorder;
            });
    }

    void startAudioTake(const TrackInfo& track, std::vector<int> channels,
                        std::optional<int> sessionScene = {}) {
        if (session_ == nullptr)
            return;

        const auto adjustment = audioRecordingAdjustmentSamples();
        const auto generation = inputGeneration_.load(std::memory_order_relaxed);
        audioRecordingRoutes_[track.id] = ActiveAudioRoute{.input = track.audioInputDevice,
                                                           .channels = channels,
                                                           .deviceGeneration = generation,
                                                           .sessionScene = sessionScene};
        const engine::TakeKey key{track.id, engine::RecordMaterial::audio};
        erasePreview(key);
        session_->startTake(
            key,
            {.target =
                 sessionScene ? engine::RecordTarget::slot : engine::RecordTarget::arrangement,
             .scene = sessionScene.value_or(-1),
             .maxPeaks = kRecordingPreviewPeaks},
            [this, channels = std::move(channels), adjustment, sessionScene,
             trackId = track.id](engine::RecordTap& tap) {
                engine::TakeRecorderSettings settings;
                settings.channels = channels;
                settings.latencySamples = adjustment;
                settings.directory = ProjectManager::getInstance().getRecordingsDirectory();
                settings.name = "Track " + juce::String(trackId);
                if (sessionScene)
                    settings.slot = session_->slotRunTarget({trackId, *sessionScene});
                auto recorder = std::make_unique<engine::TakeRecorder>(
                    session_->liveInputs(), context_, tap, std::move(settings));
                recordThread_.add(recorder->stream());
                return recorder;
            });
    }

    bool materializeMidi(engine::ClosedTake closed, bool createClip = true,
                         std::optional<int> sessionScene = {}) {
        if (closed.take == nullptr)
            return false;

        recordThread_.remove(closed.take->stream());
        auto* recorder = dynamic_cast<engine::MidiTakeRecorder*>(closed.take.get());
        if (recorder == nullptr)
            return false;

        auto take = recorder->finish(map_);
        if (take.eventsLost > 0 || take.messagesDropped > 0 || take.passesLost > 0)
            juce::Logger::writeToLog("[engine] MIDI take track " +
                                     juce::String(closed.key.trackId) + ": " +
                                     juce::String(take.eventsLost) + " events lost, " +
                                     juce::String(take.messagesDropped) + " messages dropped, " +
                                     juce::String(take.passesLost) + " pass boundaries lost");

        if (take.empty() || !createClip)
            return false;

        if (TrackManager::getInstance().getTrack(closed.key.trackId) == nullptr) {
            juce::Logger::writeToLog("[engine] discarded MIDI take for deleted track " +
                                     juce::String(closed.key.trackId));
            return false;
        }

        RecordedMidiClipData data{.startBeat = take.startBeat,
                                  .lengthBeats = take.lengthBeats,
                                  .active = std::move(take.active),
                                  .takeModel = std::move(take.clip)};
        if (sessionScene)
            data.startBeat = 0.0;
        auto& clips = ClipManager::getInstance();
        bool collisionFallback = false;
        if (sessionScene &&
            clips.getClipInSlot(closed.key.trackId, *sessionScene) != INVALID_CLIP_ID) {
            juce::Logger::writeToLog("[engine] Session MIDI target was filled during capture; "
                                     "preserving take in Arrangement");
            sessionScene.reset();
            collisionFallback = true;
            data.startBeat = take.startBeat;
        }
        return clips.createRecordedMidiClip(closed.key.trackId, std::move(data),
                                            collisionFallback ? ClipOverlapPolicy::PreserveExisting
                                                              : ClipOverlapPolicy::ResolveOverlaps,
                                            sessionScene ? ClipView::Session
                                                         : ClipView::Arrangement,
                                            sessionScene.value_or(-1)) != INVALID_CLIP_ID &&
               sessionScene.has_value();
    }

    bool materializeAudio(engine::ClosedTake closed, bool createClip = true,
                          std::optional<int> sessionScene = {}) {
        if (closed.take == nullptr)
            return false;

        recordThread_.remove(closed.take->stream());
        auto* recorder = dynamic_cast<engine::TakeRecorder*>(closed.take.get());
        if (recorder == nullptr)
            return false;

        auto take = recorder->finish();
        if (take.samplesLost > 0 || take.passesLost > 0 || take.failed)
            juce::Logger::writeToLog(
                "[engine] audio take track " + juce::String(closed.key.trackId) + ": " +
                juce::String(take.samplesLost) + " samples lost, " + juce::String(take.passesLost) +
                " pass boundaries lost" + (take.failed ? ", file write failed" : ""));

        if (take.empty() || !createClip)
            return false;

        if (TrackManager::getInstance().getTrack(closed.key.trackId) == nullptr) {
            juce::Logger::writeToLog("[engine] discarded audio take for deleted track " +
                                     juce::String(closed.key.trackId));
            return false;
        }

        RecordedAudioClipData data{.startBeat = take.startBeat,
                                   .lengthBeats = take.lengthBeats,
                                   .filePath = take.file.getFullPathName(),
                                   .takeModel = std::move(take.clip)};
        if (sessionScene)
            data.startBeat = 0.0;
        auto& clips = ClipManager::getInstance();
        bool collisionFallback = false;
        if (sessionScene &&
            clips.getClipInSlot(closed.key.trackId, *sessionScene) != INVALID_CLIP_ID) {
            juce::Logger::writeToLog("[engine] Session audio target was filled during capture; "
                                     "preserving take in Arrangement");
            sessionScene.reset();
            collisionFallback = true;
            data.startBeat = take.startBeat;
        }
        return clips.createRecordedAudioClip(closed.key.trackId, std::move(data),
                                             collisionFallback ? ClipOverlapPolicy::PreserveExisting
                                                               : ClipOverlapPolicy::ResolveOverlaps,
                                             sessionScene ? ClipView::Session
                                                          : ClipView::Arrangement,
                                             sessionScene.value_or(-1)) != INVALID_CLIP_ID &&
               sessionScene.has_value();
    }

    bool harvestClosedTakes(bool createClips = true) {
        if (session_ == nullptr)
            return false;

        bool removedSessionTarget = false;
        bool harvested = false;
        std::vector<engine::SlotKey> releases;
        for (auto& closed : session_->takeClosedTakes()) {
            harvested = true;
            const auto trackId = closed.key.trackId;
            if (closed.key.material == engine::RecordMaterial::audio) {
                const auto route = audioRecordingRoutes_.find(trackId);
                const auto scene = route == audioRecordingRoutes_.end()
                                       ? std::optional<int>{}
                                       : route->second.sessionScene;
                auto launchedScene = std::optional<int>{};
                if (const auto target = sessionSlotTargets_.find(trackId);
                    target != sessionSlotTargets_.end() && target->second.launched)
                    launchedScene = target->second.scene;
                auto closeSettings = ClosingTake{.createClip = createClips};
                if (const auto closing = closingAudioTakes_.find(trackId);
                    closing != closingAudioTakes_.end()) {
                    closeSettings = closing->second;
                    closingAudioTakes_.erase(closing);
                }
                if (scene)
                    removedSessionTarget |= sessionSlotTargets_.erase(trackId) > 0;
                audioRecordingRoutes_.erase(trackId);
                erasePreview(closed.key);
                const auto handedOver =
                    materializeAudio(std::move(closed), closeSettings.createClip, scene);
                const auto releaseScene = launchedScene ? launchedScene : scene;
                if (releaseScene && (!handedOver || closeSettings.releaseSessionOnSuccess))
                    releases.push_back({trackId, *releaseScene});
                if (closeSettings.pendingSessionScene) {
                    sessionSlotTargets_[trackId] = {.scene = *closeSettings.pendingSessionScene};
                    removedSessionTarget = true;
                }
                continue;
            }
            const auto route = recordingRoutes_.find(trackId);
            const auto scene =
                route == recordingRoutes_.end() ? std::optional<int>{} : route->second.sessionScene;
            auto launchedScene = std::optional<int>{};
            if (const auto target = sessionSlotTargets_.find(trackId);
                target != sessionSlotTargets_.end() && target->second.launched)
                launchedScene = target->second.scene;
            auto closeSettings = ClosingTake{.createClip = createClips};
            if (const auto closing = closingMidiTakes_.find(trackId);
                closing != closingMidiTakes_.end()) {
                closeSettings = closing->second;
                closingMidiTakes_.erase(closing);
            }
            if (scene)
                removedSessionTarget |= sessionSlotTargets_.erase(trackId) > 0;
            recordingRoutes_.erase(trackId);
            erasePreview(closed.key);
            const auto handedOver =
                materializeMidi(std::move(closed), closeSettings.createClip, scene);
            const auto releaseScene = launchedScene ? launchedScene : scene;
            if (releaseScene && (!handedOver || closeSettings.releaseSessionOnSuccess))
                releases.push_back({trackId, *releaseScene});
            if (closeSettings.pendingSessionScene) {
                sessionSlotTargets_[trackId] = {.scene = *closeSettings.pendingSessionScene};
                removedSessionTarget = true;
            }
        }
        if (!releases.empty()) {
            engine::LaunchRequestQueue::Gesture gesture(session_->launchRequests());
            for (const auto& key : releases)
                gesture.backToArrangement(key);
        }
        if (removedSessionTarget) {
            launcher_.recordTargetsChanged();
            publishClips();
        }
        return harvested;
    }

    bool stopMidiTake(TrackId trackId, bool createClip = true, bool allowPostRoll = true,
                      bool releaseSessionOnSuccess = false) {
        if (session_ == nullptr)
            return false;

        if (auto closing = closingMidiTakes_.find(trackId); closing != closingMidiTakes_.end()) {
            closing->second.createClip &= createClip;
            closing->second.releaseSessionOnSuccess |= releaseSessionOnSuccess;
            if (allowPostRoll)
                return true;
            createClip = closing->second.createClip;
            closingMidiTakes_.erase(closing);
        }

        const auto route = recordingRoutes_.find(trackId);
        const auto scene =
            route == recordingRoutes_.end() ? std::optional<int>{} : route->second.sessionScene;
        const engine::TakeKey key{trackId, engine::RecordMaterial::midi};
        erasePreview(key);
        if (allowPostRoll) {
            auto stopped = session_->stopTakeAfterPostRoll(key);
            if (stopped.deferred) {
                closingMidiTakes_[trackId] = {.createClip = createClip,
                                              .releaseSessionOnSuccess = releaseSessionOnSuccess};
                return true;
            }
            recordingRoutes_.erase(trackId);
            return materializeMidi(std::move(stopped.closed), createClip, scene);
        }

        recordingRoutes_.erase(trackId);
        return materializeMidi(session_->stopTake(key), createClip, scene);
    }

    bool stopAudioTake(TrackId trackId, bool createClip = true, bool allowPostRoll = true,
                       bool releaseSessionOnSuccess = false) {
        if (session_ == nullptr)
            return false;

        if (auto closing = closingAudioTakes_.find(trackId); closing != closingAudioTakes_.end()) {
            closing->second.createClip &= createClip;
            closing->second.releaseSessionOnSuccess |= releaseSessionOnSuccess;
            if (allowPostRoll)
                return true;
            createClip = closing->second.createClip;
            closingAudioTakes_.erase(closing);
        }

        const auto route = audioRecordingRoutes_.find(trackId);
        const auto scene = route == audioRecordingRoutes_.end() ? std::optional<int>{}
                                                                : route->second.sessionScene;
        const engine::TakeKey key{trackId, engine::RecordMaterial::audio};
        erasePreview(key);
        if (allowPostRoll) {
            auto stopped = session_->stopTakeAfterPostRoll(key);
            if (stopped.deferred) {
                closingAudioTakes_[trackId] = {.createClip = createClip,
                                               .releaseSessionOnSuccess = releaseSessionOnSuccess};
                return true;
            }
            audioRecordingRoutes_.erase(trackId);
            return materializeAudio(std::move(stopped.closed), createClip, scene);
        }

        audioRecordingRoutes_.erase(trackId);
        return materializeAudio(session_->stopTake(key), createClip, scene);
    }

    void stopRecording(bool createClips = true, bool allowPostRoll = true) {
        arrangementPunchActive_.store(false, std::memory_order_release);
        sessionPunchPending_.store(false, std::memory_order_release);
        const auto invalidatePunch = punch_.recordingRequested;
        if (punch_.recordingRequested) {
            punch_.recordingRequested = false;
            ++punch_.recordingGeneration;
            publishTransport();
        }
        recording_ = false;
        arrangementRecording_ = false;
        for (auto& [trackId, closing] : closingMidiTakes_) {
            juce::ignoreUnused(trackId);
            closing.pendingSessionScene.reset();
        }
        for (auto& [trackId, closing] : closingAudioTakes_) {
            juce::ignoreUnused(trackId);
            closing.pendingSessionScene.reset();
        }
        sessionCapture_.disarm(createClips);
        if (invalidatePunch)
            sessionCapture_.invalidateRecordingGeneration(punch_.recordingGeneration);
        const auto active = recordingRoutes_;
        std::vector<engine::SlotKey> releases;
        for (const auto& [trackId, unused] : active) {
            juce::ignoreUnused(unused);
            const auto target = sessionSlotTargets_.find(trackId);
            const auto launched = target != sessionSlotTargets_.end() && target->second.launched;
            const auto handedOver = stopMidiTake(trackId, createClips, allowPostRoll, false);
            if (launched && !handedOver)
                releases.push_back({trackId, target->second.scene});
        }
        const auto activeAudio = audioRecordingRoutes_;
        for (const auto& [trackId, unused] : activeAudio) {
            juce::ignoreUnused(unused);
            const auto target = sessionSlotTargets_.find(trackId);
            const auto launched = target != sessionSlotTargets_.end() && target->second.launched;
            const auto handedOver = stopAudioTake(trackId, createClips, allowPostRoll, false);
            if (launched && !handedOver)
                releases.push_back({trackId, target->second.scene});
        }
        if (session_ != nullptr && !releases.empty()) {
            engine::LaunchRequestQueue::Gesture gesture(session_->launchRequests());
            for (const auto& key : releases)
                gesture.backToArrangement(key);
        }
        std::erase_if(sessionSlotTargets_,
                      [this](const auto& entry) { return !hasClosingTake(entry.first); });
        launcher_.recordTargetsChanged();
        harvestClosedTakes(createClips);
        publishClips();
    }

    struct SessionSlotTarget {
        int scene = -1;
        bool launched = false;
        std::uint64_t launchedAtCallback = 0;
    };

    void stopSessionMaterialTakes(TrackId trackId, bool createClip = true,
                                  bool releaseSessionOnSuccess = false) {
        if (const auto route = recordingRoutes_.find(trackId);
            route != recordingRoutes_.end() && route->second.sessionScene)
            stopMidiTake(trackId, createClip, true, releaseSessionOnSuccess);
        if (const auto route = audioRecordingRoutes_.find(trackId);
            route != audioRecordingRoutes_.end() && route->second.sessionScene)
            stopAudioTake(trackId, createClip, true, releaseSessionOnSuccess);
    }

    void setPendingSessionScene(TrackId trackId, std::optional<int> scene) {
        if (auto closing = closingMidiTakes_.find(trackId); closing != closingMidiTakes_.end())
            closing->second.pendingSessionScene = scene;
        if (auto closing = closingAudioTakes_.find(trackId); closing != closingAudioTakes_.end())
            closing->second.pendingSessionScene = scene;
    }

    void armSessionSlotRecording(TrackId trackId, int sceneIndex) {
        if (trackId == INVALID_TRACK_ID || sceneIndex < 0 ||
            TrackManager::getInstance().getTrack(trackId) == nullptr ||
            ClipManager::getInstance().getClipInSlot(trackId, sceneIndex) != INVALID_CLIP_ID)
            return;

        const auto found = sessionSlotTargets_.find(trackId);
        if (found != sessionSlotTargets_.end() && found->second.scene == sceneIndex) {
            auto deferred = false;
            if (found->second.launched) {
                stopSessionMaterialTakes(trackId, true, true);
                deferred = hasClosingTake(trackId);
                if (session_ != nullptr && !deferred) {
                    engine::LaunchRequestQueue::Gesture gesture(session_->launchRequests());
                    gesture.backToArrangement({trackId, found->second.scene});
                }
            }
            if (deferred)
                setPendingSessionScene(trackId, {});
            else
                sessionSlotTargets_.erase(found);
        } else {
            auto deferred = false;
            if (found != sessionSlotTargets_.end() && found->second.launched) {
                stopSessionMaterialTakes(trackId, true, true);
                deferred = hasClosingTake(trackId);
                if (session_ != nullptr && !deferred) {
                    engine::LaunchRequestQueue::Gesture gesture(session_->launchRequests());
                    gesture.backToArrangement({trackId, found->second.scene});
                }
            }
            if (deferred)
                setPendingSessionScene(trackId, sceneIndex);
            else
                sessionSlotTargets_[trackId] = {.scene = sceneIndex};
        }
        recording_ = sessionCapture_.ownsRecording() ||
                     sessionPunchPending_.load(std::memory_order_acquire) || hasActiveMidiTakes() ||
                     hasActiveAudioTakes();
        launcher_.recordTargetsChanged();
        publishClips();
    }

    bool isSessionSlotRecordArmed(TrackId trackId, int sceneIndex) const {
        const auto found = sessionSlotTargets_.find(trackId);
        return found != sessionSlotTargets_.end() && found->second.scene == sceneIndex;
    }

    bool isSessionSlotRecording(TrackId trackId, int sceneIndex) const {
        const auto found = sessionSlotTargets_.find(trackId);
        if (found == sessionSlotTargets_.end() || found->second.scene != sceneIndex ||
            !found->second.launched)
            return false;
        if (session_ == nullptr)
            return false;
        const engine::RecordTap* tap = nullptr;
        if (const auto route = audioRecordingRoutes_.find(trackId);
            route != audioRecordingRoutes_.end() && route->second.sessionScene == sceneIndex &&
            !closingAudioTakes_.contains(trackId))
            tap = session_->takeTap({trackId, engine::RecordMaterial::audio});
        else if (const auto route = recordingRoutes_.find(trackId);
                 route != recordingRoutes_.end() && route->second.sessionScene == sceneIndex &&
                 !closingMidiTakes_.contains(trackId))
            tap = session_->takeTap({trackId, engine::RecordMaterial::midi});
        if (tap == nullptr)
            return false;
        engine::RecordTap::Reading reading;
        return tap->read(reading) && reading.recording && reading.pass > 0 &&
               reading.target == engine::RecordTarget::slot && reading.scene == sceneIndex;
    }

    bool stopSessionSlotRecording(TrackId trackId) {
        const auto found = sessionSlotTargets_.find(trackId);
        if (found == sessionSlotTargets_.end())
            return false;
        const auto target = found->second;
        if (target.launched)
            stopSessionMaterialTakes(trackId, true, true);
        const auto deferred = hasClosingTake(trackId);
        if (deferred)
            setPendingSessionScene(trackId, {});
        else
            sessionSlotTargets_.erase(found);
        launcher_.recordTargetsChanged();
        if (session_ != nullptr && !deferred) {
            engine::LaunchRequestQueue::Gesture gesture(session_->launchRequests());
            gesture.backToArrangement({trackId, target.scene});
        }
        recording_ = sessionCapture_.ownsRecording() ||
                     sessionPunchPending_.load(std::memory_order_acquire) || hasActiveMidiTakes() ||
                     hasActiveAudioTakes();
        publishClips();
        return true;
    }

    void beginArmedSessionSlotRecordings(std::optional<double> positionSeconds = {},
                                         std::optional<double> dueBeat = {},
                                         std::optional<bool> wasPlaying = {}) {
        handleUpdateNowIfNeeded();
        if (session_ == nullptr || !audioRunning_.load(std::memory_order_acquire) ||
            offlineRenders_ > 0)
            return;

        sources_.registerAvailableDevices();
        auto devices = sources_.deviceSources();
        std::erase_if(devices, [this](auto source) {
            return sources_.slotFor(source) == LiveMidiSources::kNoSlot;
        });
        std::ranges::sort(devices);
        devices.erase(std::unique(devices.begin(), devices.end()), devices.end());

        std::vector<TrackId> launched;
        {
            std::optional<double> due;
            if (dueBeat) {
                const auto sync = session_->syncPoint();
                due = wasPlaying.value_or(request_.playing)
                          ? sync.monotonicAt(*dueBeat)
                          : sync.monotonicBeat + countInBeats() +
                                (*dueBeat - map_.timeToBeat(positionSeconds.value_or(0.0)));
            } else if (wasPlaying.value_or(request_.playing)) {
                const auto sync = session_->syncPoint();
                const auto period = launchBeatsPerBar();
                const auto boundary = std::floor(sync.beat / period + 1.0) * period;
                due = sync.monotonicAt(boundary);
            } else if (const auto countIn = countInBeats(); countIn > 0.0) {
                // The roll-in moves the cursor back but not the monotonic count,
                // so the count-in ends that many beats past where it stands now.
                due = session_->syncPoint().monotonicBeat + countIn;
            }
            engine::LaunchRequestQueue::Gesture gesture(session_->launchRequests());

            for (auto& [trackId, target] : sessionSlotTargets_) {
                if (target.launched)
                    continue;
                const auto* track = TrackManager::getInstance().getTrack(trackId);
                if (track == nullptr || ClipManager::getInstance().getClipInSlot(
                                            trackId, target.scene) != INVALID_CLIP_ID)
                    continue;
                auto channels = recordingChannels(*track);
                auto inputs = recordingSources(*track, devices);
                if (!channels && !inputs)
                    continue;

                const engine::SlotKey key{trackId, target.scene};
                if (!session_->slotRunTarget(key))
                    continue;
                if (recordingRoutes_.contains(trackId))
                    stopMidiTake(trackId);
                if (audioRecordingRoutes_.contains(trackId))
                    stopAudioTake(trackId);
                if (channels)
                    startAudioTake(*track, std::move(*channels), target.scene);
                else
                    startMidiTake(*track, std::move(*inputs), target.scene);

                for (const auto clipId :
                     ClipManager::getInstance().getClipsOnTrack(trackId, ClipView::Session)) {
                    if (const auto* clip = ClipManager::getInstance().getClip(clipId))
                        gesture.stop({trackId, clip->sceneIndex}, due);
                }
                gesture.play(key, due);
                target.launched = true;
                launcher_.recordTargetLaunched(trackId);
                launched.push_back(trackId);
                recording_ = true;
            }
        }
        const auto committedAfter = rendered_.load(std::memory_order_acquire);
        for (const auto trackId : launched)
            if (const auto target = sessionSlotTargets_.find(trackId);
                target != sessionSlotTargets_.end())
                target->second.launchedAtCallback = committedAfter;

        if (recording_ && !request_.playing)
            publishRequest({.playing = true,
                            .locate = positionSeconds.has_value(),
                            .positionBeat = map_.timeToBeat(positionSeconds.value_or(0.0)),
                            .countInBeats = countInBeats()});
    }

    void reconcileRecording() {
        if (session_ == nullptr)
            return;

        harvestClosedTakes();
        if (!recording_)
            return;

        sources_.registerAvailableDevices();
        auto devices = sources_.deviceSources();
        std::erase_if(devices, [this](auto source) {
            return sources_.slotFor(source) == LiveMidiSources::kNoSlot;
        });
        std::ranges::sort(devices);
        devices.erase(std::unique(devices.begin(), devices.end()), devices.end());

        std::vector<TrackId> endedSessionTargets;
        for (const auto& [trackId, target] : sessionSlotTargets_) {
            if (!target.launched)
                continue;
            const auto* track = TrackManager::getInstance().getTrack(trackId);
            if (track == nullptr) {
                endedSessionTargets.push_back(trackId);
                continue;
            }

            const auto channels = recordingChannels(*track);
            if (channels) {
                const auto active = audioRecordingRoutes_.find(trackId);
                const ActiveAudioRoute wanted{.input = track->audioInputDevice,
                                              .channels = *channels,
                                              .deviceGeneration =
                                                  inputGeneration_.load(std::memory_order_relaxed),
                                              .sessionScene = target.scene};
                if (active == audioRecordingRoutes_.end() || active->second != wanted)
                    endedSessionTargets.push_back(trackId);
                continue;
            }

            const auto sources = recordingSources(*track, devices);
            if (!sources) {
                endedSessionTargets.push_back(trackId);
                continue;
            }
            const auto active = recordingRoutes_.find(trackId);
            const ActiveMidiRoute wanted{
                .input = track->midiInputDevice, .sources = *sources, .sessionScene = target.scene};
            if (active == recordingRoutes_.end() || active->second != wanted)
                endedSessionTargets.push_back(trackId);
        }
        for (const auto trackId : endedSessionTargets)
            stopSessionSlotRecording(trackId);

        if (!arrangementRecording_) {
            recording_ = sessionCapture_.ownsRecording() ||
                         sessionPunchPending_.load(std::memory_order_acquire) ||
                         hasActiveMidiTakes() || hasActiveAudioTakes();
            return;
        }

        std::set<TrackId> eligible;
        for (const auto& track : TrackManager::getInstance().getTracks()) {
            auto sources = recordingSources(track, devices);
            if (!sources.has_value())
                continue;

            eligible.insert(track.id);
            if (closingMidiTakes_.contains(track.id))
                continue;
            const auto active = recordingRoutes_.find(track.id);
            if (sessionSlotTargets_.contains(track.id))
                continue;
            const ActiveMidiRoute wanted{
                .input = track.midiInputDevice, .sources = *sources, .sessionScene = {}};
            if (active != recordingRoutes_.end() && active->second == wanted)
                continue;

            if (active != recordingRoutes_.end()) {
                stopMidiTake(track.id);
                if (closingMidiTakes_.contains(track.id))
                    continue;
            }
            startMidiTake(track, std::move(*sources));
        }

        const auto active = recordingRoutes_;
        for (const auto& [trackId, unused] : active) {
            juce::ignoreUnused(unused);
            if (closingMidiTakes_.contains(trackId))
                continue;
            if (!eligible.contains(trackId))
                stopMidiTake(trackId);
        }

        std::set<TrackId> audioEligible;
        for (const auto& track : TrackManager::getInstance().getTracks()) {
            auto channels = recordingChannels(track);
            if (!channels)
                continue;

            // A Session target owns this track's recording gesture and its
            // route was reconciled above. Never duplicate it in Arrangement.
            if (sessionSlotTargets_.contains(track.id))
                continue;

            audioEligible.insert(track.id);
            if (closingAudioTakes_.contains(track.id))
                continue;
            const auto activeAudio = audioRecordingRoutes_.find(track.id);
            const ActiveAudioRoute wanted{.input = track.audioInputDevice,
                                          .channels = *channels,
                                          .deviceGeneration =
                                              inputGeneration_.load(std::memory_order_relaxed),
                                          .sessionScene = {}};
            if (activeAudio != audioRecordingRoutes_.end() && activeAudio->second == wanted)
                continue;

            if (activeAudio != audioRecordingRoutes_.end()) {
                stopAudioTake(track.id);
                if (closingAudioTakes_.contains(track.id))
                    continue;
            }
            startAudioTake(track, std::move(*channels));
        }

        const auto activeAudio = audioRecordingRoutes_;
        for (const auto& [trackId, unused] : activeAudio) {
            juce::ignoreUnused(unused);
            if (closingAudioTakes_.contains(trackId))
                continue;
            if (!audioEligible.contains(trackId))
                stopAudioTake(trackId);
        }

        recording_ = sessionCapture_.ownsRecording() ||
                     sessionPunchPending_.load(std::memory_order_acquire) || hasActiveMidiTakes() ||
                     hasActiveAudioTakes() ||
                     std::ranges::any_of(eligible,
                                         [this](const auto trackId) {
                                             return closingMidiTakes_.contains(trackId);
                                         }) ||
                     std::ranges::any_of(audioEligible, [this](const auto trackId) {
                         return closingAudioTakes_.contains(trackId);
                     });
    }

    void reschedulePunchedSessionSlots() {
        if (!punch_.recordingRequested || session_ == nullptr)
            return;
        const auto sync = session_->syncPoint();
        const auto due = punch_.punchInEnabled && punch_.valid()
                             ? std::optional<double>{sync.monotonicAt(punch_.startBeat)}
                             : std::optional<double>{};
        engine::LaunchRequestQueue::Gesture gesture(session_->launchRequests());
        for (const auto& [trackId, target] : sessionSlotTargets_) {
            if (!target.launched)
                continue;
            const auto material = audioRecordingRoutes_.contains(trackId)
                                      ? engine::RecordMaterial::audio
                                      : engine::RecordMaterial::midi;
            const auto* tap = session_->takeTap({trackId, material});
            engine::RecordTap::Reading reading;
            if (tap != nullptr && tap->read(reading) && !reading.recording)
                gesture.play({trackId, target.scene}, due);
        }
    }

    bool startRecording(double positionSeconds, bool armedSlotsWillLaunch = false) {
        if (recording_)
            return recording_;

        // Arm and route edits are coalesced. Record is a boundary: the live
        // epoch must name the takes before its next callback can feed them.
        handleUpdateNowIfNeeded();
        if (session_ == nullptr || !audioRunning_.load(std::memory_order_acquire) ||
            offlineRenders_ > 0)
            return false;

        recording_ = true;
        arrangementRecording_ = true;
        sessionPunchPending_.store(sessionCapture_.hasMaterial(), std::memory_order_release);
        punch_.recordingRequested = true;
        ++punch_.recordingGeneration;
        publishTransport();
        reconcileRecording();
        if (!recording_ && armedSlotsWillLaunch) {
            sources_.registerAvailableDevices();
            auto devices = sources_.deviceSources();
            std::erase_if(devices, [this](auto source) {
                return sources_.slotFor(source) == LiveMidiSources::kNoSlot;
            });
            for (const auto& [trackId, target] : sessionSlotTargets_) {
                const auto* track = TrackManager::getInstance().getTrack(trackId);
                if (track != nullptr &&
                    ClipManager::getInstance().getClipInSlot(trackId, target.scene) ==
                        INVALID_CLIP_ID &&
                    session_->slotRunTarget({trackId, target.scene}) &&
                    (recordingChannels(*track) || recordingSources(*track, devices))) {
                    recording_ = true;
                    break;
                }
            }
        }
        if (!recording_) {
            arrangementRecording_ = false;
            punch_.recordingRequested = false;
            ++punch_.recordingGeneration;
            sessionCapture_.invalidateRecordingGeneration(punch_.recordingGeneration);
            publishTransport();
            return false;
        }

        activePunchGeneration_.store(punch_.recordingGeneration, std::memory_order_release);
        arrangementPunchActive_.store(punch_.recordingRequested, std::memory_order_release);

        if (!request_.playing)
            publishRequest({.playing = true,
                            .locate = true,
                            .positionBeat = map_.timeToBeat(positionSeconds),
                            .countInBeats = countInBeats()});
        return true;
    }

    void resumeRecording() {
        recording_ = true;
        arrangementRecording_ = true;
        sessionPunchPending_.store(sessionCapture_.hasMaterial(), std::memory_order_release);
        punch_.recordingRequested = true;
        ++punch_.recordingGeneration;
        publishTransport();
        reconcileRecording();
        activePunchGeneration_.store(punch_.recordingGeneration, std::memory_order_release);
        arrangementPunchActive_.store(recording_, std::memory_order_release);
    }

    void reconcileFinishedSessionTakes() {
        if (session_ == nullptr)
            return;
        std::vector<TrackId> finishedSlots;
        const auto completedCallbacks = completedCallbacks_.load(std::memory_order_acquire);
        for (const auto& [trackId, target] : sessionSlotTargets_) {
            if (!target.launched)
                continue;
            // A host-requested close owns this target until its correction-sized
            // post-roll has been materialized into the same slot. Removing it
            // here would retire the launch handle between the recording run and
            // the completed clip.
            if (hasClosingTake(trackId))
                continue;
            auto material = std::optional<engine::RecordMaterial>{};
            if (const auto route = audioRecordingRoutes_.find(trackId);
                route != audioRecordingRoutes_.end() && route->second.sessionScene == target.scene)
                material = engine::RecordMaterial::audio;
            else if (const auto route = recordingRoutes_.find(trackId);
                     route != recordingRoutes_.end() && route->second.sessionScene == target.scene)
                material = engine::RecordMaterial::midi;
            if (!material)
                continue;

            const auto* tap = session_->takeTap({trackId, *material});
            engine::RecordTap::Reading reading;
            if (tap == nullptr || !tap->read(reading))
                continue;
            const auto* launch = session_->launchTap({trackId, target.scene});
            const auto launchReading =
                launch != nullptr ? launch->read() : engine::LaunchTap::Reading{};
            const auto launchInactive = !launchReading.playing && !launchReading.holdsSection &&
                                        launchReading.queued == engine::LaunchTap::Queued::nothing;
            const auto callbackAcknowledged = completedCallbacks > target.launchedAtCallback;
            if ((reading.pass > 0 && !reading.recording) ||
                (reading.pass == 0 && callbackAcknowledged && launchInactive))
                finishedSlots.push_back(trackId);
        }
        for (const auto trackId : finishedSlots) {
            const auto target = sessionSlotTargets_.find(trackId);
            if (target == sessionSlotTargets_.end())
                continue;
            const engine::SlotKey key{trackId, target->second.scene};
            sessionSlotTargets_.erase(target);
            const auto handedOver = audioRecordingRoutes_.contains(trackId)
                                        ? stopAudioTake(trackId, true, true, false)
                                        : stopMidiTake(trackId);
            if (!handedOver) {
                engine::LaunchRequestQueue::Gesture gesture(session_->launchRequests());
                gesture.backToArrangement(key);
            }
        }
        if (!finishedSlots.empty()) {
            launcher_.recordTargetsChanged();
            recording_ = sessionCapture_.ownsRecording() ||
                         sessionPunchPending_.load(std::memory_order_acquire) ||
                         hasActiveMidiTakes() || hasActiveAudioTakes();
            publishClips();
        }
    }

    const std::unordered_map<TrackId, RecordingPreview>& recordingPreviews() {
        reconcileFinishedSessionTakes();
        if (session_ == nullptr || !recording_) {
            previewReadings_.clear();
            recordingPreviews_.clear();
            return recordingPreviews_;
        }

        for (auto at = previewReadings_.begin(); at != previewReadings_.end();) {
            if (!previewTakeActive(at->first))
                at = previewReadings_.erase(at);
            else
                ++at;
        }

        const auto refresh = [this](const engine::TakeKey& key) {
            const auto* tap = session_->takeTap(key);
            if (tap == nullptr) {
                previewReadings_.erase(key);
                return;
            }

            auto& reading = previewReadings_[key];
            tap->read(reading);
            if (reading.pass == 0 || reading.material != key.material)
                previewReadings_.erase(key);
        };

        for (const auto& [trackId, route] : audioRecordingRoutes_) {
            juce::ignoreUnused(route);
            if (closingAudioTakes_.contains(trackId))
                continue;
            refresh({trackId, engine::RecordMaterial::audio});
        }
        for (const auto& [trackId, route] : recordingRoutes_) {
            juce::ignoreUnused(route);
            if (closingMidiTakes_.contains(trackId))
                continue;
            refresh({trackId, engine::RecordMaterial::midi});
        }

        recordingPreviews_.clear();
        for (const auto& [trackId, route] : recordingRoutes_) {
            if (closingMidiTakes_.contains(trackId))
                continue;
            const auto reading = previewReadings_.find({trackId, engine::RecordMaterial::midi});
            if (reading == previewReadings_.end())
                continue;

            auto& preview = recordingPreviews_[trackId];
            preview.trackId = trackId;
            const auto scene = route.sessionScene;
            preview.target =
                scene ? RecordingTargetKind::SessionSlot : RecordingTargetKind::Arrangement;
            preview.sceneIndex = scene.value_or(-1);
            preview.startBeat = reading->second.startBeat;
            preview.currentLengthBeats = reading->second.lengthBeats;
            preview.isAudioRecording = false;
            preview.notes.resize(reading->second.notes.size());
            std::ranges::transform(reading->second.notes, preview.notes.begin(),
                                   [](const auto& note) {
                                       return MidiNote{.noteNumber = note.noteNumber,
                                                       .velocity = note.velocity,
                                                       .startBeat = note.startBeat,
                                                       .lengthBeats = note.lengthBeats};
                                   });
        }

        // The public surface has one preview per track. When both material
        // types record, the waveform is the useful visual and wins without
        // destroying the MIDI reading kept above.
        for (const auto& [trackId, route] : audioRecordingRoutes_) {
            if (closingAudioTakes_.contains(trackId))
                continue;
            const auto reading = previewReadings_.find({trackId, engine::RecordMaterial::audio});
            if (reading == previewReadings_.end())
                continue;

            auto& preview = recordingPreviews_[trackId];
            preview.trackId = trackId;
            const auto scene = route.sessionScene;
            preview.target =
                scene ? RecordingTargetKind::SessionSlot : RecordingTargetKind::Arrangement;
            preview.sceneIndex = scene.value_or(-1);
            preview.startBeat = reading->second.startBeat;
            preview.currentLengthBeats = reading->second.lengthBeats;
            preview.isAudioRecording = true;
            preview.notes.clear();
            preview.audioPeaks.resize(reading->second.peaks.size());
            std::ranges::transform(
                reading->second.peaks, preview.audioPeaks.begin(), [](const auto& peak) {
                    return AudioPeakSample{.peakL = peak.left, .peakR = peak.right};
                });
        }

        return recordingPreviews_;
    }

    /// What every track plays, resolved against the tempo the transport is
    /// published with: a snapshot compiled against a different map would place
    /// every clip at the seconds that map gave it.
    void publishClips() {
        if (session_ == nullptr)
            return;

        // A publish can retire and complete a captured run. One extra pass puts
        // the resulting Arrangement clip in the same live model transition.
        for (auto pass = 0; pass < 2; ++pass) {
            const auto& tracks = TrackManager::getInstance().getTracks();
            auto lanes = lanesAsPlayed(clipLanesFor(tracks), tracks, frozen_, tempoMap());
            for (auto& lane : lanes) {
                // Live handles own sample-exact section boundaries; model mode only reports
                // them (#2725).
                lane.playbackMode = TrackPlaybackMode::Arrangement;
                if (const auto target = sessionSlotTargets_.find(lane.trackId);
                    target != sessionSlotTargets_.end())
                    lane.recordSlots.push_back(target->second.scene);
            }
            sessionCapture_.prepare(lanes, tempoMap().bpmAt(0.0));
            auto snapshot = std::make_shared<const engine::ClipSnapshot>(
                engine::compileClipSnapshot(lanes, clipSources(), tempoMap()));
            report("clips", snapshot->diagnostics);

            traceEdit(EngineTrace::Kind::Publish);
            session_->publishClips(std::move(snapshot));
            const auto created = sessionCapture_.published();
            if (!created)
                break;
        }
    }

    void publishTransport() {
        if (session_ == nullptr)
            return;

        session_->publishTransport({.tempo = tempoMap(),
                                    .loop = loop_,
                                    .punch = punch_,
                                    .click = click_,
                                    .request = request_});
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
        forgetProject();
    }

    /// Before the first publish reads the loaded model.
    void projectOpened(const ProjectInfo&) override {
        unfreezeTracksWithoutFiles();
    }

    /// What live playback compiles: the model, with each frozen track playing its file.
    std::vector<TrackInfo> playedTracks() const {
        return tracksAsPlayed(TrackManager::getInstance().getTracks(), frozen_);
    }

    /// Whether a track froze, thawed or lost its file since the last reading.
    bool refreshFrozenTracks() {
        auto frozen = frozenTracksWithFiles(TrackManager::getInstance().getTracks());
        if (frozen == frozen_)
            return false;

        frozen_ = std::move(frozen);
        return true;
    }

    void forgetProject() {
        stopRecording(false, false);
        if (session_ != nullptr) {
            // Track and clip IDs restart in the next project. Retire every old
            // handle before the capture forgets their incarnation and source.
            session_->publishClips(nullptr);
            sessionCapture_.published();
            sessionCapture_.attach(*session_);
        }
        factory_.forgetBuiltDevices();
        routing_.reset();
        devicePaths_.clear();

        // Before the next project names the same addresses, since the first
        // tick that reports over them is a frame away (#2570).
        if (deviceMeters_ != nullptr)
            deviceMeters_->clear();
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
    // A property is the kind of edit that can move the plan's shape -- a
    // bypass takes the device out of it, a route moves an edge -- so these say
    // so, and publishValues() checks. A parameter cannot: that is what values
    // are for.
    void trackPropertyChanged(int) override {
        wantValues(Shape::MayHaveMoved);
    }
    void masterChannelChanged() override {
        wantValues(Shape::MayHaveMoved);
    }
    void devicePropertyChanged(const ChainNodePath&) override {
        wantValues(Shape::MayHaveMoved);
    }
    void deviceParameterChanged(const ChainNodePath&, int, float) override {
        wantValues(Shape::Unchanged);
    }
    void deviceModifiersChanged(TrackId) override {
        wantValues(Shape::MayHaveMoved);
    }
    /// A macro's value is a base the table carries.
    void macroValueChanged(TrackId, ChainScope, int, int, float) override {
        wantValues(Shape::Unchanged);
    }
    /// Lanes are read where the table is compiled, and drawing one is what
    /// makes its parameter one the table carries.
    void automationLanesChanged() override {
        wantValues(Shape::MayHaveMoved);
    }
    void automationLanePropertyChanged(AutomationLaneId) override {
        wantValues(Shape::MayHaveMoved);
    }
    void automationPointsChanged(AutomationLaneId) override {
        wantValues(Shape::Unchanged);
    }
    void automationClipsChanged(AutomationLaneId) override {
        wantValues(Shape::Unchanged);
    }
    /// Bindings decide which hosted slots have model-owned base values.
    void bindingRegistryChanged(BindingScope) override {
        wantValues(Shape::MayHaveMoved);
    }

    void clipsChanged() override {
        wantClips();
    }
    void clipPropertyChanged(ClipId) override {
        wantClips();
    }

    /// The session grid's launches, which is the one model signal that is an
    /// order rather than an edit: it asks for something to happen at a beat
    /// rather than describing what to publish (#2552).
    void clipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) override {
        if (request == ClipPlaybackRequest::Play)
            launcher_.launch(clipId);
        else
            launcher_.stop(clipId);
    }

    // ===== LaunchHost =====

    engine::EngineSession* launchSession() override {
        return session_.get();
    }

    const engine::TempoMap& launchTempo() const override {
        return map_;
    }

    double launchBeatsPerBar() const override {
        return static_cast<double>(numerator_) * 4.0 / static_cast<double>(denominator_);
    }

    /// The count-in in the clock's quarter-note beats. A beat of the count is
    /// the signature's, so 6/8 counts in eighths.
    double countInBeats() const {
        const auto beat = 4.0 / static_cast<double>(denominator_);
        switch (countInMode_) {
            case 1:
                return launchBeatsPerBar();
            case 2:
                return 2.0 * launchBeatsPerBar();
            case 3:
                return 2.0 * beat;
            case 4:
                return beat;
            default:
                return 0.0;
        }
    }

    bool launchTransportPlaying() const override {
        return request_.playing;
    }
    std::optional<engine::SlotKey> launchRecordTarget(TrackId trackId) const override {
        const auto found = sessionSlotTargets_.find(trackId);
        if (found == sessionSlotTargets_.end() || !found->second.launched)
            return std::nullopt;
        return engine::SlotKey{trackId, found->second.scene};
    }

    void startLaunchTransport() override {
        publishRequest({.playing = true, .locate = false});
    }

    void wantPlan() {
        requests_.fetch_add(1, std::memory_order_relaxed);
        plan_.store(true, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
    void wantValues(Shape shape) {
        requests_.fetch_add(1, std::memory_order_relaxed);
        if (shape == Shape::MayHaveMoved)
            shape_.store(true, std::memory_order_relaxed);

        values_.store(true, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
    std::uint64_t publishRequests() const {
        return requests_.load(std::memory_order_relaxed);
    }

    bool isSettled() const {
        return session_ != nullptr && !isUpdatePending() &&
               hardwareOutputReadyGeneration_.load(std::memory_order_acquire) ==
                   hardwareOutputGeneration_.load(std::memory_order_acquire);
    }

    void wantClips() {
        clips_.store(true, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }

    /// Everything the model has asked for since the last one. A plan publish
    /// resolves values on its way through, so the two are alternatives; clips
    /// travel on their own and are published beside either.
    void handleAsyncUpdate() override {
        // Held rather than dropped: the flags stay set, and the end of the render asks again.
        if (offlineRenders_ > 0)
            return;

        const auto outputGeneration = hardwareOutputGeneration_.load(std::memory_order_acquire);
        if (hardwareOutputsStale_.exchange(false, std::memory_order_acq_rel) &&
            refreshHardwareOutputMap())
            plan_.store(true, std::memory_order_relaxed);
        const auto inputsRefreshed =
            hardwareInputsStale_.exchange(false, std::memory_order_acq_rel);
        const auto inputsMoved = inputsRefreshed && refreshHardwareInputMap();

        // A physical device stop has no block in which a take can close. An
        // intentional remove/add during rebuild reaches this handler only
        // after the callback has been installed again.
        if ((recording_ || !closingAudioTakes_.empty() || !closingMidiTakes_.empty()) &&
            !audioRunning_.load(std::memory_order_acquire))
            stopRecording(true, false);
        if (arrangementRecording_ &&
            punchedOutGeneration_.load(std::memory_order_acquire) == punch_.recordingGeneration)
            stopRecording();

        if (session_ != nullptr && session_->takeCompletionPending() && harvestClosedTakes()) {
            publishClips();
            if (recording_)
                reconcileRecording();
        }

        const engine::RenderContext wanted{.sampleRate = rate_.load(),
                                           .maxBlockSize = blockSize_.load(),
                                           .numChannels = kChannels};
        if (wanted.sampleRate <= 0.0 || wanted.maxBlockSize <= 0)
            return;

        // The feed only grows while no callback runs, so more inputs is a rebuild.
        if (session_ == nullptr || wanted != context_ ||
            scratch_.getNumChannels() != hardwareOutputChannels_ ||
            session_->liveInputs().preparedChannels() <
                inputChannels_.load(std::memory_order_relaxed)) {
            rebuild(wanted);
            return;
        }

        // A freeze changes the plan's shape and the lanes together.
        const auto frozenMoved = refreshFrozenTracks();
        if (frozenMoved)
            shape_.store(true, std::memory_order_relaxed);

        // All three taken before any of them runs, so a plan publish clears the
        // values it already resolved rather than leaving them to be published
        // again at whatever the next edit turns out to be.
        const auto plan = plan_.exchange(false);
        const auto values = values_.exchange(false) || frozenMoved;
        const auto clips = clips_.exchange(false) || frozenMoved;

        // Before either publish reads the model, so the table it compiles
        // carries whatever the edit just addressed (#2635).
        if (plan || values)
            syncMirroredParameters();

        if (plan)
            publishPlan();
        else if (values)
            publishValues();

        if (clips)
            publishClips();

        // Before the output gate opens below, so a restarted device's first
        // block reads its inputs where they are now.
        if (inputsRefreshed)
            reconcileRecording();
        if (inputsMoved)
            publishRouting(playedTracks());

        if (renderedStale_.exchange(false, std::memory_order_acq_rel))
            markRenderedDevices();

        if (hardwareOutputGeneration_.load(std::memory_order_acquire) == outputGeneration &&
            livePlan_ != nullptr && publishedHardwareOutputs_ == hardwareOutputs_) {
            hardwareOutputReadyGeneration_.store(outputGeneration, std::memory_order_release);
        } else if (hardwareOutputGeneration_.load(std::memory_order_acquire) != outputGeneration) {
            hardwareOutputsStale_.store(true, std::memory_order_release);
            triggerAsyncUpdate();
        }
    }

    /**
     * @brief What a plugin said one of its parameters holds, fanned out.
     *
     * The cache and the knob take every observation, since a parameter the
     * document knows nothing about still has to draw itself. The document
     * takes only a gesture in the plugin's own editor, and only as the base of
     * a parameter it holds (docs/specs/hosted-plugin-parameter-control.md).
     */
    void observePluginParameter(engine::DeviceKey key,
                                adapter::EngineExternalDevice::Observation observation) {
        const auto path = publishObservation(key, observation);
        if (!path.isValid() || observation.source != ObservationSource::EditorGesture)
            return;

        // setDeviceParameterValueFromPlugin does nothing for a slot the
        // document holds no value for, and that is the rule, not an accident.
        auto* device = modelDevice(key);
        if (device != nullptr && device->findParameterByIndex(observation.slot) != nullptr)
            TrackManager::getInstance().setDeviceParameterValueFromPlugin(path, observation.slot,
                                                                          observation.normalised);
    }

    /// Into the cache and out to the UI. The path it went to, invalid for a
    /// device the model no longer has.
    ChainNodePath publishObservation(
        engine::DeviceKey key, const adapter::EngineExternalDevice::Observation& observation) {
        auto& tracks = TrackManager::getInstance();
        const auto path = tracks.findDevicePath(key.deviceId, key.segment);
        if (!path.isValid())
            return path;

        observed_[key][observation.slot] = observation.normalised;
        tracks.notifyDeviceParameterObserved(path, observation.slot, observation.normalised,
                                             observation.source);
        return path;
    }

    /// The last value each plugin reported per slot. Runtime only: the plugin
    /// owns these, and its chunk carries them into a save.
    std::map<engine::DeviceKey, std::map<int, float>> observed_;

    /// Accepted edits not yet completed, per slot. Shared with each completion,
    /// which can outlive this.
    std::shared_ptr<std::map<std::pair<engine::DeviceKey, int>, int>> pendingEdits_ =
        std::make_shared<std::map<std::pair<engine::DeviceKey, int>, int>>();

    /**
     * @brief Mirror the slots something addresses, for every plugin this renders.
     *
     * The model carries a hosted plugin's value only where something reaches it
     * (#2629); the chunk carries the rest. The instance is asked to describe
     * itself only for a device that has gained a slot, since describing one is
     * a call per parameter.
     */
    void syncMirroredParameters() {
        if (session_ == nullptr)
            return;

        auto& tracks = TrackManager::getInstance();
        const auto addressed = addressedInOpenProject();

        for (const auto key : factory_.externalKeys()) {
            auto* device = modelDevice(key);
            if (device == nullptr)
                continue;

            const auto path = tracks.findDevicePath(key.deviceId, key.segment);
            const auto slots = addressed.forDevice(path);

            const auto gained = std::ranges::any_of(slots, [device](int slot) {
                return device->findParameterByIndex(slot) == nullptr;
            });

            std::vector<ParameterInfo> described;
            if (gained) {
                auto* external = externalDeviceFor(key);
                if (external == nullptr)
                    continue;

                described = external->describeParameters().parameters;
            }

            mirrorAddressedParameters(*device, described, slots);
        }
    }

    /**
     * @brief Let each plugin forget what was driving it, which the plan decides.
     *
     * After a publish rather than before one: a device the new plan dropped
     * renders no block, so nothing else would clear the driver state its last
     * one left behind.
     */
    void forgetPluginDriverState() {
        if (session_ == nullptr)
            return;

        for (const auto key : factory_.externalKeys())
            if (auto* external = externalDeviceFor(key))
                external->forgetDriverState();
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

        const auto resumeAfterRebuild = arrangementRecording_;
        stopRecording(true, false);

        sessionCapture_.reset();
        session_.reset();
        voiceThread_.reset();
        voices_.reset();
        livePlan_.reset();
        publishedHardwareOutputs_.clear();

        context_ = context;
        scratch_.setSize(hardwareOutputChannels_, context.maxBlockSize);

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
        sessionCapture_.attach(*session_);
        factory_.attach(session_->clipFeed(), voices_->feed(), session_->launchHandleFeed(),
                        session_->liveInputs());

        // The publish below fills the new feed with a full snapshot.
        routing_.reset();

        session_->liveInputs().prepare(inputChannels_.load(std::memory_order_relaxed),
                                       context.maxBlockSize);
        prepareLiveMidi();
        refreshFrozenTracks();

        plan_.store(false, std::memory_order_relaxed);
        values_.store(false, std::memory_order_relaxed);
        shape_.store(false, std::memory_order_relaxed);
        clips_.store(false, std::memory_order_relaxed);

        publishTransport();
        publishPlan();
        publishClips();

        // Installing the callback synchronously publishes the device's new
        // latency and generation. Resume against that epoch so the follow-up
        // device update cannot immediately split the replacement take.
        devices_->addAudioCallback(this);

        if (resumeAfterRebuild)
            resumeRecording();
    }

    // ===== Offline renders (#2555) =====

    void beginOfflineRender() override {
        if (offlineRenders_++ > 0)
            return;

        stopRecording(true, false);
        if (request_.playing)
            publishRequest({.playing = false, .locate = false});

        // removeAudioCallback returns once the callback has left, so a render
        // borrowing a device never shares it with a block.
        if (devices_ != nullptr)
            devices_->removeAudioCallback(this);
    }

    void endOfflineRender(bool resumePlayback) override {
        if (--offlineRenders_ > 0)
            return;

        // Structural, so the live executor prepares its devices' MIDI bounds again.
        if (session_ != nullptr)
            publishPlan(livePlan_);

        if (devices_ != nullptr)
            devices_->addAudioCallback(this);

        if (resumePlayback)
            publishRequest({.playing = true, .locate = false});

        triggerAsyncUpdate();
    }

    std::shared_ptr<engine::EngineDevice> liveDevice(engine::DeviceKey key) const override {
        return session_ != nullptr ? session_->device(key) : nullptr;
    }

    std::optional<engine::RenderContext> liveContext() const override {
        return session_ != nullptr ? std::optional(context_) : std::nullopt;
    }

    engine::TempoMap renderTempo() const override {
        return map_;
    }

    adapter::ExternalPluginServices pluginServices() const override {
        return {.formats = formats_, .knownPlugins = knownPlugins_, .context = context_};
    }

    // ===== The device =====

    bool refreshHardwareOutputMap() {
        juce::BigInteger active;
        if (devices_ != nullptr)
            if (auto* device = devices_->getCurrentAudioDevice())
                active = device->getActiveOutputChannels();

        const auto hasProvider = static_cast<bool>(hardwareOutputProvider_);
        auto catalog =
            hasProvider ? hardwareOutputProvider_() : EngineHost::HardwareChannelCatalog{};
        auto enabled = catalog.enabledChannels;
        if (!hasProvider && enabled.isZero())
            enabled = active;

        std::vector<int> physicalChannels;
        for (auto channel = 0; channel <= active.getHighestBit(); ++channel)
            if (active[channel] && enabled[channel])
                physicalChannels.push_back(channel);

        const auto packed = [&active](int physical) {
            auto index = 0;
            for (auto channel = 0; channel < physical; ++channel)
                if (active[channel])
                    ++index;
            return index;
        };
        const auto route = [&packed](const std::vector<int>& channels) {
            return engine::HardwareOutputRoute{
                .leftChannel = packed(channels.front()),
                .rightChannel = channels.size() == 2 ? packed(channels.back()) : -1};
        };

        std::map<std::string, engine::HardwareOutputRoute> resolved;
        for (std::size_t i = 0; i < physicalChannels.size();) {
            const auto first = physicalChannels[i];
            if (i + 1 < physicalChannels.size()) {
                resolved["stereo:Out " + std::to_string(first + 1)] =
                    route({first, physicalChannels[i + 1]});
                i += 2;
            } else {
                resolved["Out " + std::to_string(first + 1)] = route({first});
                ++i;
            }
        }

        struct NamedOutput {
            juce::String name;
            std::vector<int> channels;
        };
        std::vector<NamedOutput> named;
        if (!catalog.namesByChannel.empty()) {
            for (const auto channel : physicalChannels) {
                const auto found = catalog.namesByChannel.find(channel);
                const auto name = found != catalog.namesByChannel.end()
                                      ? found->second
                                      : "Out " + juce::String(channel + 1);
                if (!named.empty() && named.back().name == name &&
                    named.back().channels.size() < 2) {
                    named.back().channels.push_back(channel);
                } else {
                    named.push_back({name, {channel}});
                }
            }

            std::set<std::string> exactNames;
            for (const auto& output : named) {
                const auto name = output.name.toStdString();
                if (!exactNames.insert(name).second)
                    continue;

                const auto destination = route(output.channels);
                resolved[name] = destination;
                if (output.channels.size() == 2)
                    resolved["stereo:" + name] = destination;
            }
        }

        const auto changed = resolved != hardwareOutputs_;
        hardwareOutputs_ = std::move(resolved);
        hardwareOutputChannels_ = std::max(kChannels, active.countNumberOfSetBits());
        return changed;
    }

    /// Whether the input map moved. Channels are packed by the device's active
    /// mask, so a restart can move them without any name changing.
    bool refreshHardwareInputMap() {
        juce::BigInteger active;
        if (devices_ != nullptr)
            if (auto* device = devices_->getCurrentAudioDevice())
                active = device->getActiveInputChannels();

        const auto catalog = hardwareInputProvider_ ? hardwareInputProvider_()
                                                    : EngineHost::HardwareChannelCatalog{};
        const auto enabled = hardwareInputProvider_
                                 ? std::optional<juce::BigInteger>(catalog.enabledChannels)
                                 : std::nullopt;
        auto resolved = resolveHardwareInputs(enabled, catalog.namesByChannel, active);

        const auto changed = resolved != hardwareInputs_;
        hardwareInputs_ = std::move(resolved);
        return changed;
    }

    void requestHardwareInputRefresh() {
        hardwareInputsStale_.store(true, std::memory_order_release);
        triggerAsyncUpdate();
    }

    void requestHardwareOutputRefresh() {
        hardwareOutputGeneration_.fetch_add(1, std::memory_order_acq_rel);
        hardwareOutputsStale_.store(true, std::memory_order_release);
        triggerAsyncUpdate();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        // Only what the device is. Building a session allocates and publishing
        // one waits, and both belong to the publishing thread.
        const auto sampleRate = device->getCurrentSampleRate();
        rate_.store(sampleRate);
        blockSize_.store(device->getCurrentBufferSizeSamples());
        inputChannels_.store(device->getActiveInputChannels().countNumberOfSetBits());
        const auto reportedAdjustment =
            static_cast<std::int64_t>(device->getInputLatencyInSamples()) +
            static_cast<std::int64_t>(device->getOutputLatencyInSamples());
        const auto limit = static_cast<std::int64_t>(std::clamp(
            sampleRate * 3.0, 0.0, static_cast<double>(std::numeric_limits<int>::max())));
        deviceRecordingAdjustmentSamples_.store(
            static_cast<int>(std::clamp(reportedAdjustment, -limit, limit)),
            std::memory_order_relaxed);
        deviceOutputAdjustmentSamples_.store(
            static_cast<int>(
                std::clamp<std::int64_t>(device->getOutputLatencyInSamples(), -limit, limit)),
            std::memory_order_relaxed);
        inputGeneration_.fetch_add(1, std::memory_order_relaxed);
        audioRunning_.store(true, std::memory_order_release);
        renderedStale_.store(true, std::memory_order_release);
        hardwareOutputGeneration_.fetch_add(1, std::memory_order_acq_rel);
        hardwareOutputsStale_.store(true, std::memory_order_release);
        hardwareInputsStale_.store(true, std::memory_order_release);
        triggerAsyncUpdate();

        if (EngineTrace::enabled())
            EngineTrace::print(
                "device \"" + device->getName() + "\" at " +
                juce::String(device->getCurrentSampleRate(), 0) + " Hz, " +
                juce::String(device->getCurrentBufferSizeSamples()) + " samples, " +
                juce::String(device->getActiveOutputChannels().countNumberOfSetBits()) +
                " outputs");
    }

    /// Also called by removeAudioCallback, so a rebuild passes through here.
    void audioDeviceStopped() override {
        audioRunning_.store(false, std::memory_order_release);
        renderedStale_.store(true, std::memory_order_release);
        hardwareOutputGeneration_.fetch_add(1, std::memory_order_acq_rel);
        triggerAsyncUpdate();
    }

    void audioDeviceIOCallbackWithContext(const float* const* input, int numInputChannels,
                                          float* const* output, int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        for (auto channel = 0; channel < numOutputChannels; ++channel)
            if (output[channel] != nullptr)
                juce::FloatVectorOperations::clear(output[channel], numSamples);

        const auto outputGeneration = hardwareOutputGeneration_.load(std::memory_order_acquire);
        if (outputGeneration == 0 ||
            hardwareOutputReadyGeneration_.load(std::memory_order_acquire) != outputGeneration)
            return;

        const auto callback = rendered_.fetch_add(1, std::memory_order_relaxed) + 1;

        // Read without a guard because there is nothing to guard against:
        // rebuild() takes this callback off the device before it touches the
        // session, and removeAudioCallback returns only once this has left.
        if (session_ == nullptr || numSamples <= 0)
            return;

        const auto outputs = std::min(numOutputChannels, scratch_.getNumChannels());
        const auto streams = collectLiveMidi();
        const auto audioIn = liveAudioIn(input, numInputChannels, numSamples);

        // In pieces no longer than the plan was prepared for. A driver handing
        // over more than the block size it declared is rare and legal, and
        // rendering it in one go is a buffer overrun rather than a wrong sound.
        for (auto done = 0; done < numSamples;) {
            const auto piece = std::min(numSamples - done, scratch_.getNumSamples());

            // The callback's live MIDI belongs to its first piece alone: every
            // event is stamped at offset 0, so handing the streams to a second
            // piece would sound each note again.
            const auto pieceIn = audioIn.getNumChannels() > 0
                                     ? audioIn.getSubBlock(static_cast<std::size_t>(done),
                                                           static_cast<std::size_t>(piece))
                                     : juce::dsp::AudioBlock<const float>{};
            session_->process(piece, scratch_,
                              done == 0 ? engine::LiveInputBlock{pieceIn, streams}
                                        : engine::LiveInputBlock{pieceIn, {}});

            if (session_->takeCompletionPending())
                triggerAsyncUpdate();

            const auto activeGeneration = activePunchGeneration_.load(std::memory_order_acquire);
            if (arrangementPunchActive_.load(std::memory_order_acquire) &&
                session_->punchOutGeneration() == activeGeneration) {
                punchedOutGeneration_.store(activeGeneration, std::memory_order_release);
                triggerAsyncUpdate();
            }

            for (auto channel = 0; channel < outputs; ++channel)
                if (output[channel] != nullptr)
                    juce::FloatVectorOperations::copy(output[channel] + done,
                                                      scratch_.getReadPointer(channel), piece);
            done += piece;
        }
        completedCallbacks_.store(callback, std::memory_order_release);
    }

    /// The device's input for this callback, or none when a channel is missing.
    static juce::dsp::AudioBlock<const float> liveAudioIn(const float* const* input, int channels,
                                                          int numSamples) {
        if (input == nullptr || channels <= 0 ||
            std::any_of(input, input + channels, [](const float* c) { return c == nullptr; }))
            return {};

        return {input, static_cast<std::size_t>(channels), static_cast<std::size_t>(numSamples)};
    }

    /// Room for every source the callback may see. Off the device, from
    /// rebuild(), because it allocates.
    void prepareLiveMidi() {
        collector_.prepare();
    }

    /// The queue's events as the streams the session renders. Audio thread.
    std::span<const engine::LiveMidiStream> collectLiveMidi() {
        return collector_.collect(queue_, sources_);
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

    DeviceInfo* modelDevice(engine::DeviceKey key) {
        return modelDeviceAt(key);
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

        // Asked with the segment, because a DeviceId is section-local and on
        // its own names up to three devices (#1899).
        const auto path = TrackManager::getInstance().findDevicePath(key.deviceId, key.segment);

        // Here rather than where the parameters were described, because this is
        // where the device's address is known: a DeviceInfo does not say which
        // section holds it, and a provider that had to look the id up again
        // would find the wrong device (#2600).
        attachParameterTextProviders(*device, path);

        if (path.isValid()) {
            TrackManager::getInstance().notifyDevicePropertyChanged(path);

            // The chain rebuilds its slots on this one, and a slot built
            // before its plugin loaded holds an empty array until it does
            // (#2617).
            TrackManager::getInstance().notifyTrackDevicesChanged(path.trackId);
        }

        // The instance is here, so what it reports can be mirrored for the
        // slots something addresses and dropped for the rest (#2635).
        syncMirroredParameters();

        // Last, so the plan that binds the loader's instance is compiled from
        // the model as corrected above.
        wantPlan();
    }

    // ===== What the plugins hold =====

    /**
     * @brief Read every external plugin this renders back into the model.
     *
     * A key with no plugin bound is logged and skipped, so a plugin that is
     * still loading keeps its saved state (#2581).
     */
    void captureExternalPluginStates() {
        if (session_ == nullptr)
            return;

        for (const auto key : factory_.externalKeys())
            requestCapture(key);

        // drain() rather than returning to the message loop: the caller
        // writes the project file as soon as this returns.
        control_->drain();
    }

    // Message-thread menu operations. Drain before returning to the caller;
    // commit the resulting patch only onto the assignment that requested it.
    adapter::PresetOutcome pluginPreset(const ChainNodePath& path,
                                        adapter::PresetRequest operation) {
        const auto key = keyOfDeviceAt(path);
        if (!key || !factory_.isExternalKey(*key))
            return {.failure = "no hosted plugin at this path"};
        operation.assignment = loader_.request(*key);
        const auto assignment = *operation.assignment;
        adapter::PresetOutcome result{.failure = "the control plane rejected the preset request"};
        if (plane_.pluginPreset(*key, std::move(operation), [&](adapter::PresetOutcome outcome) {
                if (outcome.ok() && outcome.snapshot) {
                    if (!adapter::commitCapturedState(assignment, *outcome.snapshot, modelDeviceAt))
                        outcome.failure =
                            "the plugin assignment changed before its state was captured";
                    else
                        TrackManager::getInstance().notifyDevicePropertyChanged(path);
                }
                result = std::move(outcome);
            }))
            control_->drain();
        if (!result.ok())
            juce::Logger::writeToLog("[engine] preset: " + result.failure);
        return result;
    }

    /**
     * @brief The editor of the device at @p devicePath, and what it is now (#2580).
     *
     * Synchronous: every caller is a click whose slot draws the answer, and
     * the drain is what makes it arrive before this returns.
     */
    bool deviceEditor(const ChainNodePath& devicePath, adapter::EditorAction action) {
        const auto key = keyOfDeviceAt(devicePath);
        if (!key.has_value() || !factory_.isExternalKey(*key))
            return false;

        auto showing = false;
        const auto asked = plane_.editorWindow(*key, action, [&showing](adapter::EditorOutcome it) {
            // Said out loud: a click that opened nothing has no other way to
            // report why.
            if (!it.ok()) {
                juce::Logger::writeToLog("[engine] editor: " + it.failure());
                return;
            }

            showing = it.isShowing();
        });

        if (!asked)
            return false;

        // The completion writes a local, so it must have run before this
        // returns. drain() is what says it has.
        control_->drain();
        return showing;
    }

    /**
     * @brief The plugin's own text for one parameter value (#2600).
     *
     * Straight to the device rather than through the plane: this is called
     * from a paint, and the plane queues everything it is given
     * (ControlExecutor.hpp). Safe because it is a query -- nothing is
     * suspended and nothing moves -- and because the message thread this runs
     * on is the control executor's own, so it overlaps no operation that does.
     */
    juce::String formatDeviceParameter(const ChainNodePath& devicePath, int paramIndex,
                                       float normalised) const {
        auto* external = externalDeviceAt(devicePath);
        return external != nullptr ? external->parameterText(paramIndex, normalised)
                                   : juce::String{};
    }

    /** @brief The plugin rendering at @p devicePath, or null. Message thread. */
    adapter::EngineExternalDevice* externalDeviceAt(const ChainNodePath& devicePath) const {
        const auto key = keyOfDeviceAt(devicePath);
        return key.has_value() ? externalDeviceFor(*key) : nullptr;
    }

    /** @brief The plugin the live session renders for @p key, or null. */
    adapter::EngineExternalDevice* externalDeviceFor(engine::DeviceKey key) const {
        if (session_ == nullptr)
            return nullptr;

        auto held = session_->device(key);
        return held != nullptr ? externalIn(*held) : nullptr;
    }

    HostParameters describeDeviceParameters(const ChainNodePath& devicePath) const {
        auto* external = externalDeviceAt(devicePath);
        return external != nullptr ? external->describeParameters() : HostParameters{};
    }

    /**
     * @brief The MAGDA device rendering at @p devicePath, or null (#2585).
     *
     * Addressed off the same map the slot meters read, so the device a
     * faceplate draws and the device it reads telemetry off cannot come from
     * two walks that disagree -- and so a repaint costs a scan of the project's
     * devices rather than a walk of its chains.
     *
     * The store's lease comes back with it: a UI reading a ring holds the
     * instance open for the duration of the read, whatever a publish does to
     * the plan in between.
     */
    std::shared_ptr<audio::MagdaDevice> renderedDevice(const ChainNodePath& devicePath) const {
        if (session_ == nullptr || !devicePath.isValid())
            return {};

        const auto slot = std::ranges::find(devicePaths_, devicePath,
                                            [](const auto& entry) { return entry.second; });
        if (slot == devicePaths_.end())
            return {};

        auto held = session_->device(slot->first);
        if (held == nullptr)
            return {};

        auto* device = magdaIn(*held);
        return device != nullptr ? std::shared_ptr<audio::MagdaDevice>{std::move(held), device}
                                 : nullptr;
    }

    std::optional<float> observedParameter(const ChainNodePath& devicePath, int paramIndex) const {
        const auto key = keyOfDeviceAt(devicePath);
        if (!key.has_value())
            return std::nullopt;

        const auto device = observed_.find(*key);
        if (device == observed_.end())
            return std::nullopt;

        const auto slot = device->second.find(paramIndex);
        return slot == device->second.end() ? std::nullopt : std::optional{slot->second};
    }

    bool hostedEditPending(const ChainNodePath& devicePath, int paramIndex) const {
        const auto key = keyOfDeviceAt(devicePath);
        return key.has_value() && pendingEdits_->contains({*key, paramIndex});
    }

    /**
     * @brief Hand a hosted parameter a value of its own, through the plane.
     *
     * Nothing is written here and nothing is published: the plugin owns this
     * parameter's value, and the document is not involved
     * (docs/specs/hosted-plugin-parameter-control.md).
     */
    EditReceipt editHostedParameter(const ChainNodePath& devicePath, int paramIndex,
                                    float normalised, EditOrigin,
                                    std::function<void(EditCompletion)> completed) {
        const EditReceipt refused{.status = EditStatus::Unavailable, .requested = normalised};

        // Refused rather than clamped: a caller working in a configured
        // display range would otherwise silently move the wrong distance.
        if (!std::isfinite(normalised) || normalised < 0.0f || normalised > 1.0f)
            return {.status = EditStatus::InvalidValue, .requested = normalised};

        if (paramIndex < 0)
            return {.status = EditStatus::UnknownParameter, .requested = normalised};

        const auto key = keyOfDeviceAt(devicePath);
        if (session_ == nullptr || !key.has_value() || !factory_.isExternalKey(*key))
            return refused;

        // The assignment as it is now, so a slot that changes plugin before
        // this runs refuses the edit rather than moving the new one.
        // The reading goes out as an observation whether or not the write was
        // taken, so a control that asked for a value it did not get drops it.
        // `this` is read only while the request is wanted: the table it
        // watches dies with the loader, which dies with this.
        const auto request = loader_.request(*key);
        const std::pair pendingKey{*key, paramIndex};
        ++(*pendingEdits_)[pendingKey];

        const auto status = plane_.editParameter(
            *key, {.slot = paramIndex, .normalised = normalised, .request = request},
            [this, request, pending = pendingEdits_, pendingKey,
             completed = std::move(completed)](EditCompletion done) {
                // Settled first, so a control reading it from the observation
                // below already sees nothing outstanding.
                settlePendingEdit(*pending, pendingKey);

                if (done.observed.has_value() && request.isStillWanted())
                    publishObservation(pendingKey.first,
                                       {.slot = pendingKey.second,
                                        .normalised = *done.observed,
                                        .source = ObservationSource::CommandReadback});
                if (completed)
                    completed(done);
            });

        if (status != EditStatus::Accepted)
            settlePendingEdit(*pendingEdits_, pendingKey);

        return {.status = status, .requested = normalised};
    }

    void captureExternalPluginStateAt(const ChainNodePath& devicePath) {
        if (session_ == nullptr)
            return;

        // Internal devices are handled by the fork. Asking for one here
        // would find no external plugin and log a spurious failure.
        auto asked = false;
        for (const auto key : keysOfDeviceAt(devicePath))
            if (factory_.isExternalKey(key)) {
                requestCapture(key);
                asked = true;
            }

        if (asked)
            control_->drain();
    }

    /**
     * @brief Write the model's state for @p devicePath into the plugin (#2573).
     *
     * The plan sends the parameter array every block; the state chunk is
     * otherwise only written when the instance is built.
     */
    void applyExternalPluginStateAt(const ChainNodePath& devicePath) {
        if (session_ == nullptr)
            return;

        auto asked = false;
        for (const auto key : keysOfDeviceAt(devicePath))
            if (factory_.isExternalKey(key)) {
                requestApply(key);
                asked = true;
            }

        if (asked)
            control_->drain();
    }

    /**
     * @brief Write the model's state for @p key into its plugin, then read it back.
     *
     * The read-back is committed only if @p key still names the same plugin,
     * since a slot can be given a different one while the work runs.
     */
    void requestApply(engine::DeviceKey key) {
        const auto* saved = modelDeviceAt(key);
        if (saved == nullptr)
            return;

        plane_.applyState(
            key, *saved, [request = loader_.request(key)](adapter::CaptureOutcome held) {
                if (!held.ok()) {
                    if (request.isStillWanted())
                        juce::Logger::writeToLog("[engine] not applied: " + held.failure());
                    return;
                }

                adapter::commitCapturedState(request, held.snapshot(), modelDeviceAt);
            });
    }

    /**
     * @brief Read @p key's plugin and store the result on the model.
     *
     * The callback captures only its AssignmentRequest: it runs after this
     * returns, and capturing `this` would tie it to a project that may close
     * first (PluginAssignments.hpp).
     */
    void requestCapture(engine::DeviceKey key) {
        plane_.captureState(key, [request = loader_.request(key)](adapter::CaptureOutcome taken) {
            if (!taken.ok()) {
                // A device deleted or replaced mid-read fails for a reason
                // nobody needs reporting.
                if (request.isStillWanted())
                    juce::Logger::writeToLog("[engine] not saved: " + taken.failure());
                return;
            }

            adapter::commitCapturedState(request, taken.snapshot(), modelDeviceAt);
        });
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
    engine::RecordThread recordThread_;

    /// Who is playing, what they played, and which track hears it. The message
    /// thread and the MIDI threads share the registry; the queue carries
    /// messages between them.
    LiveMidiSources sources_;
    LiveMidiRouting routing_{sources_};
    LiveMidiQueue queue_;

    /// The queue's events as per-slot streams, sized in rebuild().
    LiveMidiCollector collector_;

    std::uint32_t tracedDrops_ = 0;

    EngineRuntimeFactory factory_;

    /// After the factory, so it is destroyed first: a load still in flight is
    /// gated on the assignment table this owns, and a table that has gone is a
    /// load nobody is waiting for.
    ExternalPluginLoader loader_;

    /// Where everything that is not a block runs (#2270). The message thread,
    /// which is where a plugin's editor lives and where the model a completion
    /// writes is edited from.
    std::shared_ptr<adapter::ControlExecutor> control_ =
        std::make_shared<adapter::MessageThreadControlExecutor>();

    /// Held by shared_ptr because the plane holds it weakly: a capture whose
    /// project closed first finds no registry, which is the answer rather than
    /// a session that has gone.
    std::shared_ptr<SessionDevices> sessionDevices_ =
        std::make_shared<SessionDevices>([this] { return session_.get(); });

    /// After both, since it is built from them.
    adapter::LocalDeviceControlPlane plane_{control_, sessionDevices_};

    juce::AudioDeviceManager* devices_ = nullptr;

    /// Plugin services an offline render loads a plugin with when no live instance exists.
    juce::AudioPluginFormatManager* formats_ = nullptr;
    const juce::KnownPluginList* knownPlugins_ = nullptr;

    /// Offline render sessions open. Nested ones share the first one's suspension.
    int offlineRenders_ = 0;

    engine::RenderContext context_{};
    juce::AudioBuffer<float> scratch_;
    EngineHost::HardwareChannelProvider hardwareOutputProvider_;
    std::map<std::string, engine::HardwareOutputRoute> hardwareOutputs_;
    std::map<std::string, engine::HardwareOutputRoute> publishedHardwareOutputs_;
    int hardwareOutputChannels_ = kChannels;
    std::atomic<bool> hardwareOutputsStale_{true};
    std::atomic<std::uint64_t> hardwareOutputGeneration_{1};
    std::atomic<std::uint64_t> hardwareOutputReadyGeneration_{0};

    /// Input names resolved against the open device, and the ones reported
    /// unresolvable since they last resolved (#2553).
    EngineHost::HardwareChannelProvider hardwareInputProvider_;
    HardwareInputMap hardwareInputs_;
    std::set<juce::String> unresolvedInputs_;
    std::atomic<bool> hardwareInputsStale_{true};
    /// Automatic take correction owned by the active interface: its input and
    /// output latency, positive because TakeRecorder removes it from the head.
    std::atomic<int> deviceRecordingAdjustmentSamples_{0};
    /// The portion a MIDI arrival shares with a simultaneously recorded audio
    /// input: playback through the active interface.
    std::atomic<int> deviceOutputAdjustmentSamples_{0};
    std::atomic<std::uint64_t> inputGeneration_{0};

    // Declared so that destruction unwinds inwards: the session lets go of the
    // pool before the thread servicing it stops, and the thread stops before
    // the pool it is inside goes away.
    std::unique_ptr<engine::ClipVoicePool> voices_;
    std::unique_ptr<engine::ClipVoiceThread> voiceThread_;
    std::unique_ptr<engine::EngineSession> session_;
    SessionArrangementCapture sessionCapture_;

    /// Arrangement takes owned by the live session. Resolved inputs are part
    /// of the identity so a hot-plug or route edit closes one before another begins.
    std::map<TrackId, ActiveMidiRoute> recordingRoutes_;
    std::map<TrackId, ActiveAudioRoute> audioRecordingRoutes_;
    std::map<TrackId, ClosingTake> closingAudioTakes_;
    std::map<TrackId, ClosingTake> closingMidiTakes_;
    std::map<TrackId, SessionSlotTarget> sessionSlotTargets_;
    std::set<engine::TakeKey> publishedTakeKeys_;
    bool recording_ = false;
    bool arrangementRecording_ = false;
    std::atomic<bool> arrangementPunchActive_{false};
    std::atomic<std::uint64_t> activePunchGeneration_{0};
    std::atomic<std::uint64_t> punchedOutGeneration_{0};
    std::atomic<bool> sessionPunchPending_{false};
    std::map<engine::TakeKey, engine::RecordTap::Reading> previewReadings_;
    std::unordered_map<TrackId, RecordingPreview> recordingPreviews_;

    /// What the session grid asks of the launcher, and what it reads back
    /// (#2552). Holds this rather than the session, which is rebuilt whenever
    /// the device changes.
    SlotLauncher launcher_{*this};

    /// The plan the session is rendering, kept so a mixer move can resolve
    /// values against it without compiling another.
    std::shared_ptr<const engine::RenderPlan> livePlan_;

    /// The frozen tracks the plan and the lanes were last published with.
    std::vector<FrozenTrack> frozen_;

    double bpm_ = 120.0;
    int numerator_ = 4;
    int denominator_ = 4;
    int countInMode_ = 0;

    /// The three above, baked. Cached rather than rebuilt per read: everything
    /// published with a tempo reads it, and so does the app through @ref view_.
    engine::TempoMap map_ = tempoMapAt(bpm_, numerator_, denominator_);
    TempoMapView view_{map_};

    engine::LoopRange loop_;
    engine::PunchRange punch_;
    engine::ClickSettings click_;
    engine::TransportRequest request_;
    std::uint64_t generation_ = 0;

    std::atomic<double> rate_{0.0};

    /// Whether the device is calling back, and whether the plugins have been told since it changed.
    std::atomic<bool> audioRunning_{false};
    std::atomic<bool> renderedStale_{false};
    std::atomic<int> blockSize_{0};
    std::atomic<int> inputChannels_{0};
    /// Every ask to republish, whether or not one followed.
    std::atomic<std::uint64_t> requests_{0};

    std::atomic<bool> plan_{false};
    std::atomic<bool> values_{false};

    /// An edit arrived that a values publish may not be able to carry.
    std::atomic<bool> shape_{false};
    std::atomic<bool> clips_{false};

    /// Callback serials at entry and after processing completes.
    std::atomic<std::uint64_t> rendered_{0};
    std::atomic<std::uint64_t> completedCallbacks_{0};

    std::vector<juce::String> unbuilt_;
    EngineTrace trace_;

    /// Where the levels go. Null until a caller asks (#2570).
    EngineHost::MeterSink meters_;

    /// The app's store for the per-slot levels, emptied at a project
    /// boundary. Null until a caller hands one over (#2570).
    DeviceMeters* deviceMeters_ = nullptr;

    /// Where each device the model holds sits, by the key its ops carry.
    /// Rebuilt with every plan, which is what a device moving is (#2570).
    std::map<engine::DeviceKey, ChainNodePath> devicePaths_;

    /// The racks the model holds, which a rack id alone addresses (#2649).
    std::set<RackId> rackIds_;

    /// Hot-plug, on the message thread. Last, so it is destroyed first and
    /// nothing calls back into a host that is already unwinding.
    juce::MidiDeviceListConnection deviceList_ =
        juce::MidiDeviceListConnection::make([this] { refreshLiveMidiDevices(); });
};

EngineHost::EngineHost() : impl_(std::make_unique<Impl>()) {}

EngineHost::~EngineHost() = default;

void EngineHost::start(juce::AudioDeviceManager& devices) {
    impl_->start(devices);
}

void EngineHost::setPluginServices(juce::AudioPluginFormatManager& formats,
                                   const juce::KnownPluginList& knownPlugins) {
    impl_->loader_.setServices(&formats, &knownPlugins);
    impl_->formats_ = &formats;
    impl_->knownPlugins_ = &knownPlugins;
}

void EngineHost::setHardwareOutputProvider(HardwareChannelProvider provider) {
    impl_->hardwareOutputProvider_ = std::move(provider);
    impl_->requestHardwareOutputRefresh();
}

void EngineHost::refreshHardwareOutputs() {
    impl_->requestHardwareOutputRefresh();
}

void EngineHost::setHardwareInputProvider(HardwareChannelProvider provider) {
    impl_->hardwareInputProvider_ = std::move(provider);
    impl_->requestHardwareInputRefresh();
}

void EngineHost::refreshHardwareInputs() {
    impl_->requestHardwareInputRefresh();
}

void EngineHost::meterInto(MeterSink sink) {
    impl_->meters_ = std::move(sink);
}

void EngineHost::meterDevicesInto(DeviceMeters& devices) {
    impl_->deviceMeters_ = &devices;
}

void EngineHost::forgetProject() {
    impl_->forgetProject();
}

std::uint64_t EngineHost::publishRequests() const {
    return impl_->publishRequests();
}

bool EngineHost::isSettled() const {
    return impl_->isSettled();
}

void EngineHost::stop() {
    impl_->detach();
}

void EngineHost::audition(TrackId trackId, const juce::MidiMessage& message) {
    const auto source = impl_->sources_.auditionSourceFor(trackId);
    impl_->queue_.push(source, impl_->sources_.slotFor(source), message);
}

void EngineHost::pushMidi(const juce::String& deviceId, const juce::MidiMessage& message) {
    const auto source = impl_->sources_.sourceFor(deviceId);
    impl_->queue_.push(source, impl_->sources_.slotFor(source), message);
}

void EngineHost::registerVirtualMidiSource(const juce::String& deviceId) {
    impl_->sources_.registerVirtualDevice(deviceId);
}

void EngineHost::refreshMidiInputs() {
    impl_->refreshLiveMidiDevices();
}

void EngineHost::captureExternalPluginStates() {
    impl_->captureExternalPluginStates();
}

void EngineHost::captureExternalPluginStateAt(const ChainNodePath& devicePath) {
    impl_->captureExternalPluginStateAt(devicePath);
}

void EngineHost::applyExternalPluginStateAt(const ChainNodePath& devicePath) {
    impl_->applyExternalPluginStateAt(devicePath);
}

std::optional<PluginPrograms> EngineHost::getPluginPrograms(const ChainNodePath& path) {
    return impl_->pluginPreset(path, {.action = adapter::PresetAction::Programs}).programs;
}
bool EngineHost::setPluginCurrentProgram(const ChainNodePath& path, int index) {
    return impl_
        ->pluginPreset(path,
                       {.action = adapter::PresetAction::SelectProgram, .programIndex = index})
        .ok();
}
bool EngineHost::loadPluginPresetFile(const ChainNodePath& path, const juce::File& file) {
    return impl_->pluginPreset(path, {.action = adapter::PresetAction::LoadFile, .file = file})
        .ok();
}
bool EngineHost::savePluginPresetFile(const ChainNodePath& path, const juce::File& file) {
    return impl_->pluginPreset(path, {.action = adapter::PresetAction::SaveFile, .file = file})
        .ok();
}

bool EngineHost::showDeviceEditor(const ChainNodePath& devicePath) {
    return impl_->deviceEditor(devicePath, adapter::EditorAction::Show);
}

bool EngineHost::hideDeviceEditor(const ChainNodePath& devicePath) {
    return impl_->deviceEditor(devicePath, adapter::EditorAction::Hide);
}

bool EngineHost::toggleDeviceEditor(const ChainNodePath& devicePath) {
    return impl_->deviceEditor(devicePath, adapter::EditorAction::Toggle);
}

bool EngineHost::isDeviceEditorOpen(const ChainNodePath& devicePath) {
    // UI refreshes must not queue control work or drain pending operations.
    // Like parameter formatting, this is a message-thread read of the live instance.
    auto* external = impl_->externalDeviceAt(devicePath);
    return external != nullptr && external->isEditorOpen();
}

juce::String EngineHost::formatDeviceParameter(const ChainNodePath& devicePath, int paramIndex,
                                               float normalised) const {
    return impl_->formatDeviceParameter(devicePath, paramIndex, normalised);
}

HostParameters EngineHost::describeDeviceParameters(const ChainNodePath& devicePath) const {
    return impl_->describeDeviceParameters(devicePath);
}

std::shared_ptr<audio::MagdaDevice> EngineHost::renderedDevice(
    const ChainNodePath& devicePath) const {
    return impl_->renderedDevice(devicePath);
}

std::optional<float> EngineHost::observedParameter(const ChainNodePath& devicePath,
                                                   int paramIndex) const {
    return impl_->observedParameter(devicePath, paramIndex);
}

bool EngineHost::hostedEditPending(const ChainNodePath& devicePath, int paramIndex) const {
    return impl_->hostedEditPending(devicePath, paramIndex);
}

EditReceipt EngineHost::editHostedParameter(const ChainNodePath& devicePath, int paramIndex,
                                            float normalised, EditOrigin origin,
                                            std::function<void(EditCompletion)> completed) {
    return impl_->editHostedParameter(devicePath, paramIndex, normalised, origin,
                                      std::move(completed));
}

void EngineHost::play() {
    const auto starting = !impl_->request_.playing;
    impl_->publishRequest({.playing = true, .locate = false});
    if (starting)
        impl_->launcher_.transportStarted();
}

void EngineHost::stopPlaying() {
    impl_->stopRecording();
    const auto stopping = impl_->request_.playing;
    impl_->publishRequest({.playing = false, .locate = false});
    if (stopping)
        impl_->launcher_.transportStopped();
}

void EngineHost::locateSeconds(double seconds) {
    impl_->stopRecording();
    impl_->publishRequest({.playing = impl_->request_.playing,
                           .locate = true,
                           .positionBeat = impl_->tempoMap().timeToBeat(seconds)});
}

bool EngineHost::isPlaying() const {
    return impl_->request_.playing;
}

bool EngineHost::startMidiRecording(double positionSeconds) {
    return impl_->startRecording(positionSeconds);
}

bool EngineHost::startPunchRecording(double positionSeconds, std::optional<double> punchInBeat) {
    const auto wasPlaying = impl_->request_.playing;
    if (!impl_->startRecording(positionSeconds, true))
        return false;
    impl_->beginArmedSessionSlotRecordings(positionSeconds, punchInBeat, wasPlaying);
    return true;
}

void EngineHost::stopMidiRecording() {
    impl_->stopRecording();
}

bool EngineHost::isRecording() const {
    return impl_->recording_;
}

void EngineHost::armSessionSlotRecording(TrackId trackId, int sceneIndex) {
    impl_->armSessionSlotRecording(trackId, sceneIndex);
}

void EngineHost::beginArmedSessionSlotRecordings() {
    impl_->beginArmedSessionSlotRecordings();
}

void EngineHost::beginArmedSessionSlotRecordings(double positionSeconds) {
    impl_->beginArmedSessionSlotRecordings(positionSeconds);
}

bool EngineHost::isSessionSlotRecordArmed(TrackId trackId, int sceneIndex) const {
    return impl_->isSessionSlotRecordArmed(trackId, sceneIndex);
}

bool EngineHost::isSessionSlotRecording(TrackId trackId, int sceneIndex) const {
    return impl_->isSessionSlotRecording(trackId, sceneIndex);
}

const std::unordered_map<TrackId, RecordingPreview>& EngineHost::recordingPreviews() {
    return impl_->recordingPreviews();
}

double EngineHost::positionBeats() const {
    return impl_->positionBeats();
}

double EngineHost::positionSeconds() const {
    return impl_->tempoMap().beatToTime(positionBeats());
}

void EngineHost::setTempo(double bpm) {
    if (impl_->bpm_ == bpm)
        return;

    const auto resumeRecording = impl_->arrangementRecording_;
    impl_->stopRecording();
    impl_->bpm_ = bpm;
    impl_->refreshTempoMap();
    impl_->publishTransport();

    // The snapshot's seconds were derived through the map that just changed, so
    // it is compiled again rather than left placing clips at the old tempo.
    impl_->publishClips();
    if (resumeRecording) {
        impl_->resumeRecording();
    }
}

double EngineHost::tempo() const {
    return impl_->bpm_;
}

void EngineHost::setTimeSignature(int numerator, int denominator) {
    if (impl_->numerator_ == numerator && impl_->denominator_ == denominator)
        return;

    const auto resumeRecording = impl_->arrangementRecording_;
    impl_->stopRecording();
    impl_->numerator_ = numerator;
    impl_->denominator_ = denominator;
    impl_->refreshTempoMap();
    impl_->publishTransport();
    if (resumeRecording) {
        impl_->resumeRecording();
    }
}

void EngineHost::getTimeSignature(int& numerator, int& denominator) const {
    numerator = impl_->numerator_;
    denominator = impl_->denominator_;
}

void EngineHost::setLoop(bool enabled, double startBeat, double endBeat) {
    const engine::LoopRange wanted{.enabled = enabled, .startBeat = startBeat, .endBeat = endBeat};
    if (impl_->loop_ == wanted)
        return;

    const auto resumeRecording = impl_->arrangementRecording_;
    impl_->stopRecording();
    impl_->loop_ = wanted;
    impl_->publishTransport();
    if (resumeRecording) {
        impl_->resumeRecording();
    }
}

void EngineHost::setPunch(double startBeat, double endBeat, bool punchInEnabled,
                          bool punchOutEnabled) {
    auto wanted = impl_->punch_;
    wanted.startBeat = startBeat;
    wanted.endBeat = endBeat;
    wanted.punchInEnabled = punchInEnabled;
    wanted.punchOutEnabled = punchOutEnabled;
    if (impl_->punch_ == wanted)
        return;
    impl_->punch_ = wanted;
    impl_->publishTransport();
    impl_->reschedulePunchedSessionSlots();
}

EngineHost::PunchState EngineHost::punch() const {
    return {impl_->punch_.startBeat, impl_->punch_.endBeat, impl_->punch_.punchInEnabled,
            impl_->punch_.punchOutEnabled};
}

void EngineHost::setMetronomeEnabled(bool enabled) {
    impl_->click_.enabled = enabled;
    impl_->publishTransport();
}

void EngineHost::setCountInMode(int mode) {
    impl_->countInMode_ = mode;
}

int EngineHost::countInMode() const {
    return impl_->countInMode_;
}

bool EngineHost::isMetronomeEnabled() const {
    return impl_->click_.enabled;
}

EngineHost::FreezePlan EngineHost::planFreeze(TrackId trackId) const {
    const auto& tracks = TrackManager::getInstance().getTracks();
    if (auto refusal = freezeRefusal(tracks, trackId); refusal.isNotEmpty())
        return {.refusal = std::move(refusal)};

    const auto file = freezeFileFor(trackId);
    if (file == juce::File())
        return {.refusal = "There is no project to keep the freeze in"};

    const auto context = impl_->liveContext().value_or(engine::RenderContext{
        .sampleRate = ProjectManager::getInstance().getCurrentProjectInfo().sampleRate,
        .maxBlockSize = 512,
        .numChannels = kChannels});

    auto request = freezeRequest(tracks, clipLanesFor(tracks), trackId, file, context);
    if (!request.has_value())
        return {.refusal = "Nothing to freeze"};

    return {.request = std::make_shared<OfflineRenderRequest>(std::move(*request))};
}

void EngineHost::adoptFreeze(const OfflineRenderRequest& request) {
    refreshPooledFile(request.destination);
}

std::unique_ptr<OfflineRenderSession> EngineHost::createOfflineRenderSession(
    bool resumePlaybackWhenFinished) {
    return createEngineOfflineRenderSession(*impl_, resumePlaybackWhenFinished);
}

EngineHost::LoopState EngineHost::loop() const {
    return {.enabled = impl_->loop_.enabled,
            .startBeat = impl_->loop_.startBeat,
            .endBeat = impl_->loop_.endBeat};
}

void EngineHost::launchClip(ClipId clipId) {
    impl_->launcher_.launch(clipId);
}

void EngineHost::stopClip(ClipId clipId) {
    impl_->launcher_.stop(clipId);
}

void EngineHost::launchScene(const std::vector<TrackId>& trackIds, int sceneIndex) {
    impl_->launcher_.launchScene(trackIds, sceneIndex);
}

void EngineHost::stopSessionTrack(TrackId trackId) {
    impl_->stopSessionSlotRecording(trackId);
    impl_->launcher_.stopTrack(trackId);
}

void EngineHost::stopAllSessionClips() {
    std::vector<TrackId> targets;
    targets.reserve(impl_->sessionSlotTargets_.size());
    for (const auto& [trackId, unused] : impl_->sessionSlotTargets_) {
        juce::ignoreUnused(unused);
        targets.push_back(trackId);
    }
    for (const auto trackId : targets)
        impl_->stopSessionSlotRecording(trackId);
    impl_->launcher_.stopEverything();
}

SessionClipPlayState EngineHost::sessionClipPlayState(ClipId clipId) const {
    return impl_->launcher_.playState(clipId);
}

bool EngineHost::sessionTrackStopPending(TrackId trackId) const {
    return impl_->launcher_.stopPending(trackId);
}

double EngineHost::sessionPlayheadSeconds() const {
    return impl_->launcher_.playheadSeconds(impl_->launcher_.playheadClip());
}

ClipId EngineHost::sessionPlayheadClip() const {
    return impl_->launcher_.playheadClip();
}

std::unordered_map<ClipId, double> EngineHost::sessionPlayheads() const {
    return impl_->launcher_.playheads();
}

void EngineHost::processSessionStateEvents() {
    if (impl_->sessionCapture_.update())
        impl_->publishClips();
    impl_->launcher_.processStateEvents();
}

const magda::TempoMap* EngineHost::tempoMap() const {
    return &impl_->view_;
}

}  // namespace magda::daw::engine_host
