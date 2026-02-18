#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "TypeIds.hpp"

namespace magda {

constexpr int MACROS_PER_PAGE = 8;
constexpr int DEFAULT_MACRO_PAGES = 2;
constexpr int NUM_MACROS = MACROS_PER_PAGE * DEFAULT_MACRO_PAGES;

/**
 * @brief Target for a macro link (which device parameter it controls)
 */
struct MacroTarget {
    DeviceId deviceId = INVALID_DEVICE_ID;
    int paramIndex = -1;  // Which parameter on the device

    bool isValid() const {
        return deviceId != INVALID_DEVICE_ID && paramIndex >= 0;
    }

    bool operator==(const MacroTarget& other) const {
        return deviceId == other.deviceId && paramIndex == other.paramIndex;
    }

    bool operator!=(const MacroTarget& other) const {
        return !(*this == other);
    }
};

/**
 * @brief A single macro link with per-link amount
 */
struct MacroLink {
    MacroTarget target;
    float amount = 0.0f;   // Per-link amount (-1.0 to 1.0)
    bool bipolar = false;  // true: macro 0-1 maps to -1..+1; false: stays 0..+1
};

/**
 * @brief A macro knob that can be linked to device parameters
 *
 * Macros provide quick access to key parameters without opening device UIs.
 * Each rack and chain has 16 macro knobs.
 *
 * Supports multiple links: one macro can control multiple parameters simultaneously.
 */
struct MacroInfo {
    MacroId id = INVALID_MACRO_ID;
    juce::String name;             // e.g., "Macro 1" or user-defined
    float value = 0.5f;            // 0.0 to 1.0, normalized (global macro value)
    MacroTarget target;            // Legacy: single linked parameter (for backward compatibility)
    std::vector<MacroLink> links;  // New: multiple links with per-link amounts

    // Default constructor
    MacroInfo() = default;

    // Constructor with index (for initialization)
    explicit MacroInfo(int index) : id(index), name("Macro " + juce::String(index + 1)) {}

    bool isLinked() const {
        return target.isValid() || !links.empty();
    }

    // Get link for a specific target
    const MacroLink* getLink(const MacroTarget& target) const {
        for (const auto& link : links) {
            if (link.target == target) {
                return &link;
            }
        }
        return nullptr;
    }

    // Get mutable link for a specific target
    MacroLink* getLink(const MacroTarget& target) {
        for (auto& link : links) {
            if (link.target == target) {
                return &link;
            }
        }
        return nullptr;
    }

    // Remove link to a specific target
    void removeLink(const MacroTarget& t) {
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [&t](const MacroLink& link) { return link.target == t; }),
                    links.end());
    }
};

/**
 * @brief Vector of macros (used by RackInfo and ChainInfo)
 */
using MacroArray = std::vector<MacroInfo>;

/**
 * @brief Initialize a MacroArray with default values
 */
inline MacroArray createDefaultMacros(int numMacros = NUM_MACROS) {
    MacroArray macros;
    macros.reserve(numMacros);
    for (int i = 0; i < numMacros; ++i) {
        macros.push_back(MacroInfo(i));
    }
    return macros;
}

/**
 * @brief Add a page of macros (8 macros) to an existing array
 */
inline void addMacroPage(MacroArray& macros) {
    int startIndex = static_cast<int>(macros.size());
    for (int i = 0; i < MACROS_PER_PAGE; ++i) {
        macros.push_back(MacroInfo(startIndex + i));
    }
}

/**
 * @brief Remove a page of macros (8 macros) from an existing array
 * @return true if page was removed, false if at minimum size
 */
inline bool removeMacroPage(MacroArray& macros, int minMacros = NUM_MACROS) {
    if (static_cast<int>(macros.size()) <= minMacros) {
        return false;  // At minimum size
    }

    int toRemove = juce::jmin(MACROS_PER_PAGE, static_cast<int>(macros.size()) - minMacros);
    for (int i = 0; i < toRemove; ++i) {
        macros.pop_back();
    }
    return true;
}

}  // namespace magda
