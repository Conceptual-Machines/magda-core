#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <variant>
#include <vector>

#include "DeviceInfo.hpp"
#include "MacroInfo.hpp"

namespace magda {

using ChainId = int;
using RackId = int;
constexpr ChainId INVALID_CHAIN_ID = -1;
constexpr RackId INVALID_RACK_ID = -1;

// Forward declare for recursive variant
struct RackInfo;

/**
 * @brief A chain element can be either a device or a nested rack
 *
 * Uses unique_ptr for RackInfo to handle the recursive structure
 * (RackInfo contains ChainInfo which contains ChainElement which can be RackInfo)
 */
using ChainElement = std::variant<DeviceInfo, std::unique_ptr<RackInfo>>;

// Forward declare deep copy function (defined after RackInfo)
ChainElement deepCopyElement(const ChainElement& element);

// Helper functions for working with ChainElement
inline bool isDevice(const ChainElement& element) {
    return std::holds_alternative<DeviceInfo>(element);
}

inline bool isRack(const ChainElement& element) {
    return std::holds_alternative<std::unique_ptr<RackInfo>>(element);
}

inline DeviceInfo& getDevice(ChainElement& element) {
    return std::get<DeviceInfo>(element);
}

inline const DeviceInfo& getDevice(const ChainElement& element) {
    return std::get<DeviceInfo>(element);
}

inline RackInfo& getRack(ChainElement& element) {
    return *std::get<std::unique_ptr<RackInfo>>(element);
}

inline const RackInfo& getRack(const ChainElement& element) {
    return *std::get<std::unique_ptr<RackInfo>>(element);
}

/**
 * @brief A chain is an ordered sequence of elements (devices or nested racks)
 *
 * Chains represent a signal flow path within a rack. Each chain can route
 * to a different output (main output or auxiliary sends). Elements can be
 * either devices or nested racks, allowing for complex routing structures.
 */
struct ChainInfo {
    ChainId id = INVALID_CHAIN_ID;
    juce::String name;                   // e.g., "Chain 1"
    std::vector<ChainElement> elements;  // Ordered sequence of devices/racks
    int outputIndex = 0;                 // Output routing (0 = main, 1-N = aux)
    bool muted = false;
    bool solo = false;
    float volume = 0.0f;  // Chain volume in dB (0 = unity)
    float pan = 0.0f;     // Chain pan (-1 to 1)

    // Macro controls for chain-level parameter mapping
    MacroArray macros = createDefaultMacros();

    // UI state
    bool expanded = true;

    // Default constructor
    ChainInfo() = default;

    // Move operations (default is fine)
    ChainInfo(ChainInfo&&) = default;
    ChainInfo& operator=(ChainInfo&&) = default;

    // Copy constructor - deep copies elements
    ChainInfo(const ChainInfo& other)
        : id(other.id),
          name(other.name),
          outputIndex(other.outputIndex),
          muted(other.muted),
          solo(other.solo),
          volume(other.volume),
          pan(other.pan),
          macros(other.macros),
          expanded(other.expanded) {
        elements.reserve(other.elements.size());
        for (const auto& element : other.elements) {
            elements.push_back(deepCopyElement(element));
        }
    }

    // Copy assignment - deep copies elements
    ChainInfo& operator=(const ChainInfo& other) {
        if (this != &other) {
            id = other.id;
            name = other.name;
            outputIndex = other.outputIndex;
            muted = other.muted;
            solo = other.solo;
            volume = other.volume;
            pan = other.pan;
            macros = other.macros;
            expanded = other.expanded;
            elements.clear();
            elements.reserve(other.elements.size());
            for (const auto& element : other.elements) {
                elements.push_back(deepCopyElement(element));
            }
        }
        return *this;
    }

    // Convenience methods for backward compatibility
    std::vector<DeviceInfo*> getDevices() {
        std::vector<DeviceInfo*> devices;
        for (auto& element : elements) {
            if (isDevice(element)) {
                devices.push_back(&getDevice(element));
            }
        }
        return devices;
    }

    std::vector<const DeviceInfo*> getDevices() const {
        std::vector<const DeviceInfo*> devices;
        for (const auto& element : elements) {
            if (isDevice(element)) {
                devices.push_back(&getDevice(element));
            }
        }
        return devices;
    }
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
    float volume = 0.0f;   // Rack output volume in dB (0 = unity)
    float pan = 0.0f;      // Rack output pan (-1 to 1)

    // Macro controls for rack-wide parameter mapping
    MacroArray macros = createDefaultMacros();

    // Default constructor
    RackInfo() = default;

    // Move operations (default is fine)
    RackInfo(RackInfo&&) = default;
    RackInfo& operator=(RackInfo&&) = default;

    // Copy constructor
    RackInfo(const RackInfo& other)
        : id(other.id),
          name(other.name),
          chains(other.chains),  // ChainInfo has its own deep copy
          bypassed(other.bypassed),
          expanded(other.expanded),
          volume(other.volume),
          pan(other.pan),
          macros(other.macros) {}

    // Copy assignment
    RackInfo& operator=(const RackInfo& other) {
        if (this != &other) {
            id = other.id;
            name = other.name;
            chains = other.chains;  // ChainInfo has its own deep copy
            bypassed = other.bypassed;
            expanded = other.expanded;
            volume = other.volume;
            pan = other.pan;
            macros = other.macros;
        }
        return *this;
    }
};

// Implementation of deep copy for ChainElement (must be after RackInfo is complete)
inline ChainElement deepCopyElement(const ChainElement& element) {
    if (isDevice(element)) {
        return getDevice(element);  // DeviceInfo is copyable
    } else {
        // Deep copy the nested rack
        return std::make_unique<RackInfo>(getRack(element));
    }
}

// Factory function to create a ChainElement from a RackInfo
inline ChainElement makeRackElement(RackInfo rack) {
    return std::make_unique<RackInfo>(std::move(rack));
}

// Factory function to create a ChainElement from a DeviceInfo
inline ChainElement makeDeviceElement(DeviceInfo device) {
    return device;
}

}  // namespace magda
