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

/// What the arp does not consume, copied onto its output in timestamp order
/// (#2417).
///
/// Notes are the device's material: they go in and an arpeggio comes out.
/// Everything else the channel carries -- mod wheel, expression, sustain,
/// bend, aftertouch, program change -- is addressed to the instrument behind
/// this one, and reaches it nowhere else: thru carries the held chord too, so
/// a track that wants the pedal cannot have it without the notes under it.
///
/// Forwarded on channel 1, the one the generated notes are on, so an
/// instrument that keeps per-channel state applies them to what it is
/// playing. SysEx is not forwarded: copying one allocates on the audio thread
/// (JUCE holds anything past eight bytes on the heap), and it addresses a
/// device rather than the notes.
class NonNoteForwarder {
  public:
    explicit NonNoteForwarder(const DeviceMidiInput& in) : in_(in) {}

    /// Everything up to and including @p timeInBlock. A message coincident
    /// with a generated note goes out first: a controller moved on the beat
    /// belongs to the note that starts on it.
    void flushUpTo(DeviceMidiOutput& midi, double timeInBlock) {
        for (const int count = in_.size(); next_ < count; ++next_) {
            const auto& message = in_.message(next_);
            if (message.getTimeStamp() > timeInBlock)
                return;
            // Channel messages only: getChannel() is 0 for SysEx and for
            // everything the transport carries.
            if (message.getChannel() == 0 || message.isNoteOnOrOff())
                continue;

            auto forwarded = message;  // short message: inline storage, no allocation
            forwarded.setChannel(1);
            midi.addEvent({std::move(forwarded), in_.sourceId(next_)});
        }
    }

    void flushRest(DeviceMidiOutput& midi) {
        flushUpTo(midi, std::numeric_limits<double>::max());
    }

