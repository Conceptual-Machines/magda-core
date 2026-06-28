#include "plugins/PolyStepSequencerPlugin.hpp"

namespace magda::daw::audio {

const char* PolyStepSequencerPlugin::xmlTypeName = "polystepsequencer";

// ValueTree property IDs
namespace PSeqIDs {
static const juce::Identifier numSteps("seqNumSteps");
static const juce::Identifier rate("seqRate");
static const juce::Identifier direction("seqDirection");
static const juce::Identifier swing("seqSwing");
static const juce::Identifier gateLength("seqGateLength");
static const juce::Identifier ramp("seqRamp");
static const juce::Identifier skew("seqSkew");
static const juce::Identifier rampCycles("seqRampCycles");
static const juce::Identifier hardAngle("seqHardAngle");
static const juce::Identifier quantize("seqQuantize");
static const juce::Identifier quantizeSub("seqQuantizeSub");
static const juce::Identifier midiThru("seqMidiThru");
static const juce::Identifier viewMode("seqViewMode");

// Per-step child tree
static const juce::Identifier stepTree("STEP");
static const juce::Identifier stepIndex("idx");
static const juce::Identifier stepGate("gate");
static const juce::Identifier stepTie("tie");
static const juce::Identifier stepProb("prob");
static const juce::Identifier stepVel("vel");

// Per-note child tree (inside STEP)
static const juce::Identifier noteTree("NOTE");
static const juce::Identifier noteNumber("note");
static const juce::Identifier noteVel("vel");
}  // namespace PSeqIDs

PolyStepSequencerPlugin::PolyStepSequencerPlugin(const te::PluginCreationInfo& info)
    : MidiDevicePlugin(info) {
    auto um = getUndoManager();
    numSteps.referTo(state, PSeqIDs::numSteps, um, 16);
    rate.referTo(state, PSeqIDs::rate, um, static_cast<int>(StepClock::Rate::Sixteenth));
    direction.referTo(state, PSeqIDs::direction, um,
                      static_cast<int>(StepClock::Direction::Forward));
    swing.referTo(state, PSeqIDs::swing, um, 0.0f);
    gateLength.referTo(state, PSeqIDs::gateLength, um, 0.8f);
    ramp.referTo(state, PSeqIDs::ramp, um, 0.0f);
    skew.referTo(state, PSeqIDs::skew, um, 0.0f);
    rampCycles.referTo(state, PSeqIDs::rampCycles, um, 1);
    hardAngle.referTo(state, PSeqIDs::hardAngle, um, false);
    quantize.referTo(state, PSeqIDs::quantize, um, 0.0f);
    quantizeSub.referTo(state, PSeqIDs::quantizeSub, um, 16);
    midiThru.referTo(state, PSeqIDs::midiThru, um, false);
    viewMode.referTo(state, PSeqIDs::viewMode, um, "keys");

    // Register automatable parameters for macro/mod linking
    rateParam = addParam("rate", "Rate", {0.0f, 9.0f, 1.0f});
    directionParam = addParam("direction", "Direction", {0.0f, 3.0f, 1.0f});
    swingParam = addParam("swing", "Swing", {0.0f, 1.0f});
    gateLengthParam = addParam("gatelength", "Gate", {0.05f, 1.0f});
    rampParam = addParam("ramp", "Timing Depth", {-1.0f, 1.0f});
    skewParam = addParam("skew", "Timing Skew", {-1.0f, 1.0f});

    // Initialize automatable params from CachedValues
    rateParam->setParameterFromHost(static_cast<float>(rate.get()), juce::dontSendNotification);
    directionParam->setParameterFromHost(static_cast<float>(direction.get()),
                                         juce::dontSendNotification);
    swingParam->setParameterFromHost(swing.get(), juce::dontSendNotification);
    gateLengthParam->setParameterFromHost(gateLength.get(), juce::dontSendNotification);
    rampParam->setParameterFromHost(ramp.get(), juce::dontSendNotification);
    skewParam->setParameterFromHost(skew.get(), juce::dontSendNotification);

    // Listen for state changes: param sync + pattern mirror (incl. undo/redo)
    state.addListener(&stateListener_);

    // Mirror steps from ValueTree (if restoring from saved state)
    loadStepsFromState();
}

PolyStepSequencerPlugin::~PolyStepSequencerPlugin() {
    state.removeListener(&stateListener_);
}

void PolyStepSequencerPlugin::StateListener::valueTreePropertyChanged(
    juce::ValueTree& tree, const juce::Identifier& property) {
    if (tree == owner.state)
        owner.syncParamFromProperty(property);
    else if (tree.hasType(PSeqIDs::stepTree) || tree.hasType(PSeqIDs::noteTree))
        owner.loadStepsFromState();
}

void PolyStepSequencerPlugin::StateListener::valueTreeChildAdded(juce::ValueTree&,
                                                                 juce::ValueTree& child) {
    if (child.hasType(PSeqIDs::stepTree) || child.hasType(PSeqIDs::noteTree))
        owner.loadStepsFromState();
}

void PolyStepSequencerPlugin::StateListener::valueTreeChildRemoved(juce::ValueTree&,
                                                                   juce::ValueTree& child, int) {
    if (child.hasType(PSeqIDs::stepTree) || child.hasType(PSeqIDs::noteTree))
        owner.loadStepsFromState();
}

void PolyStepSequencerPlugin::syncParamFromProperty(const juce::Identifier& property) {
    if (property == PSeqIDs::rate && rateParam)
        rateParam->setParameterFromHost(static_cast<float>(rate.get()), juce::dontSendNotification);
    else if (property == PSeqIDs::direction && directionParam)
        directionParam->setParameterFromHost(static_cast<float>(direction.get()),
                                             juce::dontSendNotification);
    else if (property == PSeqIDs::swing && swingParam)
        swingParam->setParameterFromHost(swing.get(), juce::dontSendNotification);
    else if (property == PSeqIDs::gateLength && gateLengthParam)
        gateLengthParam->setParameterFromHost(gateLength.get(), juce::dontSendNotification);
    else if (property == PSeqIDs::ramp && rampParam)
        rampParam->setParameterFromHost(ramp.get(), juce::dontSendNotification);
    else if (property == PSeqIDs::skew && skewParam)
        skewParam->setParameterFromHost(skew.get(), juce::dontSendNotification);
}

void PolyStepSequencerPlugin::initialise(const te::PluginInitialisationInfo& info) {
    MidiDevicePlugin::initialise(info);
    sampleRate_ = info.sampleRate;
    stepClock_.setSampleRate(info.sampleRate);
    stepClock_.reset();
    soundingCount_ = 0;
    noteOffCountdown_ = 0;
    silentBlockCount_ = 0;
    needsAllNotesOff_ = true;
}

void PolyStepSequencerPlugin::deinitialise() {
    stepClock_.reset();
    soundingCount_ = 0;
    noteOffCountdown_ = 0;
    silentBlockCount_ = 0;
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    MidiDevicePlugin::deinitialise();
}

void PolyStepSequencerPlugin::reset() {
    stepClock_.reset();
    soundingCount_ = 0;
    noteOffCountdown_ = 0;
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    clearMidiOutDisplay();
}

void PolyStepSequencerPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    tracktion::copyPropertiesToCachedValues(v, numSteps, rate, direction, swing, gateLength, ramp,
                                            skew, rampCycles, hardAngle, quantize, quantizeSub,
                                            midiThru);

