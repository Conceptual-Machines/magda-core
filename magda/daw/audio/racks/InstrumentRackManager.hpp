#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <map>

#include "../../core/ChainNodePath.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Manages instrument wrapping in TE RackTypes for audio passthrough
 *
 * When an instrument plugin (synth) is loaded on a track, it overwrites the audio
 * buffer, making audio clips inaudible. This manager wraps instruments in a RackType
 * that passes audio through and sums it with the synth output, so both audio clips
 * and synth output are audible on the same track.
 *
 * Rack wiring:
 *   MIDI in:           rack I/O pin 0 --> synth pin 0
 *   Plugin MIDI out:   synth pin 0 --> rack out pin 0 (always; lets a wrapped
 *                      sequencer/arp trigger downstream instruments)
 *   MIDI in thru:      rack I/O pin 0 --> rack out pin 0 (only when midiInThru;
 *                      passes raw input past the plugin to a downstream MIDI FX)
 *   Audio passthrough: rack I/O pin 1 --> rack out pin 1, pin 2 --> rack out pin 2
 *   Synth output:      synth pin 1/2 --> meter tap pin 1/2 --> rack out pin 1/2
 *   (Multiple connections to same output pin are summed by TE automatically)
 */
class InstrumentRackManager {
  public:
    InstrumentRackManager(te::Edit& edit);

    /**
     * @brief Wrap an instrument plugin in a RackType with audio passthrough
     * @param instrument The instrument plugin to wrap
     * @param midiInThru Wire raw MIDI input past the plugin to the rack output
     *                   (for a MIDI-FX placed after the instrument). Default on.
     * @return The RackInstance plugin to insert on the track (or nullptr on failure)
     */
    te::Plugin::Ptr wrapInstrument(te::Plugin::Ptr instrument, bool midiInThru = true);

    /**
     * @brief Wrap a multi-output instrument in a RackType with all output pins exposed
     * @param instrument The instrument plugin to wrap
     * @param numOutputChannels Total number of output channels (e.g. 32 for 16 stereo pairs)
     * @param midiInThru See wrapInstrument().
     * @return The main RackInstance plugin (outputs 1,2) to insert on the track
     */
    te::Plugin::Ptr wrapMultiOutInstrument(te::Plugin::Ptr instrument, int numOutputChannels,
                                           bool midiInThru = true);

    /**
     * @brief Create a RackInstance for a specific output pair from a multi-out instrument
     * @param deviceId The MAGDA device ID of the instrument
     * @param pairIndex 0-based pair index (1 = outputs 3,4; 2 = outputs 5,6; etc.)
     * @return The RackInstance plugin to insert on the output track
     */
    te::Plugin::Ptr createOutputInstance(DeviceId deviceId, int pairIndex, int firstPin,
                                         int numChannels);

    /**
     * @brief Remove a RackInstance for a specific output pair
     * @param deviceId The MAGDA device ID of the instrument
     * @param pairIndex The output pair index to remove
     */
    void removeOutputInstance(DeviceId deviceId, int pairIndex);

    /**
     * @brief Unwrap an instrument when it's removed - cleans up the RackType
     * @param deviceId The MAGDA device ID of the instrument
     */
    void unwrap(DeviceId deviceId);

    /**
     * @brief Record a wrapping association between a device ID and its rack components
     * @param deviceId The MAGDA device ID
     * @param rackType The RackType that wraps the instrument
     * @param innerPlugin The actual instrument plugin inside the rack
     * @param rackInstance The RackInstance plugin on the track
     */
    void recordWrapping(const ChainNodePath& devicePath, te::RackType::Ptr rackType,
                        te::Plugin::Ptr innerPlugin, te::Plugin::Ptr rackInstance,
                        bool isMultiOut = false, int numOutputChannels = 2);

    /**
     * @brief Get the inner instrument plugin for parameter/window access
     * @param deviceId The MAGDA device ID
     * @return The actual synth plugin inside the rack, or nullptr
     */
    te::Plugin* getInnerPlugin(DeviceId deviceId) const;

    /**
     * @brief Get the RackInstance plugin on the TE track for a wrapped instrument
     * @param deviceId The MAGDA device ID
     * @return The RackInstance plugin, or nullptr if not wrapped
     */
    te::Plugin* getRackInstance(DeviceId deviceId) const;

    /**
     * @brief Get the explicit meter tap plugin inside a wrapped instrument rack
     */
    te::Plugin* getMeterTapPlugin(DeviceId deviceId) const;

    /**
     * @brief Get the RackType wrapping an instrument (for modifier/macro support)
     * @param deviceId The MAGDA device ID
     * @return The RackType, or nullptr if not wrapped
     */
    te::RackType::Ptr getRackType(DeviceId deviceId) const;

    /**
     * @brief Toggle the "MIDI in thru" passthrough on a wrapped instrument live
     * @param deviceId The MAGDA device ID
     * @param enabled  When true, raw MIDI input is passed past the plugin to the
     *                 rack output; when false it is not. The plugin's own MIDI
     *                 output to the rack output is always wired and unaffected.
     */
    void setMidiInThru(DeviceId deviceId, bool enabled);

    /**
     * @brief Check if a TE plugin on a track is one of our wrapper racks
     * @param plugin The TE plugin to check
     * @return true if this is a wrapper RackInstance managed by us
     */
    bool isWrapperRack(te::Plugin* plugin) const;

    /**
     * @brief Get the device ID associated with a wrapper rack instance
     * @param plugin The RackInstance plugin
     * @return The device ID, or INVALID_DEVICE_ID if not found
     */
    DeviceId getDeviceIdForRack(te::Plugin* plugin) const;

    /**
     * @brief Clear all wrapping state (for shutdown)
     */
    void clear();

  private:
    te::Edit& edit_;

    struct WrappedInstrument {
        te::RackType::Ptr rackType;
        te::Plugin::Ptr innerPlugin;   // The actual synth
        te::Plugin::Ptr rackInstance;  // The RackInstance on the main track
        te::Plugin::Ptr meterTapPlugin;
        bool isMultiOut = false;
        int numOutputChannels = 2;
        std::map<int, te::Plugin::Ptr> outputInstances;  // pairIndex → RackInstance
    };

    std::map<DeviceId, WrappedInstrument> wrapped_;
    std::map<te::EditItemID, te::Plugin::Ptr> pendingMeterTapsByRack_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentRackManager)
};

}  // namespace magda
