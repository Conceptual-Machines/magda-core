#include "ArpeggiatorPlugin.hpp"

#include <algorithm>

#include "StepClock.hpp"

namespace magda::daw::audio {

const char* ArpeggiatorPlugin::xmlTypeName = "arpeggiator";

// ValueTree property IDs
namespace ArpIDs {
static const juce::Identifier pattern("arpPattern");
static const juce::Identifier rate("arpRate");
static const juce::Identifier octaveRange("arpOctaves");
static const juce::Identifier gate("arpGate");
static const juce::Identifier swing("arpSwing");
static const juce::Identifier ramp("arpRamp");
static const juce::Identifier skew("arpSkew");
static const juce::Identifier latch("arpLatch");
static const juce::Identifier velocityMode("arpVelMode");
static const juce::Identifier fixedVelocity("arpFixedVel");
static const juce::Identifier rampCycles("arpRampCycles");
static const juce::Identifier quantize("arpQuantize");
static const juce::Identifier quantizeSub("arpQuantizeSub");
static const juce::Identifier hardAngle("arpHardAngle");
}  // namespace ArpIDs

ArpeggiatorPlugin::ArpeggiatorPlugin(const te::PluginCreationInfo& info) : MidiDevicePlugin(info) {
    auto um = getUndoManager();
    pattern.referTo(state, ArpIDs::pattern, um, 0);
    rate.referTo(state, ArpIDs::rate, um, 4);  // Eighth note
    octaveRange.referTo(state, ArpIDs::octaveRange, um, 1);
    gate.referTo(state, ArpIDs::gate, um, 0.8f);
    swing.referTo(state, ArpIDs::swing, um, 0.0f);
    ramp.referTo(state, ArpIDs::ramp, um, 0.0f);
    skew.referTo(state, ArpIDs::skew, um, 0.0f);
    rampCycles.referTo(state, ArpIDs::rampCycles, um, 1);
    latch.referTo(state, ArpIDs::latch, um, false);
    velocityMode.referTo(state, ArpIDs::velocityMode, um, 0);
    fixedVelocity.referTo(state, ArpIDs::fixedVelocity, um, 100);
    quantize.referTo(state, ArpIDs::quantize, um, 0.0f);
    quantizeSub.referTo(state, ArpIDs::quantizeSub, um, 16);
    hardAngle.referTo(state, ArpIDs::hardAngle, um, false);

    // Register automatable parameters so macros can link to them
    patternParam = addParam("pattern", "Pattern", {0.0f, 5.0f, 1.0f});
    rateParam = addParam("rate", "Rate", {0.0f, 9.0f, 1.0f});
    octavesParam = addParam("octaves", "Octaves", {1.0f, 4.0f, 1.0f});
    gateParam = addParam("gate", "Gate", {0.01f, 1.0f});
    swingParam = addParam("swing", "Swing", {0.0f, 1.0f});
    rampParam = addParam("ramp", "Timing Depth", {-1.0f, 1.0f});
    skewParam = addParam("skew", "Timing Skew", {-1.0f, 1.0f});
    latchParam = addParam("latch", "Latch", {0.0f, 1.0f, 1.0f});
    velModeParam = addParam("velmode", "Velocity Mode", {0.0f, 2.0f, 1.0f});
    fixedVelParam = addParam("fixedvel", "Fixed Velocity", {1.0f, 127.0f, 1.0f});

    // Initialize automatable params from CachedValues
    patternParam->setParameter(static_cast<float>(pattern.get()), juce::dontSendNotification);
    rateParam->setParameter(static_cast<float>(rate.get()), juce::dontSendNotification);
    octavesParam->setParameter(static_cast<float>(octaveRange.get()), juce::dontSendNotification);
    gateParam->setParameter(gate.get(), juce::dontSendNotification);
    swingParam->setParameter(swing.get(), juce::dontSendNotification);
    rampParam->setParameter(ramp.get(), juce::dontSendNotification);
    skewParam->setParameter(skew.get(), juce::dontSendNotification);
    latchParam->setParameter(latch.get() ? 1.0f : 0.0f, juce::dontSendNotification);
    velModeParam->setParameter(static_cast<float>(velocityMode.get()), juce::dontSendNotification);
    fixedVelParam->setParameter(static_cast<float>(fixedVelocity.get()),
                                juce::dontSendNotification);

    // Listen for CachedValue changes (from UI) to sync to AutomatableParams
    state.addListener(&paramSyncListener_);
}

ArpeggiatorPlugin::~ArpeggiatorPlugin() {
    state.removeListener(&paramSyncListener_);
    notifyListenersOfDeletion();
}

void ArpeggiatorPlugin::syncParamFromProperty(const juce::Identifier& property) {
    // Push CachedValue changes to AutomatableParam base values
    if (property == ArpIDs::pattern && patternParam)
        patternParam->setParameter(static_cast<float>(pattern.get()), juce::dontSendNotification);
    else if (property == ArpIDs::rate && rateParam)
        rateParam->setParameter(static_cast<float>(rate.get()), juce::dontSendNotification);
    else if (property == ArpIDs::octaveRange && octavesParam)
        octavesParam->setParameter(static_cast<float>(octaveRange.get()),
                                   juce::dontSendNotification);
    else if (property == ArpIDs::gate && gateParam)
        gateParam->setParameter(gate.get(), juce::dontSendNotification);
    else if (property == ArpIDs::swing && swingParam)
        swingParam->setParameter(swing.get(), juce::dontSendNotification);
    else if (property == ArpIDs::ramp && rampParam)
        rampParam->setParameter(ramp.get(), juce::dontSendNotification);
    else if (property == ArpIDs::skew && skewParam)
        skewParam->setParameter(skew.get(), juce::dontSendNotification);
    else if (property == ArpIDs::latch && latchParam)
        latchParam->setParameter(latch.get() ? 1.0f : 0.0f, juce::dontSendNotification);
    else if (property == ArpIDs::velocityMode && velModeParam)
        velModeParam->setParameter(static_cast<float>(velocityMode.get()),
                                   juce::dontSendNotification);
    else if (property == ArpIDs::fixedVelocity && fixedVelParam)
        fixedVelParam->setParameter(static_cast<float>(fixedVelocity.get()),
                                    juce::dontSendNotification);
}

void ArpeggiatorPlugin::initialise(const te::PluginInitialisationInfo& info) {
    MidiDevicePlugin::initialise(info);
    resetArpState();
}

void ArpeggiatorPlugin::deinitialise() {
    MidiDevicePlugin::deinitialise();
    resetArpState();
}

void ArpeggiatorPlugin::reset() {
    resetArpState();
    clearHeldNotes();
}

void ArpeggiatorPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    tracktion::copyPropertiesToCachedValues(v, pattern, rate, octaveRange, gate, swing, ramp, skew,
                                            latch, velocityMode, fixedVelocity, quantize,
                                            quantizeSub, hardAngle);
}

