#include "ExternalPluginState.hpp"

#include <algorithm>

#include "core/ParameterUtils.hpp"

namespace magda {

namespace {

/// The slots the host puts in front of a plugin's own: dry, then wet.
constexpr int kWrapperParameterCount = 2;

/// The model's record of the parameter at @p index, or none.
///
/// Both buckets, because MAGDA splits the incumbent's one list in two: the
/// plugin's parameters and the wrapper pair it never declared. Both carry the
/// index a project addresses them by.
const ParameterInfo* modelParameterAt(const DeviceInfo& device, int index) {
    for (const auto* bucket : {&device.parameters, &device.wrapperParameters})
        for (const auto& info : *bucket)
            if (info.paramIndex == index)
                return &info;

    return nullptr;
}

/// The saved chunk, decoded. Empty for a device that saved none and for a
/// string that is not base64, which is what a project truncated by a failed
/// write looks like.
juce::MemoryBlock decodeSavedChunk(const juce::String& savedState) {
    juce::MemoryBlock chunk;

    if (savedState.isEmpty())
        return chunk;

    if (!chunk.fromBase64Encoding(savedState))
        chunk.reset();

    return chunk;
}

}  // namespace

std::vector<juce::AudioProcessorParameter*> hostParameterOrder(
    const juce::AudioPluginInstance& instance) {
    std::vector<juce::AudioProcessorParameter*> order;

    // The wrapper pair, which nothing on the plugin stands behind.
    order.resize(kWrapperParameterCount, nullptr);

    for (auto* parameter : instance.getParameters())
        if (parameter != nullptr && parameter->isAutomatable())
            order.push_back(parameter);

    return order;
}

bool applySavedPluginState(juce::AudioPluginInstance& instance, const DeviceInfo& device) {
    const auto order = hostParameterOrder(instance);

    // The array first. A parameter the model does not describe keeps whatever
    // the plugin was built with: the project has nothing to say about it, which
    // is what a plugin that has gained a parameter since the project was saved
    // looks like.
    for (int index = 0; index < static_cast<int>(order.size()); ++index) {
        auto* parameter = order[static_cast<std::size_t>(index)];
        if (parameter == nullptr)
            continue;

        const auto* info = modelParameterAt(device, index);
        if (info == nullptr)
            continue;

        parameter->setValue(
            std::clamp(ParameterUtils::realToNormalized(info->currentValue, *info), 0.0f, 1.0f));
    }

    const auto chunk = decodeSavedChunk(device.pluginState);
    if (chunk.getSize() == 0)
        return false;

    // DeviceInfo::vst3Preset is deliberately not applied here. It is the
    // portable .vstpreset a DAWproject carries, applied once on import and
    // cleared, and its own field comment says what this relies on: interchange
    // only, native state is pluginState. When the native engine grows an import
    // path of its own (#2244) it belongs in this function beside the chunk,
    // rather than in whichever engine happens to be loading the project.
    try {
        instance.setStateInformation(chunk.getData(), static_cast<int>(chunk.getSize()));
    } catch (...) {
        // Third-party code, handed the input it is least likely to have been
        // tested against. The baseline above is still standing, which is where
        // a plugin that refuses a chunk quietly leaves things anyway.
        return false;
    }

    return true;
}

std::vector<RestoredParameter> snapshotHostParameters(const juce::AudioPluginInstance& instance) {
    const auto order = hostParameterOrder(instance);

    std::vector<RestoredParameter> restored;
    restored.reserve(order.size());

    for (int index = 0; index < static_cast<int>(order.size()); ++index)
        if (auto* parameter = order[static_cast<std::size_t>(index)]; parameter != nullptr)
            restored.push_back({.paramIndex = index, .value = parameter->getValue()});

    return restored;
}

void applyRestoredParameters(DeviceInfo& device, const std::vector<RestoredParameter>& restored) {
    for (const auto& parameter : restored)
        for (auto* bucket : {&device.parameters, &device.wrapperParameters})
            for (auto& info : *bucket)
                if (info.paramIndex == parameter.paramIndex)
                    info.currentValue = ParameterUtils::normalizedToReal(
                        parameter.value, ParameterUtils::domainOf(info));
}

}  // namespace magda
