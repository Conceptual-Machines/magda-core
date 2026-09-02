#include "plugins/StepSequencerPlugin.hpp"

#include <algorithm>

#include "plugins/DeviceNoteSink.hpp"

namespace magda::daw::audio {

const char* StepSequencerPlugin::xmlTypeName = "stepsequencer";

const juce::Identifier StepSequencerPlugin::SettingIDs::numSteps("seqNumSteps");
const juce::Identifier StepSequencerPlugin::SettingIDs::midiThru("seqMidiThru");
const juce::Identifier StepSequencerPlugin::SettingIDs::rampCycles("seqRampCycles");
const juce::Identifier StepSequencerPlugin::SettingIDs::hardAngle("seqHardAngle");
const juce::Identifier StepSequencerPlugin::SettingIDs::quantize("seqQuantize");
const juce::Identifier StepSequencerPlugin::SettingIDs::quantizeSub("seqQuantizeSub");

namespace {

// The pattern's element and property names. Frozen: saved projects carry them,
// and the model writes the same spellings (core/StepPatternState.cpp).
const juce::Identifier kStepTree("STEP");
const juce::Identifier kStepIndex("idx");
const juce::Identifier kStepNote("note");
const juce::Identifier kStepOctave("oct");
const juce::Identifier kStepGate("gate");
const juce::Identifier kStepAccent("accent");
const juce::Identifier kStepGlide("glide");
const juce::Identifier kStepTie("tie");

/// One slot's metadata. The ids, order and display ranges are pinned to what
/// the retired host-native plugin registered, because saved links address the
/// slots by index and projects store parameter values in display units.
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    info.paramIndex = index;

    switch (index) {
        case StepSequencerPlugin::kRate:
            info.stableId = "rate";
            info.name = "Rate";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 9.0f;
            info.defaultValue = 7.0f;  // 1/16
            info.choices = {"1/4D", "1/4",   "1/4T", "1/8D",  "1/8",
                            "1/8T", "1/16D", "1/16", "1/16T", "1/32"};
            break;

        case StepSequencerPlugin::kDirection:
            info.stableId = "direction";
            info.name = "Direction";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 3.0f;
            info.defaultValue = 0.0f;
            info.choices = {"Forward", "Reverse", "Ping-Pong", "Random"};
            break;

        case StepSequencerPlugin::kSwing:
            info.stableId = "swing";
            info.name = "Swing";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case StepSequencerPlugin::kGateLength:
            info.stableId = "gatelength";
            info.name = "Gate";
            info.minValue = 0.05f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.8f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case StepSequencerPlugin::kAccentVelocity:
            info.stableId = "accentvel";
            info.name = "Accent Vel";
            info.minValue = 1.0f;
            info.maxValue = 127.0f;
            info.defaultValue = 120.0f;
            break;

        case StepSequencerPlugin::kNormalVelocity:
            info.stableId = "normalvel";
            info.name = "Normal Vel";
            info.minValue = 1.0f;
            info.maxValue = 127.0f;
            info.defaultValue = 90.0f;
            break;

        case StepSequencerPlugin::kRamp:
            info.stableId = "ramp";
            info.name = "Timing Depth";
            info.minValue = -1.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.bipolarModulation = true;
            break;

        case StepSequencerPlugin::kSkew:
            info.stableId = "skew";
            info.name = "Timing Skew";
            info.minValue = -1.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.bipolarModulation = true;
            break;

        default:
            break;
    }

    return info;
}

}  // namespace

StepSequencerPlugin::StepSequencerPlugin() {
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }
}

StepSequencerPlugin::~StepSequencerPlugin() = default;

ParameterInfo StepSequencerPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= kNumParams)
        return {};
    return slotInfo(index);
}

float StepSequencerPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void StepSequencerPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
}

float StepSequencerPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

int StepSequencerPlugin::displayIndex(int index) const {
    return juce::roundToInt(displayValue(index));
}

// =============================================================================
// Lifecycle
// =============================================================================

void StepSequencerPlugin::prepare(const DevicePrepareContext& context) {
    MidiMagdaDevice::prepare(context);
    sequencer_.setSampleRate(context.sampleRate);
    sequencer_.reset();
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    needsAllNotesOff_ = true;
}

void StepSequencerPlugin::reset() {
    sequencer_.reset();
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    clearMidiOutDisplay();
}

// =============================================================================
// Pattern and state
// =============================================================================