// =============================================================================
// Helpers
// =============================================================================

double ArpeggiatorPlugin::rateToBeats(Rate r) {
    switch (r) {
        case Rate::DottedQuarter:
            return 1.5;
        case Rate::Quarter:
            return 1.0;
        case Rate::TripletQuarter:
            return 2.0 / 3.0;
        case Rate::DottedEighth:
            return 0.75;
        case Rate::Eighth:
            return 0.5;
        case Rate::TripletEighth:
            return 1.0 / 3.0;
        case Rate::DottedSixteenth:
            return 0.375;
        case Rate::Sixteenth:
            return 0.25;
        case Rate::TripletSixteenth:
            return 0.5 / 3.0;
        case Rate::ThirtySecond:
            return 0.125;
        default:
            return 0.5;
    }
}

double ArpeggiatorPlugin::applyRampCurve(double t, float depth, float skew, bool hardAngle) {
    return StepClock::applyRampCurve(t, depth, skew, hardAngle);
}

void ArpeggiatorPlugin::addHeldNote(int noteNumber, int velocity) {
    // Check if already held
    for (int i = 0; i < heldCount_; ++i) {
        if (heldNotes_[static_cast<size_t>(i)].noteNumber == noteNumber)
            return;
    }
    if (heldCount_ < MAX_HELD) {
        heldNotes_[static_cast<size_t>(heldCount_)] = {noteNumber, velocity, nextOrder_++};
        ++heldCount_;
    }
}

