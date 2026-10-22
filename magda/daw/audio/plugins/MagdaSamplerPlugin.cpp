#include "plugins/MagdaSamplerPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace magda::daw::audio {

namespace {

/// One slot's metadata, pinned to what the retired host-native plugin
/// registered: saved automation addresses these normalised positions.
/// Its curves came from `juce::NormalisableRange` skews, where real =
/// min + span * norm^(1/skew); MAGDA's Exponential is norm^k * span + min,
/// so k is the RECIPROCAL of the JUCE skew.
sdk::ParameterDescriptor slotInfo(int index) {
    sdk::ParameterDescriptor info;
    info.index = index;

    // Time params used a JUCE skew of 0.4 so equal knob/macro movement gives
    // perceptually-even change: most audible action is in the first few hundred
    // ms, which a linear range crams into the bottom of the range.
    constexpr float kTimeSkew = 1.0f / 0.4f;

    switch (index) {
        case MagdaSamplerPlugin::kAttack:
            info.stableId = "attack";
            info.name = "Attack";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.001f;
            break;

        case MagdaSamplerPlugin::kDecay:
            info.stableId = "decay";
            info.name = "Decay";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.1f;
            break;

        case MagdaSamplerPlugin::kSustain:
            info.stableId = "sustain";
            info.name = "Sustain";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 1.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kRelease:
            info.stableId = "release";
            info.name = "Release";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 10.0f;
            info.defaultValue = 0.1f;
            break;

        case MagdaSamplerPlugin::kPitch:
            info.stableId = "pitch";
            info.name = "Pitch";
            info.unit = "st";
            info.minValue = -24.0f;
            info.maxValue = 24.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kFine:
            info.stableId = "fine";
            info.name = "Fine";
            info.unit = "ct";
            info.minValue = -100.0f;
            info.maxValue = 100.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kLevel:
            info.stableId = "level";
            info.name = "Level";
            info.unit = "dB";
            // JUCE skew 4.0: unity sits high in the range, so the usable trim
            // around 0 dB gets most of the travel.
            info.scale = ParameterScale::Exponential;
            info.exponent = 1.0f / 4.0f;
            info.minValue = -60.0f;
            info.maxValue = 12.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kSampleStart:
            info.stableId = "sampleStart";
            info.name = "Sample Start";
            info.unit = "s";
            info.minValue = 0.0f;
            info.maxValue = 300.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kSampleEnd:
            info.stableId = "sampleEnd";
            info.name = "Sample End";
            info.unit = "s";
            info.minValue = 0.0f;
            info.maxValue = 300.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kLoopStart:
            info.stableId = "loopStart";
            info.name = "Loop Start";
            info.unit = "s";
            info.minValue = 0.0f;
            info.maxValue = 300.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kLoopEnd:
            info.stableId = "loopEnd";
            info.name = "Loop End";
            info.unit = "s";
            info.minValue = 0.0f;
            info.maxValue = 300.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kVelAmount:
            info.stableId = "velAmount";
            info.name = "Vel Amount";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 1.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kVoiceMode:
            info.stableId = "voiceMode";
            info.name = "Voice Mode";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 2.0f;
            info.defaultValue = 0.0f;
            info.choices = sdk::choicesFromLabels({"Poly", "Mono", "Legato"});
            break;

        case MagdaSamplerPlugin::kGlide:
            info.stableId = "glide";
            info.name = "Glide";
            info.unit = "ms";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.0f;
            info.maxValue = 2000.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kPitchEnvAttack:
            info.stableId = "pitchEnvAttack";
            info.name = "Pitch Attack";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.001f;
            break;

        case MagdaSamplerPlugin::kPitchEnvDecay:
            info.stableId = "pitchEnvDecay";
            info.name = "Pitch Decay";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.2f;
            break;

        case MagdaSamplerPlugin::kPitchEnvSustain:
            info.stableId = "pitchEnvSustain";
            info.name = "Pitch Sustain";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kPitchEnvRelease:
            info.stableId = "pitchEnvRelease";
            info.name = "Pitch Release";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 10.0f;
            info.defaultValue = 0.1f;
            break;

        case MagdaSamplerPlugin::kPitchEnvAmount:
            info.stableId = "pitchEnvAmount";
            info.name = "Pitch Env";
            info.unit = "st";
            info.minValue = -48.0f;
            info.maxValue = 48.0f;
            info.defaultValue = 0.0f;
            break;

        // Off first, then the Filter device's engines in its order.
        case MagdaSamplerPlugin::kFilterType:
            info.stableId = "filterType";
            info.name = "Filter Type";
            info.scale = ParameterScale::Discrete;
            info.choices = sdk::choicesFromLabels(
                {"Off", "SVF", "Ladder", "Korg 35", "Oberheim", "Sallen-Key", "Diode", "Comb"});
            info.minValue = 0.0f;
            info.maxValue = static_cast<float>(info.choices.size() - 1);
            info.defaultValue = 0.0f;
            break;

        // A mode the type lacks plays as its first; Comb reads the first two as + and -.
        case MagdaSamplerPlugin::kFilterMode:
            info.stableId = "filterMode";
            info.name = "Filter Mode";
            info.scale = ParameterScale::Discrete;
            info.choices = sdk::choicesFromLabels({"LP", "BP", "HP", "Notch", "Notch+"});
            info.minValue = 0.0f;
            info.maxValue = 4.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kFilterPoles:
            info.stableId = "filterPoles";
            info.name = "Filter Poles";
            info.scale = ParameterScale::Discrete;
            info.choices = sdk::choicesFromLabels({"2", "4", "6"});
            info.minValue = 0.0f;
            info.maxValue = 2.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaSamplerPlugin::kFilterCutoff:
            info.stableId = "filterCutoff";
            info.name = "Cutoff";
            info.unit = "Hz";
            info.scale = ParameterScale::Logarithmic;
            info.scaleAnchor = 1000.0f;
            info.minValue = 20.0f;
            info.maxValue = 20000.0f;
            info.defaultValue = 1000.0f;
            break;

        case MagdaSamplerPlugin::kFilterResonance:
            info.stableId = "filterResonance";
            info.name = "Resonance";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kFilterKeyTrack:
            info.stableId = "filterKeyTrack";
            info.name = "Key Track";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kFilterVelocity:
            info.stableId = "filterVelocity";
            info.name = "Filter Velocity";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kFilterEnvAttack:
            info.stableId = "filterEnvAttack";
            info.name = "Filter Attack";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.001f;
            break;

        case MagdaSamplerPlugin::kFilterEnvDecay:
            info.stableId = "filterEnvDecay";
            info.name = "Filter Decay";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.3f;
            break;

        case MagdaSamplerPlugin::kFilterEnvSustain:
            info.stableId = "filterEnvSustain";
            info.name = "Filter Sustain";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaSamplerPlugin::kFilterEnvRelease:
            info.stableId = "filterEnvRelease";
            info.name = "Filter Release";
            info.unit = "s";
            info.scale = ParameterScale::Exponential;
            info.exponent = kTimeSkew;
            info.minValue = 0.001f;
            info.maxValue = 10.0f;
            info.defaultValue = 0.1f;
            break;

        case MagdaSamplerPlugin::kFilterEnvAmount:
            info.stableId = "filterEnvAmount";
            info.name = "Filter Env";
            info.unit = "st";
            info.minValue = -72.0f;
            info.maxValue = 72.0f;
            info.defaultValue = 0.0f;
            break;

        default:
            break;
    }

    return info;
}

/// The root note a file's own metadata names, if it names one.
std::optional<int> rootNoteFromMetadata(const juce::AudioFormatReader& reader) {
    const auto& metadata = reader.metadataValues;
    if (metadata.containsKey("MidiUnityNote"))
        return metadata.getValue("MidiUnityNote", "60").getIntValue();
    if (metadata.containsKey("smpl_MIDIUnityNote"))
        return metadata.getValue("smpl_MIDIUnityNote", "60").getIntValue();
    return std::nullopt;
}

}  // namespace

