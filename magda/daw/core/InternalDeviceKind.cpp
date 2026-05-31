#include "InternalDeviceKind.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <vector>

#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "audio/plugins/AudioSidechainMonitorPlugin.hpp"
#include "audio/plugins/DrumGridPlugin.hpp"
#include "audio/plugins/FaustPlugin.hpp"
#include "audio/plugins/InstrumentMeterTapPlugin.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "audio/plugins/MidiReceivePlugin.hpp"
#include "audio/plugins/OscilloscopePlugin.hpp"
#include "audio/plugins/SidechainMonitorPlugin.hpp"
#include "audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "audio/session/SessionMonitorPlugin.hpp"

namespace magda {

namespace {

// Two id forms get matched: MAGDA's simplified pluginId (what the picker
// stamps onto a fresh DeviceInfo, e.g. "eq" / "lowpass") and TE's actual
// `xmlTypeName` (what an instantiated plugin reports back, e.g. "4bandEq"
// / "pitchShifter"). Both forms are valid and showing up in the wild, so
// the classifier accepts either. Compiled MAGDA plugins are registry-driven
// and deliberately not classified here.
struct Mapping {
    InternalDeviceKind kind;
    const char* a;
    const char* b;  // optional alternate id; nullptr if same as a
};

bool matches(const juce::String& id, const char* a, const char* b) {
    if (id.equalsIgnoreCase(a))
        return true;
    return b != nullptr && id.equalsIgnoreCase(b);
}

const InternalDeviceMetadata kMetadata[] = {
    {InternalDeviceKind::TeEq, "Equaliser", "", "EQ",
     "Four-band equaliser for broad tonal shaping and corrective filtering."},
    {InternalDeviceKind::TeCompressor, "Compressor", "", "Dynamics",
     "Track compressor for controlling level, transient shape, and sustain."},
    {InternalDeviceKind::TeReverb, "Reverb", "", "Reverb",
     "Algorithmic space effect for room, plate, and ambience-style tails."},
    {InternalDeviceKind::TeDelay, "Delay", "", "Delay",
     "Tempo-capable delay effect for echoes and rhythmic repeats."},
    {InternalDeviceKind::TeChorus, "Chorus", "", "Modulation",
     "Modulated delay effect for width, movement, and ensemble-style thickening."},
    {InternalDeviceKind::TePhaser, "Phaser", "", "Modulation",
     "Swept phase-cancellation effect for resonant movement and stereo motion."},
    {InternalDeviceKind::TeLowpass, "Lowpass", "", "Filter",
     "Low-pass filter for removing high-frequency content."},
    {InternalDeviceKind::TePitchShift, "Pitch Shift", "", "Pitch",
     "Pitch shifting effect for transposition and special effects."},
    {InternalDeviceKind::TeImpulseResponse, "IR Reverb", "", "Reverb",
     "Convolution-style response loader for captured spaces and resonant bodies."},
    {InternalDeviceKind::TeVolumeAndPan, "Legacy Volume/Pan", "", "Legacy",
     "Legacy Tracktion volume and pan device, kept for old project loads."},
    {InternalDeviceKind::TeFourOsc, "4OSC Synth", "", "Synth",
     "Four-oscillator subtractive instrument with modulation and macro-friendly controls."},
    {InternalDeviceKind::TeToneGenerator, "Test Tone", "", "Utility",
     "Simple tone generator for calibration, routing checks, and utility signals."},
    {InternalDeviceKind::TeLevelMeter, "Level Meter", "", "Meter",
     "Signal meter for monitoring level inside a chain."},
    {InternalDeviceKind::MagdaSampler, "Sampler", "", "Sampler",
     "Sample playback instrument with envelope, pitch, start/end, and looping controls."},
    {InternalDeviceKind::DrumGrid, "Drum Grid", "", "Drums",
     "Pad-based drum instrument with per-pad sample and effect chains."},
    {InternalDeviceKind::MidiReceive, "MIDI Receive", "", "MIDI",
     "Internal MIDI routing endpoint used by MAGDA track and device routing."},
    {InternalDeviceKind::MidiChordEngine, "Chord Engine", "", "MIDI",
     "MIDI processor for chord generation, voicing, and harmonic transforms."},
    {InternalDeviceKind::Arpeggiator, "Arpeggiator", "", "MIDI",
     "MIDI arpeggiator for rhythmic note patterns and held-note motion."},
    {InternalDeviceKind::StepSequencer, "Step Sequencer", "", "MIDI",
     "MIDI step sequencer for pattern-driven notes and rhythmic control."},
    {InternalDeviceKind::SidechainMonitor, "Sidechain Monitor", "", "Utility",
     "Internal monitor used to expose sidechain signal state."},
    {InternalDeviceKind::AudioSidechainMonitor, "Audio Sidechain Monitor", "", "Utility",
     "Internal audio monitor used by sidechain-aware devices."},
    {InternalDeviceKind::InstrumentMeterTap, "Instrument Meter Tap", "", "Meter",
     "Internal meter tap used to observe instrument output levels."},
    {InternalDeviceKind::SessionMonitor, "Session Monitor", "", "Session",
     "Internal monitor used by session playback and launch state."},
    {InternalDeviceKind::Oscilloscope, "Oscilloscope", "", "Analysis",
     "Transparent waveform monitor for inspecting signal shape over time."},
    {InternalDeviceKind::SpectrumAnalyzer, "Spectrum Analyzer", "", "Analysis",
     "Real-time FFT spectrum display with log-frequency axis and peak hold."},
    {InternalDeviceKind::Faust, "Faust", "", "Experimental",
     "Interpreted Faust device for loading and editing user DSP code."},
};

struct CompiledMetadataCache {
    std::vector<InternalDeviceMetadata> metadata;
    std::vector<const daw::audio::compiled::CompiledPluginSpec*> specs;

