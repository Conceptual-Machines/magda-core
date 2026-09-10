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
}

std::unique_ptr<engine::EngineDevice> EngineRuntimeFactory::createDevice(engine::DeviceKey key) {
    const auto found = devices_.find(key);
    if (found == devices_.end())
        return nullptr;

    if (auto device = adapter::createEngineDevice(found->second)) {
        if (trace_ != nullptr)
            return std::make_unique<TracingDevice>(std::move(device), *trace_);

        return device;
    }

    // Said out loud once per publish rather than left as silence. A device the
    // app can build and the engine cannot is a project playing without part of
    // itself, and the executor's own "unbound op" says a key, not a name.
    unbuilt_.push_back(found->second.name);
    return nullptr;
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
