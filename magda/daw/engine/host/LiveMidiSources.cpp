#include "LiveMidiSources.hpp"

#include <algorithm>

namespace magda::daw::engine_host {

void LiveMidiSources::registerAvailableDevices() {
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        sourceFor(device.identifier);
}

int LiveMidiSources::sourceFor(const juce::String& deviceId) {
    const juce::ScopedLock held(lock_);

    if (const auto found = devices_.find(deviceId); found != devices_.end())
        return found->second;

    return devices_.emplace(deviceId, next_++).first->second;
}

int LiveMidiSources::auditionSourceFor(TrackId trackId) {
    const juce::ScopedLock held(lock_);

    if (const auto found = auditions_.find(trackId); found != auditions_.end())
        return found->second;

    return auditions_.emplace(trackId, next_++).first->second;
}

std::vector<int> LiveMidiSources::deviceSources() const {
    const juce::ScopedLock held(lock_);

    std::vector<int> sources;
    sources.reserve(devices_.size());
    for (const auto& [deviceId, source] : devices_)
        sources.push_back(source);

    return sources;
}

int LiveMidiSources::resolveRoute(const juce::String& midiInputDevice) {
    if (midiInputDevice.isEmpty() || midiInputDevice == "all" ||
        midiInputDevice.startsWith("track:"))
        return kNoSource;

    const auto available = juce::MidiInput::getAvailableDevices();

    const auto byIdentifier = [&](const juce::MidiDeviceInfo& device) {
        return device.identifier == midiInputDevice;
    };
    if (const auto* found = std::ranges::find_if(available, byIdentifier); found != available.end())
        return sourceFor(found->identifier);

    // Older projects stored the name. Resolved to the identifier either way,
    // because that is what a message pushes under.
    const auto byName = [&](const juce::MidiDeviceInfo& device) {
        return device.name == midiInputDevice;
    };
    if (const auto* found = std::ranges::find_if(available, byName); found != available.end())
        return sourceFor(found->identifier);

    return sourceFor(midiInputDevice);
}

}  // namespace magda::daw::engine_host
