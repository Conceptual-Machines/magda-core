#include "plugins/BaseDevicePack.hpp"

#include <array>
#include <memory>

#include "audio/plugins/SavedDeviceState.hpp"
#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/DeviceServices.hpp"
#include "plugins/FaustInstrumentPlugin.hpp"
#include "plugins/FaustPlugin.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/LevelsPlugin.hpp"
#include "plugins/MagdaConvolutionPlugin.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/MidiStrumPlugin.hpp"
#include "plugins/OscilloscopePlugin.hpp"
#include "plugins/PolyStepSequencerPlugin.hpp"
#include "plugins/SidechainPlugin.hpp"
#include "plugins/SpectrumAnalyzerPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "plugins/ToneGeneratorPlugin.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "plugins/mutable/MutableCloudsPlugin.hpp"
#include "plugins/mutable/MutableElementsPlugin.hpp"
#include "plugins/mutable/MutableRingsPlugin.hpp"

namespace magda::daw::audio {

namespace {

template <typename DeviceType>
std::unique_ptr<MagdaDevice> createDevice(const DevicePluginCreationContext&) {
    return std::make_unique<DeviceType>();
}

/// What a device is created from: the host's own preferences when it supplied
/// them, and otherwise whatever the session it belongs to registered (#2663).
DevicePluginDefaults defaultsFor(const DevicePluginCreationContext& context) {
    return context.defaults.value_or(getDeviceServices(context.sessionKey).defaults);
}

std::unique_ptr<MagdaDevice> createOscilloscopeDevice(const DevicePluginCreationContext& context) {
    return std::make_unique<OscilloscopePlugin>(defaultsFor(context).oscilloscope);
}

std::unique_ptr<MagdaDevice> createSpectrumAnalyzerDevice(
    const DevicePluginCreationContext& context) {
    return std::make_unique<SpectrumAnalyzerPlugin>(defaultsFor(context).spectrum);
}

void add(InternalPluginRegistry& registry, InternalPluginSpec spec) {
    const bool registered = registry.registerPlugin(spec);
    jassert(registered);
    juce::ignoreUnused(registered);
}

constexpr const char* kToneAliases[] = {"tone", "tonegenerator"};
constexpr const char* kToneTags[] = {"utility", "test", "tone"};
constexpr const char* kMeterAliases[] = {"meter", "levelmeter"};
constexpr const char* kOscilloscopeAliases[] = {"scope"};
constexpr const char* kSpectrumAliases[] = {"spectrum", "analyzer"};
constexpr const char* kLevelsAliases[] = {"loudness", "lufs"};
constexpr const char* kSidechainAliases[] = {"duck", "pump", "volumeshaper"};
// 0.17 shipped the runtime Faust devices as "faust" / "faustinstrument". The
// ids are persisted in project state, so the old spellings stay registered as
// load aliases: a project saved before the rename still resolves its device.
constexpr const char* kFaustAliases[] = {"faust"};
constexpr const char* kFaustInstrumentAliases[] = {"faustinstrument"};

constexpr const char* kLegacyTags[] = {"legacy"};
constexpr const char* kExternalInsertTags[] = {"external-insert"};
constexpr const char* kDrumGridTags[] = {"drum-grid"};
// Not midi-generator: it reads the chain's MIDI and writes none (#2427).
constexpr const char* kChordEngineTags[] = {"chord-engine"};
constexpr const char* kArpeggiatorTags[] = {"arpeggiator", "midi-generator"};
constexpr const char* kStrumTags[] = {"strum"};
constexpr const char* kStepSequencerTags[] = {"step-sequencer", "midi-generator"};
constexpr const char* kPolyStepSequencerTags[] = {"poly-step-sequencer", "midi-generator"};
constexpr const char* kSidechainTags[] = {"sidechain"};
constexpr const char* kConvolutionTags[] = {"convolution", "impulse-response"};

// Tracktion's retired IR Reverb loads here; see core/LegacyDeviceAliases.hpp.
// The other eight retired devices declare theirs on their compiled specs.
// One spelling only: lookup is case-insensitive, and registerPlugin rejects a
// spec whose own aliases collide that way.
constexpr const char* kConvolutionLoadAliases[] = {"impulseResponse"};
constexpr const char* kFaustTags[] = {"faust"};
constexpr const char* kFaustInstrumentTags[] = {"faust-instrument"};
constexpr const char* kOscilloscopeTags[] = {"analysis", "analyzer-popout", "post-fx-analysis-0"};
constexpr const char* kSpectrumTags[] = {"analysis", "analyzer-popout", "post-fx-analysis-1"};
constexpr const char* kLevelsTags[] = {"analysis", "post-fx-analysis-2"};
constexpr const char* kMutableElementsTags[] = {"mutable-instrument", "mutable-elements"};
constexpr const char* kMutableRingsTags[] = {"mutable-instrument", "mutable-rings"};
constexpr const char* kMutableCloudsTags[] = {"mutable-clouds"};

// Persisted identifiers retained for existing project translation. The old set (EQ,
// Compressor, Delay, Chorus, Phaser, Reverb, Pitch Shift, Lowpass, IR Reverb)
// was retired once MAGDA's own devices replaced them; their type names now
// resolve to those successors instead of to a registration here
// (core/LegacyDeviceAliases.hpp).
void registerUtilityDevices(InternalPluginRegistry& registry) {
    add(registry,
        {
            .pluginId = "volume",
            .displayName = "Legacy Volume/Pan",
            .browserCategory = "Legacy",
            .description = "Legacy Tracktion volume and pan device, kept for old project loads.",
            .createMode = InternalPluginCreateMode::SavedStateOrFresh,
            .tags = kLegacyTags,
            .tagCount = static_cast<int>(std::size(kLegacyTags)),
        });
    add(registry,
        {.pluginId = ToneGeneratorPlugin::xmlTypeName,
         .displayName = "Test Tone",
         .browserCategory = "Utility",
         .description =
             "Simple tone generator for calibration, routing checks, and utility signals.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kToneAliases,
         .loadAliasCount = static_cast<int>(std::size(kToneAliases)),
         .showInBrowser = true,
         .tags = kToneTags,
         .tagCount = static_cast<int>(std::size(kToneTags)),
         .createDevice = createDevice<ToneGeneratorPlugin>});
    add(registry, {
                      .pluginId = "level",
                      .displayName = "Level Meter",
                      .browserCategory = "Meter",
                      .description = "Signal meter for monitoring level inside a chain.",
                      .createMode = InternalPluginCreateMode::LevelMeterValueTree,
                      .canCreateDetached = false,
                      .loadAliases = kMeterAliases,
                      .loadAliasCount = static_cast<int>(std::size(kMeterAliases)),
                      .tags = kLegacyTags,
                      .tagCount = static_cast<int>(std::size(kLegacyTags)),
                  });
    add(registry, {
                      .pluginId = "insert",
                      .displayName = "External Insert",
                      .browserCategory = "External",
                      .description =
                          "Hardware send/return insert for outboard audio FX and MIDI instruments.",
                      .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                      .canCreateDetached = false,
                      .tags = kExternalInsertTags,
                      .tagCount = static_cast<int>(std::size(kExternalInsertTags)),
                  });
}

void registerNativeDevices(InternalPluginRegistry& registry) {
    add(registry,
        {.pluginId = MagdaSamplerPlugin::xmlTypeName,
         .displayName = "Sampler",
         .browserCategory = "Sampler",
         .description =
             "Sample playback instrument with envelope, pitch, start/end, and looping controls.",
         // The sample path lives in the device state, so a restore has to
         // rebuild the plugin from it rather than from a fresh tree.
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .showInBrowser = true,
         .isInstrument = true,
         .createDevice = createDevice<MagdaSamplerPlugin>});
    add(registry,
        {
            .pluginId = "drumgrid",
            .displayName = "Drum Grid",
            .browserCategory = "Drums",
            .description = "Pad-based drum instrument with per-pad sample and effect chains.",
            .createMode = InternalPluginCreateMode::FreshValueTree,
            .showInBrowser = true,
            .isInstrument = true,
            .tags = kDrumGridTags,
            .tagCount = static_cast<int>(std::size(kDrumGridTags)),
        });
    add(registry,
        {.pluginId = MidiChordEnginePlugin::xmlTypeName,
         .displayName = "Chord Engine",
         .browserCategory = "MIDI",
         .description = "MIDI processor for chord generation, voicing, and harmonic transforms.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .showInBrowser = true,
         .tags = kChordEngineTags,
         .tagCount = static_cast<int>(std::size(kChordEngineTags)),
         .createDevice = createDevice<MidiChordEnginePlugin>});
    add(registry,
        {.pluginId = ArpeggiatorPlugin::xmlTypeName,
         .displayName = "Arpeggiator",
         .browserCategory = "MIDI",
         .description = "MIDI arpeggiator for rhythmic note patterns and held-note motion.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .showInBrowser = true,
         .tags = kArpeggiatorTags,
         .tagCount = static_cast<int>(std::size(kArpeggiatorTags)),
         .createDevice = createDevice<ArpeggiatorPlugin>});
    add(registry, {.pluginId = MidiStrumPlugin::xmlTypeName,
                   .displayName = "Strum",
                   .browserCategory = "MIDI",
                   .description = "Curve-shaped strum: turns a held chord into a strum / roll / "
                                  "arpeggio for any instrument.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .showInBrowser = true,
                   .tags = kStrumTags,
                   .tagCount = static_cast<int>(std::size(kStrumTags)),
                   .createDevice = createDevice<MidiStrumPlugin>});
    add(registry,
        {.pluginId = StepSequencerPlugin::xmlTypeName,
         .displayName = "Step Sequencer",
         .browserCategory = "MIDI",
         .description = "MIDI step sequencer for pattern-driven notes and rhythmic control.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .showInBrowser = true,
         .tags = kStepSequencerTags,
         .tagCount = static_cast<int>(std::size(kStepSequencerTags)),
         .createDevice = createDevice<StepSequencerPlugin>});
    add(registry,
        {.pluginId = PolyStepSequencerPlugin::xmlTypeName,
         .displayName = "Poly Sequencer",
         .browserCategory = "MIDI",
         .description =
             "Polyphonic MIDI step sequencer with multiple notes per step for chord patterns.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .showInBrowser = true,
         .tags = kPolyStepSequencerTags,
         .tagCount = static_cast<int>(std::size(kPolyStepSequencerTags)),
         .createDevice = createDevice<PolyStepSequencerPlugin>});
    add(registry, {.pluginId = SidechainPlugin::xmlTypeName,
                   .displayName = "Sidechain",
                   .browserCategory = "Dynamics",
                   .description = "MIDI-triggered volume shaper: ducks its own gain with a "
                                  "retriggerable curve keyed from a chosen source track's notes.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kSidechainAliases,
                   .loadAliasCount = static_cast<int>(std::size(kSidechainAliases)),
                   .showInBrowser = true,
                   .tags = kSidechainTags,
                   .tagCount = static_cast<int>(std::size(kSidechainTags)),
                   .defaultModulationParamIndex = SidechainPlugin::kGainParamIndex,
                   .createDevice = createDevice<SidechainPlugin>});
    add(registry,
        {.pluginId = MagdaConvolutionPlugin::xmlTypeName,
         .displayName = "IR Reverb",
         .browserCategory = "Reverb",
         .description =
             "Convolution reverb: loads an impulse response of a room, a plate or a resonant "
             "body and plays the signal through it, with cutoff filters and a dry/wet mix.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kConvolutionLoadAliases,
         .loadAliasCount = static_cast<int>(std::size(kConvolutionLoadAliases)),
         .showInBrowser = true,
         .tags = kConvolutionTags,
         .tagCount = static_cast<int>(std::size(kConvolutionTags)),
         // The impulse response lives in the device state, so a restore has to
         // rebuild the plugin from it rather than from a fresh tree.
         .createDevice = createDevice<MagdaConvolutionPlugin>});
    add(registry, {.pluginId = FaustPlugin::xmlTypeName,
                   .displayName = "Faust",
                   .browserCategory = "Custom DSP",
                   .description = "Faust device for loading and editing user DSP code.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kFaustAliases,
                   .loadAliasCount = static_cast<int>(std::size(kFaustAliases)),
                   .showInBrowser = true,
                   .tags = kFaustTags,
                   .tagCount = static_cast<int>(std::size(kFaustTags)),
                   .stateDefinesParameters = true,
                   .createDevice = createDevice<FaustPlugin>});
    add(registry, {.pluginId = FaustInstrumentPlugin::xmlTypeName,
                   .displayName = "Faust Instrument",
                   .browserCategory = "Custom DSP",
                   .description = "Polyphonic Faust synth instrument driven by MIDI.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kFaustInstrumentAliases,
                   .loadAliasCount = static_cast<int>(std::size(kFaustInstrumentAliases)),
                   .showInBrowser = true,
                   .isInstrument = true,
                   .tags = kFaustInstrumentTags,
                   .tagCount = static_cast<int>(std::size(kFaustInstrumentTags)),
                   .stateDefinesParameters = true,
                   .createDevice = createDevice<FaustInstrumentPlugin>});
    add(registry,
        {.pluginId = OscilloscopePlugin::xmlTypeName,
         .displayName = "Oscilloscope",
         .browserCategory = "Analysis",
         .description = "Transparent waveform monitor for inspecting signal shape over time.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kOscilloscopeAliases,
         .loadAliasCount = static_cast<int>(std::size(kOscilloscopeAliases)),
         .showInBrowser = true,
         .tags = kOscilloscopeTags,
         .tagCount = static_cast<int>(std::size(kOscilloscopeTags)),
         .createDevice = createOscilloscopeDevice});
    add(registry,
        {.pluginId = SpectrumAnalyzerPlugin::xmlTypeName,
         .displayName = "Spectrum Analyzer",
         .browserCategory = "Analysis",
         .description = "Real-time FFT spectrum display with log-frequency axis and peak hold.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kSpectrumAliases,
         .loadAliasCount = static_cast<int>(std::size(kSpectrumAliases)),
         .showInBrowser = true,
         .tags = kSpectrumTags,
         .tagCount = static_cast<int>(std::size(kSpectrumTags)),
         .createDevice = createSpectrumAnalyzerDevice});
    add(registry,
        {.pluginId = LevelsPlugin::xmlTypeName,
         .displayName = "Levels",
         .browserCategory = "Analysis",
         .description = "Loudness, true-peak and stereo meter (LUFS, dBTP, correlation, dynamics).",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kLevelsAliases,
         .loadAliasCount = static_cast<int>(std::size(kLevelsAliases)),
         .showInBrowser = true,
         .tags = kLevelsTags,
         .tagCount = static_cast<int>(std::size(kLevelsTags)),
         .createDevice = createDevice<LevelsPlugin>});
    add(registry,
        {.pluginId = MutableElementsPlugin::xmlTypeName,
         .displayName = "Materia",
         .browserCategory = "Synth",
         .description = "Mutable Instruments Elements port: modal-synthesis voice (bow/blow/strike "
                        "exciter into a modal + string resonator and stereo space).",
         .createMode = InternalPluginCreateMode::FreshValueTree,
         .showInBrowser = true,
         .isInstrument = true,
         .tags = kMutableElementsTags,
         .tagCount = static_cast<int>(std::size(kMutableElementsTags)),
         .createDevice = createDevice<MutableElementsPlugin>});
    add(registry, {.pluginId = MutableRingsPlugin::xmlTypeName,
                   .displayName = "Halo",
                   .browserCategory = "Synth",
                   .description = "Mutable Instruments Rings port: polyphonic resonator (modal / "
                                  "sympathetic / inharmonic / FM models) excited by MIDI.",
                   .createMode = InternalPluginCreateMode::FreshValueTree,
                   .showInBrowser = true,
                   .isInstrument = true,
                   .tags = kMutableRingsTags,
                   .tagCount = static_cast<int>(std::size(kMutableRingsTags)),
                   .createDevice = createDevice<MutableRingsPlugin>});
    add(registry, {.pluginId = MutableCloudsPlugin::xmlTypeName,
                   .displayName = "Nimbus",
                   .browserCategory = "Texture",
                   .description = "Mutable Instruments Clouds port: granular texture processor "
                                  "(granular / stretch / looping-delay / spectral) with freeze.",
                   .createMode = InternalPluginCreateMode::FreshValueTree,
                   .showInBrowser = true,
                   .tags = kMutableCloudsTags,
                   .tagCount = static_cast<int>(std::size(kMutableCloudsTags)),
                   .createDevice = createDevice<MutableCloudsPlugin>});
}

void registerCompiledParameterAliases(InternalPluginRegistry& registry) {
    for (const auto* plugin : compiled::getAllCompiledPluginSpecs()) {
        if (plugin == nullptr || plugin->aliases == nullptr)
            continue;

        const char* pluginKey = plugin->aliasKey != nullptr ? plugin->aliasKey : plugin->pluginId;
        for (int i = 0; i < plugin->aliasCount; ++i) {
            const auto& alias = plugin->aliases[i];
            const bool registered = registry.registerParameterAlias(
                {pluginKey, alias.alias, alias.paramIndex, alias.paramName});
            jassert(registered);
            juce::ignoreUnused(registered);
        }
    }
}

}  // namespace

void registerBaseDevices(InternalPluginRegistry& registry) {
    registerUtilityDevices(registry);
    registerNativeDevices(registry);
    registerCompiledParameterAliases(registry);
}

namespace {
[[maybe_unused]] const bool baseDevicePackRegistered = registerDevicePack(registerBaseDevices);
}

}  // namespace magda::daw::audio
