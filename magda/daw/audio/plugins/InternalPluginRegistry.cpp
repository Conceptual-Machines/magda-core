#include "plugins/InternalPluginRegistry.hpp"

#include "TracktionHelpers.hpp"
#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/FaustPlugin.hpp"
#include "plugins/InstrumentMeterTapPlugin.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/MidiReceivePlugin.hpp"
#include "plugins/SidechainMonitorPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "processors/DeviceProcessor.hpp"
#include "processors/internal/MidiDeviceProcessors.hpp"
#include "processors/internal/NativeDeviceProcessors.hpp"
#include "session/SessionMonitorPlugin.hpp"

namespace magda::daw::audio {

namespace {

template <typename PluginType> bool matches(te::Plugin* plugin) {
    return dynamic_cast<PluginType*>(plugin) != nullptr;
}

template <typename ProcessorType>
std::unique_ptr<DeviceProcessor> makeProcessor(DeviceId deviceId, te::Plugin::Ptr plugin) {
    return std::make_unique<ProcessorType>(deviceId, plugin);
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
constexpr const char* kUtilityAliases[] = {"utility"};
constexpr const char* kFourOscAliases[] = {"4osc"};
constexpr const char* kToneAliases[] = {"tone", "tonegenerator"};
constexpr const char* kMeterAliases[] = {"meter", "levelmeter"};

const InternalPluginSpec kSpecs[] = {
    {InternalDeviceKind::TeEq, te::EqualiserPlugin::xmlTypeName, "Equaliser", "EQ",
     "Four-band equaliser for broad tonal shaping and corrective filtering.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kEqAliases, std::size(kEqAliases),
     matches<te::EqualiserPlugin>, makeProcessor<EqualiserProcessor>},
    {InternalDeviceKind::TeCompressor, te::CompressorPlugin::xmlTypeName, "Compressor", "Dynamics",
     "Track compressor for controlling level, transient shape, and sustain.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kCompressorAliases,
     std::size(kCompressorAliases), matches<te::CompressorPlugin>,
     makeProcessor<CompressorProcessor>},
    {InternalDeviceKind::TeReverb, te::ReverbPlugin::xmlTypeName, "Reverb", "Reverb",
     "Algorithmic space effect for room, plate, and ambience-style tails.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kReverbAliases,
     std::size(kReverbAliases), matches<te::ReverbPlugin>, makeProcessor<ReverbProcessor>},
    {InternalDeviceKind::TeDelay, te::DelayPlugin::xmlTypeName, "Delay", "Delay",
     "Tempo-capable delay effect for echoes and rhythmic repeats.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kDelayAliases,
     std::size(kDelayAliases), matches<te::DelayPlugin>, makeProcessor<DelayProcessor>},
    {InternalDeviceKind::TeChorus, te::ChorusPlugin::xmlTypeName, "Chorus", "Modulation",
     "Modulated delay effect for width, movement, and ensemble-style thickening.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kChorusAliases,
     std::size(kChorusAliases), matches<te::ChorusPlugin>, makeProcessor<ChorusProcessor>},
    {InternalDeviceKind::TePhaser, te::PhaserPlugin::xmlTypeName, "Phaser", "Modulation",
     "Swept phase-cancellation effect for resonant movement and stereo motion.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kPhaserAliases,
     std::size(kPhaserAliases), matches<te::PhaserPlugin>, makeProcessor<PhaserProcessor>},
    {InternalDeviceKind::TeLowpass, te::LowPassPlugin::xmlTypeName, "Lowpass", "Filter",
     "Low-pass filter for removing high-frequency content.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kLowpassAliases,
     std::size(kLowpassAliases), matches<te::LowPassPlugin>, makeProcessor<FilterProcessor>},
    {InternalDeviceKind::TePitchShift, te::PitchShiftPlugin::xmlTypeName, "Pitch Shift", "Pitch",
     "Pitch shifting effect for transposition and special effects.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kPitchShiftAliases,
     std::size(kPitchShiftAliases), matches<te::PitchShiftPlugin>,
     makeProcessor<PitchShiftProcessor>},
    {InternalDeviceKind::TeImpulseResponse, te::ImpulseResponsePlugin::xmlTypeName, "IR Reverb",
     "Reverb", "Convolution-style response loader for captured spaces and resonant bodies.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kImpulseResponseAliases,
     std::size(kImpulseResponseAliases), matches<te::ImpulseResponsePlugin>,
     makeProcessor<ImpulseResponseProcessor>},
    {InternalDeviceKind::TeVolumeAndPan, te::VolumeAndPanPlugin::xmlTypeName, "Utility", "Utility",
     "Gain and pan utility for simple level and stereo placement changes.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kUtilityAliases,
     std::size(kUtilityAliases), matches<te::VolumeAndPanPlugin>, makeProcessor<UtilityProcessor>},
    {InternalDeviceKind::TeFourOsc, te::FourOscPlugin::xmlTypeName, "4OSC Synth", "Synth",
     "Four-oscillator subtractive instrument with modulation and macro-friendly controls.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kFourOscAliases,
     std::size(kFourOscAliases), matches<te::FourOscPlugin>, makeProcessor<FourOscProcessor>},
    {InternalDeviceKind::TeToneGenerator, te::ToneGeneratorPlugin::xmlTypeName, "Test Tone",
     "Utility", "Simple tone generator for calibration, routing checks, and utility signals.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, kToneAliases, std::size(kToneAliases),
     matches<te::ToneGeneratorPlugin>, makeProcessor<ToneGeneratorProcessor>},
    {InternalDeviceKind::TeLevelMeter, te::LevelMeterPlugin::xmlTypeName, "Level Meter", "Meter",
     "Signal meter for monitoring level inside a chain.",
     InternalPluginCreateMode::LevelMeterValueTree, false, true, kMeterAliases,
     std::size(kMeterAliases), matches<te::LevelMeterPlugin>, nullptr},
    {InternalDeviceKind::MagdaSampler, MagdaSamplerPlugin::xmlTypeName, "Sampler", "Sampler",
     "Sample playback instrument with envelope, pitch, start/end, and looping controls.",
     InternalPluginCreateMode::FreshValueTree, true, true, nullptr, 0, matches<MagdaSamplerPlugin>,
     makeProcessor<MagdaSamplerProcessor>},
    {InternalDeviceKind::DrumGrid, DrumGridPlugin::xmlTypeName, "Drum Grid", "Drums",
     "Pad-based drum instrument with per-pad sample and effect chains.",
     InternalPluginCreateMode::FreshValueTree, true, true, nullptr, 0, matches<DrumGridPlugin>,
     makeProcessor<DrumGridProcessor>},
    {InternalDeviceKind::MidiChordEngine, MidiChordEnginePlugin::xmlTypeName, "Chord Engine",
     "MIDI", "MIDI processor for chord generation, voicing, and harmonic transforms.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, nullptr, 0,
     matches<MidiChordEnginePlugin>, nullptr},
    {InternalDeviceKind::Arpeggiator, ArpeggiatorPlugin::xmlTypeName, "Arpeggiator", "MIDI",
     "MIDI arpeggiator for rhythmic note patterns and held-note motion.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, nullptr, 0,
     matches<ArpeggiatorPlugin>, makeProcessor<ArpeggiatorProcessor>},
    {InternalDeviceKind::StepSequencer, StepSequencerPlugin::xmlTypeName, "Step Sequencer", "MIDI",
     "MIDI step sequencer for pattern-driven notes and rhythmic control.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, nullptr, 0,
     matches<StepSequencerPlugin>, makeProcessor<StepSequencerProcessor>},
    {InternalDeviceKind::Faust, FaustPlugin::xmlTypeName, "Faust", "Experimental",
     "Interpreted Faust device for loading and editing user DSP code.",
     InternalPluginCreateMode::SavedStateOrFresh, true, true, nullptr, 0, matches<FaustPlugin>,
     makeProcessor<FaustProcessor>},
    {InternalDeviceKind::MidiReceive, ::magda::MidiReceivePlugin::xmlTypeName, "MIDI Receive",
     "MIDI", "Internal MIDI routing endpoint used by MAGDA track and device routing.",
     InternalPluginCreateMode::Unsupported, false, false, nullptr, 0,
     matches<::magda::MidiReceivePlugin>, nullptr},
    {InternalDeviceKind::SidechainMonitor, ::magda::SidechainMonitorPlugin::xmlTypeName,
     "Sidechain Monitor", "Utility", "Internal monitor used to expose sidechain signal state.",
     InternalPluginCreateMode::Unsupported, false, false, nullptr, 0,
     matches<::magda::SidechainMonitorPlugin>, nullptr},
    {InternalDeviceKind::AudioSidechainMonitor, "audiosidechainmonitor", "Audio Sidechain Monitor",
     "Utility", "Internal audio monitor used by sidechain-aware devices.",
     InternalPluginCreateMode::Unsupported, false, false, nullptr, 0, nullptr, nullptr},
    {InternalDeviceKind::InstrumentMeterTap, InstrumentMeterTapPlugin::xmlTypeName,
     "Instrument Meter Tap", "Meter",
     "Internal meter tap used to observe instrument output levels.",
     InternalPluginCreateMode::Unsupported, false, false, nullptr, 0,
     matches<InstrumentMeterTapPlugin>, nullptr},
    {InternalDeviceKind::SessionMonitor, ::magda::SessionMonitorPlugin::xmlTypeName,
     "Session Monitor", "Session", "Internal monitor used by session playback and launch state.",
     InternalPluginCreateMode::Unsupported, false, false, nullptr, 0,
     matches<::magda::SessionMonitorPlugin>, nullptr},
};

const InternalPluginSpec* const kSpecPtrs[] = {
    &kSpecs[0],  &kSpecs[1],  &kSpecs[2],  &kSpecs[3],  &kSpecs[4],  &kSpecs[5],
    &kSpecs[6],  &kSpecs[7],  &kSpecs[8],  &kSpecs[9],  &kSpecs[10], &kSpecs[11],
    &kSpecs[12], &kSpecs[13], &kSpecs[14], &kSpecs[15], &kSpecs[16], &kSpecs[17],
    &kSpecs[18], &kSpecs[19], &kSpecs[20], &kSpecs[21], &kSpecs[22], &kSpecs[23],
};

bool typeMatchesAlias(const juce::String& type, const InternalPluginSpec& spec) {
    if (spec.pluginId != nullptr && type.equalsIgnoreCase(spec.pluginId))
        return true;

    for (int i = 0; i < spec.loadAliasCount; ++i) {
        if (spec.loadAliases[i] != nullptr && type.equalsIgnoreCase(spec.loadAliases[i]))
            return true;
    }

    return false;
}

te::Plugin::Ptr createFreshValueTreePlugin(te::Edit& edit, const char* xmlTypeName) {
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, xmlTypeName, nullptr);
    return edit.getPluginCache().createNewPlugin(pluginState);
}

}  // namespace

std::span<const InternalPluginSpec* const> getAllInternalPluginSpecs() {
    return {kSpecPtrs, std::size(kSpecPtrs)};
}

const InternalPluginSpec* findInternalPluginSpec(InternalDeviceKind kind) {
    if (kind == InternalDeviceKind::External)
        return nullptr;

    for (const auto* spec : kSpecPtrs)
        if (spec->kind == kind)
            return spec;

    return nullptr;
}

const InternalPluginSpec* findInternalPluginSpec(const juce::String& pluginId) {
    return findInternalPluginSpec(classifyInternalDevice(pluginId));
}

const InternalPluginSpec* findInternalPluginSpecForLoadType(const juce::String& type) {
    for (const auto* spec : kSpecPtrs)
        if (typeMatchesAlias(type, *spec))
            return spec;

    return nullptr;
}

te::Plugin::Ptr createInternalPluginFromSpec(const InternalPluginSpec& spec, te::Edit& edit,
                                             const juce::String& savedPluginState) {
    if (spec.pluginId == nullptr || spec.createMode == InternalPluginCreateMode::Unsupported)
        return nullptr;

    if (spec.createMode == InternalPluginCreateMode::LevelMeterValueTree)
        return edit.getPluginCache().createNewPlugin(te::LevelMeterPlugin::create());

    if (spec.createMode == InternalPluginCreateMode::SavedStateOrFresh &&
        savedPluginState.isNotEmpty()) {
        if (auto xml = juce::parseXML(savedPluginState)) {
            auto savedState = juce::ValueTree::fromXml(*xml);
            if (savedState.isValid()) {
                stripTracktionIdsRecursive(savedState);
                return edit.getPluginCache().createNewPlugin(savedState);
            }
        } else {
            DBG("createInternalPluginFromSpec: failed to parse saved state for " << spec.pluginId);
        }
    }

    if (spec.createMode == InternalPluginCreateMode::FreshValueTree)
        return createFreshValueTreePlugin(edit, spec.pluginId);

    auto plugin = edit.getPluginCache().createNewPlugin(spec.pluginId, {});
    if (!plugin)
        plugin = createFreshValueTreePlugin(edit, spec.pluginId);

    if (plugin != nullptr && spec.kind == InternalDeviceKind::TeEq) {
        auto params = plugin->getAutomatableParameters();
        const float defaultFreqs[4] = {100.0f, 500.0f, 3000.0f, 10000.0f};
        for (int band = 0; band < 4; ++band) {
            int freqIndex = band * 3;
            if (freqIndex < params.size() && params[freqIndex])
                params[freqIndex]->setParameterFromHost(defaultFreqs[band],
                                                        juce::sendNotificationSync);
        }
    }

    return plugin;
}

std::unique_ptr<DeviceProcessor> createInternalPluginProcessor(const InternalPluginSpec& spec,
                                                               DeviceId deviceId,
                                                               te::Plugin::Ptr plugin) {
    if (!plugin || spec.createProcessor == nullptr)
        return nullptr;

    if (spec.matchesPlugin != nullptr && !spec.matchesPlugin(plugin.get()))
        return nullptr;

    return spec.createProcessor(deviceId, plugin);
}

}  // namespace magda::daw::audio
