#include "InternalDeviceKind.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "audio/plugins/AudioSidechainMonitorPlugin.hpp"
#include "audio/plugins/DrumGridPlugin.hpp"
#include "audio/plugins/FaustPlugin.hpp"
#include "audio/plugins/InstrumentMeterTapPlugin.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "audio/plugins/MidiReceivePlugin.hpp"
#include "audio/plugins/SidechainMonitorPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "audio/plugins/compiled/MagdaChorusCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaCompressorCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaDelayCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaFilterCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaFlangerCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaFreqShiftCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaGrainDelayCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaGritCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaModCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaMultibandCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaPhaserCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaRingModCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaSaturatorCompiledPlugin.hpp"
#include "audio/session/SessionMonitorPlugin.hpp"

namespace magda {

namespace {

// Two id forms get matched: MAGDA's simplified pluginId (what the picker
// stamps onto a fresh DeviceInfo, e.g. "eq" / "lowpass") and TE's actual
// `xmlTypeName` (what an instantiated plugin reports back, e.g. "4bandEq"
// / "pitchShifter"). Both forms are valid and showing up in the wild, so
// the classifier accepts either. Compiled MAGDA plugins use the same id
// for both because their xmlTypeName IS the picker id.
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
    {InternalDeviceKind::TeVolumeAndPan, "Utility", "", "Utility",
     "Gain and pan utility for simple level and stereo placement changes."},
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
    {InternalDeviceKind::Faust, "Faust", "", "Experimental",
     "Interpreted Faust device for loading and editing user DSP code."},
    {InternalDeviceKind::CompiledFilter, "Filter", "", "Filter",
     "Compiled Faust multimode filter.\n"
     "SVF: clean 2-pole LP/BP/HP/Notch for precise shaping.\n"
     "Ladder: classic 4-pole low-pass with driven resonance.\n"
     "Korg 35: MS-style LP/HP character with sharper analog bite.\n"
     "Oberheim: SEM-style LP/BP/HP/Notch with broad musical sweeps.\n"
     "Sallen-Key: smooth 2nd-order LP/BP/HP response.\n"
     "Diode: resonant 4-pole diode ladder with input drive.\n"
     "Warning: high resonance can create very loud peaks or self-oscillation. "
     "Keep monitoring levels conservative to protect speakers and ears."},
    {InternalDeviceKind::CompiledSaturator, "Saturator", "", "Distortion",
     "Compiled Faust waveshaper with drive, mode, bias, tone, mix, and output."},
    {InternalDeviceKind::CompiledDelay, "Delay", "", "Delay",
     "Compiled Faust stereo delay with sync, tone, feedback, and crossfeed."},
    {InternalDeviceKind::CompiledGrainDelay, "Grain Delay", "", "Delay",
     "Compiled Faust granular delay for smeared repeats, pitch motion, and texture."},
    {InternalDeviceKind::CompiledGrit, "Grit", "", "Distortion",
     "Compiled Faust bit-depth and sample-rate reduction effect."},
    {InternalDeviceKind::CompiledCompressor, "Compressor", "", "Dynamics",
     "Compiled Faust compressor with peak/RMS detection, soft knee, stereo link, "
     "audio sidechain input, parallel mix, and output safety limiting."},
    {InternalDeviceKind::CompiledMultiband, "Multiband Compressor", "", "Dynamics",
     "Compiled Faust multiband compressor with editable band thresholds."},
    {InternalDeviceKind::CompiledPhaser, "Phaser", "", "Modulation",
     "Compiled Faust phaser with selectable stages, feedback, and sweep window."},
    {InternalDeviceKind::CompiledMod, "Mod", "", "Modulation",
     "Compiled Faust modulation: tremolo, vibrato, or auto-pan, sharing one LFO. "
     "Free Hz or tempo-synced (musical division). "
     "Sine, triangle, square, or sample-and-hold shape."},
    {InternalDeviceKind::CompiledChorus, "Chorus", "", "Modulation",
     "Compiled Faust stereo chorus with 1 to 3 modulated voices per channel. "
     "Free Hz or tempo-synced rate, depth, feedback, mix, and stereo width."},
    {InternalDeviceKind::CompiledFlanger, "Flanger", "", "Modulation",
     "Compiled Faust stereo flanger — short modulated delay with heavy feedback for the "
     "classic comb-sweep character. Sync- or free-rate LFO."},
    {InternalDeviceKind::CompiledRingMod, "Ring Mod", "", "Modulation",
     "Compiled Faust stereo ring modulator. Multiplies the input by a sine, triangle, or "
     "square carrier from 1 Hz (tremolo) to 5 kHz (metallic clang). Sync- or free-rate."},
    {InternalDeviceKind::CompiledFreqShift, "Freq Shift", "", "Modulation",
     "Compiled Faust stereo single-sideband frequency shifter. Shifts the entire spectrum by "
     "a fixed Hz offset via a Hilbert-pair Bode design. Feedback for resonant artefacts, "
     "Spread for stereo width."},
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
    using daw::audio::StepSequencerPlugin;
    using daw::audio::compiled::MagdaChorusCompiledPlugin;
    using daw::audio::compiled::MagdaCompressorCompiledPlugin;
    using daw::audio::compiled::MagdaDelayCompiledPlugin;
    using daw::audio::compiled::MagdaFilterCompiledPlugin;
    using daw::audio::compiled::MagdaFlangerCompiledPlugin;
    using daw::audio::compiled::MagdaFreqShiftCompiledPlugin;
    using daw::audio::compiled::MagdaGrainDelayCompiledPlugin;
    using daw::audio::compiled::MagdaGritCompiledPlugin;
    using daw::audio::compiled::MagdaModCompiledPlugin;
    using daw::audio::compiled::MagdaMultibandCompiledPlugin;
    using daw::audio::compiled::MagdaPhaserCompiledPlugin;
    using daw::audio::compiled::MagdaRingModCompiledPlugin;
    using daw::audio::compiled::MagdaSaturatorCompiledPlugin;
    namespace TE = tracktion::engine;

