#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <vector>

#include "TypeIds.hpp"

namespace magda {

constexpr int MODS_PER_PAGE = 4;
constexpr int DEFAULT_MOD_PAGES = 2;
constexpr int NUM_MODS = MODS_PER_PAGE * DEFAULT_MOD_PAGES;

/**
 * @brief Type of modulator
 */
enum class ModType { LFO, Envelope, Random, Follower };

/**
 * @brief LFO waveform shapes
 */
enum class LFOWaveform {
    Sine,
    Triangle,
    Square,
    Saw,
    ReverseSaw,
    Custom  // User-defined curve from curve editor
};

/**
 * @brief Tempo sync divisions for LFO rate
 */
enum class SyncDivision {
    Whole = 1,            // 1 bar (4 beats)
    Half = 2,             // 1/2 note
    Quarter = 4,          // 1/4 note (1 beat)
    Eighth = 8,           // 1/8 note
    Sixteenth = 16,       // 1/16 note
    ThirtySecond = 32,    // 1/32 note
    DottedHalf = 3,       // 1/2 + 1/4
    DottedQuarter = 6,    // 1/4 + 1/8
    DottedEighth = 12,    // 1/8 + 1/16
    TripletHalf = 33,     // 1/2 triplet
    TripletQuarter = 66,  // 1/4 triplet
    TripletEighth = 132   // 1/8 triplet
};

/**
 * @brief Curve presets for Custom waveform
 */
enum class CurvePreset {
    Triangle,     // Simple triangle
    Sine,         // Smooth sine-like curve
    RampUp,       // Linear ramp up
    RampDown,     // Linear ramp down
    SCurve,       // S-curve (smooth transition)
    Exponential,  // Exponential rise/fall
    Logarithmic,  // Logarithmic rise/fall
    Custom        // User-edited curve
};

/**
 * @brief A point on a custom curve (for LFO Custom waveform)
 */
struct CurvePointData {
    float phase = 0.0f;    // 0.0 to 1.0, position in cycle
    float value = 0.5f;    // 0.0 to 1.0, output value
    float tension = 0.0f;  // -3 to +3, curve tension
};

/**
 * @brief LFO trigger modes
 */
enum class LFOTriggerMode {
    Free,       // Continuous, never resets
    Transport,  // Reset on transport start/loop
    MIDI,       // Reset on MIDI note-on (stubbed)
    Audio       // Reset on audio transient (stubbed)
};

/**
 * @brief Target for a mod link (which device parameter it modulates)
 */
struct ModTarget {
    DeviceId deviceId = INVALID_DEVICE_ID;
    int paramIndex = -1;  // Which parameter on the device

    bool isValid() const {
        return deviceId != INVALID_DEVICE_ID && paramIndex >= 0;
    }

    bool operator==(const ModTarget& other) const {
        return deviceId == other.deviceId && paramIndex == other.paramIndex;
    }

    bool operator!=(const ModTarget& other) const {
        return !(*this == other);
    }
};

/**
 * @brief A single mod-to-parameter link with its own amount
 */
struct ModLink {
    ModTarget target;
    float amount = 0.5f;  // 0.0 to 1.0, modulation depth for this link

    bool isValid() const {
        return target.isValid();
    }
};

/**
 * @brief A modulator that can be linked to device parameters
 *
 * Mods provide dynamic modulation of parameters.
 * Each rack and chain has 16 mods by default.
 * A single mod can link to multiple parameters, each with its own amount.
 */
struct ModInfo {
    ModId id = INVALID_MOD_ID;
    juce::String name;  // e.g., "LFO 1" or user-defined
    ModType type = ModType::LFO;
    bool enabled = true;                       // Whether the mod is active
    float rate = 1.0f;                         // Rate/speed of modulation (Hz)
    LFOWaveform waveform = LFOWaveform::Sine;  // LFO waveform shape
    float phase = 0.0f;                        // 0.0 to 1.0, current position in cycle
    float phaseOffset = 0.0f;                  // 0.0 to 1.0, phase offset (adds to phase)
    float value = 0.5f;                        // 0.0 to 1.0, current LFO output

