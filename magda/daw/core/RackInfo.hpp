#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <memory>
#include <variant>
#include <vector>

#include "ChainZones.hpp"
#include "Crossover.hpp"
#include "DeviceInfo.hpp"
#include "MacroInfo.hpp"
#include "ModInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

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
    juce::String name;                   // empty until the user names it
    std::vector<ChainElement> elements;  // Ordered sequence of devices/racks
    int outputIndex = 0;                 // Output routing (0 = main, 1-N = aux)
    bool muted = false;
    bool solo = false;
    bool bypassed = false;
    float volume = 0.0f;  // Chain volume in dB (0 = unity)
    float pan = 0.0f;     // Chain pan (-1 to 1)

    /**
     * The MIDI notes this chain answers to, for a rack whose chains are keyed
     * by pitch rather than run in parallel: a Drum Grid's pads (#2192).
     *
     * `lowNote` > `highNote` means the chain takes everything, which is what a
     * plain parallel rack chain does and what every chain built before pads
     * lived in the model reads as. `rootNote` is the pitch the range is
     * transposed onto before the chain sees it, so a sampler mapped at C0
     * plays from whichever pad triggered it.
     */
    int lowNote = 0;
    int highNote = -1;
    int rootNote = 0;

    bool answersToEveryNote() const {
        return lowNote > highNote;
    }

    ChainZones zones;

    /// A Drum Grid pad's layers (#3007), which hold its devices; a pad's own
    /// `elements` stay empty. Layer ids are unique across the grid's pads and
    /// layers. Empty on any other chain.
    std::vector<ChainInfo> layers;

    /// Compiled at all: not bypassed, and on the main output. Aux-routed chains
    /// are not wired yet.
    bool isActive() const {
        return !bypassed && outputIndex == 0;
    }

    /// A new chain is unnamed; anywhere a label is needed it reads as "Chain".
    juce::String displayName() const {
        return name.isEmpty() ? juce::String("Chain") : name;
    }

    // UI state
    bool expanded = true;

    // Default constructor
    ChainInfo() = default;

    // Move operations (default is fine)
    ChainInfo(ChainInfo&&) = default;
    ChainInfo& operator=(ChainInfo&&) = default;

    // Copy operations deep-copy the elements. Declared here and defined after
    // RackInfo: both clear the element vector, and destroying one destroys a
    // unique_ptr<RackInfo>, which needs the complete type.
    ChainInfo(const ChainInfo& other);
    ChainInfo& operator=(const ChainInfo& other);

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
    bool deltaSolo = false;
    bool expanded = true;  // UI collapsed state
    float volume = 0.0f;   // Rack output volume in dB (0 = unity)
    float pan = 0.0f;      // Rack output pan (-1 to 1)
    /// Picks which chains sound by their selector zones, 0-127 (#1808).
    float chainSelector = 0.0f;

    /// A multiband rack splits its input at `crossovers`: its chains are the bands, low to high,
    /// one more than there are crossovers.
    bool multiband = false;
    std::vector<Crossover> crossovers;

    bool isMultiband() const {
        return multiband;
    }
    /// Which of a multiband rack's faceplate and band list show; at least one does.
    bool faceplateShown = true;
    bool bandsShown = true;

    // UI panel state
    bool modPanelOpen = false;    // Modulator panel visible
    bool paramPanelOpen = false;  // Macro panel visible

    // Macro controls for rack-wide parameter mapping
    MacroArray macros = createDefaultMacros();

    // Modulators for rack-wide modulation
    ModArray mods = createDefaultMods(0);

    // The source a rack's own triggers and followers listen to (cross-track).
    // Only `type`, `sourceTrackId`, and `enabled` apply: a rack has no sidechain edge for
    // the tap point, trim and listen a device's key carries (#2329).
    SidechainConfig sidechain;

    // Default constructor
    RackInfo() = default;

    // Move operations (default is fine)
    RackInfo(RackInfo&&) = default;
    RackInfo& operator=(RackInfo&&) = default;

    // Copy constructor. chains deep-copies via ChainInfo's own copy constructor.
    RackInfo(const RackInfo& other) = default;

    // Copy assignment
    RackInfo& operator=(const RackInfo& other) {
        if (this != &other) {
            id = other.id;
            name = other.name;
            chains = other.chains;  // ChainInfo has its own deep copy
            bypassed = other.bypassed;
            deltaSolo = other.deltaSolo;
            expanded = other.expanded;
            volume = other.volume;
            pan = other.pan;
            chainSelector = other.chainSelector;
            multiband = other.multiband;
            crossovers = other.crossovers;
            faceplateShown = other.faceplateShown;
            bandsShown = other.bandsShown;
            modPanelOpen = other.modPanelOpen;
            paramPanelOpen = other.paramPanelOpen;
            macros = other.macros;
            mods = other.mods;
            sidechain = other.sidechain;
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

inline ChainInfo::ChainInfo(const ChainInfo& other)
    : id(other.id),
      name(other.name),
      outputIndex(other.outputIndex),
      muted(other.muted),
      solo(other.solo),
      bypassed(other.bypassed),
      volume(other.volume),
      pan(other.pan),
      lowNote(other.lowNote),
      highNote(other.highNote),
      rootNote(other.rootNote),
      zones(other.zones),
      layers(other.layers),
      expanded(other.expanded) {
    elements.reserve(other.elements.size());
    for (const auto& element : other.elements) {
        elements.push_back(deepCopyElement(element));
    }
}

inline ChainInfo& ChainInfo::operator=(const ChainInfo& other) {
    if (this != &other) {
        id = other.id;
        name = other.name;
        outputIndex = other.outputIndex;
        muted = other.muted;
        solo = other.solo;
        bypassed = other.bypassed;
        volume = other.volume;
        pan = other.pan;
        lowNote = other.lowNote;
        highNote = other.highNote;
        rootNote = other.rootNote;
        zones = other.zones;
        layers = other.layers;
        expanded = other.expanded;
        elements.clear();
        elements.reserve(other.elements.size());
        for (const auto& element : other.elements) {
            elements.push_back(deepCopyElement(element));
        }
    }
    return *this;
}

/**
 * @brief Repairs a stored multiband rack: crossovers ascending and spaced, one fewer than bands.
 *
 * Missing crossovers split the top band; extra ones are dropped from the top.
 */
inline void normaliseMultiband(RackInfo& rack) {
    if (!rack.multiband) {
        rack.crossovers.clear();
        return;
    }
    auto& crossovers = rack.crossovers;
    std::ranges::sort(crossovers, {}, &Crossover::frequencyHz);
    const auto bands = std::max<std::size_t>(rack.chains.size(), 1);
    if (crossovers.size() >= bands)
        crossovers.resize(bands - 1);
    while (crossovers.size() + 1 < bands && static_cast<int>(crossovers.size()) < kMaxCrossovers)
        crossovers.push_back({bandSplitFrequency(crossovers, crossovers.size())});
    for (std::size_t i = 0; i < crossovers.size(); ++i)
        crossovers[i].frequencyHz =
            clampCrossoverFrequency(crossovers, i, crossovers[i].frequencyHz);
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
