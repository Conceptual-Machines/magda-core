#include "StepSequencerPlugin.hpp"

namespace magda::daw::audio {

const char* StepSequencerPlugin::xmlTypeName = "stepsequencer";

// ValueTree property IDs
namespace SeqIDs {
static const juce::Identifier numSteps("seqNumSteps");
static const juce::Identifier rate("seqRate");
static const juce::Identifier direction("seqDirection");
static const juce::Identifier swing("seqSwing");
static const juce::Identifier gateLength("seqGateLength");
static const juce::Identifier accentVelocity("seqAccentVel");
static const juce::Identifier normalVelocity("seqNormalVel");
static const juce::Identifier ramp("seqRamp");
static const juce::Identifier skew("seqSkew");

// Per-step child tree
static const juce::Identifier stepTree("STEP");
static const juce::Identifier stepIndex("idx");
static const juce::Identifier stepNote("note");
static const juce::Identifier stepOctave("oct");
static const juce::Identifier stepGate("gate");
static const juce::Identifier stepAccent("accent");
static const juce::Identifier stepGlide("glide");
static const juce::Identifier stepTie("tie");
static const juce::Identifier midiThru("seqMidiThru");
static const juce::Identifier rampCycles("seqRampCycles");
static const juce::Identifier hardAngle("seqHardAngle");
static const juce::Identifier quantize("seqQuantize");
static const juce::Identifier quantizeSub("seqQuantizeSub");
}  // namespace SeqIDs

StepSequencerPlugin::StepSequencerPlugin(const te::PluginCreationInfo& info)
    : MidiDevicePlugin(info) {
    auto um = getUndoManager();
    numSteps.referTo(state, SeqIDs::numSteps, um, 16);
    rate.referTo(state, SeqIDs::rate, um, static_cast<int>(StepClock::Rate::Sixteenth));
    direction.referTo(state, SeqIDs::direction, um,
                      static_cast<int>(StepClock::Direction::Forward));
    swing.referTo(state, SeqIDs::swing, um, 0.0f);
    gateLength.referTo(state, SeqIDs::gateLength, um, 0.8f);
    accentVelocity.referTo(state, SeqIDs::accentVelocity, um, 120);
    normalVelocity.referTo(state, SeqIDs::normalVelocity, um, 90);
    ramp.referTo(state, SeqIDs::ramp, um, 0.0f);
    skew.referTo(state, SeqIDs::skew, um, 0.0f);
    midiThru.referTo(state, SeqIDs::midiThru, um, true);
    rampCycles.referTo(state, SeqIDs::rampCycles, um, 1);
    hardAngle.referTo(state, SeqIDs::hardAngle, um, false);
    quantize.referTo(state, SeqIDs::quantize, um, 0.0f);
    quantizeSub.referTo(state, SeqIDs::quantizeSub, um, 16);

    // Register automatable parameters for macro/mod linking
    rateParam = addParam("rate", "Rate", {0.0f, 9.0f, 1.0f});
    directionParam = addParam("direction", "Direction", {0.0f, 3.0f, 1.0f});
    swingParam = addParam("swing", "Swing", {0.0f, 1.0f});
    gateLengthParam = addParam("gatelength", "Gate", {0.05f, 1.0f});
    accentVelParam = addParam("accentvel", "Accent Vel", {1.0f, 127.0f, 1.0f});
    normalVelParam = addParam("normalvel", "Normal Vel", {1.0f, 127.0f, 1.0f});
    rampParam = addParam("ramp", "Timing Depth", {-1.0f, 1.0f});
    skewParam = addParam("skew", "Timing Skew", {-1.0f, 1.0f});

    // Initialize automatable params from CachedValues
    rateParam->setParameter(static_cast<float>(rate.get()), juce::dontSendNotification);
    directionParam->setParameter(static_cast<float>(direction.get()), juce::dontSendNotification);
    swingParam->setParameter(swing.get(), juce::dontSendNotification);
    gateLengthParam->setParameter(gateLength.get(), juce::dontSendNotification);
    accentVelParam->setParameter(static_cast<float>(accentVelocity.get()),
                                 juce::dontSendNotification);
    normalVelParam->setParameter(static_cast<float>(normalVelocity.get()),
                                 juce::dontSendNotification);
    rampParam->setParameter(ramp.get(), juce::dontSendNotification);
    skewParam->setParameter(skew.get(), juce::dontSendNotification);

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
    else if (property == SeqIDs::gateLength && gateLengthParam)
        gateLengthParam->setParameter(gateLength.get(), juce::dontSendNotification);
    else if (property == SeqIDs::accentVelocity && accentVelParam)
        accentVelParam->setParameter(static_cast<float>(accentVelocity.get()),
                                     juce::dontSendNotification);
    else if (property == SeqIDs::normalVelocity && normalVelParam)
        normalVelParam->setParameter(static_cast<float>(normalVelocity.get()),
                                     juce::dontSendNotification);
    else if (property == SeqIDs::ramp && rampParam)
        rampParam->setParameter(ramp.get(), juce::dontSendNotification);
    else if (property == SeqIDs::skew && skewParam)
        skewParam->setParameter(skew.get(), juce::dontSendNotification);
}

void StepSequencerPlugin::initialise(const te::PluginInitialisationInfo& info) {
    MidiDevicePlugin::initialise(info);
    sampleRate_ = info.sampleRate;
    stepClock_.setSampleRate(info.sampleRate);
    stepClock_.reset();
    lastPlayedNote_ = -1;
    noteOffCountdown_ = 0;
    silentBlockCount_ = 0;
    needsAllNotesOff_ = true;
}

void StepSequencerPlugin::deinitialise() {
    stepClock_.reset();
    lastPlayedNote_ = -1;
    noteOffCountdown_ = 0;
    silentBlockCount_ = 0;
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    MidiDevicePlugin::deinitialise();
}

void StepSequencerPlugin::reset() {
    stepClock_.reset();
    lastPlayedNote_ = -1;
    noteOffCountdown_ = 0;
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    clearMidiOutDisplay();
}

void StepSequencerPlugin::flushPluginStateToValueTree() {
    Plugin::flushPluginStateToValueTree();
    saveStepsToState();
}

void StepSequencerPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    tracktion::copyPropertiesToCachedValues(v, numSteps, rate, direction, swing, gateLength,
                                            accentVelocity, normalVelocity, ramp, skew, hardAngle,
                                            quantize, quantizeSub);

