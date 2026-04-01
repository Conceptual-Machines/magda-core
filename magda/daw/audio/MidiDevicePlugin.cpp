#include "MidiDevicePlugin.hpp"

namespace magda::daw::audio {

MidiDevicePlugin::MidiDevicePlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {}

MidiDevicePlugin::~MidiDevicePlugin() {
    notifyListenersOfDeletion();
}

void MidiDevicePlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = info.sampleRate;
}

void MidiDevicePlugin::deinitialise() {}

void MidiDevicePlugin::sendNoteOff(te::MidiMessageArray& midi, int noteNumber) {
    if (noteNumber >= 0) {
        midi.addMidiMessage(juce::MidiMessage::noteOff(1, noteNumber), 0.0, te::MPESourceID{});
    }
}

void MidiDevicePlugin::clearMidiOutDisplay() {
    midiOutNote_.store(-1, std::memory_order_relaxed);
    midiOutVelocity_.store(0, std::memory_order_relaxed);
}

void MidiDevicePlugin::setMidiOutDisplay(int noteNumber, int velocity) {
    midiOutNote_.store(noteNumber, std::memory_order_relaxed);
    midiOutVelocity_.store(velocity, std::memory_order_relaxed);
}

}  // namespace magda::daw::audio
