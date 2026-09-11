#include "LiveMidiSources.hpp"

#include <algorithm>

namespace magda::daw::engine_host {

void LiveMidiSources::registerAvailableDevices() {
    registerAvailableDevices(juce::MidiInput::getAvailableDevices());
}

void LiveMidiSources::registerAvailableDevices(juce::Array<juce::MidiDeviceInfo> available) {
    for (const auto& device : available)
        sourceFor(device.identifier);

    const juce::ScopedLock held(lock_);
    available_ = std::move(available);
}

int LiveMidiSources::sourceFor(const juce::String& deviceId) {
    const juce::ScopedLock held(lock_);

    if (const auto found = devices_.find(deviceId); found != devices_.end())
        return found->second;

    return devices_.emplace(deviceId, next_++).first->second;
}

int LiveMidiSources::registerVirtualDevice(const juce::String& deviceId) {
    const auto source = sourceFor(deviceId);

    const juce::ScopedLock held(lock_);
    virtual_.insert(source);
    return source;
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
    sources.reserve(static_cast<std::size_t>(available_.size()) + virtual_.size());

    for (const auto& device : available_)
        if (const auto found = devices_.find(device.identifier); found != devices_.end())
            sources.push_back(found->second);

    sources.insert(sources.end(), virtual_.begin(), virtual_.end());

    return sources;
}

int LiveMidiSources::resolveRoute(const juce::String& midiInputDevice) {
    if (midiInputDevice.isEmpty() || midiInputDevice == "all" ||
        midiInputDevice.startsWith("track:"))
        return kNoSource;

    // The list registerAvailableDevices() took, not a fresh enumeration: this
    // runs per track on every values publish.
    const auto available = [this] {
        const juce::ScopedLock held(lock_);
        return available_;
    }();

    const auto byIdentifier = [&](const juce::MidiDeviceInfo& device) {
        return device.identifier == midiInputDevice;
    };
    if (const auto* found = std::ranges::find_if(available, byIdentifier); found != available.end())
        return sourceFor(found->identifier);

    // What the fork's selectors store for a hardware input: TE's own ID,
    // derived from the JUCE identifier (tracktion_PhysicalMidiInputDevice.cpp:294).
    const auto byForkId = [&](const juce::MidiDeviceInfo& device) {
        return "midiin_" + juce::String::toHexString(device.identifier.hashCode()) ==
               midiInputDevice;
    };
    if (const auto* found = std::ranges::find_if(available, byForkId); found != available.end())
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