    CompiledMetadataCache() {
        for (auto* spec : daw::audio::compiled::getAllCompiledPluginSpecs()) {
            specs.push_back(spec);
            metadata.push_back({InternalDeviceKind::External, spec->displayName, "",
                                spec->browserCategory, spec->description});
        }
    }

    const InternalDeviceMetadata* find(const juce::String& pluginId) const {
        for (size_t i = 0; i < specs.size(); ++i) {
            if (pluginId.equalsIgnoreCase(specs[i]->pluginId))
                return &metadata[i];

            if (specs[i]->aliasKey != nullptr && pluginId.equalsIgnoreCase(specs[i]->aliasKey))
                return &metadata[i];
        }

        return nullptr;
    }
};

}  // namespace

InternalDeviceKind classifyInternalDevice(const juce::String& pluginId) {
    if (pluginId.isEmpty())
        return InternalDeviceKind::External;

    // Plugins live in two different namespaces depending on age — the
    // newer compiled / picker-facing ones in magda::daw::audio, the
    // older infrastructure plugins (MidiReceive, sidechain, session
    // monitor) directly in magda. Spell out the qualified xmlTypeName
    // for each so the classifier doesn't depend on a using-directive.
    using daw::audio::ArpeggiatorPlugin;
    using daw::audio::DrumGridPlugin;
    using daw::audio::FaustPlugin;
    using daw::audio::InstrumentMeterTapPlugin;
    using daw::audio::MagdaSamplerPlugin;
    using daw::audio::MidiChordEnginePlugin;
    using daw::audio::OscilloscopePlugin;
    using daw::audio::SpectrumAnalyzerPlugin;
    using daw::audio::StepSequencerPlugin;
    namespace TE = tracktion::engine;

    const Mapping kMappings[] = {
        // TE built-in effects — picker uses a short id, the live plugin
        // reports the real `te::*::xmlTypeName`. Match either.
        {InternalDeviceKind::TeEq, "eq", TE::EqualiserPlugin::xmlTypeName},
        {InternalDeviceKind::TeCompressor, "compressor", TE::CompressorPlugin::xmlTypeName},
        {InternalDeviceKind::TeReverb, "reverb", TE::ReverbPlugin::xmlTypeName},
        {InternalDeviceKind::TeDelay, "delay", TE::DelayPlugin::xmlTypeName},
        {InternalDeviceKind::TeChorus, "chorus", TE::ChorusPlugin::xmlTypeName},
        {InternalDeviceKind::TePhaser, "phaser", TE::PhaserPlugin::xmlTypeName},
        {InternalDeviceKind::TeLowpass, "lowpass", TE::LowPassPlugin::xmlTypeName},
        {InternalDeviceKind::TePitchShift, "pitchshift", TE::PitchShiftPlugin::xmlTypeName},
        {InternalDeviceKind::TeImpulseResponse, "impulseresponse",
         TE::ImpulseResponsePlugin::xmlTypeName},
        {InternalDeviceKind::TeVolumeAndPan, nullptr, TE::VolumeAndPanPlugin::xmlTypeName},
        {InternalDeviceKind::TeFourOsc, "4osc", TE::FourOscPlugin::xmlTypeName},
        {InternalDeviceKind::TeFourOsc, "4OSC Synth", nullptr},
        {InternalDeviceKind::TeToneGenerator, "tone", TE::ToneGeneratorPlugin::xmlTypeName},
        {InternalDeviceKind::TeLevelMeter, "meter", TE::LevelMeterPlugin::xmlTypeName},
        // MAGDA daw::audio:: plugins
        {InternalDeviceKind::MagdaSampler, MagdaSamplerPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::DrumGrid, DrumGridPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::MidiChordEngine, MidiChordEnginePlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::Arpeggiator, ArpeggiatorPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::StepSequencer, StepSequencerPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::Oscilloscope, OscilloscopePlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::SpectrumAnalyzer, SpectrumAnalyzerPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::InstrumentMeterTap, InstrumentMeterTapPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::Faust, FaustPlugin::xmlTypeName, nullptr},
        // Plugins still in plain magda:: (older infra layers).
        {InternalDeviceKind::MidiReceive, MidiReceivePlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::SidechainMonitor, SidechainMonitorPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::AudioSidechainMonitor, AudioSidechainMonitorPlugin::xmlTypeName,
         nullptr},
        {InternalDeviceKind::SessionMonitor, SessionMonitorPlugin::xmlTypeName, nullptr},
    };

    for (const auto& m : kMappings) {
        if (matches(pluginId, m.a, m.b))
            return m.kind;
    }
    return InternalDeviceKind::External;
}

const InternalDeviceMetadata* getInternalDeviceMetadata(InternalDeviceKind kind) {
    if (kind == InternalDeviceKind::External)
        return nullptr;

    for (const auto& metadata : kMetadata) {
        if (metadata.kind == kind)
            return &metadata;
    }

    return nullptr;
}

const InternalDeviceMetadata* getInternalDeviceMetadataForPluginId(const juce::String& pluginId) {
    static const CompiledMetadataCache compiledMetadata;

    if (auto* metadata = compiledMetadata.find(pluginId))
        return metadata;

    return getInternalDeviceMetadata(classifyInternalDevice(pluginId));
}

bool isAnalysisDevice(const juce::String& pluginId) {
    const auto kind = classifyInternalDevice(pluginId);
    return kind == InternalDeviceKind::Oscilloscope || kind == InternalDeviceKind::SpectrumAnalyzer;
}

int postFxAnalysisDeviceOrder(const juce::String& pluginId) {
    switch (classifyInternalDevice(pluginId)) {
        case InternalDeviceKind::Oscilloscope:
            return 0;
        case InternalDeviceKind::SpectrumAnalyzer:
            return 1;
        default:
            return -1;
    }
}

}  // namespace magda
