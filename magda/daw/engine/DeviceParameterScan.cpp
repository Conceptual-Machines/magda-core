#include "DeviceParameterScan.hpp"

#include <array>
#include <memory>
#include <utility>

#include "../audio/plugins/DeviceCatalogParameters.hpp"
#include "../audio/plugins/MagdaDevice.hpp"
#include "../core/ParameterUtils.hpp"

namespace magda {
namespace {

namespace audio = daw::audio;

/// One scan record off one parameter's own description. `position` is where the
/// record lands in the result rather than the slot it was read from: a skipped
/// parameter leaves the two apart, and a detection result is applied by
/// position.
ScannedPluginParameter scanRecord(const ParameterInfo& info, int position) {
    constexpr std::array<float, 5> samplePoints{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    ScannedPluginParameter scanned;
    scanned.name = info.name;
    scanned.stableId = info.stableId;
    scanned.defaultValue = info.defaultValue;
    scanned.unit = info.unit;
    scanned.rangeMin = info.minValue;
    scanned.rangeMax = info.maxValue;
    // A declared anchor is the real value a parameter asks to sit at the middle
    // of its own travel, which is what rangeCenter means. Asked rather than
    // compared against zero: a range that straddles zero has a real value there.
    scanned.rangeCenter = ParameterUtils::hasScaleAnchor(info)
                              ? info.scaleAnchor
                              : (info.minValue + info.maxValue) * 0.5f;
    scanned.scale = info.scale;
    scanned.valueTable = info.valueTable.empty() ? info.choices : info.valueTable;

    scanned.scanInput.paramIndex = position;
    scanned.scanInput.name = info.name;
    scanned.scanInput.label = info.unit;
    scanned.scanInput.rangeMin = info.minValue;
    scanned.scanInput.rangeMax = info.maxValue;
    scanned.scanInput.stateCount = static_cast<int>(info.choices.size());

    // Through the model's own formatter, which is what the host wraps one of
    // these parameters in as well, so Detect reads the strings the UI shows
    // rather than a second opinion about them.
    if (!info.choices.empty()) {
        scanned.scanInput.displayTexts = info.choices;
    } else {
        for (const auto sample : samplePoints)
            scanned.scanInput.displayTexts.push_back(
                ParameterUtils::formatValue(ParameterUtils::normalizedToReal(sample, info), info));
    }

    return scanned;
}

}  // namespace

std::vector<ScannedPluginParameter> scanDeviceParameters(const juce::String& pluginId) {
    std::vector<ScannedPluginParameter> result;

    const auto device = audio::createDetachedDevice(pluginId);
    if (device == nullptr)
        return result;

    for (const auto& info : device->parameters()) {
        if (info.name.isEmpty())
            continue;

        result.push_back(scanRecord(info, static_cast<int>(result.size())));
    }

    return result;
}

}  // namespace magda
