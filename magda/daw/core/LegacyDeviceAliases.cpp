#include "LegacyDeviceAliases.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <span>

#include "AutomationInfo.hpp"
#include "DeviceInfo.hpp"
#include "TrackInfo.hpp"

namespace magda::legacy_devices {

namespace {

// ============================================================================
// Value conversions
//
// Every function takes the retired device's value(s) in REAL units and returns
// the successor's value in ITS real units. `second` is only meaningful for the
// handful of mappings that fold two Tracktion parameters into one compiled
// slot; the rest ignore it.
// ============================================================================

float gainToDb(float gain, float) {
    // Tracktion's compressor stores its threshold as a linear gain (0.01..1).
    return juce::jlimit(-60.0f, 0.0f, gain > 0.0f ? 20.0f * std::log10(gain) : -60.0f);
}

float reciprocalRatio(float value, float) {
    // Tracktion stores 1/ratio in 0..0.95 and displays it as "N : 1"; 0 is its
    // INF setting, which the compiled device expresses as its maximum ratio.
    return value > 0.02f ? juce::jlimit(1.0f, 50.0f, 1.0f / value) : 50.0f;
}

float compressorAttack(float ms, float) {
    return juce::jlimit(0.1f, 200.0f, ms);
}

float compressorRelease(float ms, float) {
    return juce::jlimit(5.0f, 1000.0f, ms);
}

float outputDb(float db, float) {
    return juce::jlimit(-24.0f, 12.0f, db);
}

float eqFrequency(float hz, float) {
    return juce::jlimit(20.0f, 20000.0f, hz);
}

float eqGain(float db, float) {
    return juce::jlimit(-24.0f, 24.0f, db);
}

float eqQ(float q, float) {
    return juce::jlimit(0.1f, 10.0f, q);
}

float delayTime(float ms, float) {
    return juce::jlimit(1.0f, 2000.0f, ms);
}

float delayFeedback(float db, float) {
    // Tracktion's feedback is dB over -30..0, and treats the bottom of the
    // range as silence; the compiled delay takes a linear 0..0.95.
    if (db <= -30.0f)
        return 0.0f;
    return juce::jlimit(0.0f, 0.95f, std::pow(10.0f, db / 20.0f));
}

float unitInterval(float value, float) {
    return juce::jlimit(0.0f, 1.0f, value);
}

float chorusDepth(float ms, float) {
    // Tracktion sweeps a 20 ms base delay by depthMs (0.1..20); the compiled
    // chorus takes that sweep as a proportion.
    return juce::jlimit(0.0f, 1.0f, ms / 20.0f);
}

float lfoRateHz(float hz, float) {
    return juce::jlimit(0.05f, 10.0f, hz);
}

float phaserDepth(float octaves, float) {
    // Tracktion sweeps 2^depth octaves and defaults to 5; the compiled phaser's
    // 0..2 depth defaults to 1. Anchoring one default on the other keeps an
    // untouched device sounding the same either side of the migration.
    return juce::jlimit(0.0f, 2.0f, octaves / 5.0f);
}

float phaserFeedback(float gain, float) {
    return juce::jlimit(-0.95f, 0.95f, gain);
}

float unitToPercent(float value, float) {
    return juce::jlimit(0.0f, 100.0f, value * 100.0f);
}

float unitToWidthPercent(float value, float) {
    // The compiled reverb's width is 0..200 % with 100 % as plain stereo, which
    // is where Tracktion's 0..1 width sits at 1.
    return juce::jlimit(0.0f, 200.0f, value * 100.0f);
}

float reverbMix(float wet, float dry) {
    // Freeverb scales wet by 3 and dry by 2 before summing, so the audible
    // balance — not the raw slider pair — is what the single Mix must match.
    const float wetGain = 3.0f * wet;
    const float dryGain = 2.0f * dry;
    const float total = wetGain + dryGain;
    if (total <= 0.0f)
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, wetGain / total);
}

float pitchSemitones(float semitones, float) {
    return juce::jlimit(-24.0f, 24.0f, semitones);
}

float filterCutoff(float hz, float) {
    return juce::jlimit(5.0f, 20000.0f, hz);
}

float filterMode(float highpass, float) {
    // Tracktion's Lowpass is a two-state low/high pass; the compiled filter's
    // mode menu is LP, BP, HP, Notch.
    return highpass >= 0.5f ? 2.0f : 0.0f;
}

// ============================================================================
// Mapping table
// ============================================================================

/// One compiled slot, sourced either from the retired device's parameters or
/// from a fixed value that re-creates behaviour the retired device had baked in.
struct SlotMapping {
    int targetSlot = -1;
    const char* targetName = nullptr;
    int sourceIndex = -1;   // < 0 -> `constant` is used instead
    int sourceIndex2 = -1;  // optional second source for `convert`
    float constant = 0.0f;
    // Stands in when the second source is absent from the saved parameters.
    // The first source has no equivalent: without it the slot is skipped and
    // the successor keeps its own default.
    float source2Default = 0.0f;
    float (*convert)(float, float) = nullptr;
};

/// A fixed slot value. Used where the retired device had no control at all and
/// the successor's default would change how it sounds.
constexpr SlotMapping fixed(int slot, const char* name, float value) {
    return {.targetSlot = slot, .targetName = name, .constant = value};
}

constexpr SlotMapping mapped(int slot, const char* name, int source,
                             float (*convert)(float, float)) {
    return {.targetSlot = slot, .targetName = name, .sourceIndex = source, .convert = convert};
}

constexpr SlotMapping merged(int slot, const char* name, int source, int source2,
                             float source2Default, float (*convert)(float, float)) {
    return {.targetSlot = slot,
            .targetName = name,
            .sourceIndex = source,
            .sourceIndex2 = source2,
            .source2Default = source2Default,
            .convert = convert};
}

struct RetiredDevice {
    const char* targetPluginId;
    const char* targetDisplayName;
    std::span<const char* const> retiredTypes;  // engine type + MAGDA load aliases
    std::span<const char* const> retiredNames;  // display names replaced on migration
    std::span<const SlotMapping> slots;
};

// --- 4-band EQ ("4bandEq") -> magda_eq -------------------------------------
// Tracktion's four fixed bands are low shelf / bell / bell / high shelf, which
// is exactly bands 1-4 of the compiled EQ. Its bands are individually
// switchable and default to off, so each mapped band is explicitly enabled and
// given its filter type. Bands 5-8 stay off, as does the output trim.
// Slot layout: 5 slots per band (enabled, type, freq, gain, q), output at 40.
// Band type ordinals: 0 HP, 1 LowShelf, 2 Bell, 3 HighShelf, 4 LP, 5 Notch.
constexpr const char* kEqTypes[] = {"4bandEq", "eq", "equaliser"};
constexpr const char* kEqNames[] = {"Equaliser", "EQ"};
constexpr SlotMapping kEqSlots[] = {
    fixed(0, "Band 1 Enabled", 1.0f),
    fixed(1, "Band 1 Type", 1.0f),  // LowShelf
    mapped(2, "Band 1 Freq", 0, eqFrequency),
    mapped(3, "Band 1 Gain", 1, eqGain),
    mapped(4, "Band 1 Q", 2, eqQ),

    fixed(5, "Band 2 Enabled", 1.0f),
    fixed(6, "Band 2 Type", 2.0f),  // Bell
    mapped(7, "Band 2 Freq", 3, eqFrequency),
    mapped(8, "Band 2 Gain", 4, eqGain),
    mapped(9, "Band 2 Q", 5, eqQ),

    fixed(10, "Band 3 Enabled", 1.0f),
    fixed(11, "Band 3 Type", 2.0f),  // Bell
    mapped(12, "Band 3 Freq", 6, eqFrequency),
    mapped(13, "Band 3 Gain", 7, eqGain),
    mapped(14, "Band 3 Q", 8, eqQ),

    fixed(15, "Band 4 Enabled", 1.0f),
    fixed(16, "Band 4 Type", 3.0f),  // HighShelf
    mapped(17, "Band 4 Freq", 9, eqFrequency),
    mapped(18, "Band 4 Gain", 10, eqGain),
    mapped(19, "Band 4 Q", 11, eqQ),
};
// Dropped: phase invert (index 12) — the compiled EQ has no polarity switch.

// --- Compressor ("compressor") -> magda_compressor -------------------------
// Tracktion parameter order: threshold gain, 1/ratio, attack, release, output
// gain, sidechain gain; the wrapper appended a sidechain-trigger toggle.
constexpr const char* kCompressorTypes[] = {"compressor"};
constexpr const char* kCompressorNames[] = {"Compressor"};
constexpr SlotMapping kCompressorSlots[] = {
    fixed(0, "Engine", 0.0f),  // Clean — Glue is a different circuit
    mapped(1, "Threshold", 0, gainToDb),
    mapped(2, "Ratio", 1, reciprocalRatio),
    mapped(3, "Attack", 2, compressorAttack),
    mapped(4, "Release", 3, compressorRelease),
    fixed(5, "Knee", 0.0f),  // Tracktion's detector is hard-knee
    mapped(8, "Output", 4, outputDb),
};
// Dropped: sidechain gain (5) and the sidechain trigger toggle (6) — the
// compiled compressor keys its sidechain from MAGDA's routing, not a parameter.

// --- Delay ("delay") -> magda_delay ----------------------------------------
// Tracktion exposes feedback and mix as parameters; the wrapper surfaced the
// delay length as a virtual parameter at index 2.
constexpr const char* kDelayTypes[] = {"delay"};
constexpr const char* kDelayNames[] = {"Delay"};
constexpr SlotMapping kDelaySlots[] = {
    mapped(0, "Time", 2, delayTime),
    fixed(2, "Sync", 0.0f),  // Tracktion's delay is free-running milliseconds
    mapped(3, "Feedback", 0, delayFeedback),
    mapped(4, "Mix", 1, unitInterval),
};

// --- Chorus ("chorus") -> magda_chorus -------------------------------------
// All four Tracktion controls were wrapper-virtual: depth (ms), speed (Hz),
// width, mix.
constexpr const char* kChorusTypes[] = {"chorus"};
constexpr const char* kChorusNames[] = {"Chorus"};
constexpr SlotMapping kChorusSlots[] = {
    fixed(0, "Voices", 0.0f),  // Tracktion sweeps a single delay line
    fixed(1, "Sync", 0.0f),
    mapped(2, "Rate", 1, lfoRateHz),
    mapped(4, "Depth", 0, chorusDepth),
    fixed(5, "Feedback", 0.0f),  // Tracktion hard-codes its feedback to zero
    mapped(6, "Mix", 3, unitInterval),
    mapped(7, "Width", 2, unitInterval),
};

// --- Phaser ("phaser") -> magda_phaser -------------------------------------
// Wrapper-virtual depth (octaves swept), rate (Hz) and feedback.
constexpr const char* kPhaserTypes[] = {"phaser"};
constexpr const char* kPhaserNames[] = {"Phaser"};
constexpr SlotMapping kPhaserSlots[] = {
    mapped(0, "Rate", 1, lfoRateHz),
    mapped(1, "Depth", 0, phaserDepth),
    mapped(2, "Feedback", 2, phaserFeedback),
    fixed(3, "Stages", 1.0f),  // Tracktion runs four all-pass stages
    fixed(6, "Mix", 0.5f),     // and sums the wet stage with the dry input
};

// --- Reverb ("reverb") -> magda_reverb -------------------------------------
// Tracktion parameter order: room size, damping, wet, dry, width, freeze.
constexpr const char* kReverbTypes[] = {"reverb"};
constexpr const char* kReverbNames[] = {"Reverb"};
constexpr SlotMapping kReverbSlots[] = {
    fixed(0, "Engine", 2.0f),                 // Room — the closest of the three to Freeverb
    merged(1, "Mix", 2, 3, 0.5f, reverbMix),  // 0.5 is Tracktion's dry default
    fixed(2, "Predelay", 0.0f),               // Tracktion's reverb has none
    mapped(3, "Decay", 0, unitToPercent),
    mapped(4, "Damping", 1, unitToPercent),
    mapped(7, "Width", 4, unitToWidthPercent),
};
// Dropped: freeze (5) — the compiled reverb has no infinite-hold mode.

// --- Pitch Shift ("pitchShifter") -> magda_pitch ----------------------------
constexpr const char* kPitchTypes[] = {"pitchShifter", "pitchshift"};
constexpr const char* kPitchNames[] = {"Pitch Shift"};
constexpr SlotMapping kPitchSlots[] = {
    fixed(0, "Engine", 0.0f),  // Shifter
    mapped(1, "Pitch", 0, pitchSemitones),
    fixed(4, "Mix", 1.0f),  // Tracktion's pitch shifter is fully wet
};

// --- Lowpass ("lowpass") -> magda_filter -----------------------------------
// One automatable frequency, plus the wrapper's virtual low/high pass toggle.
constexpr const char* kFilterTypes[] = {"lowpass"};
constexpr const char* kFilterNames[] = {"Lowpass", "Filter"};
constexpr SlotMapping kFilterSlots[] = {
    mapped(0, "Cutoff", 0, filterCutoff),
    fixed(1, "Resonance", 0.0f),  // Tracktion's is a plain non-resonant biquad
    fixed(2, "Drive", 0.0f),
    fixed(3, "Engine", 0.0f),  // SVF — the cleanest of the six
    mapped(4, "Mode", 1, filterMode),
};

constexpr RetiredDevice kRetiredDevices[] = {
    {"magda_eq", "EQ", kEqTypes, kEqNames, kEqSlots},
    {"magda_compressor", "Compressor", kCompressorTypes, kCompressorNames, kCompressorSlots},
    {"magda_delay", "Delay", kDelayTypes, kDelayNames, kDelaySlots},
    {"magda_chorus", "Chorus", kChorusTypes, kChorusNames, kChorusSlots},
    {"magda_phaser", "Phaser", kPhaserTypes, kPhaserNames, kPhaserSlots},
    {"magda_reverb", "Reverb", kReverbTypes, kReverbNames, kReverbSlots},
    {"magda_pitch", "Pitch", kPitchTypes, kPitchNames, kPitchSlots},
    {"magda_filter", "Filter", kFilterTypes, kFilterNames, kFilterSlots},
};

const RetiredDevice* findRetiredDevice(const juce::String& pluginId) {
    if (pluginId.isEmpty())
        return nullptr;

    for (const auto& device : kRetiredDevices)
        for (const auto* type : device.retiredTypes)
            if (pluginId.equalsIgnoreCase(type))
                return &device;

    return nullptr;
}

/// The retired device's own parameter array, keyed by the wrapper's parameter
/// index. A project that stored fewer parameters than the wrapper exposed (or
/// none at all) simply leaves the missing slots at the successor's defaults.
std::optional<float> retiredValue(const DeviceInfo& device, int index) {
    if (index < 0)
        return std::nullopt;

    // A parameter that never recorded its own index falls back to its position,
    // which is the same rule DeviceProcessor::syncFromDeviceInfo restores by —
    // so the migration reads a saved array exactly as the engine would have.
    for (size_t i = 0; i < device.parameters.size(); ++i) {
        const auto& param = device.parameters[i];
        const int paramIndex = param.paramIndex >= 0 ? param.paramIndex : static_cast<int>(i);
        if (paramIndex == index)
            return param.currentValue;
    }

    return std::nullopt;
}

/// Drop the links that addressed a device we just rewrote. Their `paramIndex`
/// is an index into a parameter list that no longer exists, so re-pointing them
/// would be guesswork; the retired devices were hidden long before macros and
/// automation shipped, so in practice there is nothing here to lose.
bool addressesMigratedDevice(const ControlTarget& target,
                             const std::set<ChainNodePath>& migratedPaths) {
    if (target.kind != ControlTarget::Kind::PluginParam)
        return false;
    return migratedPaths.count(target.devicePath) > 0;
}

void pruneLinks(MacroArray& macros, ModArray& mods, const std::set<ChainNodePath>& migratedPaths) {
    if (migratedPaths.empty())
        return;

    for (auto& macro : macros)
        std::erase_if(macro.links, [&migratedPaths](const MacroLink& link) {
            return addressesMigratedDevice(link.target, migratedPaths);
        });

    for (auto& mod : mods)
        std::erase_if(mod.links, [&migratedPaths](const ModLink& link) {
            return addressesMigratedDevice(link.target, migratedPaths);
        });
}

void collectChainElements(std::vector<ChainElement>& elements, const ChainNodePath& parentPath,
                          TrackId trackId, std::set<ChainNodePath>& migratedPaths);

void collectRack(RackInfo& rack, const ChainNodePath& rackPath, TrackId trackId,
                 std::set<ChainNodePath>& migratedPaths) {
    std::set<ChainNodePath> rackMigrated;
    for (auto& chain : rack.chains)
        collectChainElements(chain.elements, rackPath.withChain(chain.id), trackId, rackMigrated);

    pruneLinks(rack.macros, rack.mods, rackMigrated);
    migratedPaths.insert(rackMigrated.begin(), rackMigrated.end());
}

void collectChainElements(std::vector<ChainElement>& elements, const ChainNodePath& parentPath,
                          TrackId trackId, std::set<ChainNodePath>& migratedPaths) {
    for (auto& element : elements) {
        if (isDevice(element)) {
            auto& device = getDevice(element);
            if (!migrateRetiredDevice(device))
                continue;

            // A top-level device on a track keeps the legacy flat path shape,
            // which is what the control graph stored for it.
            migratedPaths.insert(parentPath.isTrackLevel
                                     ? ChainNodePath::topLevelDevice(trackId, device.id)
                                     : parentPath.withDevice(device.id));
        } else if (isRack(element)) {
            auto& rack = getRack(element);
            const auto rackPath = parentPath.isTrackLevel ? ChainNodePath::rack(trackId, rack.id)
                                                          : parentPath.withRack(rack.id);
            collectRack(rack, rackPath, trackId, migratedPaths);
        }
    }
}

// A preset carries no surrounding project, and its device ids are unique
// inside it, so a fragment matches links by id instead of by path.
std::set<DeviceId> migrateFragment(std::vector<ChainElement>& elements);

void pruneLinksById(MacroArray& macros, ModArray& mods, const std::set<DeviceId>& migratedIds) {
    if (migratedIds.empty())
        return;

    const auto isStale = [&migratedIds](const ControlTarget& target) {
        return target.kind == ControlTarget::Kind::PluginParam &&
               migratedIds.count(target.devicePath.getDeviceId()) > 0;
    };

    for (auto& macro : macros)
        std::erase_if(macro.links,
                      [&isStale](const MacroLink& link) { return isStale(link.target); });
    for (auto& mod : mods)
        std::erase_if(mod.links, [&isStale](const ModLink& link) { return isStale(link.target); });
}

std::set<DeviceId> migrateFragmentRack(RackInfo& rack) {
    std::set<DeviceId> migratedIds;
    for (auto& chain : rack.chains) {
        auto ids = migrateFragment(chain.elements);
        migratedIds.insert(ids.begin(), ids.end());
    }

    pruneLinksById(rack.macros, rack.mods, migratedIds);
    return migratedIds;
}

std::set<DeviceId> migrateFragment(std::vector<ChainElement>& elements) {
    std::set<DeviceId> migratedIds;
    for (auto& element : elements) {
        if (isDevice(element)) {
            auto& device = getDevice(element);
            if (migrateRetiredDevice(device))
                migratedIds.insert(device.id);
        } else if (isRack(element)) {
            auto ids = migrateFragmentRack(getRack(element));
            migratedIds.insert(ids.begin(), ids.end());
        }
    }
    return migratedIds;
}

}  // namespace