const char* MagdaSamplerPlugin::xmlTypeName = "magdasampler";

//==============================================================================
// MagdaSamplerPlugin Implementation
//==============================================================================

MagdaSamplerPlugin::MagdaSamplerPlugin() {
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)].store(
            ParameterUtils::realToNormalized(info.defaultValue, ParameterUtils::domainOf(info)),
            std::memory_order_relaxed);
    }
    tailSeconds_.store(displayValue(kRelease), std::memory_order_relaxed);
}

MagdaSamplerPlugin::~MagdaSamplerPlugin() = default;

//==============================================================================
sdk::ParameterDescriptor MagdaSamplerPlugin::parameterDescriptor(int index) const {
    return slotDescriptor(index);
}

sdk::ParameterDescriptor MagdaSamplerPlugin::slotDescriptor(int index) {
    if (index < 0 || index >= kNumParams)
        return {};

    return slotInfo(index);
}

ParameterInfo MagdaSamplerPlugin::parameterInfo(int index) const {
    auto info = MagdaDevice::parameterInfo(index);
    if (index >= 0 && index < kNumParams)
        info.currentValue = displayValue(index);
    return info;
}

float MagdaSamplerPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

void MagdaSamplerPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;

    values_[static_cast<size_t>(index)].store(juce::jlimit(0.0f, 1.0f, value),
                                              std::memory_order_relaxed);

    // The tail the host asks for is the release stage's length, so it has to
    // follow the slot rather than be read once at construction.
    if (index == kRelease)
        tailSeconds_.store(displayValue(kRelease), std::memory_order_relaxed);
}

