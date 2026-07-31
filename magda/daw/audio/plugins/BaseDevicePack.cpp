#include "plugins/BaseDevicePack.hpp"

#include <array>
#include <memory>

#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/AudioSidechainMonitorPlugin.hpp"
#include "plugins/DeviceServices.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/FaustInstrumentPlugin.hpp"
#include "plugins/FaustPlugin.hpp"
#include "plugins/FollowerSourceTapPlugin.hpp"
#include "plugins/InsertCapturePlugin.hpp"
#include "plugins/InstrumentMeterTapPlugin.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/LevelsPlugin.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/MidiReceivePlugin.hpp"
#include "plugins/MidiStrumPlugin.hpp"
#include "plugins/OscilloscopePlugin.hpp"
#include "plugins/PolyStepSequencerPlugin.hpp"
#include "plugins/SidechainMonitorPlugin.hpp"
#include "plugins/SidechainPlugin.hpp"
#include "plugins/SpectrumAnalyzerPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "plugins/TrackMeasurementPlugin.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "plugins/mutable/MutableCloudsPlugin.hpp"
#include "plugins/mutable/MutableElementsPlugin.hpp"
#include "plugins/mutable/MutableRingsPlugin.hpp"
#include "plugins/tracktion/TracktionDeviceAdapters.hpp"
#include "plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "processors/DeviceProcessor.hpp"
#include "processors/internal/MidiDeviceProcessors.hpp"
#include "processors/internal/NativeDeviceProcessors.hpp"
#include "session/SessionMonitorPlugin.hpp"