sequencer::MonoPattern StepSequencerPlugin::pattern() const {
    return patternSlots_[static_cast<size_t>(livePattern_.load(std::memory_order_acquire))];
}

void StepSequencerPlugin::flushState(juce::ValueTree& state) {
    state.setProperty(SettingIDs::midiThru, midiThru.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::rampCycles, rampCycles.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::hardAngle, hardAngle.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::quantize, quantize.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::quantizeSub, quantizeSub.load(std::memory_order_relaxed),
                      nullptr);

    const auto live = pattern();
    state.setProperty(SettingIDs::numSteps, live.playingLength(), nullptr);

    for (int i = state.getNumChildren() - 1; i >= 0; --i) {
        if (state.getChild(i).hasType(kStepTree))
            state.removeChild(i, nullptr);
    }

    // Only the steps that differ from a default one, which is what the model
    // writes too: absence and a default step read back the same.
    const sequencer::MonoStep defaults;
    for (int i = 0; i < MAX_STEPS; ++i) {
        const auto& step = live.steps[static_cast<size_t>(i)];
        if (step == defaults)
            continue;

        juce::ValueTree node(kStepTree);
        node.setProperty(kStepIndex, i, nullptr);
        node.setProperty(kStepNote, step.noteNumber, nullptr);
        node.setProperty(kStepOctave, step.octaveShift, nullptr);
        node.setProperty(kStepGate, step.gate, nullptr);
        node.setProperty(kStepAccent, step.accent, nullptr);
        node.setProperty(kStepGlide, step.glide, nullptr);
        node.setProperty(kStepTie, step.tie, nullptr);
        state.appendChild(node, nullptr);
    }
}

void StepSequencerPlugin::restoreState(const juce::ValueTree& state) {
    if (const auto* value = state.getPropertyPointer(SettingIDs::midiThru))
        midiThru.store(static_cast<bool>(*value), std::memory_order_relaxed);
    if (const auto* value = state.getPropertyPointer(SettingIDs::rampCycles))
        rampCycles.store(static_cast<int>(*value), std::memory_order_relaxed);
    if (const auto* value = state.getPropertyPointer(SettingIDs::hardAngle))
        hardAngle.store(static_cast<bool>(*value), std::memory_order_relaxed);
    if (const auto* value = state.getPropertyPointer(SettingIDs::quantize))
        quantize.store(static_cast<float>(*value), std::memory_order_relaxed);
    if (const auto* value = state.getPropertyPointer(SettingIDs::quantizeSub))
        quantizeSub.store(static_cast<int>(*value), std::memory_order_relaxed);

    sequencer::MonoPattern parsed;
    if (const auto* value = state.getPropertyPointer(SettingIDs::numSteps))
        parsed.length = std::clamp(static_cast<int>(*value), 1, MAX_STEPS);

    for (int i = 0; i < state.getNumChildren(); ++i) {
        const auto child = state.getChild(i);
        if (!child.hasType(kStepTree))
            continue;

        const int index = child.getProperty(kStepIndex, -1);
        if (index < 0 || index >= MAX_STEPS)
            continue;

        auto& step = parsed.steps[static_cast<size_t>(index)];
        step.noteNumber = std::clamp(static_cast<int>(child.getProperty(kStepNote, 60)), 0, 127);
        step.octaveShift = std::clamp(static_cast<int>(child.getProperty(kStepOctave, 0)), -2, 2);
        step.gate = child.getProperty(kStepGate, true);
        step.accent = child.getProperty(kStepAccent, false);
        step.glide = child.getProperty(kStepGlide, false);
        step.tie = child.getProperty(kStepTie, false);
    }

    // Fill the slot the audio thread is not reading, then hand it over.
    const int next = 1 - livePattern_.load(std::memory_order_relaxed);
    patternSlots_[static_cast<size_t>(next)] = parsed;
    livePattern_.store(next, std::memory_order_release);
}

// =============================================================================
// Step recording
// =============================================================================

void StepSequencerPlugin::setStepRecording(bool enabled) {
    stepRecording_.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        stepRecordPosition_.store(0, std::memory_order_relaxed);
        recordReadIndex_.store(recordWriteIndex_.load(std::memory_order_acquire),
                               std::memory_order_relaxed);
    }
}