    // viewMode is restored separately: the variadic helper does
    // ValueType(juce::var), and juce::String(juce::var) is ambiguous under
    // MSVC (var converts to int/double/String, String constructs from each).
    if (auto* p = v.getPropertyPointer(viewMode.getPropertyID()))
        viewMode = p->toString();
    else
        viewMode.resetToDefault();

    // Copy step children from the incoming tree into our state
    // (copyPropertiesToCachedValues only copies properties, not children)
    for (int i = state.getNumChildren() - 1; i >= 0; --i) {
        if (state.getChild(i).hasType(PSeqIDs::stepTree))
            state.removeChild(i, nullptr);
    }
    for (int i = 0; i < v.getNumChildren(); ++i) {
        auto child = v.getChild(i);
        if (child.hasType(PSeqIDs::stepTree))
            state.appendChild(child.createCopy(), nullptr);
    }

    loadStepsFromState();
}

// =============================================================================
// Pattern accessors (message thread)
// =============================================================================

juce::ValueTree PolyStepSequencerPlugin::findStepTree(int index) const {
    for (int i = 0; i < state.getNumChildren(); ++i) {
        auto child = state.getChild(i);
        if (child.hasType(PSeqIDs::stepTree) &&
            static_cast<int>(child.getProperty(PSeqIDs::stepIndex, -1)) == index)
            return child;
    }
    return {};
}