    // Copy step children from the incoming tree into our state
    // (copyPropertiesToCachedValues only copies properties, not children)
    for (int i = state.getNumChildren() - 1; i >= 0; --i) {
        if (state.getChild(i).hasType(SeqIDs::stepTree))
            state.removeChild(i, nullptr);
    }
    for (int i = 0; i < v.getNumChildren(); ++i) {
        auto child = v.getChild(i);
        if (child.hasType(SeqIDs::stepTree))
            state.appendChild(child.createCopy(), nullptr);
    }

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

void StepSequencerPlugin::setStepTie(int index, bool tieOn) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    steps_[static_cast<size_t>(index)].tie = tieOn;
    saveStepsToState();
}

void StepSequencerPlugin::clearStep(int index) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    steps_[static_cast<size_t>(index)] = Step{};
    saveStepsToState();
}

void StepSequencerPlugin::randomizePattern() {
    juce::Random rng;
    int stepCount = juce::jlimit(1, MAX_STEPS, numSteps.get());

    for (int i = 0; i < stepCount; ++i) {
        auto& step = steps_[static_cast<size_t>(i)];
        step.noteNumber = 36 + rng.nextInt(24);  // C2 to B3 (two octaves)
        step.octaveShift = 0;
        step.gate = rng.nextFloat() < 0.7f;
        step.accent = rng.nextFloat() < 0.2f;
        step.glide = rng.nextFloat() < 0.15f;
        step.tie = false;
    }
    saveStepsToState();
}

