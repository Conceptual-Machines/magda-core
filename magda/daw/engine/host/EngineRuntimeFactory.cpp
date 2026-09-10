#include "EngineRuntimeFactory.hpp"

#include "../../audio/plugins/engine/EngineDeviceFactory.hpp"
#include "clip/ClipAudioSource.hpp"
#include "clip/ClipMidiSource.hpp"

namespace magda::daw::engine_host {

namespace adapter = magda::daw::audio::engine_adapter;

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
                                  engine::LaunchHandleFeed& handles) {
    clips_ = &clips;
    streams_ = &streams;
    handles_ = &handles;
}

void EngineRuntimeFactory::setModel(const std::vector<TrackInfo>& tracks, const TrackInfo& master) {
    devices_.clear();
    unbuilt_.clear();

    for (const auto& [key, device] : adapter::devicesIn(tracks, master))
        devices_.emplace(key, *device);

    // Before anything is asked for, so a device that has changed plugin since
    // the last publish has expired the load it had in flight by the time this
    // publish asks for one.
    if (externals_ != nullptr)
        externals_->syncAssignments(devices_);
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
        return traced(externals_ != nullptr ? externals_->device(key, found->second) : nullptr);

    if (auto device = adapter::createEngineDevice(found->second))
        return traced(std::move(device));

    // Said out loud once per publish rather than left as silence. A device the
    // app can build and the engine cannot is a project playing without part of
    // itself, and the executor's own "unbound op" says a key, not a name.
    unbuilt_.push_back(found->second.name);
    return nullptr;
}

/// Every device this factory hands over, through the trace when one is on.
std::unique_ptr<engine::EngineDevice> EngineRuntimeFactory::traced(
    std::unique_ptr<engine::EngineDevice> device) {
    if (device == nullptr || trace_ == nullptr)
        return device;

    return std::make_unique<TracingDevice>(std::move(device), *trace_);
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