  private:
    const DeviceMidiInput& in_;
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

void ArpeggiatorPlugin::sendAllNotesOff(DeviceMidiOutput& midi) {
    const std::uint32_t source = lastPlayedSourceId_;
    sendNoteOff(midi, takeSoundingNote(), source);
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

void ArpeggiatorPlugin::process(DeviceProcessContext& context) {
    if (context.midiIn == nullptr || context.midiOut == nullptr || context.numSamples <= 0)
        return;

    // The host pushed the modulated slot positions before this call; publish
    // the Time Bend pair for the UI curve display (see displayedRamp_).
    displayedRamp_.store(displayValue(kRamp), std::memory_order_relaxed);
    displayedSkew_.store(displayValue(kSkew), std::memory_order_relaxed);

    const auto& in = *context.midiIn;
    auto& midi = *context.midiOut;
    const bool isLatched = displayValue(kLatch) >= 0.5f;

    // What the arp passes on rather than consumes, emitted beside the notes it
    // generates. The guard covers the returns below: a block the arp leaves
    // early is still a block the instrument behind it is owed its pedal on.
    NonNoteForwarder forward(in);
    const juce::ScopeGuard forwardRest{[&] { forward.flushRest(midi); }};

    // --- 1. Capture incoming MIDI ---
    // Only one note can be sounding on entry, and this pass generates none, so
    // one pending note-off is enough.
    int pendingNoteOff = -1;
    const auto resetFromInput = [&] {
        const int note = resetArpState();
        if (note >= 0)
            pendingNoteOff = note;
        freeRunSamples_ = 0.0;
    };
    // Hosts raise this without a CC event, on a playhead jump or a track they
    // just muted, and then re-assert whatever should be sounding at the new
    // position without a note-off for what should not. So the host's notes go
    // and come back on the same buffer, while keys proven to be a player's are
    // not the host's to withdraw (#2416).
    const bool inputPanic = in.isAllNotesOff();
    if (inputPanic) {
        pendingNoteOff = takeSoundingNote();
        retainLiveHeldNotes();
    }
    const int incomingCount = in.size();
    for (int eventIndex = 0; eventIndex < incomingCount; ++eventIndex) {
        const auto& msg = in.message(eventIndex);
        const bool fromLiveSource = isLiveSource(context, in.sourceId(eventIndex));
        if (msg.isNoteOn()) {
            ++physicallyHeldCount_;

            // Latch: if old set is stale (all keys were released), clear before adding
            if (isLatched && latchedSetStale_) {
                heldCount_ = 0;
                nextOrder_ = 0;
                latchedSetStale_ = false;
            }

            bool wasEmpty = (heldCount_ == 0);
            addHeldNote(msg.getNoteNumber(), msg.getVelocity(), fromLiveSource,
                        in.sourceId(eventIndex));
            // Reset free-running clock when first note arrives
            if (wasEmpty && heldCount_ > 0) {
                resetFromInput();
            }
        } else if (msg.isNoteOff()) {
            --physicallyHeldCount_;
            if (physicallyHeldCount_ < 0)
                physicallyHeldCount_ = 0;

            // Latch keeps the note in the pattern and only marks the set
            // stale once every key is up.
            releaseHeldNote(msg.getNoteNumber(), fromLiveSource, !isLatched);
            if (isLatched && physicallyHeldCount_ == 0)
                latchedSetStale_ = true;
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            clearHeldNotes();
            resetFromInput();
        }
    }

    // --- 2. Pass the panic on and close the note the input pass ended ---
    // One flush covers every note-off below that lands at the top of the
    // block, since none of them advances past that instant.
    midi.setAllNotesOff(inputPanic);
    forward.flushUpTo(midi, 0.0);
    sendNoteOff(midi, pendingNoteOff, lastPlayedSourceId_);

    // --- 3. Handle transport transitions ---
    if (context.isPlaying && !wasPlaying_) {
        // The transport and the free-running clock are different clocks, so
        // the walk re-anchors below instead of carrying its step across.
        wasPlaying_ = true;
        lastBlockEndBeat_ = -1.0;
    } else if (!context.isPlaying && wasPlaying_) {
        // Keys under the player's fingers keep the arp free-running; notes a
        // clip left behind are dropped, because their note-off is never
        // coming once the transport stops (#2416).
        sendAllNotesOff(midi);
        retainLiveHeldNotes();
        resetArpState();
        freeRunSamples_ = 0.0;
    }

    // --- 4. No held notes, or no MIDI input while transport is stopped? ---
    if (heldCount_ == 0 || (!context.isPlaying && physicallyHeldCount_ <= 0)) {
        sendAllNotesOff(midi);
        freeRunSamples_ = 0.0;
        lastBlockEndBeat_ = -1.0;
        return;
    }

    // --- 5. Get beat positions ---
    double blockStartBeat, blockEndBeat;

    if (context.isPlaying && context.tempoMap != nullptr) {
        // Use transport position
        blockStartBeat = context.tempoMap->beatsAtSeconds(context.timelineStartSeconds);
        blockEndBeat = context.tempoMap->beatsAtSeconds(context.timelineEndSeconds);
    } else {
        // Free-running clock — get tempo from timeline position 0
        const double bpm =
            context.tempoMap != nullptr ? context.tempoMap->bpmAtSeconds(0.0) : 120.0;
        double beatsPerSample = bpm / (60.0 * sampleRate_);
        blockStartBeat = freeRunSamples_ * beatsPerSample;
        freeRunSamples_ += context.numSamples;
        blockEndBeat = freeRunSamples_ * beatsPerSample;
    }

    if (blockEndBeat <= blockStartBeat)
        return;

    // Seeks, loop wraps and the switch between the two clocks all arrive as a
    // block that does not continue the last one. Only some of them reach the
    // device as a panic and a loop wrap reaches it as nothing at all, so the
    // timeline is what the walk trusts (#2416).
    constexpr double kContiguousBeats = 1.0e-3;
    if (lastBlockEndBeat_ < 0.0 ||
        std::abs(blockStartBeat - lastBlockEndBeat_) > kContiguousBeats) {
        const std::uint32_t source = lastPlayedSourceId_;
        sendNoteOff(midi, takeSoundingNote(), source);
        currentStep_ = 0;
        arpOriginBeat_ = -1.0;
    }
    lastBlockEndBeat_ = blockEndBeat;

    // --- 6. Build note sequence ---
    auto seq = buildSequence();
    if (seq.length == 0)
        return;

    auto pat = static_cast<Pattern>(displayIndex(kPattern));
    auto currentRate = static_cast<Rate>(displayIndex(kRate));
    double stepBeats = rateToBeats(currentRate);
    float gateVal = juce::jlimit(0.01f, 1.0f, displayValue(kGate));
    float swingVal = juce::jlimit(0.0f, 1.0f, displayValue(kSwing));
    float rampVal = juce::jlimit(-1.0f, 1.0f, displayValue(kRamp));
    float skewVal = juce::jlimit(-1.0f, 1.0f, displayValue(kSkew));
    auto velMode = static_cast<VelocityMode>(displayIndex(kVelMode));
    int fixedVel = juce::jlimit(1, 127, displayIndex(kFixedVel));
    float quantizeAmount = juce::jlimit(0.0f, 1.0f, quantize.load(std::memory_order_relaxed));
    int quantizeSubVal = juce::jlimit(16, 512, quantizeSub.load(std::memory_order_relaxed));
    bool hardAngleVal = hardAngle.load(std::memory_order_relaxed);

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
            int cyc = juce::jlimit(1, 8, rampCycles.load(std::memory_order_relaxed));
            double tLinear = static_cast<double>(stepInCycle) / static_cast<double>(seq.length);
            double tCurved =
                ramp_curve::applyRampCurveWithCycles(tLinear, rampVal, skewVal, cyc, hardAngleVal);
            return cycleStart + tCurved * cycleBeats;
        }
        return cycleStart + stepInCycle * stepBeats;
    };

