#include "plugins/DeviceStateHydration.hpp"

#include <algorithm>
#include <array>
#include <memory>

#include "core/ChainWalk.hpp"
#include "core/DeviceParamMigrations.hpp"
#include "core/DeviceState.hpp"
#include "core/ParameterUtils.hpp"
#include "core/TrackInfo.hpp"
#include "plugins/DeviceCatalogParameters.hpp"
#include "plugins/DevicePluginHandle.hpp"
#include "plugins/MagdaDevice.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::device_state_hydration {

namespace {

namespace ds = magda::device_state;

/// The domain a pre-#2317 document's parameter record used.
enum class ParamDomain { Display, Normalized };

/// Devices wrapped before the `paramsAreDisplayDomain` marker existed: their
/// unmarked documents may hold display values (0.19) or normalised slots (later).
bool wrappedBeforeDomainMarker(const juce::String& deviceType) {
    static constexpr std::array<const char*, 5> kPreMarkerWrapped{
        "toneGenerator", "sidechain", "faust-fx", "oscilloscope", "spectrumanalyzer",
    };
    return std::any_of(kPreMarkerWrapped.begin(), kPreMarkerWrapped.end(),
                       [&deviceType](const char* id) { return deviceType == id; });
}

/// A two-era document with no provenance: a value outside [0, 1] can only be
/// display-domain; all inside is read as normalised.
bool anyValueOutsideUnitInterval(const ds::Doc& doc) {
    return std::any_of(doc.params.begin(), doc.params.end(), [](const ds::ParamValue& saved) {
        return saved.value < -1.0e-4f || saved.value > 1.0f + 1.0e-4f;
    });
}

ParamDomain resolveDomain(const DeviceInfo& device, const ds::Doc& doc,
                          const Provenance& provenance) {
    if (doc.paramsAreDisplayDomain)
        return ParamDomain::Display;

    // The compiled Faust pack's documents were normalised slots in every era.
    if (compiled::findCompiledPluginSpec(device.pluginId) != nullptr)
        return ParamDomain::Normalized;

    if (wrappedBeforeDomainMarker(device.pluginId)) {
        if (provenance.savedBeforeWrapperCutover)
            return ParamDomain::Display;
        return anyValueOutsideUnitInterval(doc) ? ParamDomain::Display : ParamDomain::Normalized;
    }

    // Every other unmarked document predates its device's wrapper.
    return ParamDomain::Display;
}

/// A throwaway SDK device for the parameter metadata, restored from the
/// document's root for devices whose parameter set depends on it. Null for a
/// device that is not a MagdaDevice.
std::unique_ptr<MagdaDevice> metadataDevice(const juce::String& pluginId, const ds::Doc& doc) {
    auto device = createDetachedDevice(pluginId);
    if (device != nullptr) {
        auto tree = ds::toValueTree(doc.root);
        tree.setProperty(juce::Identifier("type"), doc.deviceType, nullptr);
        device->restoreState(tree);
    }
    return device;
}

/// Whether the model already carries the saved parameter, by frozen index
/// first and stable id as the re-seat fallback (`DeviceParamSchema.hpp`).
bool modelCarries(const DeviceInfo& device, const ds::ParamValue& saved) {
    if (device.findParameterByIndex(saved.index) != nullptr)
        return true;
    if (saved.id.isEmpty())
        return false;
    return std::any_of(device.parameters.begin(), device.parameters.end(),
                       [&saved](const ParameterInfo& p) { return p.stableId == saved.id; });
}

/// An entry for a device without SDK metadata: value and identity, with a range
/// wide enough to hold the value. The processor fills in the rest at registration.
ParameterInfo minimalEntry(const ds::ParamValue& saved) {
    ParameterInfo info;
    info.paramIndex = saved.index;
    info.stableId = saved.id;
    info.name = saved.id;
    info.minValue = std::min(0.0f, saved.value);
    info.maxValue = std::max(1.0f, saved.value);
    info.defaultValue = saved.value;
    info.currentValue = saved.value;
    return info;
}

}  // namespace

Provenance provenanceFromMagdaVersion(const juce::String& version) {
    const auto trimmed = version.trim();
    const int major = trimmed.upToFirstOccurrenceOf(".", false, false).getIntValue();
    const auto rest = trimmed.fromFirstOccurrenceOf(".", false, false);
    const int minor = rest.upToFirstOccurrenceOf(".", false, false).getIntValue();

    // Unparseable reads as 0.0, which is old.
    return {.savedBeforeWrapperCutover = major == 0 && minor < 20};
}

