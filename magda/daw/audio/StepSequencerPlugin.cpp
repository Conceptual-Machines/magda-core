#include "StepSequencerPlugin.hpp"

namespace magda::daw::audio {

const char* StepSequencerPlugin::xmlTypeName = "stepsequencer";

// ValueTree property IDs
namespace SeqIDs {
static const juce::Identifier numSteps("seqNumSteps");
static const juce::Identifier rate("seqRate");
static const juce::Identifier direction("seqDirection");
static const juce::Identifier swing("seqSwing");
static const juce::Identifier glideTime("seqGlideTime");
static const juce::Identifier accentVelocity("seqAccentVel");
static const juce::Identifier normalVelocity("seqNormalVel");

// Per-step child tree
static const juce::Identifier stepTree("STEP");
static const juce::Identifier stepIndex("idx");
static const juce::Identifier stepNote("note");
static const juce::Identifier stepOctave("oct");
static const juce::Identifier stepGate("gate");
static const juce::Identifier stepAccent("accent");
static const juce::Identifier stepGlide("glide");
}  // namespace SeqIDs

StepSequencerPlugin::StepSequencerPlugin(const te::PluginCreationInfo& info)
    : MidiDevicePlugin(info) {
    auto um = getUndoManager();
    numSteps.referTo(state, SeqIDs::numSteps, um, 16);
    rate.referTo(state, SeqIDs::rate, um, static_cast<int>(StepClock::Rate::Sixteenth));
    direction.referTo(state, SeqIDs::direction, um,
                      static_cast<int>(StepClock::Direction::Forward));
    swing.referTo(state, SeqIDs::swing, um, 0.0f);
    glideTime.referTo(state, SeqIDs::glideTime, um, 0.3f);
    accentVelocity.referTo(state, SeqIDs::accentVelocity, um, 120);
    normalVelocity.referTo(state, SeqIDs::normalVelocity, um, 90);

    // Register automatable parameters for macro/mod linking
    rateParam = addParam("rate", "Rate", {0.0f, 9.0f, 1.0f});
    directionParam = addParam("direction", "Direction", {0.0f, 3.0f, 1.0f});
    swingParam = addParam("swing", "Swing", {0.0f, 1.0f});
    glideTimeParam = addParam("glidetime", "Glide Time", {0.0f, 1.0f});
    accentVelParam = addParam("accentvel", "Accent Vel", {1.0f, 127.0f, 1.0f});
    normalVelParam = addParam("normalvel", "Normal Vel", {1.0f, 127.0f, 1.0f});

    // Initialize automatable params from CachedValues
    rateParam->setParameter(static_cast<float>(rate.get()), juce::dontSendNotification);
    directionParam->setParameter(static_cast<float>(direction.get()), juce::dontSendNotification);
    swingParam->setParameter(swing.get(), juce::dontSendNotification);
    glideTimeParam->setParameter(glideTime.get(), juce::dontSendNotification);
    accentVelParam->setParameter(static_cast<float>(accentVelocity.get()),
                                 juce::dontSendNotification);
    normalVelParam->setParameter(static_cast<float>(normalVelocity.get()),
                                 juce::dontSendNotification);

    // Listen for CachedValue changes (from UI) to sync to AutomatableParams
    state.addListener(&paramSyncListener_);

    // Load steps from ValueTree (if restoring from saved state)
    loadStepsFromState();
}

StepSequencerPlugin::~StepSequencerPlugin() {
    state.removeListener(&paramSyncListener_);
}

void StepSequencerPlugin::syncParamFromProperty(const juce::Identifier& property) {
    if (property == SeqIDs::rate && rateParam)
        rateParam->setParameter(static_cast<float>(rate.get()), juce::dontSendNotification);
    else if (property == SeqIDs::direction && directionParam)
        directionParam->setParameter(static_cast<float>(direction.get()),
                                     juce::dontSendNotification);
    else if (property == SeqIDs::swing && swingParam)
        swingParam->setParameter(swing.get(), juce::dontSendNotification);
    else if (property == SeqIDs::glideTime && glideTimeParam)
        glideTimeParam->setParameter(glideTime.get(), juce::dontSendNotification);
    else if (property == SeqIDs::accentVelocity && accentVelParam)
        accentVelParam->setParameter(static_cast<float>(accentVelocity.get()),
                                     juce::dontSendNotification);
    else if (property == SeqIDs::normalVelocity && normalVelParam)
        normalVelParam->setParameter(static_cast<float>(normalVelocity.get()),
                                     juce::dontSendNotification);
}

void StepSequencerPlugin::initialise(const te::PluginInitialisationInfo& info) {
    MidiDevicePlugin::initialise(info);
    stepClock_.setSampleRate(info.sampleRate);
    stepClock_.reset();
}

void StepSequencerPlugin::deinitialise() {
    stepClock_.reset();
    MidiDevicePlugin::deinitialise();
}

void StepSequencerPlugin::reset() {
    stepClock_.reset();
    lastPlayedNote_ = -1;
    lastNoteOffBeat_ = -1.0;
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    clearMidiOutDisplay();
}

void StepSequencerPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    tracktion::copyPropertiesToCachedValues(v, numSteps, rate, direction, swing, glideTime,
                                            accentVelocity, normalVelocity);
    loadStepsFromState();
}

