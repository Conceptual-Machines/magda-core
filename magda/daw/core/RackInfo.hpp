#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "DeviceInfo.hpp"

namespace magda {

using ChainId = int;
using RackId = int;
constexpr ChainId INVALID_CHAIN_ID = -1;
constexpr RackId INVALID_RACK_ID = -1;

/**
 * @brief A chain is an ordered sequence of devices with an output destination
 *
 * Chains represent a signal flow path within a rack. Each chain can route
 * to a different output (main output or auxiliary sends).
 */
struct ChainInfo {
    ChainId id = INVALID_CHAIN_ID;
    juce::String name;                // e.g., "Chain 1"
    std::vector<DeviceInfo> devices;  // Ordered device sequence
    int outputIndex = 0;              // Output routing (0 = main, 1-N = aux)
    bool muted = false;
    bool solo = false;
    float volume = 1.0f;  // Chain volume (linear)
    float pan = 0.0f;     // Chain pan (-1 to 1)

    // UI state
    bool expanded = true;
};

/**
 * @brief A rack contains multiple parallel chains
 *
 * Racks allow parallel signal routing where each chain processes audio
 * independently and can route to different outputs. This enables complex
 * routing scenarios like parallel compression, multi-band processing,
 * or routing to multiple aux sends.
 */
struct RackInfo {
    RackId id = INVALID_RACK_ID;
    juce::String name;              // e.g., "FX Rack"
    std::vector<ChainInfo> chains;  // Parallel chains
    bool bypassed = false;
    bool expanded = true;  // UI collapsed state

    // Future: Macro controls for rack-wide parameter mapping
    // std::array<float, 8> macros;
};

}  // namespace magda
