#pragma once

#include <memory>

#include "device_ai_agent.hpp"

namespace magda {

/**
 * @brief Per-device "write me code" agent interface.
 *
 * Used by devices that host source code rather than parameter presets
 * (currently only Faust). The agent asks an LLM for a complete program
 * in the device's host language, validates it, and applies it via the
 * device's load-source path.
 *
 * Sibling of `SoundDesignAgent`: same call shape so `AIPanelComponent`
 * can host either, but distinct type so the two flavours don't get
 * conflated. New code-hosting devices add support by writing their
 * own subclass and extending `createCoderAgentFor`.
 */
class CoderAgent : public DeviceAIAgent {};

/**
 * Returns the CoderAgent implementation for `pluginId`, or nullptr if
 * no specialised agent exists. Match is case-insensitive on the
 * pluginId field of DeviceInfo (e.g. "faust"). The returned agent is
 * owned by the caller; create one per session so cancel state doesn't
 * leak.
 */
std::unique_ptr<CoderAgent> createCoderAgentFor(const juce::String& pluginId);

/// Quick check used by UI surfaces — true iff `createCoderAgentFor`
/// would return non-null.
bool isCoderSupported(const juce::String& pluginId);

/**
 * Returns whichever flavour of `DeviceAIAgent` is registered for
 * `pluginId` — sound-design first, then coder. Use this from generic
 * UI (the AI side panel) that doesn't care which kind of agent runs.
 */
std::unique_ptr<DeviceAIAgent> createDeviceAIAgentFor(const juce::String& pluginId);

/// True iff any kind of DeviceAIAgent (sound design OR coder) exists
/// for `pluginId`.
bool isDeviceAISupported(const juce::String& pluginId);

}  // namespace magda
