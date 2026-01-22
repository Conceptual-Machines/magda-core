#pragma once

#include <juce_core/juce_core.h>

#include <array>

#include "DeviceInfo.hpp"

namespace magda {

using MacroId = int;
constexpr MacroId INVALID_MACRO_ID = -1;
constexpr int NUM_MACROS = 16;

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
 * @brief A macro knob that can be linked to a device parameter
 *
 * Macros provide quick access to key parameters without opening device UIs.
 * Each rack and chain has 16 macro knobs.
 */
struct MacroInfo {
    MacroId id = INVALID_MACRO_ID;
    juce::String name;   // e.g., "Macro 1" or user-defined
    float value = 0.5f;  // 0.0 to 1.0, normalized
    MacroTarget target;  // Optional linked parameter

    // Default constructor
    MacroInfo() = default;

    // Constructor with index (for initialization)
    explicit MacroInfo(int index) : id(index), name("Macro " + juce::String(index + 1)) {}

    bool isLinked() const {
        return target.isValid();
    }
};

/**
 * @brief Array of macros (used by RackInfo and ChainInfo)
 */
using MacroArray = std::array<MacroInfo, NUM_MACROS>;

/**
 * @brief Initialize a MacroArray with default values
 */
inline MacroArray createDefaultMacros() {
    MacroArray macros;
    for (int i = 0; i < NUM_MACROS; ++i) {
        macros[i] = MacroInfo(i);
    }
    return macros;
}

}  // namespace magda