namespace magda::daw::audio {

namespace {

namespace ta = tracktion_adapter;

template <typename PluginType> bool matches(DevicePluginRef plugin) {
    return dynamic_cast<PluginType*>(ta::pluginFromRef(plugin)) != nullptr;
}

template <typename ProcessorType>
std::unique_ptr<DeviceProcessor> makeProcessor(DeviceId deviceId, DevicePluginPtr plugin) {
    return std::make_unique<ProcessorType>(deviceId, ta::pluginFromHandle(plugin));
}

template <typename PluginType>
DevicePluginPtr createPlugin(const DevicePluginCreationContext& context) {
    return ta::pluginHandle(new PluginType(ta::creationInfo(context)));
}

template <typename PluginType>
DevicePluginPtr createRealtimePlugin(const DevicePluginCreationContext& context) {
    auto info = ta::creationInfo(context);
    auto* plugin = new PluginType(info);
    plugin->setRealtimeContext(getDeviceServices(context.sessionKey).realtimeContext);
    return ta::pluginHandle(plugin);
}

DevicePluginPtr createDrumGridPlugin(const DevicePluginCreationContext& context) {
    auto services = getDeviceServices(context.sessionKey);
    jassert(services.deviceIdAllocator != nullptr);
    return ta::pluginHandle(
        new DrumGridPlugin(ta::creationInfo(context), *services.deviceIdAllocator));
}

DevicePluginPtr createMidiChordEnginePlugin(const DevicePluginCreationContext& context) {
    auto services = getDeviceServices(context.sessionKey);
    return ta::pluginHandle(
        new MidiChordEnginePlugin(ta::creationInfo(context), services.trackContext));
}

std::unique_ptr<MagdaDevice> createOscilloscopeDevice(const DevicePluginCreationContext& context) {
    auto services = getDeviceServices(context.sessionKey);
    return std::make_unique<OscilloscopePlugin>(services.defaults.oscilloscope);
}

std::unique_ptr<MagdaDevice> createSpectrumAnalyzerDevice(
    const DevicePluginCreationContext& context) {
    auto services = getDeviceServices(context.sessionKey);
    return std::make_unique<SpectrumAnalyzerPlugin>(services.defaults.spectrum);
}

DevicePluginPtr createMidiReceivePlugin(const DevicePluginCreationContext& context) {
    auto services = getDeviceServices(context.sessionKey);
    return ta::pluginHandle(
        new ::magda::MidiReceivePlugin(ta::creationInfo(context), services.defaults.midiReceive));
}

DevicePluginPtr createInstrumentMeterTapPlugin(const DevicePluginCreationContext& context) {
    auto services = getDeviceServices(context.sessionKey);
    return ta::pluginHandle(
        new InstrumentMeterTapPlugin(ta::creationInfo(context), services.meteringContext));
}

DevicePluginPtr createSessionMonitorPlugin(const DevicePluginCreationContext& context) {
    auto* plugin = new ::magda::SessionMonitorPlugin(ta::creationInfo(context));
    plugin->setSessionContext(getDeviceServices(context.sessionKey).sessionContext);
    return ta::pluginHandle(plugin);
}

te::Plugin::Ptr createFreshValueTreePlugin(te::Edit& edit, const char* xmlTypeName) {
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, xmlTypeName, nullptr);
    return edit.getPluginCache().createNewPlugin(pluginState);
}

te::Plugin::Ptr restoreSavedPlugin(te::Edit& edit, const juce::String& savedPluginState) {
    if (savedPluginState.isEmpty())
        return {};

    auto savedState = tracktion_adapter::devicePluginTreeFromState(savedPluginState);
    if (!savedState.isValid()) {
        DBG("restoreSavedPlugin: failed to parse saved plugin state");
        return {};
    }

    auto plugin = edit.getPluginCache().createNewPlugin(savedState);
    if (plugin != nullptr)
        tracktion_adapter::applyDeviceStateParameters(*plugin, savedPluginState);
    return plugin;
}

DevicePluginPtr createTracktionPlugin(const InternalPluginSpec& spec, DeviceSessionKey sessionKey,
                                      const juce::String& savedPluginState) {
    auto& edit = ta::editFromSessionKey(sessionKey);
    if (auto restored = restoreSavedPlugin(edit, savedPluginState))
        return ta::pluginHandle(restored);
    if (auto plugin = edit.getPluginCache().createNewPlugin(spec.pluginId, {}))
        return ta::pluginHandle(plugin);
    return ta::pluginHandle(createFreshValueTreePlugin(edit, spec.pluginId));
}

DevicePluginPtr createValueTreePlugin(const InternalPluginSpec& spec, DeviceSessionKey sessionKey,
                                      const juce::String& savedPluginState) {
    auto& edit = ta::editFromSessionKey(sessionKey);
    if (auto restored = restoreSavedPlugin(edit, savedPluginState))
        return ta::pluginHandle(restored);
    return ta::pluginHandle(createFreshValueTreePlugin(edit, spec.pluginId));
}

DevicePluginPtr createFreshValueTreePlugin(const InternalPluginSpec& spec,
                                           DeviceSessionKey sessionKey, const juce::String&) {
    return ta::pluginHandle(
        createFreshValueTreePlugin(ta::editFromSessionKey(sessionKey), spec.pluginId));
}

DevicePluginPtr createLevelMeterPlugin(const InternalPluginSpec&, DeviceSessionKey sessionKey,
                                       const juce::String&) {
    auto& edit = ta::editFromSessionKey(sessionKey);
    return ta::pluginHandle(edit.getPluginCache().createNewPlugin(te::LevelMeterPlugin::create()));
}

DevicePluginPtr createEqualiserPlugin(const InternalPluginSpec& spec, DeviceSessionKey sessionKey,
                                      const juce::String& savedPluginState) {
    auto plugin = createTracktionPlugin(spec, sessionKey, savedPluginState);
    if (!plugin)
        return {};

    auto tracktionPlugin = ta::pluginFromHandle(plugin);
    auto params = tracktionPlugin->getAutomatableParameters();
    constexpr float defaultFreqs[] = {100.0f, 500.0f, 3000.0f, 10000.0f};
    for (int band = 0; band < 4; ++band) {
        const int freqIndex = band * 3;
        if (freqIndex < params.size() && params[freqIndex])
            params[freqIndex]->setParameterFromHost(defaultFreqs[band], juce::sendNotificationSync);
    }
    return plugin;
}

void add(InternalPluginRegistry& registry, InternalPluginSpec spec) {
    const bool registered = registry.registerPlugin(std::move(spec));
    jassert(registered);
    juce::ignoreUnused(registered);
}

constexpr const char* kEqAliases[] = {"eq", "equaliser"};
constexpr const char* kCompressorAliases[] = {"compressor"};
constexpr const char* kReverbAliases[] = {"reverb"};
constexpr const char* kDelayAliases[] = {"delay"};
constexpr const char* kChorusAliases[] = {"chorus"};
constexpr const char* kPhaserAliases[] = {"phaser"};
constexpr const char* kLowpassAliases[] = {"lowpass"};
constexpr const char* kPitchShiftAliases[] = {"pitchshift"};
constexpr const char* kImpulseResponseAliases[] = {"impulseresponse"};
constexpr const char* kFourOscAliases[] = {"4osc", "4OSC Synth"};
constexpr const char* kToneAliases[] = {"tone", "tonegenerator"};
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

constexpr const char* kTracktionTags[] = {"tracktion-engine"};
constexpr const char* kCompressorTags[] = {"tracktion-engine", "legacy-te-compressor"};
constexpr const char* kExternalInsertTags[] = {"tracktion-engine", "external-insert"};
constexpr const char* kDrumGridTags[] = {"drum-grid"};
constexpr const char* kChordEngineTags[] = {"chord-engine", "midi-generator"};
constexpr const char* kArpeggiatorTags[] = {"arpeggiator", "midi-generator"};
constexpr const char* kStrumTags[] = {"strum"};
constexpr const char* kStepSequencerTags[] = {"step-sequencer", "midi-generator"};
constexpr const char* kPolyStepSequencerTags[] = {"poly-step-sequencer", "midi-generator"};
constexpr const char* kSidechainTags[] = {"sidechain"};
constexpr const char* kFaustTags[] = {"faust"};
constexpr const char* kFaustInstrumentTags[] = {"faust-instrument"};
constexpr const char* kOscilloscopeTags[] = {"analysis", "analyzer-popout", "post-fx-analysis-0"};
constexpr const char* kSpectrumTags[] = {"analysis", "analyzer-popout", "post-fx-analysis-1"};
constexpr const char* kLevelsTags[] = {"analysis", "post-fx-analysis-2"};
constexpr const char* kMutableElementsTags[] = {"mutable-instrument", "mutable-elements"};
constexpr const char* kMutableRingsTags[] = {"mutable-instrument", "mutable-rings"};
constexpr const char* kMutableCloudsTags[] = {"mutable-clouds"};

void registerTracktionDevices(InternalPluginRegistry& registry) {
    add(registry,
        {.pluginId = te::EqualiserPlugin::xmlTypeName,
         .displayName = "Equaliser",
         .browserCategory = "EQ",
         .description = "Four-band equaliser for broad tonal shaping and corrective filtering.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kEqAliases,
         .loadAliasCount = static_cast<int>(std::size(kEqAliases)),
         .matchesPlugin = matches<te::EqualiserPlugin>,
         .createProcessor = makeProcessor<EqualiserProcessor>,
         .tags = kTracktionTags,
         .tagCount = static_cast<int>(std::size(kTracktionTags)),
         .createInSession = createEqualiserPlugin});
    add(registry,
        {.pluginId = te::CompressorPlugin::xmlTypeName,
         .displayName = "Compressor",
         .browserCategory = "Dynamics",
         .description = "Track compressor for controlling level, transient shape, and sustain.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kCompressorAliases,
         .loadAliasCount = static_cast<int>(std::size(kCompressorAliases)),
         .matchesPlugin = matches<te::CompressorPlugin>,
         .createProcessor = makeProcessor<CompressorProcessor>,
         .tags = kCompressorTags,
         .tagCount = static_cast<int>(std::size(kCompressorTags)),
         .createInSession = createTracktionPlugin});
    add(registry,
        {.pluginId = te::ReverbPlugin::xmlTypeName,
         .displayName = "Reverb",
         .browserCategory = "Reverb",
         .description = "Algorithmic space effect for room, plate, and ambience-style tails.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kReverbAliases,
         .loadAliasCount = static_cast<int>(std::size(kReverbAliases)),
         .matchesPlugin = matches<te::ReverbPlugin>,
         .createProcessor = makeProcessor<ReverbProcessor>,
         .tags = kTracktionTags,
         .tagCount = static_cast<int>(std::size(kTracktionTags)),
         .createInSession = createTracktionPlugin});
    add(registry, {.pluginId = te::DelayPlugin::xmlTypeName,
                   .displayName = "Delay",
                   .browserCategory = "Delay",
                   .description = "Tempo-capable delay effect for echoes and rhythmic repeats.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kDelayAliases,
                   .loadAliasCount = static_cast<int>(std::size(kDelayAliases)),
                   .matchesPlugin = matches<te::DelayPlugin>,
                   .createProcessor = makeProcessor<DelayProcessor>,
                   .tags = kTracktionTags,
                   .tagCount = static_cast<int>(std::size(kTracktionTags)),
                   .createInSession = createTracktionPlugin});
    add(registry, {.pluginId = te::ChorusPlugin::xmlTypeName,
                   .displayName = "Chorus",
                   .browserCategory = "Modulation",
                   .description =
                       "Modulated delay effect for width, movement, and ensemble-style thickening.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kChorusAliases,
                   .loadAliasCount = static_cast<int>(std::size(kChorusAliases)),
                   .matchesPlugin = matches<te::ChorusPlugin>,
                   .createProcessor = makeProcessor<ChorusProcessor>,
                   .tags = kTracktionTags,
                   .tagCount = static_cast<int>(std::size(kTracktionTags)),
                   .createInSession = createTracktionPlugin});
    add(registry,
        {.pluginId = te::PhaserPlugin::xmlTypeName,
         .displayName = "Phaser",
         .browserCategory = "Modulation",
         .description = "Swept phase-cancellation effect for resonant movement and stereo motion.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kPhaserAliases,
         .loadAliasCount = static_cast<int>(std::size(kPhaserAliases)),
         .matchesPlugin = matches<te::PhaserPlugin>,
         .createProcessor = makeProcessor<PhaserProcessor>,
         .tags = kTracktionTags,
         .tagCount = static_cast<int>(std::size(kTracktionTags)),
         .createInSession = createTracktionPlugin});
    add(registry, {.pluginId = te::LowPassPlugin::xmlTypeName,
                   .displayName = "Lowpass",
                   .browserCategory = "Filter",
                   .description = "Low-pass filter for removing high-frequency content.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kLowpassAliases,
                   .loadAliasCount = static_cast<int>(std::size(kLowpassAliases)),
                   .matchesPlugin = matches<te::LowPassPlugin>,
                   .createProcessor = makeProcessor<FilterProcessor>,
                   .showInBrowser = true,
                   .tags = kTracktionTags,
                   .tagCount = static_cast<int>(std::size(kTracktionTags)),
                   .createInSession = createTracktionPlugin});
    add(registry, {.pluginId = te::PitchShiftPlugin::xmlTypeName,
                   .displayName = "Pitch Shift",
                   .browserCategory = "Pitch",
                   .description = "Pitch shifting effect for transposition and special effects.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kPitchShiftAliases,
                   .loadAliasCount = static_cast<int>(std::size(kPitchShiftAliases)),
                   .matchesPlugin = matches<te::PitchShiftPlugin>,
                   .createProcessor = makeProcessor<PitchShiftProcessor>,
                   .tags = kTracktionTags,
                   .tagCount = static_cast<int>(std::size(kTracktionTags)),
                   .createInSession = createTracktionPlugin});
    add(registry, {.pluginId = te::ImpulseResponsePlugin::xmlTypeName,
                   .displayName = "IR Reverb",
                   .browserCategory = "Reverb",
                   .description =
                       "Convolution-style response loader for captured spaces and resonant bodies.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kImpulseResponseAliases,
                   .loadAliasCount = static_cast<int>(std::size(kImpulseResponseAliases)),
                   .matchesPlugin = matches<te::ImpulseResponsePlugin>,
                   .createProcessor = makeProcessor<ImpulseResponseProcessor>,
                   .showInBrowser = true,
                   .tags = kTracktionTags,
                   .tagCount = static_cast<int>(std::size(kTracktionTags)),
                   .createInSession = createTracktionPlugin,
                   .createPlugin = createPlugin<te::ImpulseResponsePlugin>});
    add(registry,
        {.pluginId = te::VolumeAndPanPlugin::xmlTypeName,
         .displayName = "Legacy Volume/Pan",
         .browserCategory = "Legacy",
         .description = "Legacy Tracktion volume and pan device, kept for old project loads.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .matchesPlugin = matches<te::VolumeAndPanPlugin>,
         .createProcessor = makeProcessor<UtilityProcessor>,
         .tags = kTracktionTags,
         .tagCount = static_cast<int>(std::size(kTracktionTags)),
         .createInSession = createTracktionPlugin});
    add(registry,
        {.pluginId = te::FourOscPlugin::xmlTypeName,
         .displayName = "4OSC Synth",
         .browserCategory = "Synth",
         .description =
             "Four-oscillator subtractive instrument with modulation and macro-friendly controls.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kFourOscAliases,
         .loadAliasCount = static_cast<int>(std::size(kFourOscAliases)),
         .matchesPlugin = matches<te::FourOscPlugin>,
         .createProcessor = makeProcessor<FourOscProcessor>,
         .showInBrowser = true,
         .isInstrument = true,
         .tags = kTracktionTags,
         .tagCount = static_cast<int>(std::size(kTracktionTags)),
         .createInSession = createTracktionPlugin});
    add(registry,
        {.pluginId = te::ToneGeneratorPlugin::xmlTypeName,
         .displayName = "Test Tone",
         .browserCategory = "Utility",
         .description =
             "Simple tone generator for calibration, routing checks, and utility signals.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kToneAliases,
         .loadAliasCount = static_cast<int>(std::size(kToneAliases)),
         .matchesPlugin = matches<te::ToneGeneratorPlugin>,
         .createProcessor = makeProcessor<ToneGeneratorProcessor>,
         .showInBrowser = true,
         .tags = kTracktionTags,
         .tagCount = static_cast<int>(std::size(kTracktionTags)),
         .createInSession = createTracktionPlugin});
    add(registry, {.pluginId = te::LevelMeterPlugin::xmlTypeName,
                   .displayName = "Level Meter",
                   .browserCategory = "Meter",
                   .description = "Signal meter for monitoring level inside a chain.",
                   .createMode = InternalPluginCreateMode::LevelMeterValueTree,
                   .canCreateDetached = false,
                   .loadAliases = kMeterAliases,
                   .loadAliasCount = static_cast<int>(std::size(kMeterAliases)),
                   .matchesPlugin = matches<te::LevelMeterPlugin>,
                   .tags = kTracktionTags,
                   .tagCount = static_cast<int>(std::size(kTracktionTags)),
                   .createInSession = createLevelMeterPlugin});
    add(registry,
        {.pluginId = te::InsertPlugin::xmlTypeName,
         .displayName = "External Insert",
         .browserCategory = "External",
         .description = "Hardware send/return insert for outboard audio FX and MIDI instruments.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .canCreateDetached = false,
         .matchesPlugin = matches<te::InsertPlugin>,
         .tags = kExternalInsertTags,
         .tagCount = static_cast<int>(std::size(kExternalInsertTags)),
         .createInSession = createTracktionPlugin});
}

void registerNativeDevices(InternalPluginRegistry& registry) {
    add(registry,
        {.pluginId = MagdaSamplerPlugin::xmlTypeName,
         .displayName = "Sampler",
         .browserCategory = "Sampler",
         .description =
             "Sample playback instrument with envelope, pitch, start/end, and looping controls.",
         .createMode = InternalPluginCreateMode::FreshValueTree,
         .matchesPlugin = matches<MagdaSamplerPlugin>,
         .createProcessor = makeProcessor<MagdaSamplerProcessor>,
         .showInBrowser = true,
         .isInstrument = true,
         .createInSession = createFreshValueTreePlugin,
         .createPlugin = createPlugin<MagdaSamplerPlugin>});
    add(registry,
        {.pluginId = DrumGridPlugin::xmlTypeName,
         .displayName = "Drum Grid",
         .browserCategory = "Drums",
         .description = "Pad-based drum instrument with per-pad sample and effect chains.",
         .createMode = InternalPluginCreateMode::FreshValueTree,
         .matchesPlugin = matches<DrumGridPlugin>,
         .createProcessor = makeProcessor<DrumGridProcessor>,
         .showInBrowser = true,
         .isInstrument = true,
         .tags = kDrumGridTags,
         .tagCount = static_cast<int>(std::size(kDrumGridTags)),
         .createInSession = createFreshValueTreePlugin,
         .createPlugin = createDrumGridPlugin});
    add(registry,
        {.pluginId = MidiChordEnginePlugin::xmlTypeName,
         .displayName = "Chord Engine",
         .browserCategory = "MIDI",
         .description = "MIDI processor for chord generation, voicing, and harmonic transforms.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .matchesPlugin = matches<MidiChordEnginePlugin>,
         .showInBrowser = true,
         .tags = kChordEngineTags,
         .tagCount = static_cast<int>(std::size(kChordEngineTags)),
         .createInSession = createValueTreePlugin,
         .createPlugin = createMidiChordEnginePlugin});
    add(registry,
        {.pluginId = ArpeggiatorPlugin::xmlTypeName,
         .displayName = "Arpeggiator",
         .browserCategory = "MIDI",
         .description = "MIDI arpeggiator for rhythmic note patterns and held-note motion.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .matchesPlugin = matches<ArpeggiatorPlugin>,
         .createProcessor = makeProcessor<ArpeggiatorProcessor>,
         .showInBrowser = true,
         .tags = kArpeggiatorTags,
         .tagCount = static_cast<int>(std::size(kArpeggiatorTags)),
         .createInSession = createValueTreePlugin,
         .createPlugin = createPlugin<ArpeggiatorPlugin>});
    add(registry, {.pluginId = MidiStrumPlugin::xmlTypeName,
                   .displayName = "Strum",
                   .browserCategory = "MIDI",
                   .description = "Curve-shaped strum: turns a held chord into a strum / roll / "
                                  "arpeggio for any instrument.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .matchesPlugin = matches<MidiStrumPlugin>,
                   .createProcessor = makeProcessor<StrumProcessor>,
                   .showInBrowser = true,
                   .tags = kStrumTags,
                   .tagCount = static_cast<int>(std::size(kStrumTags)),
                   .createInSession = createValueTreePlugin,
                   .createPlugin = createPlugin<MidiStrumPlugin>});
    add(registry,
        {.pluginId = StepSequencerPlugin::xmlTypeName,
         .displayName = "Step Sequencer",
         .browserCategory = "MIDI",
         .description = "MIDI step sequencer for pattern-driven notes and rhythmic control.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .matchesPlugin = matches<StepSequencerPlugin>,
         .createProcessor = makeProcessor<StepSequencerProcessor>,
         .showInBrowser = true,
         .tags = kStepSequencerTags,
         .tagCount = static_cast<int>(std::size(kStepSequencerTags)),
         .createInSession = createValueTreePlugin,
         .createPlugin = createPlugin<StepSequencerPlugin>});
    add(registry,
        {.pluginId = PolyStepSequencerPlugin::xmlTypeName,
         .displayName = "Poly Sequencer",
         .browserCategory = "MIDI",
         .description =
             "Polyphonic MIDI step sequencer with multiple notes per step for chord patterns.",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .matchesPlugin = matches<PolyStepSequencerPlugin>,
         .createProcessor = makeProcessor<PolyStepSequencerProcessor>,
         .showInBrowser = true,
         .tags = kPolyStepSequencerTags,
         .tagCount = static_cast<int>(std::size(kPolyStepSequencerTags)),
         .createInSession = createValueTreePlugin,
         .createPlugin = createPlugin<PolyStepSequencerPlugin>});
    add(registry, {.pluginId = SidechainPlugin::xmlTypeName,
                   .displayName = "Sidechain",
                   .browserCategory = "Dynamics",
                   .description = "MIDI-triggered volume shaper: ducks its own gain with a "
                                  "retriggerable curve keyed from a chosen source track's notes.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kSidechainAliases,
                   .loadAliasCount = static_cast<int>(std::size(kSidechainAliases)),
                   .matchesPlugin = matches<SidechainPlugin>,
                   .createProcessor = makeProcessor<SidechainProcessor>,
                   .showInBrowser = true,
                   .tags = kSidechainTags,
                   .tagCount = static_cast<int>(std::size(kSidechainTags)),
                   .defaultModulationParamIndex = SidechainPlugin::kGainParamIndex,
                   .createInSession = createValueTreePlugin,
                   .createPlugin = createPlugin<SidechainPlugin>});
    add(registry, {.pluginId = FaustPlugin::xmlTypeName,
                   .displayName = "Faust",
                   .browserCategory = "Experimental",
                   .description = "Interpreted Faust device for loading and editing user DSP code.",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kFaustAliases,
                   .loadAliasCount = static_cast<int>(std::size(kFaustAliases)),
                   .matchesPlugin = matches<FaustPlugin>,
                   .createProcessor = makeProcessor<FaustProcessor>,
                   .showInBrowser = true,
                   .tags = kFaustTags,
                   .tagCount = static_cast<int>(std::size(kFaustTags)),
                   .createInSession = createValueTreePlugin,
                   .createPlugin = createPlugin<FaustPlugin>});
    add(registry, {.pluginId = FaustInstrumentPlugin::xmlTypeName,
                   .displayName = "Faust Instrument",
                   .browserCategory = "Experimental",
                   .description = "Polyphonic Faust synth instrument driven by MIDI (POC).",
                   .createMode = InternalPluginCreateMode::SavedStateOrFresh,
                   .loadAliases = kFaustInstrumentAliases,
                   .loadAliasCount = static_cast<int>(std::size(kFaustInstrumentAliases)),
                   .matchesPlugin = matches<FaustInstrumentPlugin>,
                   .createProcessor = makeProcessor<FaustInstrumentProcessor>,
                   .showInBrowser = true,
                   .isInstrument = true,
                   .tags = kFaustInstrumentTags,
                   .tagCount = static_cast<int>(std::size(kFaustInstrumentTags)),
                   .createInSession = createValueTreePlugin,
                   .createPlugin = createPlugin<FaustInstrumentPlugin>});
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
         .createInSession = createValueTreePlugin,
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
         .createInSession = createValueTreePlugin,
         .createDevice = createSpectrumAnalyzerDevice});
    add(registry,
        {.pluginId = LevelsPlugin::xmlTypeName,
         .displayName = "Levels",
         .browserCategory = "Analysis",
         .description = "Loudness, true-peak and stereo meter (LUFS, dBTP, correlation, dynamics).",
         .createMode = InternalPluginCreateMode::SavedStateOrFresh,
         .loadAliases = kLevelsAliases,
         .loadAliasCount = static_cast<int>(std::size(kLevelsAliases)),
         .matchesPlugin = matches<LevelsPlugin>,
         .showInBrowser = true,
         .tags = kLevelsTags,
         .tagCount = static_cast<int>(std::size(kLevelsTags)),
         .createInSession = createValueTreePlugin,
         .createPlugin = createPlugin<LevelsPlugin>});
    add(registry,
        {.pluginId = MutableElementsPlugin::xmlTypeName,
         .displayName = "Materia",
         .browserCategory = "Synth",
         .description = "Mutable Instruments Elements port: modal-synthesis voice (bow/blow/strike "
                        "exciter into a modal + string resonator and stereo space).",
         .createMode = InternalPluginCreateMode::FreshValueTree,
         .matchesPlugin = matches<MutableElementsPlugin>,
         .createProcessor = makeProcessor<MutableElementsProcessor>,
         .showInBrowser = true,
         .isInstrument = true,
         .tags = kMutableElementsTags,
         .tagCount = static_cast<int>(std::size(kMutableElementsTags)),
         .createInSession = createFreshValueTreePlugin,
         .createPlugin = createPlugin<MutableElementsPlugin>});
    add(registry, {.pluginId = MutableRingsPlugin::xmlTypeName,
                   .displayName = "Halo",
                   .browserCategory = "Synth",
                   .description = "Mutable Instruments Rings port: polyphonic resonator (modal / "
                                  "sympathetic / inharmonic / FM models) excited by MIDI.",
                   .createMode = InternalPluginCreateMode::FreshValueTree,
                   .matchesPlugin = matches<MutableRingsPlugin>,
                   .createProcessor = makeProcessor<MutableRingsProcessor>,
                   .showInBrowser = true,
                   .isInstrument = true,
                   .tags = kMutableRingsTags,
                   .tagCount = static_cast<int>(std::size(kMutableRingsTags)),
                   .createInSession = createFreshValueTreePlugin,
                   .createPlugin = createPlugin<MutableRingsPlugin>});
    add(registry, {.pluginId = MutableCloudsPlugin::xmlTypeName,
                   .displayName = "Nimbus",
                   .browserCategory = "Texture",
                   .description = "Mutable Instruments Clouds port: granular texture processor "
                                  "(granular / stretch / looping-delay / spectral) with freeze.",
                   .createMode = InternalPluginCreateMode::FreshValueTree,
                   .matchesPlugin = matches<MutableCloudsPlugin>,
                   .createProcessor = makeProcessor<MutableCloudsProcessor>,
                   .showInBrowser = true,
                   .tags = kMutableCloudsTags,
                   .tagCount = static_cast<int>(std::size(kMutableCloudsTags)),
                   .createInSession = createFreshValueTreePlugin,
                   .createPlugin = createPlugin<MutableCloudsPlugin>});
}