void ArpeggiatorPlugin::removeHeldNote(int noteNumber) {
    for (int i = 0; i < heldCount_; ++i) {
        if (heldNotes_[static_cast<size_t>(i)].noteNumber == noteNumber) {
            // Swap with last
            if (i < heldCount_ - 1)
                heldNotes_[static_cast<size_t>(i)] =
                    heldNotes_[static_cast<size_t>(heldCount_ - 1)];
            --heldCount_;
            return;
        }
    }
}

void ArpeggiatorPlugin::clearHeldNotes() {
    heldCount_ = 0;
    physicallyHeldCount_ = 0;
    latchedSetStale_ = false;
    nextOrder_ = 0;
}

void ArpeggiatorPlugin::sendAllNotesOff(te::MidiMessageArray& midi) {
    if (lastPlayedNote_ >= 0) {
        midi.addMidiMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), 0.0, te::MPESourceID{});
        lastPlayedNote_ = -1;
        clearMidiOutDisplay();
    }
}

void ArpeggiatorPlugin::resetArpState() {
    currentStep_ = 0;
    goingUp_ = true;
    arpOriginBeat_ = -1.0;
    lastPlayedNote_ = -1;
    lastPlayedVelocity_ = 0;
    lastNoteOffBeat_ = -1.0;
    wasPlaying_ = false;
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    currentSeqLength_.store(0, std::memory_order_relaxed);
    clearMidiOutDisplay();
}

ArpeggiatorPlugin::ExpandedSequence ArpeggiatorPlugin::buildSequence() const {
    ExpandedSequence seq;
    if (heldCount_ == 0)
        return seq;

    auto pat = static_cast<Pattern>(
        juce::roundToInt(patternParam ? patternParam->getCurrentValue() : pattern.get()));
    int octaves = juce::jlimit(
        1, 4, juce::roundToInt(octavesParam ? octavesParam->getCurrentValue() : octaveRange.get()));

    // Copy held notes for sorting
    std::array<HeldNote, MAX_HELD> sorted{};
    for (int i = 0; i < heldCount_; ++i)
        sorted[static_cast<size_t>(i)] = heldNotes_[static_cast<size_t>(i)];

    // Sort based on pattern
    if (pat == Pattern::AsPlayed) {
        std::sort(sorted.begin(), sorted.begin() + heldCount_,
                  [](const HeldNote& a, const HeldNote& b) { return a.order < b.order; });
    } else {
        std::sort(sorted.begin(), sorted.begin() + heldCount_,
                  [](const HeldNote& a, const HeldNote& b) { return a.noteNumber < b.noteNumber; });
    }

    // Expand across octaves
    for (int oct = 0; oct < octaves; ++oct) {
        for (int i = 0; i < heldCount_; ++i) {
            int note = sorted[static_cast<size_t>(i)].noteNumber + oct * 12;
            if (note > 127)
                break;
            if (seq.length >= static_cast<int>(seq.notes.size()))
                break;
            seq.notes[static_cast<size_t>(seq.length)] = {note,
                                                          sorted[static_cast<size_t>(i)].velocity,
                                                          sorted[static_cast<size_t>(i)].order};
            ++seq.length;
        }
    }

    // Reverse for Down pattern
    if (pat == Pattern::Down) {
        std::reverse(seq.notes.begin(), seq.notes.begin() + seq.length);
    }
    // UpDown: append reverse (excluding last to avoid double)
    else if (pat == Pattern::UpDown && seq.length > 1) {
        int origLen = seq.length;
        for (int i = origLen - 2; i >= 0; --i) {
            if (seq.length >= static_cast<int>(seq.notes.size()))
                break;
            seq.notes[static_cast<size_t>(seq.length)] = seq.notes[static_cast<size_t>(i)];
            ++seq.length;
        }
    }
    // DownUp: reverse then append forward (excluding first)
    else if (pat == Pattern::DownUp && seq.length > 1) {
        std::reverse(seq.notes.begin(), seq.notes.begin() + seq.length);
        int origLen = seq.length;
        for (int i = 1; i < origLen; ++i) {
            if (seq.length >= static_cast<int>(seq.notes.size()))
                break;
            // Forward order = reversed array from index 1
            seq.notes[static_cast<size_t>(seq.length)] =
                seq.notes[static_cast<size_t>(origLen - 1 - i)];
            ++seq.length;
        }
    }

    return seq;
}

