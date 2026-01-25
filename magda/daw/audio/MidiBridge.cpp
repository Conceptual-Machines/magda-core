#include "MidiBridge.hpp"

namespace magda {

MidiBridge::MidiBridge(te::Engine& engine) : engine_(engine) {
    DBG("MidiBridge initialized");
}

std::vector<MidiDeviceInfo> MidiBridge::getAvailableMidiInputs() const {
    std::vector<MidiDeviceInfo> devices;

    // Use JUCE's MidiInput::getAvailableDevices() instead of Tracktion's device manager
    // This works immediately without waiting for async scan
    auto midiInputs = juce::MidiInput::getAvailableDevices();

    for (const auto& device : midiInputs) {
        MidiDeviceInfo info;
        info.id = device.identifier;
        info.name = device.name;
        info.isEnabled = false;  // Input enable state handled separately
        info.isAvailable = true;

        devices.push_back(info);
    }

    DBG("Found " << devices.size() << " MIDI input devices");
    return devices;
}

std::vector<MidiDeviceInfo> MidiBridge::getAvailableMidiOutputs() const {
    std::vector<MidiDeviceInfo> devices;

    auto midiOutputs = juce::MidiOutput::getAvailableDevices();

    for (const auto& device : midiOutputs) {
        MidiDeviceInfo info;
        info.id = device.identifier;
        info.name = device.name;
        info.isEnabled = false;  // Output enable state not tracked same way
        info.isAvailable = true;

        devices.push_back(info);
    }

    return devices;
}

void MidiBridge::enableMidiInput(const juce::String& deviceId) {
    auto& deviceManager = engine_.getDeviceManager();
    auto device = deviceManager.findMidiInputDeviceForID(deviceId);
    if (device) {
        device->setEnabled(true);
        DBG("Enabled MIDI input: " << deviceId);
    }
}

void MidiBridge::disableMidiInput(const juce::String& deviceId) {
    auto& deviceManager = engine_.getDeviceManager();
    auto device = deviceManager.findMidiInputDeviceForID(deviceId);
    if (device) {
        device->setEnabled(false);
        DBG("Disabled MIDI input: " << deviceId);
    }
}

bool MidiBridge::isMidiInputEnabled(const juce::String& deviceId) const {
    auto device = engine_.getDeviceManager().findMidiInputDeviceForID(deviceId);
    return device ? device->isEnabled() : false;
}

void MidiBridge::setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId) {
    juce::ScopedLock lock(routingLock_);

    if (midiDeviceId.isEmpty()) {
        // Clear routing
        trackMidiInputs_.erase(trackId);
        DBG("Cleared MIDI input for track " << trackId);
    } else {
        trackMidiInputs_[trackId] = midiDeviceId;
        DBG("Set MIDI input for track " << trackId << " to device: " << midiDeviceId);

        // Auto-enable the device if not already enabled
        if (!isMidiInputEnabled(midiDeviceId)) {
            enableMidiInput(midiDeviceId);
        }
    }
}

juce::String MidiBridge::getTrackMidiInput(TrackId trackId) const {
    juce::ScopedLock lock(routingLock_);

    auto it = trackMidiInputs_.find(trackId);
    if (it != trackMidiInputs_.end()) {
        return it->second;
    }
    return {};  // Empty string = no input
}

void MidiBridge::clearTrackMidiInput(TrackId trackId) {
    setTrackMidiInput(trackId, {});
}

void MidiBridge::startMonitoring(TrackId trackId) {
    juce::ScopedLock lock(routingLock_);
    monitoredTracks_.insert(trackId);
    DBG("Started MIDI monitoring for track " << trackId);
}

void MidiBridge::stopMonitoring(TrackId trackId) {
    juce::ScopedLock lock(routingLock_);
    monitoredTracks_.erase(trackId);
    DBG("Stopped MIDI monitoring for track " << trackId);
}

bool MidiBridge::isMonitoring(TrackId trackId) const {
    juce::ScopedLock lock(routingLock_);
    return monitoredTracks_.find(trackId) != monitoredTracks_.end();
}

}  // namespace magda
