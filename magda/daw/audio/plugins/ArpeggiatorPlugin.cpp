#include "plugins/ArpeggiatorPlugin.hpp"

#include <algorithm>
#include <limits>

#include "transport/RampCurve.hpp"

namespace magda::daw::audio {

const char* ArpeggiatorPlugin::xmlTypeName = "arpeggiator";

const juce::Identifier ArpeggiatorPlugin::SettingIDs::rampCycles("arpRampCycles");
const juce::Identifier ArpeggiatorPlugin::SettingIDs::quantize("arpQuantize");
const juce::Identifier ArpeggiatorPlugin::SettingIDs::quantizeSub("arpQuantizeSub");
const juce::Identifier ArpeggiatorPlugin::SettingIDs::hardAngle("arpHardAngle");

namespace {

/// Whether the host counts this MIDI source as live input. No list is
/// "nothing is known live", never "everything is": the native engine says
/// nothing and stamps every event 0 (#2418).
bool isLiveSource(const DeviceProcessContext& context, std::uint32_t sourceId) {
    if (context.liveSourceIds == nullptr)
        return false;
    for (int i = 0; i < context.numLiveSourceIds; ++i) {
        if (context.liveSourceIds[i] == sourceId)
            return true;
    }
    return false;
}

/**
 * @brief What the arp does not consume, copied onto its output in timestamp
 *        order (#2417).
 *
 * Notes are the device's material: they go in and an arpeggio comes out.
 * Everything else the channel carries -- mod wheel, expression, sustain,
 * bend, aftertouch, program change -- is addressed to the instrument behind
 * this one, and reaches it nowhere else: thru carries the held chord too, so
 * a track that wants the pedal cannot have it without the notes under it.
 *
 * Forwarded on channel 1, the one the generated notes are on, so an
 * instrument that keeps per-channel state applies them to what it is
 * playing. SysEx is not forwarded: copying one allocates on the audio thread
 * (JUCE holds anything past eight bytes on the heap), and it addresses a
 * device rather than the notes.
 */
class NonNoteForwarder {
  public:
    /**
     * @param in The block's MIDI input.
     * @param offsetSeconds The host's sub-block offset, added to every input
     *                      timestamp.
     * @param blockDurationSecs The block's length, which no event is read past.
     */
    NonNoteForwarder(const DeviceMidiInput& in, double offsetSeconds, double blockDurationSecs)
        : in_(in), offsetSeconds_(offsetSeconds), blockDurationSecs_(blockDurationSecs) {}

    /**
     * @brief Where in the block the host put an input event.
     *
     * The one answer the arp reads an input time by, so a forwarded message and
     * the notes generated around it share a timeline (#2415).
     *
     * @param index Index into the block's MIDI input.
     * @return Seconds from the block start: the event's timestamp with the
     *         host's sub-block offset added, inside the block it belongs to.
     */
    double timeOf(int index) const {
        return juce::jlimit(0.0, blockDurationSecs_,
                            in_.message(index).getTimeStamp() + offsetSeconds_);
    }

    /**
     * @brief Forwards everything up to and including @p timeInBlock.
     *
     * A message coincident with a generated note goes out first: a controller
     * moved on the beat belongs to the note that starts on it.
     *
     * @param midi Where the arp writes its output.
     * @param timeInBlock Seconds from the block start; nothing later is
     *                    forwarded yet.
     */
    void flushUpTo(DeviceMidiOutput& midi, double timeInBlock) {
        for (const int count = in_.size(); next_ < count; ++next_) {
            const auto& message = in_.message(next_);
            const double time = timeOf(next_);
            if (time > timeInBlock)
                return;
            // Channel messages only: getChannel() is 0 for SysEx and for
            // everything the transport carries.
            if (message.getChannel() == 0 || message.isNoteOnOrOff())
                continue;

            auto forwarded = message;  // short message: inline storage, no allocation
            forwarded.setChannel(1);
            forwarded.setTimeStamp(time);
            midi.addEvent({std::move(forwarded), in_.sourceId(next_)});
        }
    }