// =============================================================================
// Audio thread
// =============================================================================

void ArpeggiatorPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.bufferForMidiMessages)
        return;

    if (!isEnabled())
        return;

    auto& midi = *fc.bufferForMidiMessages;
    bool isLatched = latchParam ? latchParam->getCurrentValue() >= 0.5f : latch.get();

    // --- 1. Capture incoming MIDI ---
    for (const auto& msg : midi) {
        if (msg.isNoteOn()) {
            ++physicallyHeldCount_;

            // Latch: if old set is stale (all keys were released), clear before adding
            if (isLatched && latchedSetStale_) {
                // Send note-off for any currently playing note before clearing
                sendAllNotesOff(midi);
                heldCount_ = 0;
                nextOrder_ = 0;
                latchedSetStale_ = false;
                currentStep_ = 0;
                goingUp_ = true;
            }

            bool wasEmpty = (heldCount_ == 0);
            addHeldNote(msg.getNoteNumber(), msg.getVelocity());
            // Reset free-running clock when first note arrives
            if (wasEmpty && heldCount_ > 0) {
                freeRunSamples_ = 0.0;
                resetArpState();
            }
        } else if (msg.isNoteOff()) {
            --physicallyHeldCount_;
            if (physicallyHeldCount_ < 0)
                physicallyHeldCount_ = 0;

            if (isLatched) {
                // Don't remove notes; mark stale when all physically released
                if (physicallyHeldCount_ == 0)
                    latchedSetStale_ = true;
            } else {
                removeHeldNote(msg.getNoteNumber());
            }
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            clearHeldNotes();
            resetArpState();
        }
    }

    // --- 2. Clear MIDI buffer (arp replaces input) ---
    midi.clear();

    // --- 3. Handle transport transitions ---
    // Note: TE briefly toggles isPlaying at loop boundaries, so we only
    // reset the arp on a genuine stop (isPlaying goes false). On start we
    // just update the flag — the step counter continues from where it was.
    if (fc.isPlaying && !wasPlaying_) {
        wasPlaying_ = true;
    } else if (!fc.isPlaying && wasPlaying_) {
        sendAllNotesOff(midi);
        clearHeldNotes();
        resetArpState();
        freeRunSamples_ = 0.0;
        wasPlaying_ = false;
    }

    // --- 4. No held notes, or no MIDI input while transport is stopped? ---
    if (heldCount_ == 0 || (!fc.isPlaying && physicallyHeldCount_ <= 0)) {
        sendAllNotesOff(midi);
        freeRunSamples_ = 0.0;
        return;
    }

    // --- 5. Get beat positions ---
    double blockStartBeat, blockEndBeat;

    if (fc.isPlaying) {
        // Use transport position
        auto& tempoSeq = edit.tempoSequence;
        blockStartBeat = tempoSeq.toBeats(fc.editTime.getStart()).inBeats();
        blockEndBeat = tempoSeq.toBeats(fc.editTime.getEnd()).inBeats();
    } else {
        // Free-running clock — get tempo from edit position 0
        auto& tempoSeq = edit.tempoSequence;
        double bpm = tempoSeq.getBpmAt(tracktion::TimePosition());
        double beatsPerSample = bpm / (60.0 * sampleRate_);
        blockStartBeat = freeRunSamples_ * beatsPerSample;
        freeRunSamples_ += fc.bufferNumSamples;
        blockEndBeat = freeRunSamples_ * beatsPerSample;
    }

    if (blockEndBeat <= blockStartBeat)
        return;

    // --- 6. Build note sequence ---
    auto seq = buildSequence();
    if (seq.length == 0)
        return;

    // Read from automatable params (includes macro modulation) with CachedValue fallbacks
    auto pat = static_cast<Pattern>(
        juce::roundToInt(patternParam ? patternParam->getCurrentValue() : pattern.get()));
    auto currentRate =
        static_cast<Rate>(juce::roundToInt(rateParam ? rateParam->getCurrentValue() : rate.get()));
    double stepBeats = rateToBeats(currentRate);
    float gateVal =
        juce::jlimit(0.01f, 1.0f, gateParam ? gateParam->getCurrentValue() : gate.get());
    float swingVal =
        juce::jlimit(0.0f, 1.0f, swingParam ? swingParam->getCurrentValue() : swing.get());
    float rampVal =
        juce::jlimit(-1.0f, 1.0f, rampParam ? rampParam->getCurrentValue() : ramp.get());
    float skewVal =
        juce::jlimit(-1.0f, 1.0f, skewParam ? skewParam->getCurrentValue() : skew.get());
    auto velMode = static_cast<VelocityMode>(
        juce::roundToInt(velModeParam ? velModeParam->getCurrentValue() : velocityMode.get()));
    int fixedVel = juce::jlimit(
        1, 127,
        juce::roundToInt(fixedVelParam ? fixedVelParam->getCurrentValue() : fixedVelocity.get()));
    float quantizeAmount = juce::jlimit(0.0f, 1.0f, quantize.get());
    int quantizeSubVal = juce::jlimit(16, 512, quantizeSub.get());
    bool hardAngleVal = hardAngle.get();

    // Cycle length in beats (one full pass through the sequence)
    double cycleBeats = seq.length * stepBeats;

    // Initialise arp origin on first buffer — align to step grid
    if (arpOriginBeat_ < 0.0) {
        arpOriginBeat_ = std::floor(blockStartBeat / stepBeats) * stepBeats;
    }

    // Compute the beat position for a given global step index.
    // With ramp, steps within each cycle are warped by the bezier curve.
    // Without ramp, steps are evenly spaced at stepBeats intervals.
    auto computeStepBeat = [&](int step) -> double {
        int cycle = step / seq.length;
        int stepInCycle = step % seq.length;
        double cycleStart = arpOriginBeat_ + cycle * cycleBeats;

        if (std::abs(rampVal) > 0.001f && seq.length > 1) {
            int cyc = juce::jlimit(1, 8, rampCycles.get());
            double tLinear = static_cast<double>(stepInCycle) / static_cast<double>(seq.length);
            double tCurved;
            if (cyc <= 1) {
                tCurved = applyRampCurve(tLinear, rampVal, skewVal, hardAngleVal);
            } else {
                double segLen = 1.0 / static_cast<double>(cyc);
                int seg = std::min(static_cast<int>(tLinear / segLen), cyc - 1);
                double tLocal = (tLinear - seg * segLen) / segLen;
                tCurved = (seg + applyRampCurve(tLocal, rampVal, skewVal, hardAngleVal)) * segLen;
            }
            return cycleStart + tCurved * cycleBeats;
        }
        return cycleStart + stepInCycle * stepBeats;
    };

    // Block duration in seconds (for MIDI timestamp conversion — TE uses seconds, not samples)
    double blockDurationSecs = static_cast<double>(fc.bufferNumSamples) / sampleRate_;

    // --- 7. Emit pending note-off from previous block ---
    if (lastNoteOffBeat_ >= blockStartBeat && lastNoteOffBeat_ < blockEndBeat &&
        lastPlayedNote_ >= 0) {
        double frac = (lastNoteOffBeat_ - blockStartBeat) / (blockEndBeat - blockStartBeat);
        double timeInBlock = frac * blockDurationSecs;
        midi.addMidiMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), timeInBlock,
                            te::MPESourceID{});
        lastPlayedNote_ = -1;
        lastNoteOffBeat_ = -1.0;
        clearMidiOutDisplay();
    }

    // --- 8. Walk steps and generate notes ---
    // Catch up: skip past any steps whose warped position is before this block
    while (computeStepBeat(currentStep_) < blockStartBeat)
        ++currentStep_;

    double stepBeat = computeStepBeat(currentStep_);

    while (stepBeat < blockEndBeat) {
        // Apply swing to odd steps (on top of ramp)
        double swungBeat = stepBeat;
        if (currentStep_ % 2 == 1 && swingVal > 0.0f) {
            swungBeat += static_cast<double>(swingVal) * stepBeats * 0.5;
        }

        // Apply quantize: pull warped beat toward a regular grid
        if (quantizeAmount > 0.0f && quantizeSubVal > 0) {
            double gridSpacing = cycleBeats / static_cast<double>(quantizeSubVal);
            double snapped = std::round(swungBeat / gridSpacing) * gridSpacing;
            swungBeat += (snapped - swungBeat) * static_cast<double>(quantizeAmount);
        }

        if (swungBeat >= blockStartBeat && swungBeat < blockEndBeat) {
            double frac = (swungBeat - blockStartBeat) / (blockEndBeat - blockStartBeat);
            double timeInBlock = frac * blockDurationSecs;

            // Note-off for previous note
            if (lastPlayedNote_ >= 0) {
                midi.addMidiMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), timeInBlock,
                                    te::MPESourceID{});
                lastPlayedNote_ = -1;
            }

            // Determine which note to play
            int stepIdx;
            if (pat == Pattern::Random) {
                stepIdx = arpRandom_.nextInt(seq.length);
            } else {
                stepIdx = currentStep_ % seq.length;
            }

            auto& note = seq.notes[static_cast<size_t>(stepIdx)];

            // Determine velocity
            int vel = note.velocity;
            if (velMode == VelocityMode::Fixed) {
                vel = fixedVel;
            } else if (velMode == VelocityMode::Accent) {
                vel = (currentStep_ % 4 == 0) ? juce::jmin(127, note.velocity + 30) : note.velocity;
            }

            // Note-on
            midi.addMidiMessage(
                juce::MidiMessage::noteOn(1, note.noteNumber, static_cast<juce::uint8>(vel)),
                timeInBlock, te::MPESourceID{});

            lastPlayedNote_ = note.noteNumber;
            lastPlayedVelocity_ = vel;
            setMidiOutDisplay(note.noteNumber, vel);

            // Schedule note-off
            double noteOffBeat = swungBeat + stepBeats * static_cast<double>(gateVal);
            if (noteOffBeat < blockEndBeat) {
                double offFrac = (noteOffBeat - blockStartBeat) / (blockEndBeat - blockStartBeat);
                double offTimeInBlock = offFrac * blockDurationSecs;
                midi.addMidiMessage(juce::MidiMessage::noteOff(1, note.noteNumber), offTimeInBlock,
                                    te::MPESourceID{});
                lastPlayedNote_ = -1;
                lastNoteOffBeat_ = -1.0;
                clearMidiOutDisplay();
            } else {
                // Note-off in a future block
                lastNoteOffBeat_ = noteOffBeat;
            }
        }

        ++currentStep_;
        currentPlayStep_.store(currentStep_ % seq.length, std::memory_order_relaxed);
        currentSeqLength_.store(seq.length, std::memory_order_relaxed);
        stepBeat = computeStepBeat(currentStep_);
    }
}

}  // namespace magda::daw::audio
