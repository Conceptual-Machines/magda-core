#include "compiled/CompiledPluginPresentation.hpp"

#include <algorithm>

namespace magda::daw::ui {

// Per-device presentation specs live next to each curve view (or the
// wrapper if there's no curve view). Add a new compiled plugin by
// writing a getMagdaXxxPresentation() accessor and wiring it in
// kAllPresentations below.
const CompiledPresentationSpec& getMagdaFilterPresentation();
const CompiledPresentationSpec& getMagdaSaturatorPresentation();
const CompiledPresentationSpec& getMagdaDelayPresentation();
const CompiledPresentationSpec& getMagdaGrainDelayPresentation();
const CompiledPresentationSpec& getMagdaGritPresentation();
const CompiledPresentationSpec& getMagdaMultibandPresentation();
const CompiledPresentationSpec& getMagdaPhaserPresentation();
const CompiledPresentationSpec& getMagdaCompressorPresentation();
const CompiledPresentationSpec& getMagdaModPresentation();
const CompiledPresentationSpec& getMagdaChorusPresentation();
const CompiledPresentationSpec& getMagdaFlangerPresentation();
const CompiledPresentationSpec& getMagdaRingModPresentation();
const CompiledPresentationSpec& getMagdaFreqShiftPresentation();
const CompiledPresentationSpec& getMagdaLimiterPresentation();
const CompiledPresentationSpec& getMagdaClipperPresentation();
const CompiledPresentationSpec& getMagdaReverbPresentation();
const CompiledPresentationSpec& getMagdaEqPresentation();
const CompiledPresentationSpec& getMagdaDimensionPresentation();
const CompiledPresentationSpec& getMagdaPitchPresentation();
const CompiledPresentationSpec& getMagdaBitcrusherPresentation();

namespace {

const CompiledPresentationSpec* const kAllPresentations[] = {
    &getMagdaFilterPresentation(),    &getMagdaSaturatorPresentation(),
    &getMagdaDelayPresentation(),     &getMagdaGrainDelayPresentation(),
    &getMagdaGritPresentation(),      &getMagdaMultibandPresentation(),
    &getMagdaPhaserPresentation(),    &getMagdaCompressorPresentation(),
    &getMagdaModPresentation(),       &getMagdaChorusPresentation(),
    &getMagdaFlangerPresentation(),   &getMagdaRingModPresentation(),
    &getMagdaFreqShiftPresentation(), &getMagdaLimiterPresentation(),
    &getMagdaClipperPresentation(),   &getMagdaReverbPresentation(),
    &getMagdaEqPresentation(),        &getMagdaDimensionPresentation(),
    &getMagdaPitchPresentation(),     &getMagdaBitcrusherPresentation(),
};

}  // namespace

std::span<const CompiledPresentationSpec* const> getAllCompiledPresentations() {
    return {kAllPresentations, std::size(kAllPresentations)};
}

const CompiledPresentationSpec* findCompiledPresentation(const juce::String& pluginId) {
    for (const auto* spec : kAllPresentations) {
        if (pluginId.equalsIgnoreCase(spec->pluginId))
            return spec;
    }
    return nullptr;
}

bool shouldSuppressLegacyUi(const juce::String& pluginId, LegacyUiKind kind) {
    const auto* spec = findCompiledPresentation(pluginId);
    if (spec == nullptr)
        return false;
    for (LegacyUiKind k : spec->suppressLegacyUis)
        if (k == kind)
            return true;
    return false;
}

}  // namespace magda::daw::ui
