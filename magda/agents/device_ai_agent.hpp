#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

#include "core/ChainNodePath.hpp"

namespace llm {
struct Conversation;
}

namespace magda {

/**
 * @brief Abstract base for any per-device AI agent driven by the AI side
 *        panel.
 *
 * Two flavours sit underneath this:
 *  - `SoundDesignAgent` — picks parameter values for an existing device
 *    (e.g. 4OSC). Output shape: a Preset of params + waves + flags.
 *  - `CoderAgent` — generates code for devices that host code (e.g.
 *    Faust). Output shape: a source string compiled by the device.
 *
 * Both share the same call-site signature so `AIPanelComponent` can hold
 * a single pointer regardless of flavour. Subclass-specific knobs
 * (`SoundDesignAgent::setCategoryOverride`) live on the leaf interface,
 * not here.
 */
class DeviceAIAgent {
  public:
    virtual ~DeviceAIAgent() = default;

    /// Per-token streaming callback. Worker-thread context; implementations
    /// must marshal to the message thread before mutating UI.
    using TokenCallback = std::function<bool(const juce::String& token)>;

    /// Run the agent on `prompt` and apply its output to the device at
    /// `path`. Returns a one-line status. Failure strings start with
    /// "(" or "error:". `conversation` carries the running multi-turn history
    /// (owned/persisted by the caller) and is updated in place; agents that
    /// don't support multi-turn may ignore it.
    virtual juce::String generateAndApply(const juce::String& prompt, const ChainNodePath& path,
                                          llm::Conversation& conversation,
                                          TokenCallback onToken = {}) = 0;

    /// Best-effort cancel; safe to call from any thread.
    virtual void requestCancel() {}

  protected:
    std::atomic<bool> shouldStop_{false};
};

}  // namespace magda