float MagdaSamplerPlugin::displayValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return ParameterUtils::normalizedToReal(
        values_[static_cast<size_t>(index)].load(std::memory_order_relaxed),
        domains_[static_cast<size_t>(index)]);
}

void MagdaSamplerPlugin::setDisplayValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    setParameterValue(
        index, ParameterUtils::realToNormalized(value, domains_[static_cast<size_t>(index)]));
}

//==============================================================================
void MagdaSamplerPlugin::prepare(const DevicePrepareContext& context) {
    sampleRate = context.sampleRate;
    engine_.prepare(sampleRate);
}

void MagdaSamplerPlugin::release() {
    engine_.stopAll();
}

void MagdaSamplerPlugin::reset() {
    engine_.stopAll();
}

SamplerVoiceSettings MagdaSamplerPlugin::voiceSettings() const {
    SamplerVoiceSettings settings;
    settings.amp = {displayValue(kAttack), displayValue(kDecay), displayValue(kSustain),
                    displayValue(kRelease)};
    settings.pitchSemitones = displayValue(kPitch);
    settings.fineCents = displayValue(kFine);
    settings.pitchEnvelope = {displayValue(kPitchEnvAttack), displayValue(kPitchEnvDecay),
                              displayValue(kPitchEnvSustain), displayValue(kPitchEnvRelease)};
    settings.pitchEnvelopeSemitones = displayValue(kPitchEnvAmount);

    auto& filter = settings.filter;
    filter.family = static_cast<int>(std::lround(displayValue(kFilterType))) - 1;
    filter.mode = static_cast<int>(std::lround(displayValue(kFilterMode)));
    filter.poleIndex = static_cast<int>(std::lround(displayValue(kFilterPoles)));
    filter.cutoffHz = displayValue(kFilterCutoff);
    filter.resonance = displayValue(kFilterResonance);
    filter.keyTrack = displayValue(kFilterKeyTrack);
    filter.velocity = displayValue(kFilterVelocity);
    filter.envelopeSemitones = displayValue(kFilterEnvAmount);
    settings.filterEnvelope = {displayValue(kFilterEnvAttack), displayValue(kFilterEnvDecay),
                               displayValue(kFilterEnvSustain), displayValue(kFilterEnvRelease)};

    // The marker parameters, for the one zone that reads them; the voice clamps to its file.
    auto& markers = settings.parameterRegion;
    markers.start = displayValue(kSampleStart);
    markers.end = displayValue(kSampleEnd);
    markers.loop = loopEnabled_.load(std::memory_order_relaxed);
    markers.loopStart = displayValue(kLoopStart);
    markers.loopEnd = displayValue(kLoopEnd);

    settings.velocityAmount = displayValue(kVelAmount);
    settings.voiceMode = static_cast<int>(std::lround(displayValue(kVoiceMode)));
    settings.glideSeconds = displayValue(kGlide) / 1000.0;
    return settings;
}

