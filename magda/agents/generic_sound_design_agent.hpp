#pragma once

#include <juce_core/juce_core.h>

#include "sound_design_agent.hpp"

namespace magda {

/**
 * @brief True iff `pluginId` is a MAGDA sound-generator instrument the generic
 *        sound-design agent can drive.
 *
 * Covers the compiled-Faust synths (Poly Synth, FM, the percussion voices) and
 * the native Mutable ports (Elements, Rings) — everything whose front-panel
 * controls are exposed as automatable parameters with readable names/ranges.
 *
 * Excludes Sampler and Drum Grid (AI adds little there) and 4OSC, which keeps
 * its bespoke `FourOscAgent` until its wave/filter/voice/FX controls are made
 * automatable and it can join this path.
 */
bool isGenericSoundGeneratorDevice(const juce::String& pluginId);

/**
 * @brief Device-agnostic "design me a preset" agent driven by parameter
 *        introspection.
 *
 * Unlike `FourOscAgent`, which hard-codes its parameter schema in the system
 * prompt, this reads the target device's automatable parameters at runtime
 * (names, units, real-unit ranges, discrete choices) and builds the prompt from
 * that. The LLM picks values in REAL units (Hz, ms, dB, semitones) — the same
 * space the device's `ParameterInfo` already reports — so there's no
 * normalization guesswork. Values are applied through the shared
 * `TrackManager::setDeviceParameterValue` path.
 *
 * The only per-device inputs are the display name + description used to prime
 * the LLM; the same class serves every generator via
 * `createSoundDesignAgentFor`.
 */
class GenericSoundDesignAgent : public SoundDesignAgent {
  public:
    GenericSoundDesignAgent(juce::String pluginId, juce::String displayName,
                            juce::String description);

    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  llm::Conversation& conversation,
                                  TokenCallback onToken = {}) override;

    void setCategoryOverride(const juce::String& category) override {
        categoryOverride_ = category;
    }

    void requestCancel() override {
        shouldStop_ = true;
    }

  private:
    juce::String pluginId_;
    juce::String displayName_;
    juce::String description_;
    juce::String categoryOverride_;
};

}  // namespace magda