juce::ValueTree PolyStepSequencerPlugin::getOrCreateStepTree(int index) {
    auto existing = findStepTree(index);
    if (existing.isValid())
        return existing;

    juce::ValueTree stepVT(PSeqIDs::stepTree);
    stepVT.setProperty(PSeqIDs::stepIndex, index, nullptr);
    stepVT.setProperty(PSeqIDs::stepGate, true, nullptr);
    stepVT.setProperty(PSeqIDs::stepTie, false, nullptr);
    stepVT.setProperty(PSeqIDs::stepProb, 1.0f, nullptr);
    stepVT.setProperty(PSeqIDs::stepVel, 100, nullptr);
    state.appendChild(stepVT, getUndoManager());
    return stepVT;
}

juce::ValueTree PolyStepSequencerPlugin::findNoteTree(const juce::ValueTree& stepTree,
                                                      int noteNumber) {
    for (int i = 0; i < stepTree.getNumChildren(); ++i) {
        auto child = stepTree.getChild(i);
        if (child.hasType(PSeqIDs::noteTree) &&
            static_cast<int>(child.getProperty(PSeqIDs::noteNumber, -1)) == noteNumber)
            return child;
    }
    return {};
}

PolyStepSequencerPlugin::Step PolyStepSequencerPlugin::getStep(int index) const {
    if (index < 0 || index >= MAX_STEPS)
        return {};
    return steps_[static_cast<size_t>(index)];
}

bool PolyStepSequencerPlugin::stepHasNote(int index, int noteNumber) const {
    if (index < 0 || index >= MAX_STEPS)
        return false;
    const auto& step = steps_[static_cast<size_t>(index)];
    for (int i = 0; i < step.noteCount; ++i) {
        if (step.notes[static_cast<size_t>(i)].noteNumber == noteNumber)
            return true;
    }
    return false;
}

void PolyStepSequencerPlugin::setStepGate(int index, bool gateOn) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    getOrCreateStepTree(index).setProperty(PSeqIDs::stepGate, gateOn, getUndoManager());
}

void PolyStepSequencerPlugin::setStepTie(int index, bool tieOn) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    getOrCreateStepTree(index).setProperty(PSeqIDs::stepTie, tieOn, getUndoManager());
}

void PolyStepSequencerPlugin::setStepProbability(int index, float probability) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    getOrCreateStepTree(index).setProperty(PSeqIDs::stepProb, juce::jlimit(0.0f, 1.0f, probability),
                                           getUndoManager());
}

void PolyStepSequencerPlugin::setStepVelocity(int index, int velocity) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    getOrCreateStepTree(index).setProperty(PSeqIDs::stepVel, juce::jlimit(1, 127, velocity),
                                           getUndoManager());
}

void PolyStepSequencerPlugin::addStepNote(int index, int noteNumber, int velocityOverride) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    noteNumber = juce::jlimit(0, 127, noteNumber);

    auto stepVT = getOrCreateStepTree(index);
    if (findNoteTree(stepVT, noteNumber).isValid())
        return;  // already present

    int noteCount = 0;
    for (int i = 0; i < stepVT.getNumChildren(); ++i) {
        if (stepVT.getChild(i).hasType(PSeqIDs::noteTree))
            ++noteCount;
    }
    if (noteCount >= MAX_NOTES_PER_STEP)
        return;  // voice cap reached

    juce::ValueTree noteVT(PSeqIDs::noteTree);
    noteVT.setProperty(PSeqIDs::noteNumber, noteNumber, nullptr);
    if (velocityOverride > 0)
        noteVT.setProperty(PSeqIDs::noteVel, juce::jlimit(1, 127, velocityOverride), nullptr);
    stepVT.appendChild(noteVT, getUndoManager());
}

