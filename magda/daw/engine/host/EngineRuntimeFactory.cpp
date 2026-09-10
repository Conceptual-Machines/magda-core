#include "EngineRuntimeFactory.hpp"

#include <utility>

#include "../../audio/plugins/engine/EngineDeviceFactory.hpp"
#include "clip/ClipAudioSource.hpp"
#include "clip/ClipMidiSource.hpp"

namespace magda::daw::engine_host {

namespace adapter = magda::daw::audio::engine_adapter;

namespace {

/// Which device a slot asks for, as one string. No display name and no role,
/// so a rename and a load's own correction are not a different device.
juce::String deviceIdentityOf(const DeviceInfo& device) {
    return device.pluginId + "|" + device.uniqueId + "|" + device.fileOrIdentifier + "|" +
           device.getFormatString();
}

/**
 * @brief A track's live MIDI: its own audition, and the devices routed to it.
 *
 * Named sources rather than kAnyLiveMidiSource, which would merge every other
 * track's audition into this one.
 */
class TrackMidiInput final : public engine::EngineMidiSource {
  public:
    TrackMidiInput(const engine::LiveInputFeed& feed, engine::LiveMidiSourceId audition,
                   std::vector<engine::LiveMidiSourceId> routed)
        : feed_(feed), audition_(audition), routed_(std::move(routed)) {}

    void render(const engine::BlockInfo& /*block*/, juce::MidiBuffer& out) override {
        feed_.appendEvents(audition_, out, engine::kMaxMidiBytesPerPort);

        for (const auto source : routed_)
            feed_.appendEvents(source, out, engine::kMaxMidiBytesPerPort);
    }

  private:
    const engine::LiveInputFeed& feed_;
    engine::LiveMidiSourceId audition_ = engine::kAnyLiveMidiSource;
    std::vector<engine::LiveMidiSourceId> routed_;
};

}  // namespace

EngineFileReaders::EngineFileReaders() {
    formats_.registerBasicFormats();
}

std::unique_ptr<engine::AudioFileReader> EngineFileReaders::open(const std::string& path) {
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats_.createReaderFor(juce::File(juce::String(path))));
    if (reader == nullptr)
        return nullptr;

    return std::make_unique<engine::JuceAudioFileReader>(std::move(reader));
}

void EngineRuntimeFactory::attach(engine::ClipSnapshotFeed& clips, engine::ClipStreamFeed& streams,
                                  engine::LaunchHandleFeed& handles,
                                  const engine::LiveInputFeed& liveInputs,
                                  LiveMidiSources& sources) {
    clips_ = &clips;
    streams_ = &streams;
    handles_ = &handles;
    liveInputs_ = &liveInputs;
    sources_ = &sources;
}

void EngineRuntimeFactory::setModel(const std::vector<TrackInfo>& tracks, const TrackInfo& master) {
    devices_.clear();
    unbuilt_.clear();
    midiRoutes_.clear();

    for (const auto& track : tracks)
        midiRoutes_.emplace(track.id, MidiRoute{track.midiInputDevice, track.monitorsInput()});

    for (const auto& [key, device] : adapter::devicesIn(tracks, master))
        devices_.emplace(key, *device);

    // A slot whose plugin changed since its instance was built (#2572). A key
    // the model has stopped naming is kept rather than dropped: the store
    // evicts on a publish that succeeded, and a rejected one leaves it holding
    // a device this would otherwise have forgotten.
    for (const auto& [key, identity] : built_) {
        const auto found = devices_.find(key);
        if (found != devices_.end() && deviceIdentityOf(found->second) != identity)
            rebuild_.insert(key);
    }

    // Before anything is asked for, so a device that has changed plugin since
    // the last publish has expired the load it had in flight by the time this
    // publish asks for one.
    if (externals_ != nullptr)
        externals_->syncAssignments(devices_);
}

void EngineRuntimeFactory::forgetBuiltDevices() {
    for (const auto& [key, identity] : built_)
        rebuild_.insert(key);

    built_.clear();

    if (externals_ != nullptr)
        externals_->forgetSlots();
}

std::set<engine::DeviceKey> EngineRuntimeFactory::devicesToRebuild() {
    // So a key the store could not realise this publish -- an external still
    // opening -- is not asked for again against an instance that has gone.
    for (const auto& key : rebuild_)
        built_.erase(key);

    return std::exchange(rebuild_, {});
}

