#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

#include "../daw/core/TypeIds.hpp"

namespace magda {

/**
 * @brief LLM-backed gain-staging reasoning (phase 2 of issue #884).
 *
 * Takes the per-device peak levels captured by GainStagingManager plus the
 * chain make-up, and asks an LLM for a musical gain-staging decision per device
 * (drive a saturator, ease off into a limiter, respect a compressor's input,
 * etc.) rather than the flat algorithmic target.
 *
 * generate() is blocking and must be called on a background thread; the caller
 * applies the returned decisions on the message thread.
 */
class GainStagingAgent {
  public:
    /** One of a device's current parameter settings (MAGDA devices). */
    struct Param {
        std::string name;
        double value = 0.0;
        std::string unit;
    };

    /** One device's state going into the decision. Devices are identified to the
     *  model by their position in this list (signal order), not by DeviceId:
     *  section-local ids are not unique within a track, so two devices could
     *  otherwise collide on the same handle. */
    struct DeviceLevel {
        std::string name;
        std::string pluginId;
        bool isInstrument = false;
        float capturedPeakDb = -100.0f;  // peak measured at the device output
        float currentGainDb = 0.0f;      // current output trim
        float suggestedGainDb = 0.0f;    // flat-target trim baseline (the math, done)
        std::vector<Param> params;       // current settings (MAGDA devices only)
    };

    /** The agent's chosen output trim for one device, identified by its index
     *  into the DeviceLevel list passed to generate(). */
    struct Decision {
        int index = -1;
        float newGainDb = 0.0f;
        std::string reason;
    };

    struct Result {
        std::vector<Decision> decisions;
        std::string summary;
        bool hasError = false;
        std::string error;
        std::string rawOutput;
    };

    /** Blocking LLM call. Run off the message thread. */
    Result generate(float targetPeakDb, const std::vector<DeviceLevel>& devices);

    static const char* getSystemPrompt();

  private:
    juce::String buildUserMessage(float targetPeakDb,
                                  const std::vector<DeviceLevel>& devices) const;
    void parseDecisions(const juce::String& rawText, const std::vector<DeviceLevel>& devices,
                        Result& result) const;
};

}  // namespace magda