void PolyStepSequencerPlugin::removeStepNote(int index, int noteNumber) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    auto stepVT = findStepTree(index);
    if (!stepVT.isValid())
        return;
    auto noteVT = findNoteTree(stepVT, noteNumber);
    if (noteVT.isValid())
        stepVT.removeChild(noteVT, getUndoManager());
}

void PolyStepSequencerPlugin::toggleStepNote(int index, int noteNumber) {
    if (stepHasNote(index, noteNumber))
        removeStepNote(index, noteNumber);
    else
        addStepNote(index, noteNumber);
}

void PolyStepSequencerPlugin::setStepNoteVelocity(int index, int noteNumber, int velocityOverride) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    auto stepVT = findStepTree(index);
    if (!stepVT.isValid())
        return;
    auto noteVT = findNoteTree(stepVT, noteNumber);
    if (!noteVT.isValid())
        return;

    if (velocityOverride > 0)
        noteVT.setProperty(PSeqIDs::noteVel, juce::jlimit(1, 127, velocityOverride),
                           getUndoManager());
    else
        noteVT.removeProperty(PSeqIDs::noteVel, getUndoManager());
}

void PolyStepSequencerPlugin::clearStep(int index) {
    if (index < 0 || index >= MAX_STEPS)
        return;
    auto stepVT = findStepTree(index);
    if (stepVT.isValid())
        state.removeChild(stepVT, getUndoManager());
}

void PolyStepSequencerPlugin::clearPattern() {
    auto* um = getUndoManager();
    if (um != nullptr)
        um->beginNewTransaction();
    for (int i = state.getNumChildren() - 1; i >= 0; --i) {
        if (state.getChild(i).hasType(PSeqIDs::stepTree))
            state.removeChild(i, um);
    }
}

void PolyStepSequencerPlugin::setStepRecording(bool enabled) {
    stepRecording_.store(enabled, std::memory_order_relaxed);
    if (enabled) {
        stepRecordPosition_.store(0, std::memory_order_relaxed);
        recordHeldCount_ = 0;
    }
}

void PolyStepSequencerPlugin::randomizePattern() {
    juce::Random rng;
    auto* um = getUndoManager();
    if (um != nullptr)
        um->beginNewTransaction();

    // Clear existing steps first.
    for (int i = state.getNumChildren() - 1; i >= 0; --i) {
        if (state.getChild(i).hasType(PSeqIDs::stepTree))
            state.removeChild(i, um);
    }

    const int stepCount = juce::jlimit(1, MAX_STEPS, numSteps.get());
    for (int i = 0; i < stepCount; ++i) {
        const bool gate = rng.nextFloat() < 0.7f;
        getOrCreateStepTree(i).setProperty(PSeqIDs::stepGate, gate, um);
        if (!gate)
            continue;
        // 1-3 notes per active step, C2-B3 (addStepNote dedups + caps voices).
        const int noteCount = 1 + rng.nextInt(3);
        for (int n = 0; n < noteCount; ++n)
            addStepNote(i, 36 + rng.nextInt(24));
    }
}

bool PolyStepSequencerPlugin::transposePattern(int semitones) {
    if (semitones == 0)
        return true;

    for (int i = 0; i < state.getNumChildren(); ++i) {
        auto stepVT = state.getChild(i);
        if (!stepVT.hasType(PSeqIDs::stepTree))
            continue;
        for (int n = 0; n < stepVT.getNumChildren(); ++n) {
            auto noteVT = stepVT.getChild(n);
            if (!noteVT.hasType(PSeqIDs::noteTree))
                continue;
            int note = static_cast<int>(noteVT.getProperty(PSeqIDs::noteNumber, 60));
            if (note + semitones < 0 || note + semitones > 127)
                return false;
        }
    }

    auto* um = getUndoManager();
    if (um != nullptr)
        um->beginNewTransaction();
    for (int i = 0; i < state.getNumChildren(); ++i) {
        auto stepVT = state.getChild(i);
        if (!stepVT.hasType(PSeqIDs::stepTree))
            continue;
        for (int n = 0; n < stepVT.getNumChildren(); ++n) {
            auto noteVT = stepVT.getChild(n);
            if (!noteVT.hasType(PSeqIDs::noteTree))
                continue;
            int note = static_cast<int>(noteVT.getProperty(PSeqIDs::noteNumber, 60));
            noteVT.setProperty(PSeqIDs::noteNumber, note + semitones, um);
        }
    }
    return true;
}

