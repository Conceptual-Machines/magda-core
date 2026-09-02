#include "plugins/PolyStepSequencerPlugin.hpp"

#include <algorithm>

#include "plugins/DeviceNoteSink.hpp"

namespace magda::daw::audio {

const char* PolyStepSequencerPlugin::xmlTypeName = "polystepsequencer";

const juce::Identifier PolyStepSequencerPlugin::SettingIDs::numSteps("seqNumSteps");
const juce::Identifier PolyStepSequencerPlugin::SettingIDs::midiThru("seqMidiThru");
const juce::Identifier PolyStepSequencerPlugin::SettingIDs::rampCycles("seqRampCycles");
const juce::Identifier PolyStepSequencerPlugin::SettingIDs::hardAngle("seqHardAngle");
const juce::Identifier PolyStepSequencerPlugin::SettingIDs::quantize("seqQuantize");
const juce::Identifier PolyStepSequencerPlugin::SettingIDs::quantizeSub("seqQuantizeSub");
const juce::Identifier PolyStepSequencerPlugin::SettingIDs::viewMode("seqViewMode");

namespace {

// The pattern's element and property names. Frozen: saved projects carry them,
// and the model writes the same spellings (core/StepPatternState.cpp).
const juce::Identifier kStepTree("STEP");
const juce::Identifier kStepIndex("idx");
const juce::Identifier kStepGate("gate");
const juce::Identifier kStepTie("tie");
const juce::Identifier kStepProbability("prob");
const juce::Identifier kStepVelocity("vel");
const juce::Identifier kNoteTree("NOTE");
const juce::Identifier kNoteNumber("note");
const juce::Identifier kNoteVelocity("vel");

/// One slot's metadata, pinned to what the retired host-native plugin
/// registered: saved links address the slots by index.
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    info.paramIndex = index;

    switch (index) {
        case PolyStepSequencerPlugin::kRate:
            info.stableId = "rate";
            info.name = "Rate";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 9.0f;
            info.defaultValue = 7.0f;  // 1/16
            info.choices = {"1/4D", "1/4",   "1/4T", "1/8D",  "1/8",
                            "1/8T", "1/16D", "1/16", "1/16T", "1/32"};
            break;

        case PolyStepSequencerPlugin::kDirection:
            info.stableId = "direction";
            info.name = "Direction";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 3.0f;
            info.defaultValue = 0.0f;
            info.choices = {"Forward", "Reverse", "Ping-Pong", "Random"};
            break;

        case PolyStepSequencerPlugin::kSwing:
            info.stableId = "swing";
            info.name = "Swing";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case PolyStepSequencerPlugin::kGateLength:
            info.stableId = "gatelength";
            info.name = "Gate";
            info.minValue = 0.05f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.8f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case PolyStepSequencerPlugin::kRamp:
            info.stableId = "ramp";
            info.name = "Timing Depth";
            info.minValue = -1.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.bipolarModulation = true;
            break;

        case PolyStepSequencerPlugin::kSkew:
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

PolyStepSequencerPlugin::PolyStepSequencerPlugin() {
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }
}

PolyStepSequencerPlugin::~PolyStepSequencerPlugin() = default;

ParameterInfo PolyStepSequencerPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= kNumParams)
        return {};
    return slotInfo(index);
}

float PolyStepSequencerPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void PolyStepSequencerPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
}

float PolyStepSequencerPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

int PolyStepSequencerPlugin::displayIndex(int index) const {
    return juce::roundToInt(displayValue(index));
}

// =============================================================================
// Lifecycle
// =============================================================================

void PolyStepSequencerPlugin::prepare(const DevicePrepareContext& context) {
    MidiMagdaDevice::prepare(context);
    sequencer_.setSampleRate(context.sampleRate);
    sequencer_.reset();
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    needsAllNotesOff_ = true;
}

void PolyStepSequencerPlugin::reset() {
    sequencer_.reset();
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    clearMidiOutDisplay();
}

// =============================================================================
// Pattern and state
// =============================================================================

sequencer::PolyPattern PolyStepSequencerPlugin::pattern() const {
    return patternSlots_[static_cast<size_t>(livePattern_.load(std::memory_order_acquire))];
}