    // Block duration in seconds (MIDI timestamps stay in seconds, not samples)
    double blockDurationSecs = static_cast<double>(context.numSamples) / sampleRate_;

    const auto addTimedMessage = [&](juce::MidiMessage message, double timeInBlock,
                                     std::uint32_t sourceId) {
        forward.flushUpTo(midi, timeInBlock);
        message.setTimeStamp(timeInBlock);
        midi.addEvent({std::move(message), sourceId});
    };

    // --- 7. Emit pending note-off from previous block ---
    if (lastNoteOffBeat_ >= blockStartBeat && lastNoteOffBeat_ < blockEndBeat &&
        lastPlayedNote_ >= 0) {
        double frac = (lastNoteOffBeat_ - blockStartBeat) / (blockEndBeat - blockStartBeat);
        addTimedMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), frac * blockDurationSecs,
                        lastPlayedSourceId_);
        lastPlayedNote_ = -1;
        lastNoteOffBeat_ = -1.0;
        clearMidiOutDisplay();
    }

    // --- 8. Walk steps and generate notes ---
    // Catch up to the block. Cycles are evenly spaced whatever the ramp does
    // inside one, so the cycle holding the block start is a division and only
    // its steps are walked: bounded work, not one pass per skipped step.
    if (computeStepBeat(currentStep_) < blockStartBeat) {
        const int cycle =
            static_cast<int>(std::floor((blockStartBeat - arpOriginBeat_) / cycleBeats));
        currentStep_ = std::max(currentStep_, cycle * seq.length);
        while (computeStepBeat(currentStep_) < blockStartBeat)
            ++currentStep_;
    }

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
                addTimedMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_), timeInBlock,
                                lastPlayedSourceId_);
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

            // Note-on, carrying the provenance of whoever holds the pitch, so
            // a device behind this one reads it the way this one does (#2416).
            const std::uint32_t noteSource =
                note.liveHolds > 0 ? note.liveSourceId : note.hostSourceId;
            addTimedMessage(
                juce::MidiMessage::noteOn(1, note.noteNumber, static_cast<juce::uint8>(vel)),
                timeInBlock, noteSource);

            lastPlayedNote_ = note.noteNumber;
            lastPlayedSourceId_ = noteSource;
            setMidiOutDisplay(note.noteNumber, vel);

            // Schedule note-off
            double noteOffBeat = swungBeat + stepBeats * static_cast<double>(gateVal);
            if (noteOffBeat < blockEndBeat) {
                double offFrac = (noteOffBeat - blockStartBeat) / (blockEndBeat - blockStartBeat);
                addTimedMessage(juce::MidiMessage::noteOff(1, note.noteNumber),
                                offFrac * blockDurationSecs, noteSource);
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