    /**
     * @brief Forwards whatever is left of the block.
     * @param midi Where the arp writes its output.
     */
    void flushRest(DeviceMidiOutput& midi) {
        flushUpTo(midi, std::numeric_limits<double>::max());
    }

  private:
    const DeviceMidiInput& in_;
    double offsetSeconds_ = 0.0;
    double blockDurationSecs_ = 0.0;
    int next_ = 0;
};

/// One slot's metadata. The ids, order and display ranges are pinned to what
/// the retired host-native plugin registered, because saved links address the
/// slots by index and projects store parameter values in display units.
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    info.paramIndex = index;

    switch (index) {
        case ArpeggiatorPlugin::kPattern:
            info.stableId = "pattern";
            info.name = "Pattern";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 5.0f;
            info.defaultValue = 0.0f;
            info.choices = {"Up", "Down", "Up/Down", "Down/Up", "Random", "As Played"};
            break;

        case ArpeggiatorPlugin::kRate:
            info.stableId = "rate";
            info.name = "Rate";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 9.0f;
            info.defaultValue = 4.0f;  // 1/8
            info.choices = {"1/4D", "1/4",   "1/4T", "1/8D",  "1/8",
                            "1/8T", "1/16D", "1/16", "1/16T", "1/32"};
            break;

        case ArpeggiatorPlugin::kOctaves:
            info.stableId = "octaves";
            info.name = "Octaves";
            info.minValue = 1.0f;
            info.maxValue = 4.0f;
            info.defaultValue = 1.0f;
            break;

        case ArpeggiatorPlugin::kGate:
            info.stableId = "gate";
            info.name = "Gate";
            info.minValue = 0.01f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.8f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case ArpeggiatorPlugin::kSwing:
            info.stableId = "swing";
            info.name = "Swing";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case ArpeggiatorPlugin::kRamp:
            info.stableId = "ramp";
            info.name = "Timing Depth";
            info.minValue = -1.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.bipolarModulation = true;
            break;

        case ArpeggiatorPlugin::kSkew:
            info.stableId = "skew";
            info.name = "Timing Skew";
            info.minValue = -1.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.bipolarModulation = true;
            break;

        case ArpeggiatorPlugin::kLatch:
            info.stableId = "latch";
            info.name = "Latch";
            info.scale = ParameterScale::Boolean;
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 0.0f;
            info.choices = {"Off", "On"};
            info.modulatable = false;
            break;

        case ArpeggiatorPlugin::kVelMode:
            info.stableId = "velmode";
            info.name = "Velocity Mode";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 2.0f;
            info.defaultValue = 0.0f;
            info.choices = {"Original", "Fixed", "Accent"};
            break;

        case ArpeggiatorPlugin::kFixedVel:
            info.stableId = "fixedvel";
            info.name = "Fixed Velocity";
            info.minValue = 1.0f;
            info.maxValue = 127.0f;
            info.defaultValue = 100.0f;
            break;

        default:
            break;
    }

    return info;
}

}  // namespace

ArpeggiatorPlugin::ArpeggiatorPlugin() {
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }
}

ArpeggiatorPlugin::~ArpeggiatorPlugin() = default;

ParameterInfo ArpeggiatorPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= kNumParams)
        return {};
    return slotInfo(index);
}

float ArpeggiatorPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void ArpeggiatorPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
}

float ArpeggiatorPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

int ArpeggiatorPlugin::displayIndex(int index) const {
    return juce::roundToInt(displayValue(index));
}

void ArpeggiatorPlugin::prepare(const DevicePrepareContext& context) {
    MidiMagdaDevice::prepare(context);
    resetArpState();
}

void ArpeggiatorPlugin::reset() {
    resetArpState();
    clearHeldNotes();
}

void ArpeggiatorPlugin::flushState(juce::ValueTree& state) {
    state.setProperty(SettingIDs::rampCycles, rampCycles.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::quantize, quantize.load(std::memory_order_relaxed), nullptr);
    state.setProperty(SettingIDs::quantizeSub, quantizeSub.load(std::memory_order_relaxed),
                      nullptr);
    state.setProperty(SettingIDs::hardAngle, hardAngle.load(std::memory_order_relaxed), nullptr);
}

