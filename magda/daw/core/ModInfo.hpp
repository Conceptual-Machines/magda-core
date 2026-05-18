#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <vector>

#include "ControlTarget.hpp"
#include "TypeIds.hpp"

namespace magda {

constexpr int MODS_PER_PAGE = 8;
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
    Whole = 1,                  // 1 bar (4 beats)
    Half = 2,                   // 1/2 note
    Quarter = 4,                // 1/4 note (1 beat)
    Eighth = 8,                 // 1/8 note
    Sixteenth = 16,             // 1/16 note
    ThirtySecond = 32,          // 1/32 note
    DottedHalf = 3,             // 1/2 + 1/4
    DottedQuarter = 6,          // 1/4 + 1/8
    DottedEighth = 12,          // 1/8 + 1/16
    DottedSixteenth = 24,       // 1/16 + 1/32
    DottedThirtySecond = 48,    // 1/32 + 1/64
    TripletHalf = 33,           // 1/2 triplet
    TripletQuarter = 66,        // 1/4 triplet
    TripletEighth = 132,        // 1/8 triplet
    TripletSixteenth = 264,     // 1/16 triplet
    TripletThirtySecond = 528,  // 1/32 triplet
    TwoBars = 200,              // 2 bars (8 beats)
    FourBars = 400,             // 4 bars (16 beats)
    EightBars = 800,            // 8 bars (32 beats)
    SixteenBars = 1600          // 16 bars (64 beats)
};

/**
 * @brief Map MAGDA SyncDivision to TE's ModifierCommon::RateType ordinal.
 *
 * Mirror of audio/ModifierHelpers.hpp::mapSyncDivision but kept in core/ so
 * TrackManager and the automation lane code can convert without dragging
 * in TE's namespace. Numbers must match TE's RateType enum declaration
 * order (slow → fast: hertz=0, sixteenBars=1, ..., sixtyFourthT=23).
 */
inline int syncDivisionToTeRateOrdinal(SyncDivision div) {
    switch (div) {
        case SyncDivision::SixteenBars:
            return 1;
        case SyncDivision::EightBars:
            return 2;
        case SyncDivision::FourBars:
            return 3;
        case SyncDivision::TwoBars:
            return 4;
        case SyncDivision::Whole:
            return 5;  // bar
        case SyncDivision::DottedHalf:
            return 6;
        case SyncDivision::Half:
            return 7;
        case SyncDivision::TripletHalf:
            return 8;
        case SyncDivision::DottedQuarter:
            return 9;
        case SyncDivision::Quarter:
            return 10;
        case SyncDivision::TripletQuarter:
            return 11;
        case SyncDivision::DottedEighth:
            return 12;
        case SyncDivision::Eighth:
            return 13;
        case SyncDivision::TripletEighth:
            return 14;
        case SyncDivision::DottedSixteenth:
            return 15;
        case SyncDivision::Sixteenth:
            return 16;
        case SyncDivision::TripletSixteenth:
            return 17;
        case SyncDivision::DottedThirtySecond:
            return 18;
        case SyncDivision::ThirtySecond:
            return 19;
        case SyncDivision::TripletThirtySecond:
            return 20;
    }
    return 10;  // quarter fallback
}

/**
 * @brief Inverse: TE ordinal → MAGDA SyncDivision (closest match).
 *
 * MAGDA's slider doesn't expose every TE division (no 1/16 D/T, no 1/64s)
 * so unsupported ordinals snap to the closest MAGDA-known division. Used
 * by automation playback when TE writes a discrete rateType value back
 * into MAGDA state.
 */
