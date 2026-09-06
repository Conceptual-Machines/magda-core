#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "sound_design_agent.hpp"

namespace magda {

/**
 * @brief Device-agnostic "design me a preset" agent driven by parameter
 *        introspection.
 *
 * Unlike `FourOscAgent`, which hard-codes its parameter schema in the system
 * prompt, this reads the target device's eligible automatable parameters at runtime
 * (names, units, real-unit ranges, discrete choices) and builds the prompt from
 * that. The LLM picks values in REAL units (Hz, ms, dB, semitones) — the same
 * space the device's `ParameterInfo` already reports — so there's no
 * normalization guesswork. Values are applied through the shared
 * `TrackManager::setDeviceParameterValue` path.
 *
 * Internal generators expose their whole parameter set. External plugins pass
 * the explicit indices selected by the user in the parameter configuration
 * panel. The same class serves both via `createSoundDesignAgentFor`.
 */
class GenericSoundDesignAgent : public SoundDesignAgent {
  public:
    GenericSoundDesignAgent(juce::String pluginId, juce::String displayName,
                            juce::String description,
                            std::vector<int> includedParameterIndices = {},
                            juce::String customPrompt = {});

    juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                  llm::Conversation& conversation, TokenCallback onToken) override;

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
    // Empty means all parameters (the existing internal-device behaviour).
    // External-plugin factories only construct this agent with a non-empty,
    // explicit user selection.
    std::vector<int> includedParameterIndices_;
    juce::String customPrompt_;
    juce::String categoryOverride_;
};

}  // namespace magda