void ArpeggiatorPlugin::restoreState(const juce::ValueTree& state) {
    if (const auto* value = state.getPropertyPointer(SettingIDs::rampCycles))
        rampCycles.store(static_cast<int>(*value), std::memory_order_relaxed);
    if (const auto* value = state.getPropertyPointer(SettingIDs::quantize))
        quantize.store(static_cast<float>(*value), std::memory_order_relaxed);
    if (const auto* value = state.getPropertyPointer(SettingIDs::quantizeSub))
        quantizeSub.store(static_cast<int>(*value), std::memory_order_relaxed);
    if (const auto* value = state.getPropertyPointer(SettingIDs::hardAngle))
        hardAngle.store(static_cast<bool>(*value), std::memory_order_relaxed);
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
    return ramp_curve::applyRampCurve(t, depth, skew, hardAngle);
}

void ArpeggiatorPlugin::addHeldNote(int noteNumber, int velocity, bool fromLiveSource,
                                    std::uint32_t sourceId) {
    for (int i = 0; i < heldCount_; ++i) {
        auto& held = heldNotes_[static_cast<size_t>(i)];
        if (held.noteNumber == noteNumber) {
            // The pattern already has this pitch; this is another holder of
            // it, not another note.
            ++(fromLiveSource ? held.liveHolds : held.hostHolds);
            (fromLiveSource ? held.liveSourceId : held.hostSourceId) = sourceId;
            return;
        }
    }
    if (heldCount_ < MAX_HELD) {
        heldNotes_[static_cast<size_t>(heldCount_)] = {noteNumber,
                                                       velocity,
                                                       nextOrder_++,
                                                       fromLiveSource ? 1 : 0,
                                                       fromLiveSource ? 0 : 1,
                                                       fromLiveSource ? sourceId : 0,
                                                       fromLiveSource ? 0 : sourceId};
        ++heldCount_;
    }
}

void ArpeggiatorPlugin::releaseHeldNote(int noteNumber, bool fromLiveSource,
                                        bool removeWhenUnheld) {
    for (int i = 0; i < heldCount_; ++i) {
        auto& held = heldNotes_[static_cast<size_t>(i)];
        if (held.noteNumber != noteNumber)
            continue;
        auto& holds = fromLiveSource ? held.liveHolds : held.hostHolds;
        if (holds > 0)
            --holds;
        if (removeWhenUnheld && held.liveHolds == 0 && held.hostHolds == 0)
            removeHeldNoteAt(i);
        return;
    }
}

void ArpeggiatorPlugin::retainLiveHeldNotes() {
    int kept = 0;
    int holds = 0;
    for (int i = 0; i < heldCount_; ++i) {
        auto note = heldNotes_[static_cast<size_t>(i)];
        if (note.liveHolds == 0)
            continue;
        // Whatever the host was holding it is withdrawing, and re-asserts on
        // the same buffer what should still sound.
        note.hostHolds = 0;
        note.hostSourceId = 0;
        holds += note.liveHolds;
        heldNotes_[static_cast<size_t>(kept++)] = note;
    }
    heldCount_ = kept;
    // Keys, not pitches: two inputs on one pitch stay held when one lets go.
    physicallyHeldCount_ = holds;
    latchedSetStale_ = false;
    if (kept == 0)
        nextOrder_ = 0;
}

void ArpeggiatorPlugin::removeHeldNoteAt(int index) {
    // Swap with last
    if (index < heldCount_ - 1)
        heldNotes_[static_cast<size_t>(index)] = heldNotes_[static_cast<size_t>(heldCount_ - 1)];
    --heldCount_;
}

void ArpeggiatorPlugin::clearHeldNotes() {
    heldCount_ = 0;
    physicallyHeldCount_ = 0;
    latchedSetStale_ = false;
    nextOrder_ = 0;
}

int ArpeggiatorPlugin::takeSoundingNote() {
    const int note = lastPlayedNote_;
    lastPlayedNote_ = -1;
    lastNoteOffBeat_ = -1.0;
    clearMidiOutDisplay();
    return note;
}