void PolyStepSequencerPlugin::flushState(juce::ValueTree& state) {
    state.setProperty(SettingIDs::midiThru, midiThru.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::rampCycles, rampCycles.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::hardAngle, hardAngle.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::quantize, quantize.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::quantizeSub, quantizeSub.load(std::memory_order_relaxed),
                      nullptr);
    state.setProperty(SettingIDs::viewMode, viewMode_, nullptr);

    const auto live = pattern();
    state.setProperty(SettingIDs::numSteps, live.playingLength(), nullptr);

    for (int i = state.getNumChildren() - 1; i >= 0; --i) {
        if (state.getChild(i).hasType(kStepTree))
            state.removeChild(i, nullptr);
    }

    // Only the steps that differ from a default one, which is what the model
    // writes too: absence and a default step read back the same.
    const sequencer::PolyStep defaults;
    for (int i = 0; i < MAX_STEPS; ++i) {
        const auto& step = live.steps[static_cast<size_t>(i)];
        if (step == defaults)
            continue;

        juce::ValueTree node(kStepTree);
        node.setProperty(kStepIndex, i, nullptr);
        node.setProperty(kStepGate, step.gate, nullptr);
        node.setProperty(kStepTie, step.tie, nullptr);
        node.setProperty(kStepProbability, step.probability, nullptr);
        node.setProperty(kStepVelocity, step.velocity, nullptr);

        const int noteCount = std::min(step.noteCount, MAX_NOTES_PER_STEP);
        for (int n = 0; n < noteCount; ++n) {
            const auto& note = step.notes[static_cast<size_t>(n)];
            juce::ValueTree noteNode(kNoteTree);
            noteNode.setProperty(kNoteNumber, note.noteNumber, nullptr);
            if (note.velocity > 0)
                noteNode.setProperty(kNoteVelocity, note.velocity, nullptr);
            node.appendChild(noteNode, nullptr);
        }

        state.appendChild(node, nullptr);
    }
}

void PolyStepSequencerPlugin::restoreState(const juce::ValueTree& state) {
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
    if (const auto* value = state.getPropertyPointer(SettingIDs::viewMode))
        viewMode_ = value->toString();

    sequencer::PolyPattern parsed;
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
        step.gate = child.getProperty(kStepGate, true);
        step.tie = child.getProperty(kStepTie, false);
        step.probability =
            std::clamp(static_cast<float>(child.getProperty(kStepProbability, 1.0f)), 0.0f, 1.0f);
        step.velocity = std::clamp(static_cast<int>(child.getProperty(kStepVelocity, 100)), 1, 127);

        step.noteCount = 0;
        for (int n = 0; n < child.getNumChildren() && step.noteCount < MAX_NOTES_PER_STEP; ++n) {
            const auto noteNode = child.getChild(n);
            if (!noteNode.hasType(kNoteTree))
                continue;

            auto& note = step.notes[static_cast<size_t>(step.noteCount)];
            note.noteNumber =
                std::clamp(static_cast<int>(noteNode.getProperty(kNoteNumber, 60)), 0, 127);
            note.velocity =
                std::clamp(static_cast<int>(noteNode.getProperty(kNoteVelocity, 0)), 0, 127);
            ++step.noteCount;
        }
    }

    // Fill the slot the audio thread is not reading, then hand it over.
    const int next = 1 - livePattern_.load(std::memory_order_relaxed);
    patternSlots_[static_cast<size_t>(next)] = parsed;
    livePattern_.store(next, std::memory_order_release);
}

// =============================================================================
// Step recording
// =============================================================================

void PolyStepSequencerPlugin::setStepRecording(bool enabled) {
    stepRecording_.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        stepRecordPosition_.store(0, std::memory_order_relaxed);
        recordHeldCount_ = 0;
        recordReadIndex_.store(recordWriteIndex_.load(std::memory_order_acquire),
                               std::memory_order_relaxed);
    }
}

bool PolyStepSequencerPlugin::popRecordedStep(RecordedStep& step) {
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

void PolyStepSequencerPlugin::process(DeviceProcessContext& context) {
    if (context.midi == nullptr || context.numSamples <= 0)
        return;

    auto& midi = *context.midi;
    const auto& live =
        patternSlots_[static_cast<size_t>(livePattern_.load(std::memory_order_acquire))];

    // --- Step recording: a chord fills one step, and releasing it advances ---
    if (stepRecording_.load(std::memory_order_relaxed)) {
        const int stepCount = live.playingLength();
        const int incoming = midi.size();
        for (int i = 0; i < incoming; ++i) {
            const auto& message = midi.message(i);
            if (message.isNoteOn()) {
                const int position = stepRecordPosition_.load(std::memory_order_relaxed);
                if (position < stepCount) {
                    const unsigned write = recordWriteIndex_.load(std::memory_order_relaxed);
                    if (write - recordReadIndex_.load(std::memory_order_acquire) <
                        kRecordQueueSize) {
                        recordQueue_[write % kRecordQueueSize] = {position,
                                                                  message.getNoteNumber()};
                        recordWriteIndex_.store(write + 1, std::memory_order_release);
                    }
                }
                ++recordHeldCount_;
            } else if (message.isNoteOff()) {
                if (recordHeldCount_ > 0)
                    --recordHeldCount_;
                if (recordHeldCount_ == 0) {
                    const int position = stepRecordPosition_.load(std::memory_order_relaxed);
                    if (position < stepCount) {
                        const int next = position + 1;
                        stepRecordPosition_.store(next, std::memory_order_relaxed);
                        if (next >= stepCount)
                            stepRecording_.store(false, std::memory_order_relaxed);
                    }
                }
            }
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

    sequencer::PolyStepSequencer::Params params;
    params.rate = static_cast<sequencer::StepClock::Rate>(displayIndex(kRate));
    params.direction = static_cast<sequencer::StepClock::Direction>(displayIndex(kDirection));
    params.swing = displayValue(kSwing);
    params.gateLength = displayValue(kGateLength);
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