// =============================================================================
// State mirroring
// =============================================================================

void PolyStepSequencerPlugin::loadStepsFromState() {
    // Reset all steps to defaults
    steps_.fill(Step{});

    for (int i = 0; i < state.getNumChildren(); ++i) {
        auto child = state.getChild(i);
        if (!child.hasType(PSeqIDs::stepTree))
            continue;

        int idx = child.getProperty(PSeqIDs::stepIndex, -1);
        if (idx < 0 || idx >= MAX_STEPS)
            continue;

        auto& s = steps_[static_cast<size_t>(idx)];
        s.gate = child.getProperty(PSeqIDs::stepGate, true);
        s.tie = child.getProperty(PSeqIDs::stepTie, false);
        s.probability = juce::jlimit(0.0f, 1.0f, float(child.getProperty(PSeqIDs::stepProb, 1.0f)));
        s.velocity = juce::jlimit(1, 127, int(child.getProperty(PSeqIDs::stepVel, 100)));

        s.noteCount = 0;
        for (int n = 0; n < child.getNumChildren() && s.noteCount < MAX_NOTES_PER_STEP; ++n) {
            auto noteVT = child.getChild(n);
            if (!noteVT.hasType(PSeqIDs::noteTree))
                continue;

            auto& note = s.notes[static_cast<size_t>(s.noteCount)];
            note.noteNumber =
                juce::jlimit(0, 127, int(noteVT.getProperty(PSeqIDs::noteNumber, 60)));
            note.velocity = juce::jlimit(0, 127, int(noteVT.getProperty(PSeqIDs::noteVel, 0)));
            ++s.noteCount;
        }
    }
}

// =============================================================================
// Audio thread
// =============================================================================

void PolyStepSequencerPlugin::killAllNotes(te::MidiMessageArray& midi, double time) {
    if (soundingCount_ > 0) {
        for (int i = 0; i < soundingCount_; ++i)
            midi.addMidiMessage(
                juce::MidiMessage::noteOff(1, soundingNotes_[static_cast<size_t>(i)]), time,
                te::MPESourceID{});
        soundingCount_ = 0;
        noteOffCountdown_ = 0;
        clearMidiOutDisplay();
    }
}

void PolyStepSequencerPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
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

    // --- Step recording: held notes form a chord on the current step; the
    // step advances once the whole chord is released. ---
    if (stepRecording_.load(std::memory_order_relaxed)) {
        const int maxSteps = juce::jlimit(1, MAX_STEPS, numSteps.get());
        for (auto& msg : midi) {
            if (msg.isNoteOn()) {
                const int pos = stepRecordPosition_.load(std::memory_order_relaxed);
                if (pos < maxSteps) {
                    const int note = msg.getNoteNumber();
                    juce::MessageManager::callAsync([this, pos, note] {
                        addStepNote(pos, note);
                        setStepGate(pos, true);
                    });
                }
                ++recordHeldCount_;
            } else if (msg.isNoteOff()) {
                if (recordHeldCount_ > 0)
                    --recordHeldCount_;
                if (recordHeldCount_ == 0) {
                    const int pos = stepRecordPosition_.load(std::memory_order_relaxed);
                    if (pos < maxSteps) {
                        const int nextPos = pos + 1;
                        stepRecordPosition_.store(nextPos, std::memory_order_relaxed);
                        if (nextPos >= maxSteps)
                            stepRecording_.store(false, std::memory_order_relaxed);
                    }
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
        killAllNotes(midi, 0.0);
        stepClock_.reset();
        currentPlayStep_.store(-1, std::memory_order_relaxed);
        silentBlockCount_ = 0;
        for (auto& msg : thruMessages)
            midi.addMidiMessage(msg, msg.getTimeStamp(), te::MPESourceID{});
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

    // --- Detect structural parameter changes -> reset clock to re-sync ---
    // Only reset for changes that alter the step grid timing structure
    // (same rationale as StepSequencerPlugin).
    int rateInt = static_cast<int>(currentRate);
    if (rateInt != prevRate_ || cyclesVal != prevCycles_ || hardAngleVal != prevHardAngle_) {
        killAllNotes(midi, 0.0);
        stepClock_.reset();
        prevRate_ = rateInt;
        prevCycles_ = cyclesVal;
        prevHardAngle_ = hardAngleVal;
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
    if (noteOffCountdown_ > 0 && soundingCount_ > 0) {
        if (noteOffCountdown_ <= bufferSamples) {
            double countdownTime = static_cast<double>(noteOffCountdown_) / sampleRate_;
            bool stepFiresFirst = (eventCount > 0 && events[0].timeInBlock <= countdownTime);

            // If the next step is a tie, never kill the notes - let the tie hold them
            bool nextStepIsTie =
                (eventCount > 0 && steps_[static_cast<size_t>(events[0].stepIndex)].tie &&
                 steps_[static_cast<size_t>(events[0].stepIndex)].gate);

            if (!stepFiresFirst && !nextStepIsTie) {
                killAllNotes(midi, countdownTime);
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

        // Rest step - send note-offs if needed
        if (!step.gate) {
            killAllNotes(midi, evt.timeInBlock);
            continue;
        }

        // Tie step - keep the current notes playing, no retrigger, no new countdown
        if (step.tie && soundingCount_ > 0) {
            noteOffCountdown_ = 0;  // Cancel any pending note-off - notes hold
            continue;
        }

        // Probability - evaluated once per step fire; failure acts as a rest
        if (step.probability < 1.0f && nextRandom01() >= step.probability) {
            killAllNotes(midi, evt.timeInBlock);
            continue;
        }

        // Empty step (gate on but no notes) acts as a rest
        if (step.noteCount == 0) {
            killAllNotes(midi, evt.timeInBlock);
            continue;
        }

        // Note-offs for sounding notes - always BEFORE the new note-ons
        if (soundingCount_ > 0) {
            double offTime = std::max(0.0, evt.timeInBlock - 0.0001);
            for (int n = 0; n < soundingCount_; ++n)
                midi.addMidiMessage(
                    juce::MidiMessage::noteOff(1, soundingNotes_[static_cast<size_t>(n)]), offTime,
                    te::MPESourceID{});
            soundingCount_ = 0;
            noteOffCountdown_ = 0;
        }

        // Note-ons for each note in the step
        int displayVel = step.velocity;
        for (int n = 0; n < step.noteCount; ++n) {
            const auto& note = step.notes[static_cast<size_t>(n)];
            int vel = juce::jlimit(1, 127, note.velocity > 0 ? note.velocity : step.velocity);
            midi.addMidiMessage(
                juce::MidiMessage::noteOn(1, note.noteNumber, static_cast<juce::uint8>(vel)),
                evt.timeInBlock, te::MPESourceID{});
            soundingNotes_[static_cast<size_t>(n)] = note.noteNumber;
            displayVel = vel;
        }
        soundingCount_ = step.noteCount;
        setMidiOutDisplay(step.notes[0].noteNumber, displayVel);

        // Schedule note-off via sample countdown
        // 100% gate if the next step is a tie (must hold for the tie to extend)
        int nextIdx = (evt.stepIndex + 1) % stepCount;
        bool nextIsTie = steps_[static_cast<size_t>(nextIdx)].tie;
        double gateRatio = nextIsTie ? 1.0 : static_cast<double>(gateLengthVal);
        int noteOnSample = static_cast<int>(evt.timeInBlock * sampleRate_);
        int gateSamples = static_cast<int>(stepDurationSamples * gateRatio);
        noteOffCountdown_ = gateSamples - (bufferSamples - noteOnSample);
        if (noteOffCountdown_ <= 0) {
            // Note-off falls within this block
            double offTimeInBlock =
                evt.timeInBlock + static_cast<double>(gateSamples) / sampleRate_;
            offTimeInBlock = std::min(offTimeInBlock, blockDurationSecs);
            killAllNotes(midi, offTimeInBlock);
        }
    }

    // --- Stuck-note safety: kill notes left hanging with no pending note-off ---
    if (eventCount > 0) {
        silentBlockCount_ = 0;
    } else if (soundingCount_ > 0 && noteOffCountdown_ <= 0) {
        ++silentBlockCount_;
        if (silentBlockCount_ > 4) {
            killAllNotes(midi, 0.0);
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