int ArpeggiatorPlugin::resetArpState() {
    const int note = takeSoundingNote();
    currentStep_ = 0;
    arpOriginBeat_ = -1.0;
    lastBlockEndBeat_ = -1.0;
    wasPlaying_ = false;
    currentPlayStep_.store(-1, std::memory_order_relaxed);
    currentSeqLength_.store(0, std::memory_order_relaxed);
    return note;
}

ArpeggiatorPlugin::ExpandedSequence ArpeggiatorPlugin::buildSequence() const {
    ExpandedSequence seq;
    if (heldCount_ == 0)
        return seq;

    auto pat = static_cast<Pattern>(displayIndex(kPattern));
    int octaves = juce::jlimit(1, 4, displayIndex(kOctaves));

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
            auto expanded = sorted[static_cast<size_t>(i)];
            expanded.noteNumber = note;
            seq.notes[static_cast<size_t>(seq.length)] = expanded;
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

/**
 * @brief What one process() call holds still.
 *
 * The parameters are read once, here: a block is one position for every slot,
 * whatever the input does inside it. What changes inside a block is what the
 * player holds, and that belongs to the stretches below.
 */
struct ArpeggiatorPlugin::BlockScope {
    DeviceProcessContext& context;
    const DeviceMidiInput& in;
    DeviceMidiOutput& midi;
    NonNoteForwarder forward;

    double blockDurationSecs = 0.0;
    bool isLatched = false;

    /// The transport's clock, against the free-running one a stopped transport
    /// leaves the arp on.
    bool onTransportClock = false;
    double blockStartBeat = 0.0;
    double blockEndBeat = 0.0;

    Pattern pattern = Pattern::Up;
    double stepBeats = 0.5;
    float gate = 1.0f;
    float swing = 0.0f;
    float ramp = 0.0f;
    float skew = 0.0f;
    VelocityMode velocityMode = VelocityMode::Original;
    int fixedVelocity = 100;
    float quantizeAmount = 0.0f;
    int quantizeSubdivisions = 16;
    bool hardAngleCurve = false;
    int rampCycleCount = 1;

    /**
     * @brief Where in the block the host put an input event.
     * @param index Index into the block's MIDI input.
     * @return Seconds from the block start.
     */
    double eventTime(int index) const {
        return forward.timeOf(index);
    }

    /**
     * @brief Emits an event the arp generated, behind the non-note traffic that
     *        precedes it.
     * @param message The event; its timestamp is set from @p timeInBlock.
     * @param timeInBlock Seconds from the block start.
     * @param sourceId The provenance to stamp it with.
     */
    void addTimedMessage(juce::MidiMessage message, double timeInBlock, std::uint32_t sourceId) {
        forward.flushUpTo(midi, timeInBlock);
        message.setTimeStamp(timeInBlock);
        midi.addEvent({std::move(message), sourceId});
    }

    /**
     * @brief Emits a note-off, or nothing when @p noteNumber is negative.
     * @param noteNumber The note to close, or -1 for none.
     * @param timeInBlock Seconds from the block start.
     * @param sourceId The provenance its note-on carried.
     */
    void emitNoteOff(int noteNumber, double timeInBlock, std::uint32_t sourceId) {
        if (noteNumber < 0)
            return;
        forward.flushUpTo(midi, timeInBlock);
        sendNoteOff(midi, noteNumber, sourceId, timeInBlock);
    }
};

/**
 * @brief One stretch of a block over which the held notes do not change.
 *
 * The block is cut where the input changes what is held (#2415) and each piece
 * walks its own steps against its own span, so a step plays the chord that was
 * down when it sounded.
 */
struct ArpeggiatorPlugin::Stretch {
    const BlockScope& block;

    double startSecs = 0.0;
    double endSecs = 0.0;
    double durationSecs = 0.0;
    double startBeat = 0.0;
    double endBeat = 0.0;

    /// The grid the walk is anchored to, and the notes it steps through.
    double originBeat = 0.0;
    ExpandedSequence seq;
    /// One full pass through the sequence.
    double cycleBeats = 0.0;

    /**
     * @brief Where a beat inside this stretch lands in the block.
     * @param beat A beat between startBeat and endBeat.
     * @return Seconds from the block start.
     */
    double timeOf(double beat) const {
        return startSecs + (beat - startBeat) / (endBeat - startBeat) * durationSecs;
    }

    /**
     * @brief The grid position of a step, warped by the ramp curve inside its
     *        cycle. Without ramp, steps are evenly spaced.
     * @param step Global step index.
     * @return The beat the step sits on before swing and quantize.
     */
    double gridBeat(int step) const {
        const int cycle = step / seq.length;
        const int stepInCycle = step % seq.length;
        const double cycleStart = originBeat + cycle * cycleBeats;

        if (std::abs(block.ramp) > 0.001f && seq.length > 1) {
            const double tLinear =
                static_cast<double>(stepInCycle) / static_cast<double>(seq.length);
            const double tCurved = ramp_curve::applyRampCurveWithCycles(
                tLinear, block.ramp, block.skew, block.rampCycleCount, block.hardAngleCurve);
            return cycleStart + tCurved * cycleBeats;
        }
        return cycleStart + stepInCycle * block.stepBeats;
    }

    /**
     * @brief Where the step is actually played: swing on odd steps, then
     *        quantize pulling that toward a regular grid.
     *
     * Neither can carry a step past its neighbour, so the walk keys on this
     * instead of the raw grid and a step warped past the end of a stretch stays
     * pending rather than being consumed unplayed (#2362).
     *
     * @param step Global step index.
     * @return The beat the step is played on.
     */
    double warpedStepBeat(int step) const {
        double beat = gridBeat(step);
        // Half the gap to the next step, not half the nominal rate: Time Bend
        // can compress two steps onto one beat, and a fixed offset would then
        // swing the odd one past the even one that follows it.
        if (step % 2 == 1 && block.swing > 0.0f)
            beat += static_cast<double>(block.swing) * (gridBeat(step + 1) - beat) * 0.5;

        if (block.quantizeAmount > 0.0f && block.quantizeSubdivisions > 0) {
            const double gridSpacing = cycleBeats / static_cast<double>(block.quantizeSubdivisions);
            const double snapped = std::round(beat / gridSpacing) * gridSpacing;
            beat += (snapped - beat) * static_cast<double>(block.quantizeAmount);
        }
        return beat;
    }
};

void ArpeggiatorPlugin::closeSoundingNote(BlockScope& block, double timeInBlock) {
    const std::uint32_t source = lastPlayedSourceId_;
    block.emitNoteOff(takeSoundingNote(), timeInBlock, source);
}

void ArpeggiatorPlugin::restartAt(BlockScope& block, double timeInBlock) {
    const std::uint32_t source = lastPlayedSourceId_;
    block.emitNoteOff(resetArpState(), timeInBlock, source);
    freeRunSamples_ = 0.0;
}

void ArpeggiatorPlugin::applyInputEvent(BlockScope& block, int index, double timeInBlock) {
    const auto& msg = block.in.message(index);
    const auto sourceId = block.in.sourceId(index);
    const bool fromLiveSource = isLiveSource(block.context, sourceId);

    if (msg.isNoteOn()) {
        ++physicallyHeldCount_;

        // Latch: if old set is stale (all keys were released), clear before adding
        if (block.isLatched && latchedSetStale_) {
            heldCount_ = 0;
            nextOrder_ = 0;
            latchedSetStale_ = false;
        }

        const bool wasEmpty = (heldCount_ == 0);
        addHeldNote(msg.getNoteNumber(), msg.getVelocity(), fromLiveSource, sourceId);
        if (wasEmpty && heldCount_ > 0)
            restartAt(block, timeInBlock);
    } else if (msg.isNoteOff()) {
        --physicallyHeldCount_;
        if (physicallyHeldCount_ < 0)
            physicallyHeldCount_ = 0;

        // Latch keeps the note in the pattern and only marks the set stale once
        // every key is up.
        releaseHeldNote(msg.getNoteNumber(), fromLiveSource, !block.isLatched);
        if (block.isLatched && physicallyHeldCount_ == 0)
            latchedSetStale_ = true;
    } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
        clearHeldNotes();
        restartAt(block, timeInBlock);
    }
}

