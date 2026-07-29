#include "CompiledPluginRegistry.hpp"

#include <iterator>

namespace magda::daw::audio::compiled {

// Per-device specs are defined alongside each plugin's wrapper (in its own
// .cpp). The accessors below are the single point of contact between this
// registry and the plugin TUs — no static self-registration, no link-order
// magic. To add a new compiled plugin: write its spec accessor next to the
// wrapper and add a line below.
const CompiledPluginSpec& getMagdaFilterSpec();
const CompiledPluginSpec& getMagdaSaturatorSpec();
const CompiledPluginSpec& getMagdaDelaySpec();
const CompiledPluginSpec& getMagdaGrainDelaySpec();
const CompiledPluginSpec& getMagdaGritSpec();
const CompiledPluginSpec& getMagdaMultibandSpec();
const CompiledPluginSpec& getMagdaPhaserSpec();
const CompiledPluginSpec& getMagdaCompressorSpec();
const CompiledPluginSpec& getMagdaModSpec();
const CompiledPluginSpec& getMagdaChorusSpec();
const CompiledPluginSpec& getMagdaFlangerSpec();
const CompiledPluginSpec& getMagdaRingModSpec();
const CompiledPluginSpec& getMagdaFreqShiftSpec();
const CompiledPluginSpec& getMagdaLimiterSpec();
const CompiledPluginSpec& getMagdaGateExpanderSpec();
const CompiledPluginSpec& getMagdaClipperSpec();
const CompiledPluginSpec& getMagdaReverbSpec();
const CompiledPluginSpec& getMagdaEqSpec();
const CompiledPluginSpec& getMagdaDimensionSpec();
const CompiledPluginSpec& getMagdaPitchSpec();
const CompiledPluginSpec& getMagdaBitcrusherSpec();
const CompiledPluginSpec& getMagdaUtilitySpec();
const CompiledPluginSpec& getMagdaPolySynthSpec();
const CompiledPluginSpec& getMagdaFMSpec();
const CompiledPluginSpec& getMagdaKickSpec();
const CompiledPluginSpec& getMagdaSnareSpec();
const CompiledPluginSpec& getMagdaClapSpec();
const CompiledPluginSpec& getMagdaHatSpec();
const CompiledPluginSpec& getMagdaTomSpec();
const CompiledPluginSpec& getMagdaMarimbaSpec();
const CompiledPluginSpec& getMagdaDjembeSpec();
const CompiledPluginSpec& getMagdaBellSpec();

namespace {

const CompiledPluginSpec* const kAllSpecs[] = {
    &getMagdaFilterSpec(),     &getMagdaSaturatorSpec(),  &getMagdaDelaySpec(),
    &getMagdaGrainDelaySpec(), &getMagdaGritSpec(),       &getMagdaMultibandSpec(),
    &getMagdaPhaserSpec(),     &getMagdaCompressorSpec(), &getMagdaModSpec(),
    &getMagdaChorusSpec(),     &getMagdaFlangerSpec(),    &getMagdaRingModSpec(),
    &getMagdaFreqShiftSpec(),  &getMagdaLimiterSpec(),    &getMagdaGateExpanderSpec(),
    &getMagdaClipperSpec(),    &getMagdaReverbSpec(),     &getMagdaEqSpec(),
    &getMagdaDimensionSpec(),  &getMagdaPitchSpec(),      &getMagdaBitcrusherSpec(),
    &getMagdaUtilitySpec(),    &getMagdaPolySynthSpec(),  &getMagdaFMSpec(),
    &getMagdaKickSpec(),       &getMagdaSnareSpec(),      &getMagdaClapSpec(),
    &getMagdaHatSpec(),        &getMagdaTomSpec(),        &getMagdaMarimbaSpec(),
    &getMagdaDjembeSpec(),     &getMagdaBellSpec(),
};

}  // namespace

std::span<const CompiledPluginSpec* const> getAllCompiledPluginSpecs() {
    return {kAllSpecs, std::size(kAllSpecs)};
}

const CompiledPluginSpec* findCompiledPluginSpec(const juce::String& pluginId) {
    for (const auto* spec : kAllSpecs) {
        if (pluginId.equalsIgnoreCase(spec->pluginId))
            return spec;

        if (spec->aliasKey != nullptr && pluginId.equalsIgnoreCase(spec->aliasKey))
            return spec;
    }
    return nullptr;
}

}  // namespace magda::daw::audio::compiled