bool isRetiredDeviceId(const juce::String& pluginId) {
    return findRetiredDevice(pluginId) != nullptr;
}

bool migrateRetiredDevice(DeviceInfo& device) {
    const auto* retired = findRetiredDevice(device.pluginId);
    if (retired == nullptr)
        return false;

    std::vector<ParameterInfo> migrated;
    migrated.reserve(retired->slots.size());

    for (const auto& slot : retired->slots) {
        float value = slot.constant;

        if (slot.sourceIndex >= 0) {
            const auto source = retiredValue(device, slot.sourceIndex);
            if (!source)
                continue;  // never saved — leave the successor's default alone

            const auto source2 = retiredValue(device, slot.sourceIndex2);
            value = slot.convert(*source, source2.value_or(slot.source2Default));
        }

        ParameterInfo info;
        info.paramIndex = slot.targetSlot;
        info.name = slot.targetName;
        info.currentValue = value;
        migrated.push_back(std::move(info));
    }

    for (const auto* name : retired->retiredNames)
        if (device.name.isEmpty() || device.name.equalsIgnoreCase(name)) {
            device.name = retired->targetDisplayName;
            break;
        }

    device.pluginId = retired->targetPluginId;
    device.uniqueId = retired->targetPluginId;
    device.parameters = std::move(migrated);

    // Everything below described the retired plugin's parameter list, which the
    // successor does not share. The live device repopulates all of it from the
    // compiled slots as soon as it is created.
    device.pluginState.clear();
    device.wrapperParameters.clear();
    device.meters.clear();
    device.visibleParameters.clear();
    device.miniMixerParameters.clear();
    device.aiSoundDesignerParameters.clear();

    for (auto& macro : device.macros)
        std::erase_if(macro.links, [](const MacroLink& link) {
            return link.target.kind == ControlTarget::Kind::PluginParam;
        });
    for (auto& mod : device.mods)
        std::erase_if(mod.links, [](const ModLink& link) {
            return link.target.kind == ControlTarget::Kind::PluginParam;
        });

    return true;
}