void ArpeggiatorPlugin::walkSteps(BlockScope& block, const Stretch& stretch) {
    // Catch up to the stretch. Cycles are evenly spaced whatever the warp does
    // inside one, so the cycle is a division and only its steps are walked:
    // bounded work, not one pass per skipped step.
    if (stretch.warpedStepBeat(currentStep_) < stretch.startBeat) {
        // Warp carries a step across the cycle boundary either way, so the
        // division lands a cycle early and the walk closes the rest.
        const int cycle = static_cast<int>(std::floor((stretch.startBeat - stretch.originBeat) /
                                                      stretch.cycleBeats)) -
                          1;
        currentStep_ = std::max(currentStep_, cycle * stretch.seq.length);
        while (stretch.warpedStepBeat(currentStep_) < stretch.startBeat)
            ++currentStep_;
    }

    for (;;) {
        const double stepBeat = stretch.warpedStepBeat(currentStep_);
        // Left where it is rather than consumed: a step the warp moved past this
        // stretch is played by the stretch that holds it, so the same input
        // plays the same notes however the host cuts its callbacks up (#2415).
        if (stepBeat >= stretch.endBeat)
            break;

        // Never ahead of the stretch it is played in: the catch-up above leaves
        // the walk on a step at or after the start, and a sequence rebuilt
        // mid-block can only move one closer to it.
        const double timeInBlock = std::max(stretch.startSecs, stretch.timeOf(stepBeat));

        // Note-off for previous note
        if (lastPlayedNote_ >= 0) {
            block.addTimedMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), timeInBlock,
                                  lastPlayedSourceId_);
            lastPlayedNote_ = -1;
        }

        // Determine which note to play
        int stepIdx;
        if (block.pattern == Pattern::Random) {
            stepIdx = arpRandom_.nextInt(stretch.seq.length);
        } else {
            stepIdx = currentStep_ % stretch.seq.length;
        }

        const auto& note = stretch.seq.notes[static_cast<size_t>(stepIdx)];

        // Determine velocity
        int vel = note.velocity;
        if (block.velocityMode == VelocityMode::Fixed) {
            vel = block.fixedVelocity;
        } else if (block.velocityMode == VelocityMode::Accent) {
            vel = (currentStep_ % 4 == 0) ? juce::jmin(127, note.velocity + 30) : note.velocity;
        }

        // Note-on, carrying the provenance of whoever holds the pitch, so a
        // device behind this one reads it the way this one does (#2416).
        const std::uint32_t noteSource = note.liveHolds > 0 ? note.liveSourceId : note.hostSourceId;
        block.addTimedMessage(
            juce::MidiMessage::noteOn(1, note.noteNumber, static_cast<juce::uint8>(vel)),
            timeInBlock, noteSource);

        lastPlayedNote_ = note.noteNumber;
        lastPlayedSourceId_ = noteSource;
        setMidiOutDisplay(note.noteNumber, vel);

        // Schedule note-off
        const double noteOffBeat = stepBeat + block.stepBeats * static_cast<double>(block.gate);
        if (noteOffBeat < stretch.endBeat) {
            block.addTimedMessage(juce::MidiMessage::noteOff(1, note.noteNumber),
                                  stretch.timeOf(noteOffBeat), noteSource);
            lastPlayedNote_ = -1;
            lastNoteOffBeat_ = -1.0;
            clearMidiOutDisplay();
        } else {
            // Note-off in a later stretch, or a later block
            lastNoteOffBeat_ = noteOffBeat;
        }

        ++currentStep_;
        currentPlayStep_.store(currentStep_ % stretch.seq.length, std::memory_order_relaxed);
        currentSeqLength_.store(stretch.seq.length, std::memory_order_relaxed);
    }
}