bool hydrateParametersFromDeviceState(DeviceInfo& device, const Provenance& provenance) {
    if (device.format != PluginFormat::Internal || !device.hasPluginState())
        return false;

    const auto doc = ds::decode(device.pluginState);
    if (!doc || doc->params.empty())
        return false;

    // A renumbered parameter order is identified by the record's own length,
    // since an old preset has no model array. Remap before matching, or an old
    // index lands on the wrong slot. Dropped indices stay dropped.
    auto savedParams = doc->params;
    const auto* migration = device_param_migrations::findMigrationForSavedCount(
        device.pluginId, static_cast<int>(savedParams.size()));
    if (migration != nullptr) {
        std::vector<ds::ParamValue> remapped;
        remapped.reserve(savedParams.size());
        for (auto saved : savedParams) {
            const auto newIndex =
                device_param_migrations::migratedParamIndex(*migration, saved.index);
            if (!newIndex.has_value())
                continue;
            saved.index = *newIndex;
            remapped.push_back(std::move(saved));
        }
        savedParams = std::move(remapped);
    }

    std::vector<const ds::ParamValue*> missing;
    for (const auto& saved : savedParams)
        if (saved.index >= 0 && !modelCarries(device, saved))
            missing.push_back(&saved);
    if (missing.empty())
        return false;

    const bool arrayWasEmpty = device.parameters.empty();
    const auto domain = resolveDomain(device, *doc, provenance);

    // Real slot metadata whenever the device has an SDK factory: the plan
    // converts through the model's ranges, so a placeholder range would corrupt
    // a headless native render. Devices without a factory never reach one.
    const auto metadata = metadataDevice(device.pluginId, *doc);

    bool added = false;
    for (const auto* saved : missing) {
        const bool haveSlot = metadata != nullptr && saved->index < metadata->parameterCount();
        if (haveSlot) {
            auto info = metadata->parameterInfo(saved->index);
            info.paramIndex = saved->index;
            if (info.stableId.isEmpty())
                info.stableId = saved->id;
            info.currentValue = domain == ParamDomain::Normalized
                                    ? ParameterUtils::normalizedToReal(saved->value, info)
                                    : saved->value;
            device.parameters.push_back(std::move(info));
            added = true;
        } else if (domain == ParamDomain::Display) {
            device.parameters.push_back(minimalEntry(*saved));
            added = true;
        }
        // A normalised value without slot metadata is dropped.
    }

    // Parameters the migrated order added, seeded so the device keeps doing
    // what the old one did. Seed values are display-domain (DeviceParamMigrations.hpp).
    if (migration != nullptr) {
        for (const auto& seed : migration->seeded) {
            if (device.findParameterByIndex(seed.index) != nullptr)
                continue;
            if (metadata != nullptr && seed.index < metadata->parameterCount()) {
                auto info = metadata->parameterInfo(seed.index);
                info.paramIndex = seed.index;
                info.currentValue = seed.value;
                device.parameters.push_back(std::move(info));
            } else {
                ParameterInfo info;
                info.paramIndex = seed.index;
                info.name = seed.name != nullptr ? juce::String(seed.name) : juce::String();
                info.minValue = std::min(0.0f, seed.value);
                info.maxValue = std::max(1.0f, seed.value);
                info.defaultValue = seed.value;
                info.currentValue = seed.value;
                device.parameters.push_back(std::move(info));
            }
            added = true;
        }
    }

    // A fully hydrated array keeps the device's own display order.
    if (added && arrayWasEmpty)
        std::stable_sort(device.parameters.begin(), device.parameters.end(),
                         [](const ParameterInfo& a, const ParameterInfo& b) {
                             return a.paramIndex < b.paramIndex;
                         });

    return added;
}

void completeDeviceParameters(DeviceInfo& device, const Provenance& provenance) {
    hydrateParametersFromDeviceState(device, provenance);
    seedDeclaredParameters(device);
}

void hydrateChainElements(std::vector<ChainElement>& elements, const Provenance& provenance) {
    chain_walk::forEachDevice(elements, ChainNodePath{}, chain_walk::Pads::Enter,
                              [&provenance](DeviceInfo& device, const ChainNodePath&) {
                                  completeDeviceParameters(device, provenance);
                                  return true;
                              });
}

void hydrateRack(RackInfo& rack, const Provenance& provenance) {
    for (auto& chain : rack.chains)
        hydrateChainElements(chain.elements, provenance);
}

void hydrateStagedProject(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                          const juce::String& magdaVersion) {
    const auto provenance = provenanceFromMagdaVersion(magdaVersion);

    auto hydrateTrack = [&provenance](TrackInfo& track) {
        hydrateChainElements(track.chain.fxChainElements, provenance);
        for (auto& element : track.chain.postFxChainElements)
            completeDeviceParameters(element.device, provenance);
        for (auto& element : track.chain.mixerAnalysisElements)
            completeDeviceParameters(element.device, provenance);
    };

    for (auto& track : tracks)
        hydrateTrack(track);
    if (masterTrack != nullptr)
        hydrateTrack(*masterTrack);
}

}  // namespace magda::daw::audio::device_state_hydration