void MagdaSamplerPlugin::process(DeviceProcessContext& context) {
    auto audio = juceAudio(context);
    const int numSamples = context.numSamples();
    const std::optional<DeviceMidiInput> in =
        context.midiIn != nullptr
            ? std::optional<DeviceMidiInput>(std::in_place, *context.midiIn, sampleRate)
            : std::nullopt;

    engine_.process(audio, in ? &*in : nullptr, numSamples, [this] { return voiceSettings(); });
    audio.applyGain(0, numSamples, juce::Decibels::decibelsToGain(displayValue(kLevel)));
}

//==============================================================================

sdk::RestoreResult MagdaSamplerPlugin::restoreState(const sdk::StateNode& state) {
    // Absent means off, as it does for the sample and the root note below: the
    // document is the whole authored state, so restoring one saved before the
    // loop was switched on has to switch it back off (#2377).
    loopEnabled_.store(state.getBool(stateKey(StateIDs::loopEnabled), false),
                       std::memory_order_relaxed);

    auto listed = SamplerEngine::listedZones(state);
    if (listed.empty()) {
        restoreRootZone(state);
        return sdk::RestoreResult::success();
    }

    // Each file is read again only if it changed, so an edit to one zone cuts no voice.
    engine_.setZones(std::move(listed), true);
    return sdk::RestoreResult::success();
}

void MagdaSamplerPlugin::restoreRootZone(const sdk::StateNode& state) {
    auto authored = SamplerEngine::zoneFromState(state);
    const auto savedPath = authored.source;

    // The document is the whole authored state, so no source MEANS no sample:
    // restoring a document saved before the sample was chosen has to unload it,
    // or the model says "empty" while playback keeps sounding the old audio.
    if (savedPath.isEmpty()) {
        unloadSample();
        return;
    }

    // Already holding this audio is not a reload. An authored-state edit is
    // projected as a whole document, so without this a loop-switch or root-note
    // write would re-read the file and cut every sounding voice (#2379). A file
    // that CHANGED under the same name is not the audio this holds, so it falls
    // through and is read again.
    if (engine_.holdsAudioFrom(savedPath)) {
        engine_.setZones({authored}, false);
        return;
    }

    // loadSample() re-derives the markers from the file, which is right for a
    // newly chosen sample and wrong here: the document's markers are the
    // authored ones. Take them back afterwards.
    const float savedStart = displayValue(kSampleStart);
    const float savedEnd = displayValue(kSampleEnd);
    const float savedLoopStart = displayValue(kLoopStart);
    const float savedLoopEnd = displayValue(kLoopEnd);

    const juce::File file(savedPath);
    if (file.existsAsFile())
        loadSample(file);

    // The path is still what the project authored even when the file is missing: a
    // relocate away, not a reason to forget which sample this pad holds.
    engine_.setZones({authored}, false);

    const auto maxLen = static_cast<float>(getSampleLengthSeconds());
    const auto restore = [this, maxLen](int index, float saved) {
        if (saved > 0.001f && (maxLen <= 0.0f || saved < maxLen))
            setDisplayValue(index, saved);
    };
    restore(kSampleStart, savedStart);
    restore(kSampleEnd, savedEnd);
    restore(kLoopStart, savedLoopStart);
    restore(kLoopEnd, savedLoopEnd);
}