void ArpeggiatorPlugin::playStretch(BlockScope& block, double startSecs, double endSecs) {
    // Nothing to play: close what is sounding and idle the clock.
    if (heldCount_ == 0 || (!block.context.isPlaying && physicallyHeldCount_ <= 0)) {
        closeSoundingNote(block, startSecs);
        freeRunSamples_ = 0.0;
        lastBlockEndBeat_ = -1.0;
        return;
    }

    Stretch stretch{.block = block};
    stretch.startSecs = startSecs;
    stretch.endSecs = endSecs;
    stretch.durationSecs = endSecs - startSecs;
    if (stretch.durationSecs <= 0.0)
        return;

    if (block.onTransportClock) {
        // The block's span, divided the way the output timestamps are computed:
        // a beat and a time within the block are each other's inverse.
        const double beats = block.blockEndBeat - block.blockStartBeat;
        stretch.startBeat = block.blockStartBeat + (startSecs / block.blockDurationSecs) * beats;
        stretch.endBeat = block.blockStartBeat + (endSecs / block.blockDurationSecs) * beats;
    } else {
        // Free-running clock — get tempo from timeline position 0
        const double bpm =
            block.context.tempoMap != nullptr ? block.context.tempoMap->bpmAtSeconds(0.0) : 120.0;
        const double beatsPerSample = bpm / (60.0 * sampleRate_);
        stretch.startBeat = freeRunSamples_ * beatsPerSample;
        freeRunSamples_ += stretch.durationSecs * sampleRate_;
        stretch.endBeat = freeRunSamples_ * beatsPerSample;
    }

    if (stretch.endBeat <= stretch.startBeat)
        return;

    // Seeks, loop wraps and the switch between the two clocks all arrive as a
    // stretch that does not continue the last one. Only some of them reach the
    // device as a panic and a loop wrap reaches it as nothing at all, so the
    // timeline is what the walk trusts (#2416).
    constexpr double kContiguousBeats = 1.0e-3;
    if (lastBlockEndBeat_ < 0.0 ||
        std::abs(stretch.startBeat - lastBlockEndBeat_) > kContiguousBeats) {
        closeSoundingNote(block, startSecs);
        currentStep_ = 0;
        arpOriginBeat_ = -1.0;
    }
    lastBlockEndBeat_ = stretch.endBeat;

    stretch.seq = buildSequence();
    if (stretch.seq.length == 0)
        return;
    stretch.cycleBeats = stretch.seq.length * block.stepBeats;

    // Anchor on the grid at the point the pattern starts from, which is where
    // the chord arrived rather than wherever its block began.
    if (arpOriginBeat_ < 0.0)
        arpOriginBeat_ = std::floor(stretch.startBeat / block.stepBeats) * block.stepBeats;
    stretch.originBeat = arpOriginBeat_;

    // The note-off an earlier stretch scheduled into this one.
    if (lastPlayedNote_ >= 0 && lastNoteOffBeat_ >= 0.0 && lastNoteOffBeat_ < stretch.endBeat) {
        block.addTimedMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_),
                              std::max(startSecs, stretch.timeOf(lastNoteOffBeat_)),
                              lastPlayedSourceId_);
        lastPlayedNote_ = -1;
        lastNoteOffBeat_ = -1.0;
        clearMidiOutDisplay();
    }

    walkSteps(block, stretch);
}

