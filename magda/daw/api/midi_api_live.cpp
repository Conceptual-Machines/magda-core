#include "midi_api_live.hpp"

#include "../audio/MidiBridge.hpp"

namespace magda {

bool MidiApiLive::sendMidi(const juce::String& port, const juce::MidiMessage& msg) {
    if (bridge_ == nullptr)
        return false;
    return bridge_->sendMidi(port, msg);
}

bool MidiApiLive::sendSysEx(const juce::String& port, const juce::uint8* data, size_t numBytes) {
    if (bridge_ == nullptr)
        return false;
    return bridge_->sendSysEx(port, data, numBytes);
}

std::vector<juce::String> MidiApiLive::getOutputPortNames() const {
    std::vector<juce::String> names;
    auto devices = juce::MidiOutput::getAvailableDevices();
    names.reserve(devices.size());
    for (const auto& d : devices)
        names.push_back(d.name);
    return names;
}

}  // namespace magda
