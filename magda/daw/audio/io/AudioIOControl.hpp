#pragma once

/**
 * @file AudioIOControl.hpp
 * @brief What Audio Settings drives: which interface, and exactly which channels open (#2749).
 */

#include <juce_audio_devices/juce_audio_devices.h>

#include <vector>

#include "../../core/Config.hpp"
#include "HardwareChannels.hpp"

namespace magda {

/**
 * @brief The audio interface as a choice Audio Settings makes and the engine keeps.
 *
 * AudioIOService under the native engine, an adapter over Tracktion's wave devices under
 * Tracktion.
 */
class AudioIOControl : public HardwareChannels {
  public:
    /** @brief What is chosen, including an interface chosen with no channels on it. */
    virtual AudioIOSettings chosen() const = 0;

    /** @brief Open exactly @p settings and keep them as the choice; returns the open error. */
    virtual juce::String apply(const AudioIOSettings& settings) = 0;

    /** @brief What @p interfaceName calls its channels, whether or not it is open. */
    virtual juce::StringArray channelNames(const juce::String& backend,
                                           const juce::String& interfaceName, bool inputs) = 0;

    // ===== What Audio Settings lists (#2755) =====
    //
    // Off the choice rather than off a device manager, so nothing configures the interface
    // behind this and what is chosen is what opens.

    /** @brief The platform's backends, in the order they are offered. */
    virtual juce::StringArray backendNames() = 0;

    /** @brief The interfaces @p backend has one way. */
    virtual juce::StringArray interfaceNames(const juce::String& backend, bool inputs) = 0;

    /**
     * @brief Whether @p backend names one interface for both directions.
     *
     * ASIO and the single-device backends do, and Audio Settings offers one picker for
     * them rather than two that must agree.
     */
    virtual bool isSingleInterfaceBackend(const juce::String& backend) = 0;

    /** @brief What the open interface offers. Empty when nothing is open. */
    virtual std::vector<double> availableSampleRates() const = 0;
    virtual std::vector<int> availableBufferSizes() const = 0;

    // ===== What attaches to the open interface =====

    /**
     * @brief Add @p callback beside the engine's own, for metering or preview playback.
     *
     * It hears the open interface under either engine, and nothing about the choice
     * changes: this is a reader, not a second thing configuring the device.
     */
    virtual void addCallback(juce::AudioIODeviceCallback* callback) = 0;
    virtual void removeCallback(juce::AudioIODeviceCallback* callback) = 0;

    /** @brief What the open interface is doing, for the status readout. */
    struct Status {
        juce::String interfaceName;
        double sampleRate = 0.0;
        int bufferSize = 0;
        double cpuUsage = 0.0;
        int xruns = 0;
    };
    virtual Status status() const = 0;
};

}  // namespace magda
