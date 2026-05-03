#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

#include "core/ChainNodePath.hpp"

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
 * Implementations run `generateAndApply` on a background thread; they
 * must not touch JUCE Components directly. Status text (returned) and
 * any progress signal go back to the UI through the caller.
 */
class SoundDesignAgent {
  public:
    virtual ~SoundDesignAgent() = default;

    /**
     * Per-token streaming callback. Called from the worker thread with
     * each chunk emitted by the LLM (raw text, may be JSON fragments).
     * Return false to abort generation. Implementations must not touch
     * UI directly — the caller is expected to marshal to the message
     * thread (e.g. juce::MessageManager::callAsync) before mutating
     * components.
     */
    using TokenCallback = std::function<bool(const juce::String& token)>;

    /**
     * Generate a preset from a natural-language prompt and write it to
     * the device at `path`. Returns a one-line status string for the
     * caller to show. Returns a string starting with "(" on failure
     * (e.g. "(target device not resolved)").
     *
     * If `onToken` is set the implementation streams the LLM output
     * token-by-token; otherwise it generates non-streaming.
     */
    virtual juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                          TokenCallback onToken = {}) = 0;

    /**
     * Optional category override the agent should bias toward
     * (e.g. "Bass", "Lead", "Pad"). Empty = let the model pick.
     */
    virtual void setCategoryOverride(const juce::String& /*category*/) {}

    /**
     * Best-effort cancellation. Implementations that wrap a
     * cancellable LLM call should hook this through.
     */
    virtual void requestCancel() {}

  protected:
    std::atomic<bool> shouldStop_{false};
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
