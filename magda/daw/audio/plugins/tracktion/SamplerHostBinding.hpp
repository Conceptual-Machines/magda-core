#pragma once

#include "core/ParameterUtils.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"

namespace magda::daw::audio::tracktion_adapter {

/**
 * @brief Driving a sampler that sits inside the host's device wrapper.
 *
 * Callers work in display units and the wrapper's parameters are normalised.
 * A write goes to BOTH sides: `syncParametersToDevice()` pushes on every
 * `applyToBuffer`, so a write to the device alone is undone at the next render,
 * and the device holds the value between renders, so a write to the parameter
 * alone is invisible until one happens. A load travels the other way — the
 * device derives the markers — and is pulled back.
 */

inline float samplerSlotDisplayValue(const te::Plugin& plugin, int index) {
    const auto* sampler = deviceFromPlugin<MagdaSamplerPlugin>(&plugin);
    return sampler != nullptr ? sampler->displayValue(index) : 0.0f;
}

/// setParameterFromHost, not setParameter: the latter is silently dropped once
/// a macro or mod is attached to the parameter.
inline void setSamplerSlotDisplayValue(te::Plugin& plugin, int index, float displayValue,
                                       juce::NotificationType notification) {
    auto* sampler = deviceFromPlugin<MagdaSamplerPlugin>(&plugin);
    if (sampler == nullptr || index < 0 || index >= MagdaSamplerPlugin::kNumParams)
        return;

    const float normalized =
        ParameterUtils::realToNormalized(displayValue, sampler->parameterInfo(index));

    if (auto* wrapper = dynamic_cast<TracktionMagdaDevicePlugin*>(&plugin)) {
        if (auto* parameter = wrapper->parameterForDeviceSlot(index))
            parameter->setParameterFromHost(normalized, notification);
    }
    sampler->setParameterValue(index, normalized);
}

/// Load @p file into the sampler and take the markers it derived from the audio
/// back into the host's parameters. Message thread only.
inline void loadSamplerSample(te::Plugin& plugin, const juce::File& file) {
    if (auto* sampler = deviceFromPlugin<MagdaSamplerPlugin>(&plugin))
        sampler->loadSample(file);
    if (auto* wrapper = dynamic_cast<TracktionMagdaDevicePlugin*>(&plugin))
        wrapper->pullParametersFromDevice();
}

/// The same, for a file that merely moved: the sampler puts the root note and
/// markers back rather than re-deriving them. Message thread only.
inline void relocateSamplerSample(te::Plugin& plugin, const juce::File& file) {
    if (auto* sampler = deviceFromPlugin<MagdaSamplerPlugin>(&plugin))
        sampler->relocateSample(file);
    if (auto* wrapper = dynamic_cast<TracktionMagdaDevicePlugin*>(&plugin))
        wrapper->pullParametersFromDevice();
}

}  // namespace magda::daw::audio::tracktion_adapter