std::unique_ptr<engine::EngineDevice> EngineRuntimeFactory::createDevice(engine::DeviceKey key) {
    const auto found = devices_.find(key);
    if (found == devices_.end())
        return nullptr;

    // An external plugin is a file on this machine rather than a class either
    // catalog holds, so it is not built here and not counted as unbuilt when it
    // is not ready: the loader answers null while it opens one, and the store
    // asks again at the next publish (#2566).
    if (adapter::isExternalDevice(found->second))
        return handOver(key, found->second,
                        externals_ != nullptr ? externals_->device(key, found->second) : nullptr);

    if (auto device = adapter::createEngineDevice(found->second))
        return handOver(key, found->second, std::move(device));

    // Said out loud once per publish rather than left as silence. A device the
    // app can build and the engine cannot is a project playing without part of
    // itself, and the executor's own "unbound op" says a key, not a name.
    unbuilt_.push_back(found->second.name);
    return nullptr;
}

/// Every device this factory hands over, recorded against the model it came
/// from and wrapped in the trace when one is on.
std::unique_ptr<engine::EngineDevice> EngineRuntimeFactory::handOver(
    engine::DeviceKey key, const DeviceInfo& model, std::unique_ptr<engine::EngineDevice> device) {
    if (device == nullptr)
        return nullptr;

    built_[key] = deviceIdentityOf(model);

    if (trace_ == nullptr)
        return device;

    return std::make_unique<TracingDevice>(std::move(device), *trace_);
}

/// A track's output level, and nothing else (#2570): the per-slot meters read
/// through DeviceMeteringManager and a monitored input's through #1895.
std::unique_ptr<engine::LevelTap> EngineRuntimeFactory::createMeter(const engine::OpKey& key) {
    if (key.role != engine::OpRole::TrackMeter)
        return nullptr;

    return std::make_unique<engine::LevelTap>();
}

std::unique_ptr<engine::EngineAudioSource> EngineRuntimeFactory::createClipAudioSource(
    TrackId trackId) {
    return audioSource(trackId, engine::Section::Arrangement);
}

std::unique_ptr<engine::EngineMidiSource> EngineRuntimeFactory::createClipMidiSource(
    TrackId trackId) {
    return midiSource(trackId, engine::Section::Arrangement);
}

std::unique_ptr<engine::EngineAudioSource> EngineRuntimeFactory::createSessionAudioSource(
    TrackId trackId) {
    return audioSource(trackId, engine::Section::Session);
}

std::unique_ptr<engine::EngineMidiSource> EngineRuntimeFactory::createSessionMidiSource(
    TrackId trackId) {
    return midiSource(trackId, engine::Section::Session);
}

std::unique_ptr<engine::EngineMidiSource> EngineRuntimeFactory::createMidiInput(TrackId trackId) {
    if (liveInputs_ == nullptr || sources_ == nullptr)
        return nullptr;

    const auto found = midiRoutes_.find(trackId);
    const auto route = found != midiRoutes_.end() ? found->second : MidiRoute{};

    return std::make_unique<TrackMidiInput>(*liveInputs_, sources_->auditionSourceFor(trackId),
                                            routedSources(route));
}

std::vector<engine::LiveMidiSourceId> EngineRuntimeFactory::routedSources(const MidiRoute& route) {
    // Monitoring is the fork's own gate on hearing an input, and a "track:"
    // route is carried inside the plan rather than by a device.
    if (!route.monitors || route.device.isEmpty() || route.device.startsWith("track:"))
        return {};

    if (route.device == "all")
        return sources_->deviceSources();

    const auto source = sources_->resolveRoute(route.device);
    if (source == LiveMidiSources::kNoSource)
        return {};

    return {source};
}

// Both sections through the handle-reading constructor, including the
// arrangement's. The plain one is the same source over an empty handle table,
// so taking the feed always costs a project with no session nothing and spares
// the factory a question it would have to ask the snapshot to answer.
std::unique_ptr<engine::EngineAudioSource> EngineRuntimeFactory::audioSource(
    TrackId trackId, engine::Section section) {
    if (clips_ == nullptr || streams_ == nullptr || handles_ == nullptr)
        return nullptr;

    return std::make_unique<engine::ClipAudioSource>(trackId, *clips_, *streams_, *handles_,
                                                     section);
}

std::unique_ptr<engine::EngineMidiSource> EngineRuntimeFactory::midiSource(
    TrackId trackId, engine::Section section) {
    if (clips_ == nullptr || handles_ == nullptr)
        return nullptr;

    return std::make_unique<engine::ClipMidiSource>(trackId, *clips_, *handles_, section);
}

}  // namespace magda::daw::engine_host