bool StepSequencerPlugin::popRecordedStep(RecordedStep& step) {
    const unsigned read = recordReadIndex_.load(std::memory_order_relaxed);
    if (read == recordWriteIndex_.load(std::memory_order_acquire))
        return false;

    step = recordQueue_[read % kRecordQueueSize];
    recordReadIndex_.store(read + 1, std::memory_order_release);
    return true;
}

// =============================================================================
// Audio thread
// =============================================================================

void StepSequencerPlugin::process(DeviceProcessContext& context) {
    if (context.midi == nullptr || context.numSamples <= 0)
        return;

    auto& midi = *context.midi;
    const auto& live =
        patternSlots_[static_cast<size_t>(livePattern_.load(std::memory_order_acquire))];

    // --- Step recording: an incoming note fills the next step ---
    if (stepRecording_.load(std::memory_order_relaxed)) {
        const int stepCount = live.playingLength();
        const int incoming = midi.size();
        for (int i = 0; i < incoming; ++i) {
            const auto& message = midi.message(i);
            if (!message.isNoteOn())
                continue;

            const int position = stepRecordPosition_.load(std::memory_order_relaxed);
            if (position >= stepCount)
                continue;

            // The device cannot write the model, so it hands the note to the
            // faceplate, which commits it as an undoable pattern edit.
            const unsigned write = recordWriteIndex_.load(std::memory_order_relaxed);
            if (write - recordReadIndex_.load(std::memory_order_acquire) < kRecordQueueSize) {
                recordQueue_[write % kRecordQueueSize] = {position, message.getNoteNumber()};
                recordWriteIndex_.store(write + 1, std::memory_order_release);
            }

            const int next = position + 1;
            stepRecordPosition_.store(next, std::memory_order_relaxed);
            if (next >= stepCount)
                stepRecording_.store(false, std::memory_order_relaxed);
        }
    }

    // --- Hold incoming MIDI aside, so the sequencer owns the buffer ---
    int thruCount = 0;
    if (midiThru.load(std::memory_order_relaxed)) {
        thruCount = std::min(midi.size(), kMaxThruMessages);
        for (int i = 0; i < thruCount; ++i) {
            thruMessages_[static_cast<size_t>(i)] = midi.message(i);
            thruSources_[static_cast<size_t>(i)] = midi.sourceId(i);
        }
    }
    midi.clear();

    // Clear anything a re-prepared device left sounding under it.
    if (needsAllNotesOff_) {
        midi.addEvent({juce::MidiMessage::allNotesOff(1), 0});
        needsAllNotesOff_ = false;
    }

    sequencer::MonoStepSequencer::Params params;
    params.rate = static_cast<sequencer::StepClock::Rate>(displayIndex(kRate));
    params.direction = static_cast<sequencer::StepClock::Direction>(displayIndex(kDirection));
    params.swing = displayValue(kSwing);
    params.gateLength = displayValue(kGateLength);
    params.accentVelocity = displayIndex(kAccentVelocity);
    params.normalVelocity = displayIndex(kNormalVelocity);
    params.ramp = displayValue(kRamp);
    params.skew = displayValue(kSkew);
    params.rampCycles = rampCycles.load(std::memory_order_relaxed);
    params.hardAngle = hardAngle.load(std::memory_order_relaxed);
    params.quantize = quantize.load(std::memory_order_relaxed);
    params.quantizeSub = quantizeSub.load(std::memory_order_relaxed);

    const bool haveTempo = context.tempoMap != nullptr;
    const sequencer::StepClock::BlockTiming timing{
        .startBeat =
            haveTempo ? context.tempoMap->beatsAtSeconds(context.timelineStartSeconds) : 0.0,
        .endBeat = haveTempo ? context.tempoMap->beatsAtSeconds(context.timelineEndSeconds) : 0.0,
        .isPlaying = context.isPlaying && haveTempo,
        .numSamples = context.numSamples};

    DeviceNoteSink sink{midi};
    sequencer_.processBlock(timing, live, params, sink);

    currentPlayStep_.store(sequencer_.currentStep(), std::memory_order_relaxed);
    if (sequencer_.soundingNote() >= 0)
        setMidiOutDisplay(sequencer_.soundingNote(), sequencer_.soundingVelocity());
    else
        clearMidiOutDisplay();

    // Incoming MIDI still reaches the instrument downstream.
    for (int i = 0; i < thruCount; ++i) {
        midi.addEvent(
            {thruMessages_[static_cast<size_t>(i)], thruSources_[static_cast<size_t>(i)]});
    }
}

}  // namespace magda::daw::audio
