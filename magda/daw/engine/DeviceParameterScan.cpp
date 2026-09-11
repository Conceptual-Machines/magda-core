#include "DeviceParameterScan.hpp"

#include <array>
#include <memory>
#include <utility>

#include "../audio/plugins/InternalPluginRegistry.hpp"
#include "../audio/plugins/MagdaDevice.hpp"
#include "../audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "../core/ParameterUtils.hpp"

namespace magda {
namespace {

namespace audio = daw::audio;

/// A device built to be asked questions and dropped. No session key: the
/// services behind one are a running engine's, and nothing here may reach them.
/// EngineDeviceFactory does the same for a device it is about to run, but that
/// lives in the native engine's own target and the fork cannot link it.
std::unique_ptr<audio::MagdaDevice> createDetachedDevice(const juce::String& pluginId) {
    juce::ValueTree state(juce::Identifier("PLUGIN"));
    state.setProperty(juce::Identifier("type"), pluginId, nullptr);
    const audio::DevicePluginCreationContext context{
        .sessionKey = {}, .state = std::move(state), .isNewPlugin = true};

    // The internal registry first, because that is the catalog an id is
    // canonicalised against; a compiled device is not in it.
    if (const auto* spec = audio::findInternalPluginSpec(pluginId); spec != nullptr)
        if (spec->createDevice != nullptr)
            return spec->createDevice(context);

    if (const auto* spec = audio::compiled::findCompiledPluginSpec(pluginId); spec != nullptr)
        if (spec->createDevice != nullptr)
            return spec->createDevice(context);

    return {};
}

}  // namespace

std::vector<ScannedPluginParameter> scanDeviceParameters(const juce::String& pluginId) {
    std::vector<ScannedPluginParameter> result;

    const auto device = createDetachedDevice(pluginId);
    if (device == nullptr)
        return result;

    // One scan record off one parameter's own description. `position` is where
    // the record lands in the result rather than the slot it was read from: a
    // skipped parameter leaves the two apart, and a detection result is applied
    // by position.
    const auto scanRecord = [](const ParameterInfo& info, int position) {
        constexpr std::array<float, 5> samplePoints{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

        ScannedPluginParameter scanned;
        scanned.name = info.name;
        scanned.stableId = info.stableId;
        scanned.defaultValue = info.defaultValue;
        scanned.unit = info.unit;
        scanned.rangeMin = info.minValue;
        scanned.rangeMax = info.maxValue;
        // scaleAnchor is the real value a parameter asks to sit at the middle
        // of its own travel, which is what rangeCenter means.
        scanned.rangeCenter =
            info.scaleAnchor > 0.0f ? info.scaleAnchor : (info.minValue + info.maxValue) * 0.5f;
        scanned.scale = info.scale;
        scanned.valueTable = info.valueTable.empty() ? info.choices : info.valueTable;

        scanned.scanInput.paramIndex = position;
        scanned.scanInput.name = info.name;
        scanned.scanInput.label = info.unit;
        scanned.scanInput.rangeMin = info.minValue;
        scanned.scanInput.rangeMax = info.maxValue;
        scanned.scanInput.stateCount = static_cast<int>(info.choices.size());

        // Through the model's own formatter, which is what the host wraps one
        // of these parameters in as well, so Detect reads the strings the UI
        // shows rather than a second opinion about them.
        if (!info.choices.empty()) {
            scanned.scanInput.displayTexts = info.choices;
        } else {
            for (const auto sample : samplePoints)
                scanned.scanInput.displayTexts.push_back(ParameterUtils::formatValue(
                    ParameterUtils::normalizedToReal(sample, info), info));
        }

        return scanned;
    };

    const int count = device->parameterCount();
    for (int index = 0; index < count; ++index) {
        const auto info = device->parameterInfo(index);
        if (info.name.isEmpty())
            continue;

        result.push_back(scanRecord(info, static_cast<int>(result.size())));
    }

    return result;
}

}  // namespace magda