void StepSequencerPlugin::setPattern(const std::vector<Step>& steps, bool cueOnBar) {
    int count = std::min(static_cast<int>(steps.size()), MAX_STEPS);

    if (cueOnBar && stepClock_.isRunning()) {
        // Queue pattern — audio thread swaps it in at the next cycle boundary
        for (int i = 0; i < count; ++i)
            pendingSteps_[static_cast<size_t>(i)] = steps[static_cast<size_t>(i)];
        pendingNumSteps_ = count;
    } else {
        // Apply immediately
        for (int i = 0; i < count; ++i)
            steps_[static_cast<size_t>(i)] = steps[static_cast<size_t>(i)];
        if (count != numSteps.get())
            numSteps = count;
        saveStepsToState();
    }
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
        stepVT.setProperty(SeqIDs::stepTie, s.tie, nullptr);
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
        s.tie = child.getProperty(SeqIDs::stepTie, false);
    }
}

void StepSequencerPlugin::setStepRecording(bool enabled) {
    stepRecording_.store(enabled, std::memory_order_relaxed);
    if (enabled)
        stepRecordPosition_.store(0, std::memory_order_relaxed);
}

// =============================================================================
// Audio thread
// =============================================================================

void StepSequencerPlugin::killNote(te::MidiMessageArray& midi, double time) {
    if (lastPlayedNote_ >= 0) {
        midi.addMidiMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), time,
                            te::MPESourceID{});
        lastPlayedNote_ = -1;
        noteOffCountdown_ = 0;
        clearMidiOutDisplay();
    }
}

void StepSequencerPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.bufferForMidiMessages)
        return;

    if (!isEnabled())
        return;

    auto& midi = *fc.bufferForMidiMessages;

    // Send all-notes-off after re-initialisation to clear any stuck notes
    if (needsAllNotesOff_) {
        midi.addMidiMessage(juce::MidiMessage::allNotesOff(1), 0.0, te::MPESourceID{});
        needsAllNotesOff_ = false;
    }

    // --- Step recording: incoming note-on → record to current step, advance ---
    if (stepRecording_.load(std::memory_order_relaxed)) {
        int maxSteps = juce::jlimit(1, MAX_STEPS, numSteps.get());
        for (auto& msg : midi) {
            if (msg.isNoteOn()) {
                int pos = stepRecordPosition_.load(std::memory_order_relaxed);
                if (pos < maxSteps) {
                    int note = msg.getNoteNumber();
                    int capturedPos = pos;
                    juce::MessageManager::callAsync([this, capturedPos, note] {
                        setStepNote(capturedPos, note);
                        setStepGate(capturedPos, true);
                    });
                    stepRecordPosition_.store(pos + 1, std::memory_order_relaxed);
                }
            }
        }
    }

    // Save incoming MIDI for thru, then clear for sequencer output
    te::MidiMessageArray thruMessages;
    if (midiThru.get()) {
        for (auto& msg : midi)
            thruMessages.addMidiMessage(msg, msg.getTimeStamp(), te::MPESourceID{});
    }
    midi.clear();

    // Only run when transport is playing
    if (!fc.isPlaying) {
        killNote(midi, 0.0);
        stepClock_.reset();
        currentPlayStep_.store(-1, std::memory_order_relaxed);
        silentBlockCount_ = 0;
        return;
    }

    // Read params (includes macro modulation)
    auto currentRate = static_cast<StepClock::Rate>(
        juce::roundToInt(rateParam ? rateParam->getCurrentValue() : rate.get()));
    auto currentDir = static_cast<StepClock::Direction>(
        juce::roundToInt(directionParam ? directionParam->getCurrentValue() : direction.get()));
    float swingVal =
        juce::jlimit(0.0f, 1.0f, swingParam ? swingParam->getCurrentValue() : swing.get());
    float gateLengthVal = juce::jlimit(
        0.05f, 1.0f, gateLengthParam ? gateLengthParam->getCurrentValue() : gateLength.get());
    int accentVel = juce::jlimit(1, 127,
                                 juce::roundToInt(accentVelParam ? accentVelParam->getCurrentValue()
                                                                 : accentVelocity.get()));
    int normalVel = juce::jlimit(1, 127,
                                 juce::roundToInt(normalVelParam ? normalVelParam->getCurrentValue()
                                                                 : normalVelocity.get()));

    float rampVal =
        juce::jlimit(-1.0f, 1.0f, rampParam ? rampParam->getCurrentValue() : ramp.get());
    float skewVal =
        juce::jlimit(-1.0f, 1.0f, skewParam ? skewParam->getCurrentValue() : skew.get());

    int stepCount = juce::jlimit(1, MAX_STEPS, numSteps.get());
    int bufferSamples = fc.bufferNumSamples;
    double blockDurationSecs = static_cast<double>(bufferSamples) / sampleRate_;

    // Compute step duration in samples for gate length
    double bpm = edit.tempoSequence.getBpmAt(fc.editTime.getStart());
    double stepBeats = StepClock::rateToBeats(currentRate);
    int stepDurationSamples = std::max(1, static_cast<int>(stepBeats * 60.0 / bpm * sampleRate_));

    int cyclesVal = juce::jlimit(1, 8, rampCycles.get());
    bool hardAngleVal = hardAngle.get();
    float quantizeVal = juce::jlimit(0.0f, 1.0f, quantize.get());
    int quantizeSubVal = juce::jlimit(16, 512, quantizeSub.get());

    // --- Detect structural parameter changes → reset clock to re-sync ---
    // Only reset for changes that alter the step grid timing structure.
    // numSteps is NOT included: the clock wraps cycleStep_ % numSteps naturally,
    // so pattern length changes (e.g. from streaming AI results) don't disrupt timing.
    // Ramp depth/skew are NOT included: warpedStepDuration() recalculates
    // each block, so the timing adapts smoothly without retriggering.
    int rateInt = static_cast<int>(currentRate);
    if (rateInt != prevRate_ || cyclesVal != prevCycles_ || hardAngleVal != prevHardAngle_) {
        killNote(midi, 0.0);
        stepClock_.reset();
        prevRate_ = rateInt;
        prevCycles_ = cyclesVal;
        prevHardAngle_ = hardAngleVal;
    }

    // --- Swap pending pattern at cycle boundary ---
    // The clock's cycleStep wraps to 0 at the start of each cycle.
    // Swap before processBlock so the new step count is used this block.
    if (pendingNumSteps_ > 0 && stepClock_.getCycleStep() == 0) {
        int pCount = pendingNumSteps_;
        for (int i = 0; i < pCount; ++i)
            steps_[static_cast<size_t>(i)] = pendingSteps_[static_cast<size_t>(i)];
        pendingNumSteps_ = 0;
        if (pCount != numSteps.get())
            numSteps = pCount;
        stepCount = juce::jlimit(1, MAX_STEPS, pCount);
        // Persist to ValueTree (on message thread to avoid blocking audio)
        juce::MessageManager::callAsync([this] { saveStepsToState(); });
    }

    // --- Get step events from the clock ---
    static constexpr int MAX_EVENTS_PER_BLOCK = 16;
    StepClock::StepEvent events[MAX_EVENTS_PER_BLOCK];
    int eventCount = stepClock_.processBlock(fc, edit, currentRate, currentDir, swingVal, stepCount,
                                             events, MAX_EVENTS_PER_BLOCK, rampVal, skewVal,
                                             cyclesVal, hardAngleVal, quantizeVal, quantizeSubVal);

    // --- Emit pending note-off (sample countdown) ---
    // Only emit if the countdown fires BEFORE the first step event in this block.
    // If a step fires first, it handles the note-off transition itself.
    if (noteOffCountdown_ > 0 && lastPlayedNote_ >= 0) {
        if (noteOffCountdown_ <= bufferSamples) {
            double countdownTime = static_cast<double>(noteOffCountdown_) / sampleRate_;
            bool stepFiresFirst = (eventCount > 0 && events[0].timeInBlock <= countdownTime);

            // If the next step is a tie, never kill the note — let the tie hold it
            bool nextStepIsTie =
                (eventCount > 0 && steps_[static_cast<size_t>(events[0].stepIndex)].tie &&
                 steps_[static_cast<size_t>(events[0].stepIndex)].gate);

            if (!stepFiresFirst && !nextStepIsTie) {
                killNote(midi, countdownTime);
            } else {
                noteOffCountdown_ = 0;
            }
        } else {
            if (eventCount > 0) {
                noteOffCountdown_ = 0;
            } else {
                noteOffCountdown_ -= bufferSamples;
            }
        }
    }

    // --- Process each step event ---
    for (int i = 0; i < eventCount; ++i) {
        const auto& evt = events[i];
        const auto& step = steps_[static_cast<size_t>(evt.stepIndex)];

        currentPlayStep_.store(evt.stepIndex, std::memory_order_relaxed);

        // Rest step — send note-off if needed
        if (!step.gate) {
            killNote(midi, evt.timeInBlock);
            continue;
        }

        // Tie step — just keep the current note playing, no retrigger, no new countdown
        if (step.tie && lastPlayedNote_ >= 0) {
            noteOffCountdown_ = 0;  // Cancel any pending note-off — note holds
            continue;
        }

        int noteNum = resolveNote(step);
        int vel = step.accent ? accentVel : normalVel;

        // Note-off for previous note — always BEFORE note-on
        if (lastPlayedNote_ >= 0) {
            double offTime = std::max(0.0, evt.timeInBlock - 0.0001);
            midi.addMidiMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), offTime,
                                te::MPESourceID{});
            noteOffCountdown_ = 0;
        }

        // Note-on
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, noteNum, static_cast<juce::uint8>(vel)),
                            evt.timeInBlock, te::MPESourceID{});

        lastPlayedNote_ = noteNum;
        setMidiOutDisplay(noteNum, vel);

        // Schedule note-off via sample countdown
        // 100% gate if: glide, or next step is a tie (must hold for the tie to extend)
        // Otherwise use the gate length parameter
        int nextIdx = (evt.stepIndex + 1) % stepCount;
        bool nextIsTie = steps_[static_cast<size_t>(nextIdx)].tie;
        double gateRatio = (step.glide || nextIsTie) ? 1.0 : static_cast<double>(gateLengthVal);
        int noteOnSample = static_cast<int>(evt.timeInBlock * sampleRate_);
        int gateSamples = static_cast<int>(stepDurationSamples * gateRatio);
        noteOffCountdown_ = gateSamples - (bufferSamples - noteOnSample);
        if (noteOffCountdown_ <= 0) {
            // Note-off falls within this block
            double offTimeInBlock =
                evt.timeInBlock + static_cast<double>(gateSamples) / sampleRate_;
            offTimeInBlock = std::min(offTimeInBlock, blockDurationSecs);
            killNote(midi, offTimeInBlock);
        }
    }

    // --- Strict mono safety: kill stuck notes ---
    // If no step events fired this block and there's a playing note with no
    // pending note-off countdown, the note is stuck. Count silent blocks and
    // kill after a short grace period (~50ms at 44.1kHz / 512 block = ~4 blocks).
    if (eventCount > 0) {
        silentBlockCount_ = 0;
    } else if (lastPlayedNote_ >= 0 && noteOffCountdown_ <= 0) {
        ++silentBlockCount_;
        if (silentBlockCount_ > 4) {
            killNote(midi, 0.0);
            silentBlockCount_ = 0;
        }
    }

    // Update UI play position
    if (eventCount == 0 && !stepClock_.isRunning()) {
        currentPlayStep_.store(-1, std::memory_order_relaxed);
    }

    // Re-add thru messages so incoming MIDI reaches downstream instruments
    for (auto& msg : thruMessages)
        midi.addMidiMessage(msg, msg.getTimeStamp(), te::MPESourceID{});
}

}  // namespace magda::daw::audio