void ArpeggiatorPlugin::process(DeviceProcessContext& context) {
    if (context.midiIn == nullptr || context.midiOut == nullptr || context.numSamples <= 0)
        return;

    // The host pushed the modulated slot positions before this call; publish
    // the Time Bend pair for the UI curve display (see displayedRamp_).
    displayedRamp_.store(displayValue(kRamp), std::memory_order_relaxed);
    displayedSkew_.store(displayValue(kSkew), std::memory_order_relaxed);

    const double blockDurationSecs = static_cast<double>(context.numSamples) / sampleRate_;
    const bool onTransportClock = context.isPlaying && context.tempoMap != nullptr;

    BlockScope block{
        .context = context,
        .in = *context.midiIn,
        .midi = *context.midiOut,
        .forward =
            NonNoteForwarder(*context.midiIn, context.midiTimeOffsetSeconds, blockDurationSecs),
        .blockDurationSecs = blockDurationSecs,
        .isLatched = displayValue(kLatch) >= 0.5f,
        .onTransportClock = onTransportClock,
        .blockStartBeat =
            onTransportClock ? context.tempoMap->beatsAtSeconds(context.timelineStartSeconds) : 0.0,
        .blockEndBeat =
            onTransportClock ? context.tempoMap->beatsAtSeconds(context.timelineEndSeconds) : 0.0,
        .pattern = static_cast<Pattern>(displayIndex(kPattern)),
        .stepBeats = rateToBeats(static_cast<Rate>(displayIndex(kRate))),
        .gate = juce::jlimit(0.01f, 1.0f, displayValue(kGate)),
        .swing = juce::jlimit(0.0f, 1.0f, displayValue(kSwing)),
        .ramp = juce::jlimit(-1.0f, 1.0f, displayValue(kRamp)),
        .skew = juce::jlimit(-1.0f, 1.0f, displayValue(kSkew)),
        .velocityMode = static_cast<VelocityMode>(displayIndex(kVelMode)),
        .fixedVelocity = juce::jlimit(1, 127, displayIndex(kFixedVel)),
        .quantizeAmount = juce::jlimit(0.0f, 1.0f, quantize.load(std::memory_order_relaxed)),
        .quantizeSubdivisions = juce::jlimit(16, 512, quantizeSub.load(std::memory_order_relaxed)),
        .hardAngleCurve = hardAngle.load(std::memory_order_relaxed),
        .rampCycleCount = juce::jlimit(1, 8, rampCycles.load(std::memory_order_relaxed)),
    };

    // What the arp passes on rather than consumes, emitted beside the notes it
    // generates. The guard covers the returns below: a block the arp leaves
    // early is still a block the instrument behind it is owed its pedal on.
    const juce::ScopeGuard forwardRest{[&] { block.forward.flushRest(block.midi); }};

    // --- 1. The buffer's panic, which the host raises for the block ---
    // Hosts raise this without a CC event, on a playhead jump or a track they
    // just muted, and then re-assert whatever should be sounding at the new
    // position without a note-off for what should not. So the host's notes go
    // and come back on the same buffer, while keys proven to be a player's are
    // not the host's to withdraw (#2416).
    const bool inputPanic = block.in.isAllNotesOff();
    block.midi.setAllNotesOff(inputPanic);
    if (inputPanic) {
        closeSoundingNote(block, 0.0);
        retainLiveHeldNotes();
    }

    // --- 2. Transport transitions ---
    if (context.isPlaying && !wasPlaying_) {
        // The transport and the free-running clock are different clocks, so the
        // walk re-anchors below instead of carrying its step across.
        lastBlockEndBeat_ = -1.0;
    } else if (!context.isPlaying && wasPlaying_) {
        // Keys under the player's fingers keep the arp free-running; notes a
        // clip left behind are dropped, because their note-off is never coming
        // once the transport stops (#2416).
        closeSoundingNote(block, 0.0);
        retainLiveHeldNotes();
        resetArpState();
        freeRunSamples_ = 0.0;
    }

    // --- 3. The block, cut where the input changes what is held ---
    // The pattern the arp plays from a given instant is the one its input had
    // at that instant: a latch replacement or a panic nine tenths of the way
    // through a block leaves the steps before it alone (#2415).
    const int incomingCount = block.in.size();
    int nextEvent = 0;
    double stretchStartSecs = 0.0;
    for (;;) {
        // Everything the host stamped at this instant, before anything is
        // played for it.
        while (nextEvent < incomingCount) {
            const auto& msg = block.in.message(nextEvent);
            const bool changesPattern =
                msg.isNoteOnOrOff() || msg.isAllNotesOff() || msg.isAllSoundOff();
            if (!changesPattern) {
                ++nextEvent;  // the forwarder carries it, in its own time
                continue;
            }
            if (block.eventTime(nextEvent) > stretchStartSecs)
                break;
            applyInputEvent(block, nextEvent, stretchStartSecs);
            ++nextEvent;
        }

        playStretch(block, stretchStartSecs,
                    nextEvent < incomingCount ? block.eventTime(nextEvent) : blockDurationSecs);

        // Input stamped at or past the block end still changes what the next
        // block holds; it just has no stretch of this one left to play in.
        if (nextEvent >= incomingCount)
            break;
        stretchStartSecs = block.eventTime(nextEvent);
    }

    // Re-asserted rather than left to whichever input event ran last: a chord
    // arriving mid-block resets the walk, and the walk's reset clears this too.
    wasPlaying_ = context.isPlaying;
}

}  // namespace magda::daw::audio