//==============================================================================
MagdaSamplerPlugin::SampleChoice MagdaSamplerPlugin::readSampleChoice(const juce::File& file) {
    std::unique_ptr<juce::AudioFormatReader> reader(samplerAudioFormats().createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return {};

    const auto seconds =
        static_cast<float>(static_cast<double>(reader->lengthInSamples) / reader->sampleRate);
    const auto named = rootNoteFromMetadata(*reader);
    return {true, named.value_or(60), juce::jmin(seconds, slotInfo(kSampleEnd).maxValue),
            named.has_value()};
}

void MagdaSamplerPlugin::loadSample(const juce::File& file) {
    const auto path = file.getFullPathName();
    const auto choice = readSampleChoice(file);
    if (!choice.valid || engine_.audioFor(path) == nullptr)
        return;

    SamplerZoneSettings zone;
    zone.source = path;
    zone.rootNote = choice.rootNote;
    engine_.setZones({zone}, false);

    // Markers reset to span the new sample. restoreState() puts the authored
    // ones back over the top; a user-chosen sample keeps these.
    const auto maxLen = static_cast<float>(getSampleLengthSeconds());
    setDisplayValue(kSampleStart, 0.0f);
    setDisplayValue(kSampleEnd, juce::jmin(maxLen, slotInfo(kSampleEnd).maxValue));
    setDisplayValue(kLoopStart, 0.0f);
    setDisplayValue(kLoopEnd, juce::jmin(maxLen, slotInfo(kLoopEnd).maxValue));
}

void MagdaSamplerPlugin::unloadSample() {
    engine_.setZones({}, false);
}

void MagdaSamplerPlugin::relocateSample(const juce::File& file) {
    // See the header: the audio is unchanged, only its path moved, so how the
    // user set the sampler up to interpret it has to survive the reload.
    if (engine_.zones().empty())
        return;
    auto kept = engine_.zones().front();
    const float savedStart = displayValue(kSampleStart);
    const float savedEnd = displayValue(kSampleEnd);
    const float savedLoopStart = displayValue(kLoopStart);
    const float savedLoopEnd = displayValue(kLoopEnd);

    loadSample(file);

    // loadSample() only adopts the path once it has a reader for it. If it
    // bailed, it also reset nothing, so there is nothing to put back.
    if (getSampleFile() != file)
        return;

    kept.source = file.getFullPathName();
    engine_.setZones({kept}, false);

    // Same audio means the saved markers still fit, but clamp anyway rather
    // than trust that the file on the other end is byte-identical.
    const auto maxLen = static_cast<float>(getSampleLengthSeconds());
    setDisplayValue(kSampleStart, juce::jmin(savedStart, maxLen));
    setDisplayValue(kSampleEnd, juce::jmin(savedEnd, maxLen));
    setDisplayValue(kLoopStart, juce::jmin(savedLoopStart, maxLen));
    setDisplayValue(kLoopEnd, juce::jmin(savedLoopEnd, maxLen));
}

juce::File MagdaSamplerPlugin::getSampleFile() const {
    const auto& zones = engine_.zones();
    return zones.empty() ? juce::File() : juce::File(zones.front().source);
}

const juce::AudioBuffer<float>* MagdaSamplerPlugin::getWaveform() const {
    const auto& zones = zoneMap().zones;
    if (!zones.empty() && zones.front().hasAudio())
        return &zones.front().audio->buffer;
    return nullptr;
}

double MagdaSamplerPlugin::getSampleLengthSeconds() const {
    const auto& zones = zoneMap().zones;
    return !zones.empty() && zones.front().hasAudio() ? zones.front().audio->lengthSeconds() : 0.0;
}

double MagdaSamplerPlugin::getSampleRate() const {
    const auto& zones = zoneMap().zones;
    return !zones.empty() && zones.front().hasAudio() ? zones.front().audio->sourceRate : 44100.0;
}

int MagdaSamplerPlugin::getRootNote() const {
    const auto& zones = engine_.zones();
    return zones.empty() ? 60 : zones.front().rootNote;
}

void MagdaSamplerPlugin::setRootNote(int note) {
    if (engine_.zones().empty())
        return;
    auto zones = engine_.zones();
    zones.front().rootNote = juce::jlimit(0, 127, note);
    engine_.setZones(std::move(zones), engine_.hasZoneList());
}

}  // namespace magda::daw::audio
