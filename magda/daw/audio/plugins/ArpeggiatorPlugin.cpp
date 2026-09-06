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
    NonNoteForwarder(const DeviceMidiInput& in, double offsetSeconds)
        : in_(in), offsetSeconds_(offsetSeconds) {}

    /// Everything up to and including @p timeInBlock. A message coincident
    /// with a generated note goes out first: a controller moved on the beat
    /// belongs to the note that starts on it.
    void flushUpTo(DeviceMidiOutput& midi, double timeInBlock) {
        for (const int count = in_.size(); next_ < count; ++next_) {
            const auto& message = in_.message(next_);
            if (message.getTimeStamp() + offsetSeconds_ > timeInBlock)
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
    /// The host's sub-block offset, added to every input timestamp the way the
    /// arp adds it when placing the same events on its own timeline (#2415).
    double offsetSeconds_ = 0.0;
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
    const double blockDurationSecs = static_cast<double>(context.numSamples) / sampleRate_;

    // What the arp passes on rather than consumes, emitted beside the notes it
    // generates. The guard covers the returns below: a block the arp leaves
    // early is still a block the instrument behind it is owed its pedal on.
    NonNoteForwarder forward(in, context.midiTimeOffsetSeconds);
    const juce::ScopeGuard forwardRest{[&] { forward.flushRest(midi); }};

    const auto addTimedMessage = [&](juce::MidiMessage message, double timeInBlock,
                                     std::uint32_t sourceId) {
        forward.flushUpTo(midi, timeInBlock);
        message.setTimeStamp(timeInBlock);
        midi.addEvent({std::move(message), sourceId});
    };

    /// A note-off at its own instant in the block, behind the non-note traffic
    /// that precedes it (#2415).
    const auto emitNoteOff = [&](int noteNumber, double timeInBlock, std::uint32_t source) {
        if (noteNumber < 0)
            return;
        forward.flushUpTo(midi, timeInBlock);
        sendNoteOff(midi, noteNumber, source, timeInBlock);
    };

    /// Closes the note the arp is sounding, wherever it is being closed.
    const auto closeSoundingNote = [&](double timeInBlock) {
        const std::uint32_t source = lastPlayedSourceId_;
        emitNoteOff(takeSoundingNote(), timeInBlock, source);
    };

    // --- 1. The buffer's panic, which the host raises for the block ---
    // Hosts raise this without a CC event, on a playhead jump or a track they
    // just muted, and then re-assert whatever should be sounding at the new
    // position without a note-off for what should not. So the host's notes go
    // and come back on the same buffer, while keys proven to be a player's are
    // not the host's to withdraw (#2416).
    const bool inputPanic = in.isAllNotesOff();
    midi.setAllNotesOff(inputPanic);
    if (inputPanic) {
        closeSoundingNote(0.0);
        retainLiveHeldNotes();
    }

    // --- 2. Transport transitions ---
    if (context.isPlaying && !wasPlaying_) {
        // The transport and the free-running clock are different clocks, so
        // the walk re-anchors below instead of carrying its step across.
        lastBlockEndBeat_ = -1.0;
    } else if (!context.isPlaying && wasPlaying_) {
        // Keys under the player's fingers keep the arp free-running; notes a
        // clip left behind are dropped, because their note-off is never
        // coming once the transport stops (#2416).
        closeSoundingNote(0.0);
        retainLiveHeldNotes();
        resetArpState();
        freeRunSamples_ = 0.0;
    }

    // --- 3. Where the input sits in the block ---
    const auto eventTime = [&](int index) {
        return juce::jlimit(0.0, blockDurationSecs,
                            in.message(index).getTimeStamp() + context.midiTimeOffsetSeconds);
    };

    /// The pattern's material changes here, so the block is cut here.
    const auto changesPattern = [](const juce::MidiMessage& message) {
        return message.isNoteOnOrOff() || message.isAllNotesOff() || message.isAllSoundOff();
    };

    /// Everything the arp was doing, dropped where the input dropped it: the
    /// note it sounds, its walk, and the clock the walk runs on.
    const auto restartAt = [&](double timeInBlock) {
        const std::uint32_t source = lastPlayedSourceId_;
        emitNoteOff(resetArpState(), timeInBlock, source);
        freeRunSamples_ = 0.0;
    };

    const auto applyEvent = [&](int index, double timeInBlock) {
        const auto& msg = in.message(index);
        const auto sourceId = in.sourceId(index);
        const bool fromLiveSource = isLiveSource(context, sourceId);

        if (msg.isNoteOn()) {
            ++physicallyHeldCount_;

            // Latch: if old set is stale (all keys were released), clear before adding
            if (isLatched && latchedSetStale_) {
                heldCount_ = 0;
                nextOrder_ = 0;
                latchedSetStale_ = false;
            }

            const bool wasEmpty = (heldCount_ == 0);
            addHeldNote(msg.getNoteNumber(), msg.getVelocity(), fromLiveSource, sourceId);
            if (wasEmpty && heldCount_ > 0)
                restartAt(timeInBlock);
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
            restartAt(timeInBlock);
        }
    };

    // --- 4. Settings the walk reads, fixed for the block ---
    const auto pat = static_cast<Pattern>(displayIndex(kPattern));
    const double stepBeats = rateToBeats(static_cast<Rate>(displayIndex(kRate)));
    const float gateVal = juce::jlimit(0.01f, 1.0f, displayValue(kGate));
    const float swingVal = juce::jlimit(0.0f, 1.0f, displayValue(kSwing));
    const float rampVal = juce::jlimit(-1.0f, 1.0f, displayValue(kRamp));
    const float skewVal = juce::jlimit(-1.0f, 1.0f, displayValue(kSkew));
    const auto velMode = static_cast<VelocityMode>(displayIndex(kVelMode));
    const int fixedVel = juce::jlimit(1, 127, displayIndex(kFixedVel));
    const float quantizeAmount = juce::jlimit(0.0f, 1.0f, quantize.load(std::memory_order_relaxed));
    const int quantizeSubVal = juce::jlimit(16, 512, quantizeSub.load(std::memory_order_relaxed));
    const bool hardAngleVal = hardAngle.load(std::memory_order_relaxed);

    // The block's beat span on the transport clock, which every segment
    // divides. The free-running clock counts samples and is read per segment.
    const bool onTransportClock = context.isPlaying && context.tempoMap != nullptr;
    const double blockStartBeat =
        onTransportClock ? context.tempoMap->beatsAtSeconds(context.timelineStartSeconds) : 0.0;
    const double blockEndBeat =
        onTransportClock ? context.tempoMap->beatsAtSeconds(context.timelineEndSeconds) : 0.0;

    // --- 5. One stretch of the block over which the held notes do not change ---
    const auto generate = [&](double segStartSecs, double segEndSecs) {
        // Nothing to play: close what is sounding and idle the clock.
        if (heldCount_ == 0 || (!context.isPlaying && physicallyHeldCount_ <= 0)) {
            closeSoundingNote(segStartSecs);
            freeRunSamples_ = 0.0;
            lastBlockEndBeat_ = -1.0;
            return;
        }

        const double segDurationSecs = segEndSecs - segStartSecs;
        if (segDurationSecs <= 0.0)
            return;

        double segStartBeat = 0.0;
        double segEndBeat = 0.0;
        if (onTransportClock) {
            // Divided the way the output timestamps are computed below, so a
            // beat and a time within the block are each other's inverse.
            const double beats = blockEndBeat - blockStartBeat;
            segStartBeat = blockStartBeat + (segStartSecs / blockDurationSecs) * beats;
            segEndBeat = blockStartBeat + (segEndSecs / blockDurationSecs) * beats;
        } else {
            // Free-running clock — get tempo from timeline position 0
            const double bpm =
                context.tempoMap != nullptr ? context.tempoMap->bpmAtSeconds(0.0) : 120.0;
            const double beatsPerSample = bpm / (60.0 * sampleRate_);
            segStartBeat = freeRunSamples_ * beatsPerSample;
            freeRunSamples_ += segDurationSecs * sampleRate_;
            segEndBeat = freeRunSamples_ * beatsPerSample;
        }

        if (segEndBeat <= segStartBeat)
            return;

        // Seeks, loop wraps and the switch between the two clocks all arrive as
        // a stretch that does not continue the last one. Only some of them
        // reach the device as a panic and a loop wrap reaches it as nothing at
        // all, so the timeline is what the walk trusts (#2416).
        constexpr double kContiguousBeats = 1.0e-3;
        if (lastBlockEndBeat_ < 0.0 ||
            std::abs(segStartBeat - lastBlockEndBeat_) > kContiguousBeats) {
            closeSoundingNote(segStartSecs);
            currentStep_ = 0;
            arpOriginBeat_ = -1.0;
        }
        lastBlockEndBeat_ = segEndBeat;

        const auto seq = buildSequence();
        if (seq.length == 0)
            return;

        // Cycle length in beats (one full pass through the sequence)
        const double cycleBeats = seq.length * stepBeats;

        // Anchor on the grid at the point the pattern starts from, which is
        // where the chord arrived rather than wherever its block began.
        if (arpOriginBeat_ < 0.0)
            arpOriginBeat_ = std::floor(segStartBeat / stepBeats) * stepBeats;

        // Compute the beat position for a given global step index.
        // With ramp, steps within each cycle are warped by the bezier curve.
        // Without ramp, steps are evenly spaced at stepBeats intervals.
        const auto computeStepBeat = [&](int step) -> double {
            int cycle = step / seq.length;
            int stepInCycle = step % seq.length;
            double cycleStart = arpOriginBeat_ + cycle * cycleBeats;

            if (std::abs(rampVal) > 0.001f && seq.length > 1) {
                int cyc = juce::jlimit(1, 8, rampCycles.load(std::memory_order_relaxed));
                double tLinear = static_cast<double>(stepInCycle) / static_cast<double>(seq.length);
                double tCurved = ramp_curve::applyRampCurveWithCycles(tLinear, rampVal, skewVal,
                                                                      cyc, hardAngleVal);
                return cycleStart + tCurved * cycleBeats;
            }
            return cycleStart + stepInCycle * stepBeats;
        };

        // Where the step is actually played: swing on odd steps, then quantize
        // pulling that toward a regular grid. Neither can carry a step past its
        // neighbour, so the walk below keys on this instead of the raw grid and
        // a step warped past the end of a stretch stays pending rather than
        // being consumed unplayed (#2362).
        const auto warpedStepBeat = [&](int step) -> double {
            double beat = computeStepBeat(step);
            // Half the gap to the next step, not half the nominal rate: Time
            // Bend can compress two steps onto one beat, and a fixed offset
            // would then swing the odd one past the even one that follows it.
            if (step % 2 == 1 && swingVal > 0.0f)
                beat += static_cast<double>(swingVal) * (computeStepBeat(step + 1) - beat) * 0.5;

            if (quantizeAmount > 0.0f && quantizeSubVal > 0) {
                const double gridSpacing = cycleBeats / static_cast<double>(quantizeSubVal);
                const double snapped = std::round(beat / gridSpacing) * gridSpacing;
                beat += (snapped - beat) * static_cast<double>(quantizeAmount);
            }
            return beat;
        };

        const auto timeOf = [&](double beat) {
            const double frac = (beat - segStartBeat) / (segEndBeat - segStartBeat);
            return segStartSecs + frac * segDurationSecs;
        };

        // --- Note-off a previous stretch scheduled into this one ---
        if (lastPlayedNote_ >= 0 && lastNoteOffBeat_ >= 0.0 && lastNoteOffBeat_ < segEndBeat) {
            addTimedMessage(juce::MidiMessage::noteOff(1, lastPlayedNote_),
                            std::max(segStartSecs, timeOf(lastNoteOffBeat_)), lastPlayedSourceId_);
            lastPlayedNote_ = -1;
            lastNoteOffBeat_ = -1.0;
            clearMidiOutDisplay();
        }

        // Catch up to the stretch. Cycles are evenly spaced whatever the warp
        // does inside one, so the cycle is a division and only its steps are
        // walked: bounded work, not one pass per skipped step.
        if (warpedStepBeat(currentStep_) < segStartBeat) {
            // Warp carries a step across the cycle boundary either way, so the
            // division lands a cycle early and the walk closes the rest.
            const int cycle =
                static_cast<int>(std::floor((segStartBeat - arpOriginBeat_) / cycleBeats)) - 1;
            currentStep_ = std::max(currentStep_, cycle * seq.length);
            while (warpedStepBeat(currentStep_) < segStartBeat)
                ++currentStep_;
        }

        // --- Walk steps and generate notes ---
        for (;;) {
            const double stepBeat = warpedStepBeat(currentStep_);
            // Left where it is rather than consumed: a step the warp moved past
            // this stretch is played by the stretch that holds it, so the same
            // input plays the same notes however the host cuts its callbacks up
            // (#2415).
            if (stepBeat >= segEndBeat)
                break;

            // Never ahead of the stretch it is played in: the catch-up
            // above leaves the walk on a step at or after the start, and a
            // sequence rebuilt mid-block can only move one closer to it.
            const double timeInBlock = std::max(segStartSecs, timeOf(stepBeat));

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

            const auto& note = seq.notes[static_cast<size_t>(stepIdx)];

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
            const double noteOffBeat = stepBeat + stepBeats * static_cast<double>(gateVal);
            if (noteOffBeat < segEndBeat) {
                addTimedMessage(juce::MidiMessage::noteOff(1, note.noteNumber), timeOf(noteOffBeat),
                                noteSource);
                lastPlayedNote_ = -1;
                lastNoteOffBeat_ = -1.0;
                clearMidiOutDisplay();
            } else {
                // Note-off in a later stretch, or a later block
                lastNoteOffBeat_ = noteOffBeat;
            }
            ++currentStep_;
            currentPlayStep_.store(currentStep_ % seq.length, std::memory_order_relaxed);
            currentSeqLength_.store(seq.length, std::memory_order_relaxed);
        }
    };

    // --- 6. Walk the block, cut where the input changes what is held ---
    const int incomingCount = in.size();
    int nextEvent = 0;
    double segmentStartSecs = 0.0;
    for (;;) {
        while (nextEvent < incomingCount) {
            if (!changesPattern(in.message(nextEvent))) {
                ++nextEvent;
                continue;
            }
            if (eventTime(nextEvent) > segmentStartSecs)
                break;
            applyEvent(nextEvent, segmentStartSecs);
            ++nextEvent;
        }

        const double segmentEndSecs =
            nextEvent < incomingCount ? eventTime(nextEvent) : blockDurationSecs;
        generate(segmentStartSecs, segmentEndSecs);

        // Input stamped at or past the block end still changes what the next
        // block holds; it just has no stretch of this one left to play in.
        if (nextEvent >= incomingCount)
            break;
        segmentStartSecs = segmentEndSecs;
    }

    // Re-asserted rather than left to whichever input event ran last: a chord
    // arriving mid-block resets the walk, and the walk's reset clears this too.
    wasPlaying_ = context.isPlaying;
}

}  // namespace magda::daw::audio
