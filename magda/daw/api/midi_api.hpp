#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <vector>

namespace magda {

/**
 * Outbound MIDI surface for scripts and agents — host → external device.
 *
 * Bidirectional controllers (Launchkey, Push, Launchpad) need this for
 * DAW-mode handshake, LED feedback, screen control. Inbound MIDI continues
 * to flow through MidiBridge::RawMidiListener / LuaController as before.
 *
 * `port` accepts either the device's display name (what scripts see in
 * `e.port`) or its JUCE identifier — implementations resolve both.
 */
class MidiApi {
  public:
    virtual ~MidiApi() = default;

    /** Send a single MIDI message to the named output. Returns false if
     *  the device cannot be opened. */
    virtual bool sendMidi(const juce::String& port, const juce::MidiMessage& msg) = 0;

    /** Send a SysEx payload (without F0/F7 framing — added by the impl). */
    virtual bool sendSysEx(const juce::String& port, const juce::uint8* data, size_t numBytes) = 0;

    /** All currently available MIDI output port display names. */
    virtual std::vector<juce::String> getOutputPortNames() const = 0;

    /** Default output selected for the currently loaded controller script. */
    virtual juce::String getDefaultOutputPort() const = 0;
};

}  // namespace magda