void registerInfrastructureDevices(InternalPluginRegistry& registry) {
    add(registry,
        {.pluginId = ::magda::MidiReceivePlugin::xmlTypeName,
         .displayName = "MIDI Receive",
         .browserCategory = "MIDI",
         .description = "Internal MIDI routing endpoint used by MAGDA track and device routing.",
         .createMode = InternalPluginCreateMode::Unsupported,
         .canCreateDetached = false,
         .canCreateOnTrack = false,
         .matchesPlugin = matches<::magda::MidiReceivePlugin>,
         .createPlugin = createMidiReceivePlugin});
    add(registry, {.pluginId = ::magda::SidechainMonitorPlugin::xmlTypeName,
                   .displayName = "Sidechain Monitor",
                   .browserCategory = "Utility",
                   .description = "Internal monitor used to expose sidechain signal state.",
                   .createMode = InternalPluginCreateMode::Unsupported,
                   .canCreateDetached = false,
                   .canCreateOnTrack = false,
                   .matchesPlugin = matches<::magda::SidechainMonitorPlugin>,
                   .createPlugin = createRealtimePlugin<::magda::SidechainMonitorPlugin>});
    add(registry, {.pluginId = ::magda::AudioSidechainMonitorPlugin::xmlTypeName,
                   .displayName = "Audio Sidechain Monitor",
                   .browserCategory = "Utility",
                   .description = "Internal audio monitor used by sidechain-aware devices.",
                   .createMode = InternalPluginCreateMode::Unsupported,
                   .canCreateDetached = false,
                   .canCreateOnTrack = false,
                   .matchesPlugin = matches<::magda::AudioSidechainMonitorPlugin>,
                   .createPlugin = createRealtimePlugin<::magda::AudioSidechainMonitorPlugin>});
    add(registry, {.pluginId = InstrumentMeterTapPlugin::xmlTypeName,
                   .displayName = "Instrument Meter Tap",
                   .browserCategory = "Meter",
                   .description = "Internal meter tap used to observe instrument output levels.",
                   .createMode = InternalPluginCreateMode::Unsupported,
                   .canCreateDetached = false,
                   .canCreateOnTrack = false,
                   .matchesPlugin = matches<InstrumentMeterTapPlugin>,
                   .createPlugin = createInstrumentMeterTapPlugin});
    add(registry,
        {.pluginId = TrackMeasurementPlugin::xmlTypeName,
         .displayName = "Track Measurement",
         .browserCategory = "Meter",
         .description =
             "Internal post-fader tap measuring loudness, peak and stereo for the mixing tools.",
         .createMode = InternalPluginCreateMode::Unsupported,
         .canCreateDetached = false,
         .canCreateOnTrack = false,
         .matchesPlugin = matches<TrackMeasurementPlugin>,
         .createPlugin = createPlugin<TrackMeasurementPlugin>});
    add(registry, {.pluginId = ::magda::SessionMonitorPlugin::xmlTypeName,
                   .displayName = "Session Monitor",
                   .browserCategory = "Session",
                   .description = "Internal monitor used by session playback and launch state.",
                   .createMode = InternalPluginCreateMode::Unsupported,
                   .canCreateDetached = false,
                   .canCreateOnTrack = false,
                   .matchesPlugin = matches<::magda::SessionMonitorPlugin>,
                   .createPlugin = createSessionMonitorPlugin});
    add(registry, {.pluginId = FollowerSourceTapPlugin::xmlTypeName,
                   .displayName = "Follower Source Tap",
                   .browserCategory = "Utility",
                   .description = "Internal source tap used by follower modulators.",
                   .createMode = InternalPluginCreateMode::Unsupported,
                   .canCreateDetached = false,
                   .canCreateOnTrack = false,
                   .matchesPlugin = matches<FollowerSourceTapPlugin>,
                   .createPlugin = createRealtimePlugin<FollowerSourceTapPlugin>});
    add(registry, {.pluginId = InsertCapturePlugin::xmlTypeName,
                   .displayName = "Insert Capture",
                   .browserCategory = "Utility",
                   .description = "Internal capture endpoint used by external insert routing.",
                   .createMode = InternalPluginCreateMode::Unsupported,
                   .canCreateDetached = false,
                   .canCreateOnTrack = false,
                   .matchesPlugin = matches<InsertCapturePlugin>,
                   .createPlugin = createPlugin<InsertCapturePlugin>});
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
    registerTracktionDevices(registry);
    registerNativeDevices(registry);
    registerInfrastructureDevices(registry);
    registerCompiledParameterAliases(registry);
}

namespace {
[[maybe_unused]] const bool baseDevicePackRegistered = registerDevicePack(registerBaseDevices);
}

}  // namespace magda::daw::audio
