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
#include "audio/plugins/compiled/MagdaDelayCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaFilterCompiledPlugin.hpp"
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
    using daw::audio::compiled::MagdaDelayCompiledPlugin;
    using daw::audio::compiled::MagdaFilterCompiledPlugin;
    using daw::audio::compiled::MagdaSaturatorCompiledPlugin;
    namespace TE = tracktion::engine;

    const Mapping kMappings[] = {
        // Compiled MAGDA effects (xmlTypeName == picker id)
        {InternalDeviceKind::CompiledFilter, MagdaFilterCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledSaturator, MagdaSaturatorCompiledPlugin::xmlTypeName, nullptr},
        {InternalDeviceKind::CompiledDelay, MagdaDelayCompiledPlugin::xmlTypeName, nullptr},
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

}  // namespace magda