    const Mapping kMappings[] = {
        // Compiled MAGDA effects (xmlTypeName == picker id)
        {InternalDeviceKind::CompiledFilter, MagdaFilterCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledSaturator, MagdaSaturatorCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledDelay, MagdaDelayCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledGrainDelay, MagdaGrainDelayCompiledPlugin::xmlTypeName,
         nullptr},
        {InternalDeviceKind::CompiledGrit, MagdaGritCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledCompressor, MagdaCompressorCompiledPlugin::xmlTypeName,
         nullptr},
        {InternalDeviceKind::CompiledMultiband, MagdaMultibandCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledPhaser, MagdaPhaserCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledMod, MagdaModCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledChorus, MagdaChorusCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledFlanger, MagdaFlangerCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledRingMod, MagdaRingModCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledFreqShift, MagdaFreqShiftCompiledPlugin::xmlTypeName, nullptr},
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
        {InternalDeviceKind::TeVolumeAndPan, "utility", TE::VolumeAndPanPlugin::xmlTypeName},
        {InternalDeviceKind::TeFourOsc, "4osc", TE::FourOscPlugin::xmlTypeName},
        {InternalDeviceKind::TeToneGenerator, "tone", TE::ToneGeneratorPlugin::xmlTypeName},
        {InternalDeviceKind::TeLevelMeter, "meter", TE::LevelMeterPlugin::xmlTypeName},
        // MAGDA daw::audio:: plugins
        {InternalDeviceKind::MagdaSampler, MagdaSamplerPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::DrumGrid, DrumGridPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::MidiChordEngine, MidiChordEnginePlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::Arpeggiator, ArpeggiatorPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::StepSequencer, StepSequencerPlugin::xmlTypeName, nullptr},
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
    return getInternalDeviceMetadata(classifyInternalDevice(pluginId));
}

}  // namespace magda
