#include "plugins/DeviceStateHydration.hpp"

#include <algorithm>
#include <array>
#include <memory>

#include "core/ChainWalk.hpp"
#include "core/DeviceState.hpp"
#include "core/ParameterUtils.hpp"
#include "core/TrackInfo.hpp"
#include "plugins/DevicePluginHandle.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/MagdaDevice.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::device_state_hydration {

namespace {

namespace ds = magda::device_state;

/// The domain a pre-#2317 document's parameter record used.
enum class ParamDomain { Display, Normalized };

/// Devices that were already behind `TracktionMagdaDevicePlugin` before the
/// `paramsAreDisplayDomain` marker existed. Their unmarked documents have TWO
/// eras: 0.19 captures from the retired host-native plugins hold display
/// values, later captures from the wrapper hold normalised slots. Everything
/// wrapped since the marker always writes it, so an unmarked document for any
/// other device predates its wrapper and can only be display-domain.
bool wrappedBeforeDomainMarker(const juce::String& deviceType) {
    static constexpr std::array<const char*, 5> kPreMarkerWrapped{
        "toneGenerator", "sidechain", "faust-fx", "oscilloscope", "spectrumanalyzer",
    };
    return std::any_of(kPreMarkerWrapped.begin(), kPreMarkerWrapped.end(),
                       [&deviceType](const char* id) { return deviceType == id; });
}

/// Released-state-first fallback for a two-era document with no provenance:
/// any value outside [0, 1] can only be display-domain (a 0.19 sidechain
/// document always carries one - its release defaults to 15 ms). The residue -
/// every value inside the unit interval - is read as normalised, which is
/// exact for slots whose display range IS the unit interval and correct for
/// wrapper-era captures of the rest.
bool anyValueOutsideUnitInterval(const ds::Doc& doc) {
    return std::any_of(doc.params.begin(), doc.params.end(), [](const ds::ParamValue& saved) {
        return saved.value < -1.0e-4f || saved.value > 1.0f + 1.0e-4f;
    });
}

ParamDomain resolveDomain(const DeviceInfo& device, const ds::Doc& doc,
                          const Provenance& provenance) {
    if (doc.paramsAreDisplayDomain)
        return ParamDomain::Display;

    // The compiled Faust pack's parameters were always normalised slots, and
    // so were its documents, in every era.
    if (compiled::findCompiledPluginSpec(device.pluginId) != nullptr)
        return ParamDomain::Normalized;

    if (wrappedBeforeDomainMarker(device.pluginId)) {
        if (provenance.savedBeforeWrapperCutover)
            return ParamDomain::Display;
        return anyValueOutsideUnitInterval(doc) ? ParamDomain::Display : ParamDomain::Normalized;
    }

    // Every other unmarked document was captured from a plugin whose
    // parameters ran in their own display range: either the device has never
    // been wrapped, or it crossed after the marker existed and the document
    // predates the crossing.
    return ParamDomain::Display;
}

/// The saved root as the tree `MagdaDevice::restoreState()` takes - same shape
/// the native engine's device factory builds (`EngineDeviceFactory.cpp`).
juce::ValueTree treeFromNode(const ds::Node& node) {
    juce::ValueTree tree(node.type.isNotEmpty() ? juce::Identifier(node.type)
                                                : juce::Identifier("PLUGIN"));
    for (int i = 0; i < node.props.size(); ++i)
        tree.setProperty(node.props.getName(i), node.props.getValueAt(i), nullptr);
    for (const auto& child : node.children)
        if (child.type.isNotEmpty())
            tree.appendChild(treeFromNode(child), nullptr);
    return tree;
}

/// A throwaway SDK device for the parameter metadata a normalised value needs
/// to become a display one. Restored from the document's own root first, for
/// the devices whose parameter set depends on it (the runtime Faust device
/// reads its slots out of its dsp source). Null for a device that has not
/// crossed to MagdaDevice, whose documents were never normalised.
std::unique_ptr<MagdaDevice> metadataDevice(const juce::String& pluginId, const ds::Doc& doc) {
    auto create = [&pluginId]() -> std::unique_ptr<MagdaDevice> {
        juce::ValueTree state{juce::Identifier("PLUGIN")};
        state.setProperty(juce::Identifier("type"), pluginId, nullptr);
        const DevicePluginCreationContext context{
            .sessionKey = {}, .state = std::move(state), .isNewPlugin = true};
        if (const auto* spec = findInternalPluginSpec(pluginId);
            spec != nullptr && spec->createDevice != nullptr)
            return spec->createDevice(context);
        if (const auto* spec = compiled::findCompiledPluginSpec(pluginId);
            spec != nullptr && spec->createDevice != nullptr)
            return spec->createDevice(context);
        return {};
    };

    auto device = create();
    if (device != nullptr) {
        auto tree = treeFromNode(doc.root);
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

/// A hydrated entry for a device without SDK metadata: value and identity only,
/// with a range wide enough to hold the value. The device's processor fills in
/// the real metadata at registration, preserving this value.
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

    // Unparseable reads as 0.0: no build old enough to write no version at all
    // postdates the cutover.
    return {.savedBeforeWrapperCutover = major == 0 && minor < 20};
}

bool hydrateParametersFromDeviceState(DeviceInfo& device, const Provenance& provenance) {
    if (device.format != PluginFormat::Internal || device.pluginState.isEmpty())
        return false;

    const auto doc = ds::decode(device.pluginState);
    if (!doc || doc->params.empty())
        return false;

    std::vector<const ds::ParamValue*> missing;
    for (const auto& saved : doc->params)
        if (saved.index >= 0 && !modelCarries(device, saved))
            missing.push_back(&saved);
    if (missing.empty())
        return false;

    const bool arrayWasEmpty = device.parameters.empty();
    const auto domain = resolveDomain(device, *doc, provenance);

    // Real slot metadata whenever the device has an SDK factory, whatever the
    // domain: the plan's value layer converts through the MODEL's ParameterInfo
    // ranges, so a made-up range would corrupt a headless native render before
    // any live processor could refresh it. The minimal fallback is reserved for
    // devices without a factory - exactly the ones the native engine refuses to
    // build, so nothing downstream converts through the placeholder.
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
        // A normalised value without slot metadata is meaningless and is
        // dropped: there is no device to run it on either.
    }

    // A fully hydrated array keeps the device's own display order; entries
    // appended to a partial one land at the end, which only affects display
    // order and only until the processor next refreshes the metadata.
    if (added && arrayWasEmpty)
        std::stable_sort(device.parameters.begin(), device.parameters.end(),
                         [](const ParameterInfo& a, const ParameterInfo& b) {
                             return a.paramIndex < b.paramIndex;
                         });

    return added;
}

void hydrateChainElements(std::vector<ChainElement>& elements, const Provenance& provenance) {
    chain_walk::forEachDevice(elements, ChainNodePath{}, chain_walk::Pads::Enter,
                              [&provenance](DeviceInfo& device, const ChainNodePath&) {
                                  hydrateParametersFromDeviceState(device, provenance);
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
            hydrateParametersFromDeviceState(element.device, provenance);
        for (auto& element : track.chain.mixerAnalysisElements)
            hydrateParametersFromDeviceState(element.device, provenance);
    };

    for (auto& track : tracks)
        hydrateTrack(track);
    if (masterTrack != nullptr)
        hydrateTrack(*masterTrack);
}

}  // namespace magda::daw::audio::device_state_hydration
