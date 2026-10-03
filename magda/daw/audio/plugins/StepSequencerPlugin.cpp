#include "plugins/StepSequencerPlugin.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>

#include "plugins/DeviceNoteSink.hpp"

namespace magda::daw::audio {

const char* StepSequencerPlugin::xmlTypeName = "stepsequencer";

const juce::Identifier StepSequencerPlugin::SettingIDs::numSteps("seqNumSteps");
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
sdk::ParameterDescriptor slotInfo(int index) {
    sdk::ParameterDescriptor info;
    info.index = index;

    switch (index) {
        case StepSequencerPlugin::kRate:
            info.stableId = "rate";
            info.name = "Rate";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 9.0f;
            info.defaultValue = 7.0f;  // 1/16
            info.choices = sdk::choicesFromLabels(
                {"1/4D", "1/4", "1/4T", "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T", "1/32"});
            break;

        case StepSequencerPlugin::kDirection:
            info.stableId = "direction";
            info.name = "Direction";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 3.0f;
            info.defaultValue = 0.0f;
            info.choices = sdk::choicesFromLabels({"Forward", "Reverse", "Ping-Pong", "Random"});
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
            ParameterUtils::realToNormalized(info.defaultValue, ParameterUtils::domainOf(info));
    }
}

StepSequencerPlugin::~StepSequencerPlugin() = default;

sdk::ParameterDescriptor StepSequencerPlugin::parameterDescriptor(int index) const {
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

sdk::sequencer::MonoPattern StepSequencerPlugin::pattern() const {
    return published_.current();
}

sdk::RestoreResult StepSequencerPlugin::restoreState(const sdk::StateNode& state) {
    // No seqMidiThru here. The toggle it stood for is DeviceInfo::midiInThru,
    // which the model saves and the compiler acts on (#2345), so a project that
    // has one loads with the property simply unread.
    if (state.has(stateKey(SettingIDs::rampCycles)))
        rampCycles.store(state.getInt(stateKey(SettingIDs::rampCycles)), std::memory_order_relaxed);
    if (state.has(stateKey(SettingIDs::hardAngle)))
        hardAngle.store(state.getBool(stateKey(SettingIDs::hardAngle)), std::memory_order_relaxed);
    if (state.has(stateKey(SettingIDs::quantize)))
        quantize.store(static_cast<float>(state.getDouble(stateKey(SettingIDs::quantize))),
                       std::memory_order_relaxed);
    if (state.has(stateKey(SettingIDs::quantizeSub)))
        quantizeSub.store(state.getInt(stateKey(SettingIDs::quantizeSub)),
                          std::memory_order_relaxed);

    sdk::sequencer::MonoPattern parsed;
    if (state.has(stateKey(SettingIDs::numSteps)))
        parsed.length = std::clamp(state.getInt(stateKey(SettingIDs::numSteps)), 1, MAX_STEPS);

    for (const auto& child : state.children()) {
        if (child.type() != stateKey(kStepTree))
            continue;

        const int index = child.getInt(stateKey(kStepIndex), -1);
        if (index < 0 || index >= MAX_STEPS)
            continue;

        auto& step = parsed.steps[static_cast<size_t>(index)];
        step.noteNumber = std::clamp(child.getInt(stateKey(kStepNote), 60), 0, 127);
        step.octaveShift = std::clamp(child.getInt(stateKey(kStepOctave), 0), -2, 2);
        step.gate = child.getBool(stateKey(kStepGate), true);
        step.accent = child.getBool(stateKey(kStepAccent), false);
        step.glide = child.getBool(stateKey(kStepGlide), false);
        step.tie = child.getBool(stateKey(kStepTie), false);
    }

    published_.publish(parsed);
    return sdk::RestoreResult::success();
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
    if (context.midiIn == nullptr || context.midiOut == nullptr || context.numSamples() <= 0)
        return;

    const DeviceMidiInput in(*context.midiIn, sampleRate_);
    // Carries only what this device writes; MIDI thru is the host's merge
    // behind it (#2345, #2347).
    DeviceMidiOutput midi(*context.midiOut, sampleRate_);

    // Take the published pattern for the length of this block: a publish
    // waiting on the message thread cannot touch this slot until the hold goes
    // out of scope, so the reference stays valid for the whole call.
    const sdk::sequencer::PublishedPattern<sdk::sequencer::MonoPattern>::Hold hold{published_};
    if (!hold.isValid()) {
        jassertfalse;  // one audio thread, and it always hands the slot back
        return;
    }
    const auto& live = hold.pattern();

    // --- Step recording: an incoming note fills the next step ---
    if (stepRecording_.load(std::memory_order_relaxed)) {
        const int stepCount = live.playingLength();
        const int incoming = in.size();
        for (int i = 0; i < incoming; ++i) {
            const auto& message = in.message(i);
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

    // Clear anything a re-prepared device left sounding under it.
    if (needsAllNotesOff_) {
        midi.addEvent({juce::MidiMessage::allNotesOff(1), 0});
        needsAllNotesOff_ = false;
    }

    sdk::sequencer::MonoStepSequencer::Params params;
    params.rate = static_cast<sdk::sequencer::StepClock::Rate>(displayIndex(kRate));
    params.direction = static_cast<sdk::sequencer::StepClock::Direction>(displayIndex(kDirection));
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
    const sdk::sequencer::StepClock::BlockTiming timing{
        .startBeat =
            haveTempo ? context.tempoMap->beatsAtSeconds(context.timelineStartSeconds) : 0.0,
        .endBeat = haveTempo ? context.tempoMap->beatsAtSeconds(context.timelineEndSeconds) : 0.0,
        .isPlaying = context.isPlaying && haveTempo,
        .numSamples = context.numSamples()};

    DeviceNoteSink sink{midi};
    sequencer_.processBlock(timing, live, params, sink);

    currentPlayStep_.store(sequencer_.currentStep(), std::memory_order_relaxed);
    if (sequencer_.soundingNote() >= 0)
        setMidiOutDisplay(sequencer_.soundingNote(), sequencer_.soundingVelocity());
    else
        clearMidiOutDisplay();
}

}  // namespace magda::daw::audio
