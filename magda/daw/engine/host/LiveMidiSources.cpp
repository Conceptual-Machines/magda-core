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

LiveMidiSources::LiveMidiSources() {
    for (auto& owner : slotOwner_)
        owner.store(kNoSource, std::memory_order_relaxed);

    // Handed out from the back, so the first id takes slot 0 and a dump of
    // these reads in the order they were given.
    freeSlots_.reserve(static_cast<std::size_t>(kSlots));
    for (auto slot = kSlots; slot-- > 0;)
        freeSlots_.push_back(slot);
}

int LiveMidiSources::sourceFor(const juce::String& deviceId) {
    const juce::ScopedLock held(lock_);

    if (const auto found = devices_.find(deviceId); found != devices_.end())
        return found->second;

    return devices_.emplace(deviceId, take()).first->second;
}

int LiveMidiSources::take() {
    const auto source = next_++;

    // A project bigger than the room gets an id with nowhere to put it: what
    // it pushes is dropped and counted, the way a source past the end always
    // was. The id is still the model's, so nothing else answers to it.
    if (freeSlots_.empty())
        return source;

    bind(source);
    return source;
}

void LiveMidiSources::bind(int source) {
    const auto slot = freeSlots_.back();
    freeSlots_.pop_back();
    slotForSource_.emplace(source, slot);
    slotOwner_[static_cast<std::size_t>(slot)].store(source, std::memory_order_release);
}

void LiveMidiSources::release(int source) {
    const auto found = slotForSource_.find(source);
    if (found == slotForSource_.end())
        return;

    const auto slot = found->second;

    // Disowned before it is handed on, so the callback reads either this id or
    // the next one and never a slot two ids think they hold.
    slotOwner_[static_cast<std::size_t>(slot)].store(kNoSource, std::memory_order_release);
    slotForSource_.erase(found);
    freeSlots_.push_back(slot);
}

void LiveMidiSources::bindWaiting() {
    if (freeSlots_.empty())
        return;

    std::set<int> waiting;
    const auto wants = [&](int source) {
        if (source != kNoSource && !slotForSource_.contains(source))
            waiting.insert(source);
    };

    for (const auto& [deviceId, source] : devices_)
        wants(source);
    for (const auto& [trackId, source] : auditions_)
        wants(source);

    // Oldest first, so what has been waiting longest is heard first.
    for (const auto source : waiting) {
        if (freeSlots_.empty())
            return;

        bind(source);
    }
}

int LiveMidiSources::slotFor(int source) const {
    const juce::ScopedLock held(lock_);

    const auto found = slotForSource_.find(source);
    return found == slotForSource_.end() ? kNoSlot : found->second;
}

int LiveMidiSources::ownerOfSlot(int slot) const {
    if (slot < 0 || slot >= kSlots)
        return kNoSource;

    return slotOwner_[static_cast<std::size_t>(slot)].load(std::memory_order_acquire);
}

std::size_t LiveMidiSources::freeSlots() const {
    const juce::ScopedLock held(lock_);
    return freeSlots_.size();
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

    return auditions_.emplace(trackId, take()).first->second;
}

void LiveMidiSources::retainAuditions(const std::set<TrackId>& live) {
    const juce::ScopedLock held(lock_);

    for (auto entry = auditions_.begin(); entry != auditions_.end();) {
        if (live.contains(entry->first)) {
            ++entry;
            continue;
        }

        release(entry->second);
        entry = auditions_.erase(entry);
    }

    bindWaiting();
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
