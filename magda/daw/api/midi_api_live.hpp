#pragma once

#include "midi_api.hpp"

namespace magda {

class MidiBridge;

/**
 * Live MidiApi: forwards to MidiBridge for sends, and to JUCE for the
 * output-port name list.
 *
 * The bridge pointer is set after construction (MagdaApiLive is built
 * before the engine wires its MidiBridge in some test contexts). When
 * the bridge is unset, sendMidi/sendSysEx return false rather than
 * crashing — getOutputPortNames stays functional regardless because it
 * queries JUCE directly.
 */
class MidiApiLive : public MidiApi {
  public:
    MidiApiLive() = default;

    /** Wire (or rewire) the MidiBridge. Pass nullptr to detach. */
    void setMidiBridge(MidiBridge* bridge) {
        bridge_ = bridge;
    }

    bool sendMidi(const juce::String& port, const juce::MidiMessage& msg) override;
    bool sendSysEx(const juce::String& port, const juce::uint8* data, size_t numBytes) override;
    std::vector<juce::String> getOutputPortNames() const override;
    juce::String getDefaultOutputPort() const override {
        return defaultOutputPort_;
    }

    void setDefaultOutputPort(const juce::String& port) {
        defaultOutputPort_ = port;
    }

  private:
    MidiBridge* bridge_ = nullptr;
    juce::String defaultOutputPort_;
};

}  // namespace magda