// =============================================================================
// Step accessors (message thread)
// =============================================================================

StepSequencerPlugin::Step StepSequencerPlugin::getStep(int index) const {
    if (index < 0 || index >= MAX_STEPS)
        return {};
    return steps_[static_cast<size_t>(index)];
}

void StepSequencerPlugin::setStepNote(int index, int noteNumber) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    steps_[static_cast<size_t>(index)].noteNumber = juce::jlimit(0, 127, noteNumber);
    saveStepsToState();
}

void StepSequencerPlugin::setStepOctaveShift(int index, int shift) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    steps_[static_cast<size_t>(index)].octaveShift = juce::jlimit(-2, 2, shift);
    saveStepsToState();
}

void StepSequencerPlugin::setStepGate(int index, bool gateOn) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    steps_[static_cast<size_t>(index)].gate = gateOn;
    saveStepsToState();
}

void StepSequencerPlugin::setStepAccent(int index, bool accentOn) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    steps_[static_cast<size_t>(index)].accent = accentOn;
    saveStepsToState();
}

void StepSequencerPlugin::setStepGlide(int index, bool glideOn) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    steps_[static_cast<size_t>(index)].glide = glideOn;
    saveStepsToState();
}

void StepSequencerPlugin::clearStep(int index) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    steps_[static_cast<size_t>(index)] = Step{};
    saveStepsToState();
}

int StepSequencerPlugin::resolveNote(const Step& step) {
    return juce::jlimit(0, 127, step.noteNumber + step.octaveShift * 12);
}

// =============================================================================
// State persistence
// =============================================================================

void StepSequencerPlugin::saveStepsToState() {
    // Remove existing step children
    for (int i = state.getNumChildren() - 1; i >= 0; --i) {
        if (state.getChild(i).hasType(SeqIDs::stepTree))
            state.removeChild(i, nullptr);
    }

    // Write current steps
    int count = juce::jlimit(1, MAX_STEPS, numSteps.get());
    for (int i = 0; i < count; ++i) {
        const auto& s = steps_[static_cast<size_t>(i)];
        juce::ValueTree stepVT(SeqIDs::stepTree);
        stepVT.setProperty(SeqIDs::stepIndex, i, nullptr);
        stepVT.setProperty(SeqIDs::stepNote, s.noteNumber, nullptr);
        stepVT.setProperty(SeqIDs::stepOctave, s.octaveShift, nullptr);
        stepVT.setProperty(SeqIDs::stepGate, s.gate, nullptr);
        stepVT.setProperty(SeqIDs::stepAccent, s.accent, nullptr);
        stepVT.setProperty(SeqIDs::stepGlide, s.glide, nullptr);
        state.appendChild(stepVT, nullptr);
    }
}

void StepSequencerPlugin::loadStepsFromState() {
    // Reset all steps to defaults
    steps_.fill(Step{});

    for (int i = 0; i < state.getNumChildren(); ++i) {
        auto child = state.getChild(i);
        if (!child.hasType(SeqIDs::stepTree))
            continue;

        int idx = child.getProperty(SeqIDs::stepIndex, -1);
        if (idx < 0 || idx >= MAX_STEPS)
            continue;

        auto& s = steps_[static_cast<size_t>(idx)];
        s.noteNumber = child.getProperty(SeqIDs::stepNote, 60);
        s.octaveShift = child.getProperty(SeqIDs::stepOctave, 0);
        s.gate = child.getProperty(SeqIDs::stepGate, true);
        s.accent = child.getProperty(SeqIDs::stepAccent, false);
        s.glide = child.getProperty(SeqIDs::stepGlide, false);
    }
}

// =============================================================================
// Audio thread
// =============================================================================

void StepSequencerPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.bufferForMidiMessages)
        return;

    auto& midi = *fc.bufferForMidiMessages;

    // Step sequencer ignores incoming MIDI — it generates its own pattern
    midi.clear();

    // Read params (includes macro modulation)
    auto currentRate = static_cast<StepClock::Rate>(
        juce::roundToInt(rateParam ? rateParam->getCurrentValue() : rate.get()));
    auto currentDir = static_cast<StepClock::Direction>(
        juce::roundToInt(directionParam ? directionParam->getCurrentValue() : direction.get()));
    float swingVal =
        juce::jlimit(0.0f, 1.0f, swingParam ? swingParam->getCurrentValue() : swing.get());
    [[maybe_unused]] float glideVal = juce::jlimit(
        0.0f, 1.0f, glideTimeParam ? glideTimeParam->getCurrentValue() : glideTime.get());
    int accentVel = juce::jlimit(1, 127,
                                 juce::roundToInt(accentVelParam ? accentVelParam->getCurrentValue()
                                                                 : accentVelocity.get()));
    int normalVel = juce::jlimit(1, 127,
                                 juce::roundToInt(normalVelParam ? normalVelParam->getCurrentValue()
                                                                 : normalVelocity.get()));

    int stepCount = juce::jlimit(1, MAX_STEPS, numSteps.get());
    double stepBeats = StepClock::rateToBeats(currentRate);
    double blockDurationSecs = static_cast<double>(fc.bufferNumSamples) / sampleRate_;

    // Get block beat positions for note-off scheduling
    double blockStartBeat = 0.0;
    double blockEndBeat = 0.0;
    if (fc.isPlaying) {
        auto& tempoSeq = edit.tempoSequence;
        blockStartBeat = tempoSeq.toBeats(fc.editTime.getStart()).inBeats();
        blockEndBeat = tempoSeq.toBeats(fc.editTime.getEnd()).inBeats();
    } else {
        // Approximate — StepClock handles the precise free-running position
        blockEndBeat =
            blockStartBeat + (static_cast<double>(fc.bufferNumSamples) / sampleRate_) *
                                 (edit.tempoSequence.getBpmAt(tracktion::TimePosition()) / 60.0);
    }

    // --- Emit pending note-off from previous block ---
    if (lastNoteOffBeat_ >= blockStartBeat && lastNoteOffBeat_ < blockEndBeat &&
        lastPlayedNote_ >= 0) {
        double frac = (blockEndBeat > blockStartBeat)
                          ? (lastNoteOffBeat_ - blockStartBeat) / (blockEndBeat - blockStartBeat)
                          : 0.0;
        double timeInBlock = frac * blockDurationSecs;
        midi.addMidiMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), timeInBlock,
                            te::MPESourceID{});
        lastPlayedNote_ = -1;
        lastNoteOffBeat_ = -1.0;
        clearMidiOutDisplay();
    }

    // --- Get step events from the clock ---
    static constexpr int MAX_EVENTS_PER_BLOCK = 16;
    StepClock::StepEvent events[MAX_EVENTS_PER_BLOCK];
    int eventCount = stepClock_.processBlock(fc, edit, currentRate, currentDir, swingVal, stepCount,
                                             events, MAX_EVENTS_PER_BLOCK);

    // --- Process each step event ---
    for (int i = 0; i < eventCount; ++i) {
        const auto& evt = events[i];
        const auto& step = steps_[static_cast<size_t>(evt.stepIndex)];

        currentPlayStep_.store(evt.stepIndex, std::memory_order_relaxed);

        // Rest step — just send note-off if needed
        if (!step.gate) {
            if (lastPlayedNote_ >= 0) {
                midi.addMidiMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), evt.timeInBlock,
                                    te::MPESourceID{});
                lastPlayedNote_ = -1;
                lastNoteOffBeat_ = -1.0;
                clearMidiOutDisplay();
            }
            continue;
        }

        int noteNum = resolveNote(step);
        int vel = step.accent ? accentVel : normalVel;

        // Glide: if previous step had glide, don't send note-off before the new note-on.
        // Instead, briefly overlap (legato). The synth handles portamento.
        // Also send pitch bend for glide if transitioning between different pitches.
        bool isGlideFromPrevious = (lastPlayedNote_ >= 0 && lastPlayedNote_ != noteNum);

        // Note-off for previous note (unless gliding)
        if (lastPlayedNote_ >= 0) {
            // Check if the previous step had glide enabled
            // For glide, we send note-off slightly after the new note-on (legato overlap)
            if (!isGlideFromPrevious) {
                midi.addMidiMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), evt.timeInBlock,
                                    te::MPESourceID{});
            } else {
                // Glide: send note-off just after note-on (1ms later in the block)
                double offTime = std::min(evt.timeInBlock + 0.001, blockDurationSecs);
                midi.addMidiMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), offTime,
                                    te::MPESourceID{});
            }
        }

        // Note-on
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, noteNum, static_cast<juce::uint8>(vel)),
                            evt.timeInBlock, te::MPESourceID{});

        lastPlayedNote_ = noteNum;
        setMidiOutDisplay(noteNum, vel);

        // Schedule note-off: if this step has glide, hold until next step.
        // Otherwise, use a standard gate length (e.g., 80% of step duration).
        if (step.glide) {
            // Note-off will be handled by the next step event
            lastNoteOffBeat_ = -1.0;
        } else {
            double gateLength = 0.8;  // 80% gate
            double noteOffBeat = evt.beatPosition + stepBeats * gateLength;
            if (noteOffBeat < blockEndBeat) {
                double frac = (blockEndBeat > blockStartBeat)
                                  ? (noteOffBeat - blockStartBeat) / (blockEndBeat - blockStartBeat)
                                  : 0.0;
                double offTimeInBlock = frac * blockDurationSecs;
                midi.addMidiMessage(juce::MidiMessage::noteOff(1, noteNum), offTimeInBlock,
                                    te::MPESourceID{});
                lastPlayedNote_ = -1;
                lastNoteOffBeat_ = -1.0;
                clearMidiOutDisplay();
            } else {
                // Note-off in a future block
                lastNoteOffBeat_ = noteOffBeat;
            }
        }
    }

    // Update UI play position
    if (eventCount == 0 && !stepClock_.isRunning()) {
        currentPlayStep_.store(-1, std::memory_order_relaxed);
    }
}

}  // namespace magda::daw::audio
