#pragma once

#include <memory>

#include "device_ai_agent.hpp"

namespace magda {

/**
 * @brief Per-device "design me a preset" agent interface.
 *
 * The chat console (/design) and the per-device AI side panel both go
 * through this. A device type (e.g. 4OSC) ships an implementation that
 * knows how to talk to its LLM, parse the result into the device's
 * preset shape, and write it onto a specific ChainNodePath. New
 * devices add support by writing their own subclass and extending
 * `createSoundDesignAgentFor` — no changes needed to the calling UI.
 *
 * For devices that host code rather than parameter presets (e.g.
 * Faust), use `CoderAgent` instead — different shape, same panel.
 */
class SoundDesignAgent : public DeviceAIAgent {
  public:
    /**
     * Optional category override the agent should bias toward
     * (e.g. "Bass", "Lead", "Pad"). Empty = let the model pick.
     */
    virtual void setCategoryOverride(const juce::String& /*category*/) {}
};

/**
 * Returns the SoundDesignAgent implementation for `pluginId`, or
 * nullptr if no specialised agent exists. Match is
 * case-insensitive on the pluginId field of DeviceInfo (e.g.
 * "4osc"). The returned agent is owned by the caller; create one
 * per design session so cancel state doesn't leak.
 */
std::unique_ptr<SoundDesignAgent> createSoundDesignAgentFor(const juce::String& pluginId);

/**
 * Quick check used by UI surfaces (slot button visibility, panel
 * gating) — returns true iff `createSoundDesignAgentFor(pluginId)`
 * would return a non-null agent.
 */
bool isSoundDesignSupported(const juce::String& pluginId);

}  // namespace magda
