#include "CompiledPluginRegistry.hpp"

#include "MagdaBitcrusherCompiledPlugin.hpp"
#include "MagdaChorusCompiledPlugin.hpp"
#include "MagdaClapCompiledPlugin.hpp"
#include "MagdaClipperCompiledPlugin.hpp"
#include "MagdaCompressorCompiledPlugin.hpp"
#include "MagdaDelayCompiledPlugin.hpp"
#include "MagdaDimensionCompiledPlugin.hpp"
#include "MagdaEqCompiledPlugin.hpp"
#include "MagdaFilterCompiledPlugin.hpp"
#include "MagdaFlangerCompiledPlugin.hpp"
#include "MagdaFreqShiftCompiledPlugin.hpp"
#include "MagdaGateExpanderCompiledPlugin.hpp"
#include "MagdaGrainDelayCompiledPlugin.hpp"
#include "MagdaGritCompiledPlugin.hpp"
#include "MagdaHatCompiledPlugin.hpp"
#include "MagdaKickCompiledPlugin.hpp"
#include "MagdaLimiterCompiledPlugin.hpp"
#include "MagdaModCompiledPlugin.hpp"
#include "MagdaMultibandCompiledPlugin.hpp"
#include "MagdaPhaserCompiledPlugin.hpp"
#include "MagdaPitchCompiledPlugin.hpp"
#include "MagdaPolySynthCompiledPlugin.hpp"
#include "MagdaReverbCompiledPlugin.hpp"
#include "MagdaRingModCompiledPlugin.hpp"
#include "MagdaSaturatorCompiledPlugin.hpp"
#include "MagdaSnareCompiledPlugin.hpp"
#include "MagdaTomCompiledPlugin.hpp"
#include "MagdaUtilityCompiledPlugin.hpp"
#include "plugins/compiled/CompiledFaustInterface.hpp"
#include "processors/CompiledFaustProcessor.hpp"

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
    &getMagdaHatSpec(),        &getMagdaTomSpec(),
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

std::unique_ptr<magda::DeviceProcessor> createCompiledPluginProcessor(
    const CompiledPluginSpec& spec, DeviceId deviceId, te::Plugin::Ptr plugin) {
    juce::ignoreUnused(spec);

    if (dynamic_cast<ICompiledFaustPlugin*>(plugin.get()) == nullptr)
        return nullptr;

    return std::make_unique<magda::CompiledFaustProcessor>(deviceId, plugin);
}

}  // namespace magda::daw::audio::compiled