    // Tempo sync settings
    bool tempoSync = false;                             // Use tempo-synced rate instead of Hz
    SyncDivision syncDivision = SyncDivision::Quarter;  // Musical division when synced

    // Trigger mode settings
    LFOTriggerMode triggerMode = LFOTriggerMode::Free;  // When to reset phase
    bool triggered = false;                             // Set true when trigger fires (for UI dot)

    // Advanced receiver settings (for future MIDI/Audio trigger modes)
    int midiChannel = 0;  // 0 = any, 1-16 = specific
    int midiNote = -1;    // -1 = any, 0-127 = specific

    // Custom curve settings (when waveform == Custom)
    CurvePreset curvePreset = CurvePreset::Triangle;
    std::vector<CurvePointData> curvePoints;  // User-defined curve points

    std::vector<ModLink> links;  // All parameter links for this mod

    // Legacy single target/amount for backward compatibility
    // TODO: Remove after migration
    ModTarget target;     // Deprecated - use links instead
    float amount = 0.5f;  // Deprecated - use links instead

    // Default constructor
    ModInfo() = default;

    // Constructor with index (for initialization)
    explicit ModInfo(int index)
        : id(index), name(getDefaultName(index, ModType::LFO)), type(ModType::LFO) {}

    bool isLinked() const {
        return !links.empty() || target.isValid();
    }

    // Add a link to a parameter
    void addLink(const ModTarget& t, float amt = 0.5f) {
        // Check if already linked to this target
        for (auto& link : links) {
            if (link.target == t) {
                link.amount = amt;  // Update amount if already linked
                return;
            }
        }
        links.push_back({t, amt});
    }

    // Remove a link to a parameter
    void removeLink(const ModTarget& t) {
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [&t](const ModLink& link) { return link.target == t; }),
                    links.end());
    }

    // Get link for a specific target (or nullptr if not linked)
    ModLink* getLink(const ModTarget& t) {
        for (auto& link : links) {
            if (link.target == t) {
                return &link;
            }
        }
        return nullptr;
    }

    const ModLink* getLink(const ModTarget& t) const {
        for (const auto& link : links) {
            if (link.target == t) {
                return &link;
            }
        }
        return nullptr;
    }

    static juce::String getDefaultName(int index, ModType t) {
        juce::String prefix;
        switch (t) {
            case ModType::LFO:
                prefix = "LFO ";
                break;
            case ModType::Envelope:
                prefix = "Env ";
                break;
            case ModType::Random:
                prefix = "Rnd ";
                break;
            case ModType::Follower:
                prefix = "Fol ";
                break;
        }
        return prefix + juce::String(index + 1);
    }
};

/**
 * @brief Vector of mods (used by RackInfo and ChainInfo)
 */
using ModArray = std::vector<ModInfo>;

/**
 * @brief Initialize a ModArray with default values
 *
 * By default, creates an empty array. Users add mods via + button.
 * Pass numMods > 0 to pre-populate (for testing or legacy support).
 */
inline ModArray createDefaultMods(int numMods = 0) {
    ModArray mods;
    mods.reserve(numMods);
    for (int i = 0; i < numMods; ++i) {
        mods.push_back(ModInfo(i));
    }
    return mods;
}

/**
 * @brief Add a page of mods (8 mods) to an existing array
 */
inline void addModPage(ModArray& mods) {
    int startIndex = static_cast<int>(mods.size());
    for (int i = 0; i < MODS_PER_PAGE; ++i) {
        mods.push_back(ModInfo(startIndex + i));
    }
}

/**
 * @brief Remove a page of mods (8 mods) from an existing array
 * @return true if page was removed, false if at minimum size
 */
inline bool removeModPage(ModArray& mods, int minMods = 0) {
    if (static_cast<int>(mods.size()) <= minMods) {
        return false;  // At minimum size
    }

    int toRemove = juce::jmin(MODS_PER_PAGE, static_cast<int>(mods.size()) - minMods);
    for (int i = 0; i < toRemove; ++i) {
        mods.pop_back();
    }
    return true;
}

}  // namespace magda