inline SyncDivision teRateOrdinalToSyncDivision(int ordinal) {
    switch (ordinal) {
        case 1:
            return SyncDivision::SixteenBars;
        case 2:
            return SyncDivision::EightBars;
        case 3:
            return SyncDivision::FourBars;
        case 4:
            return SyncDivision::TwoBars;
        case 5:
            return SyncDivision::Whole;
        case 6:
            return SyncDivision::DottedHalf;
        case 7:
            return SyncDivision::Half;
        case 8:
            return SyncDivision::TripletHalf;
        case 9:
            return SyncDivision::DottedQuarter;
        case 10:
            return SyncDivision::Quarter;
        case 11:
            return SyncDivision::TripletQuarter;
        case 12:
            return SyncDivision::DottedEighth;
        case 13:
            return SyncDivision::Eighth;
        case 14:
            return SyncDivision::TripletEighth;
        case 15:
            return SyncDivision::DottedSixteenth;
        case 16:
            return SyncDivision::Sixteenth;
        case 17:
            return SyncDivision::TripletSixteenth;
        case 18:
            return SyncDivision::DottedThirtySecond;
        case 19:
            return SyncDivision::ThirtySecond;
        case 20:
            return SyncDivision::TripletThirtySecond;
        case 21:
        case 22:
        case 23:
            return SyncDivision::TripletThirtySecond;
        default:
            return SyncDivision::Quarter;
    }
}

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
 * @brief A single mod-to-parameter link with its own amount.
 *
 * The target is a ControlTarget which addresses any parameter the system can
 * write to. Modifiers driving another modifier's parameter use Kind::ModParam.
 */
struct ModLink {
    ControlTarget target;
    float amount = 0.0f;   // -1.0 to 1.0, modulation depth for this link
    bool bipolar = false;  // true: LFO 0-1 maps to -1..+1; false: stays 0..+1
    bool enabled = true;   // false: keep the link/amount stored but do not apply it

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
    uint32_t triggerCount = 0;  // Monotonic counter incremented on each trigger
    bool running = false;       // Whether LFO is actively running (for triggered modes)

    // Loop/One-shot mode
    bool oneShot = false;          // If true, play once and hold at end value
    bool oneShotComplete = false;  // Runtime: true after one-shot finishes, cleared on gate reset

    // MSEG loop region (for Custom waveform)
    bool useLoopRegion = false;  // Enable loop between loopStart and loopEnd
    float loopStart = 0.0f;      // Loop region start phase (0-1)
    float loopEnd = 1.0f;        // Loop region end phase (0-1)

    // Advanced receiver settings (for future MIDI/Audio trigger modes)
    int midiChannel = 0;  // 0 = any, 1-16 = specific
    int midiNote = -1;    // -1 = any, 0-127 = specific

    // Audio trigger envelope settings (serialized)
    float audioAttackMs = 1.0f;     // Envelope follower attack time (ms)
    float audioReleaseMs = 100.0f;  // Envelope follower release time (ms)

    // Audio trigger runtime state (not serialized)
    float audioEnvLevel = 0.0f;  // Current smoothed envelope level
    bool audioGateOpen = false;  // Gate state for this mod

    // Custom curve settings (when waveform == Custom)
    CurvePreset curvePreset = CurvePreset::Triangle;
    std::vector<CurvePointData> curvePoints;  // User-defined curve points

    std::vector<ModLink> links;  // All parameter links for this mod

    // Default constructor
    ModInfo() = default;

    // Constructor with index (for initialization)
    explicit ModInfo(int index)
        : id(index), name(getDefaultName(index, ModType::LFO)), type(ModType::LFO) {}

    bool isLinked() const {
        return !links.empty();
    }

    // Add a link to a parameter
    void addLink(const ControlTarget& t, float amt = 0.0f) {
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
    void removeLink(const ControlTarget& t) {
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [&t](const ModLink& link) { return link.target == t; }),
                    links.end());
    }

    // Get link for a specific target (or nullptr if not linked)
    ModLink* getLink(const ControlTarget& t) {
        for (auto& link : links) {
            if (link.target == t) {
                return &link;
            }
        }
        return nullptr;
    }

    const ModLink* getLink(const ControlTarget& t) const {
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
 * By default, creates NUM_MODS (8) mods.
 * Pass numMods = 0 to start with empty array.
 */
inline ModArray createDefaultMods(int numMods = NUM_MODS) {
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