std::vector<ChainNodePath> migrateRetiredDevicesInTrack(TrackInfo& track) {
    std::set<ChainNodePath> migratedPaths;

    collectChainElements(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id), track.id,
                         migratedPaths);

    for (auto& element : track.chain.postFxChainElements)
        if (migrateRetiredDevice(element.device))
            migratedPaths.insert(ChainNodePath::postFxDevice(track.id, element.device.id));

    for (auto& element : track.chain.mixerAnalysisElements)
        if (migrateRetiredDevice(element.device))
            migratedPaths.insert(ChainNodePath::mixerAnalysisDevice(track.id, element.device.id));

    pruneLinks(track.macros, track.mods, migratedPaths);

    return {migratedPaths.begin(), migratedPaths.end()};
}

void migrateRetiredDevicesInProject(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                                    std::vector<AutomationLaneInfo>& lanes,
                                    std::vector<AutomationClipInfo>& clips) {
    std::set<ChainNodePath> migratedPaths;

    auto collect = [&migratedPaths](TrackInfo& track) {
        auto paths = migrateRetiredDevicesInTrack(track);
        migratedPaths.insert(paths.begin(), paths.end());
    };

    for (auto& track : tracks)
        collect(track);
    if (masterTrack != nullptr)
        collect(*masterTrack);

    if (migratedPaths.empty())
        return;

    std::set<AutomationLaneId> droppedLanes;
    for (const auto& lane : lanes)
        if (addressesMigratedDevice(lane.target, migratedPaths))
            droppedLanes.insert(lane.id);

    if (droppedLanes.empty())
        return;

    std::erase_if(lanes, [&droppedLanes](const AutomationLaneInfo& lane) {
        return droppedLanes.count(lane.id) > 0;
    });
    std::erase_if(clips, [&droppedLanes](const AutomationClipInfo& clip) {
        return droppedLanes.count(clip.laneId) > 0;
    });
}

void migrateRetiredDevicesInChain(std::vector<ChainElement>& elements) {
    migrateFragment(elements);
}

void migrateRetiredDevicesInRack(RackInfo& rack) {
    migrateFragmentRack(rack);
}

}  // namespace magda::legacy_devices
