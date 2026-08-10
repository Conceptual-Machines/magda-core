#include "clip/MidiClipCompiler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace magda::engine {

namespace {

constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kControlChange = 0xb0;
constexpr std::uint8_t kPitchWheel = 0xe0;

/// The wheel at rest. A clip whose every pitch-bend event sits here is skipped
/// whole: a stream of no-op wheel messages is pointless and deadlocks fragile
/// synths (#1193). A curve that returns to rest still has a non-rest event in it
/// and is kept.
constexpr int kPitchWheelRest = 8192;

/// TE's fixed MPE conversion range, which is what the model's semitones mean.
constexpr double kMpeSemitoneRange = 48.0;

/// Lower zone: master on channel 1, members on 2 to 16.
constexpr int kMpeFirstMemberChannel = 2;
constexpr int kMpeLastMemberChannel = 16;

/// Ordering at equal beats. Controllers before notes because a bank or program
/// change has to land before the note it configures; offs before ons because two
/// notes of one pitch meeting exactly is otherwise a coin toss between a
/// retrigger and a hung note.
int rankOf(std::uint8_t status) {
    switch (status & 0xf0u) {
        case kControlChange:
            return 0;
        case kPitchWheel:
            return 1;
        case kNoteOff:
            return 2;
        default:
            return 3;
    }
}

std::uint8_t statusFor(std::uint8_t kind, int channel) {
    return static_cast<std::uint8_t>(kind | static_cast<std::uint8_t>((channel - 1) & 0x0f));
}

/// A note-on and the note-off that ends it, before either has an index.
struct PendingEvent {
    MidiClipEvent event;

    /// Note edges only: which note of the clip this edge belongs to, so the two
    /// can be paired again once sorting has moved them.
    int pairId = -1;
};

double tensionCurve(double t, double tension) {
    if (std::abs(tension) < 0.001)
        return t;
    if (tension > 0.0)
        return std::pow(t, 1.0 + tension * 2.0);
    return 1.0 - std::pow(1.0 - t, 1.0 - tension * 2.0);
}

/**
 * @brief Walk one curve, emitting where the value changes.
 *
 * @p emit takes a beat and an already-clamped value. Authored points are handed
 * over whenever their value differs from the last emitted; the interpolation
 * between them also has to clear @p floorBeats.
 */
template <typename EventType, typename Emit>
void densify(std::vector<EventType> sorted, int maxValue, double floorBeats, Emit&& emit) {
    if (sorted.empty())
        return;

    std::stable_sort(sorted.begin(), sorted.end(), [](const EventType& a, const EventType& b) {
        return a.beatPosition < b.beatPosition;
    });

    auto lastValue = std::numeric_limits<int>::min();
    auto lastBeat = -std::numeric_limits<double>::max();

    const auto put = [&](double beat, int value, bool authored) {
        const auto clamped = std::clamp(value, 0, maxValue);
        if (clamped == lastValue)
            return;
        if (!authored && beat - lastBeat < floorBeats)
            return;

        emit(beat, clamped);
        lastValue = clamped;
        lastBeat = beat;
    };

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const auto& event = sorted[i];
        const auto isLast = i + 1 == sorted.size();

        put(event.beatPosition, event.value, true);

        if (isLast || event.curveType == MidiCurveType::Step)
            continue;  // held until the next point, which is the next iteration

        const auto& next = sorted[i + 1];
        const auto beatStart = event.beatPosition;
        const auto span = next.beatPosition - beatStart;
        if (span <= 0.0)
            continue;

        const auto from = static_cast<double>(event.value);
        const auto to = static_cast<double>(next.value);

        // Nothing moves across this segment, so nothing is sent across it. The
        // incumbent has the same guard for the same reason; here it is not a
        // special case, it is what emitting on value change means.
        if (std::lround(from) == std::lround(to))
            continue;

        const auto shape = [&](double t) {
            if (event.curveType == MidiCurveType::Bezier) {
                // Control points in (beat, value) space, the handles measured
                // against the segment's own value range, exactly as the curve
                // editor draws them.
                const auto range = to - from;
                const auto p1 = from + event.outHandle.dy * range;
                const auto p2 = to + next.inHandle.dy * range;
                const auto u = 1.0 - t;
                return u * u * u * from + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 +
                       t * t * t * to;
            }

            return from + tensionCurve(t, event.tension) * (to - from);
        };

        // Bounded twice over: by the floor, and by how many distinct values the
        // segment can pass through at all. Neither alone is enough, since a long
        // segment of a 14-bit curve would otherwise ask for tens of thousands of
        // evaluations and a short one for none.
        const auto byFloor = floorBeats > 0.0 ? static_cast<int>(std::ceil(span / floorBeats))
                                              : kMaxCurveStepsPerSegment;
        const auto byValues = 2 * (static_cast<int>(std::abs(to - from)) + 1);
        const auto steps = std::clamp(std::min(byFloor, byValues), 1, kMaxCurveStepsPerSegment);
        const auto step = span / steps;

        for (auto k = 1; k < steps; ++k) {
            const auto t = static_cast<double>(k) / steps;
            put(beatStart + k * step, static_cast<int>(std::lround(shape(t))), false);
        }
    }
}

/// One MPE member channel and what it is busy with.
struct MpeChannel {
    double freeAtBeat = -std::numeric_limits<double>::max();
    int lastNoteNumber = -1;
};

/// A channel for a note starting at @p startBeat, preferring one that is free
/// and did not last play this pitch: reusing the channel a pitch just left makes
/// a receiver read the pair as one retriggered note.
int assignMpeChannel(
    std::array<MpeChannel, kMpeLastMemberChannel - kMpeFirstMemberChannel + 1>& channels,
    double startBeat, double endBeat, int noteNumber) {
    auto best = -1;
    auto bestFreeAt = std::numeric_limits<double>::max();

    for (std::size_t i = 0; i < channels.size(); ++i) {
        const auto& channel = channels[i];
        if (channel.freeAtBeat > startBeat)
            continue;
        if (channel.lastNoteNumber == noteNumber)
            continue;

        // The one free longest, so a channel gets as much room as possible
        // between two notes of the same pitch.
        if (channel.freeAtBeat < bestFreeAt) {
            bestFreeAt = channel.freeAtBeat;
            best = static_cast<int>(i);
        }
    }

    if (best < 0) {
        // Every free channel last played this pitch, or none is free. Take
        // whichever frees earliest: overlapping on one channel is worse than
        // reusing a pitch's own.
        bestFreeAt = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < channels.size(); ++i) {
            if (channels[i].freeAtBeat < bestFreeAt) {
                bestFreeAt = channels[i].freeAtBeat;
                best = static_cast<int>(i);
            }
        }
    }

    channels[static_cast<std::size_t>(best)].freeAtBeat = endBeat;
    channels[static_cast<std::size_t>(best)].lastNoteNumber = noteNumber;
    return best + kMpeFirstMemberChannel;
}

/// The model's semitones as a 14-bit wheel value.
int wheelForSemitones(double semitones) {
    const auto clamped = std::clamp(semitones, -kMpeSemitoneRange, kMpeSemitoneRange);
    return std::clamp(
        static_cast<int>(std::lround(kPitchWheelRest + clamped / kMpeSemitoneRange * 8191.0)), 0,
        16383);
}

}  // namespace

MidiEventList compileMidiEvents(const ClipInfo& clip, double curveFloorBeats) {
    MidiEventList list;

    std::vector<PendingEvent> pending;

    // ---- Notes -------------------------------------------------------------

    auto notes = clip.midiNotes;
    std::stable_sort(notes.begin(), notes.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat < b.startBeat;
    });

    list.mpe = std::any_of(notes.begin(), notes.end(),
                           [](const MidiNote& note) { return note.hasPitchExpression(); });

    std::array<MpeChannel, kMpeLastMemberChannel - kMpeFirstMemberChannel + 1> mpeChannels{};
    std::vector<int> channelOf(notes.size(), 1);

    for (std::size_t i = 0; i < notes.size(); ++i) {
        const auto& note = notes[i];
        if (note.lengthBeats <= 0.0)
            continue;

        const auto channel =
            list.mpe ? assignMpeChannel(mpeChannels, note.startBeat,
                                        note.startBeat + note.lengthBeats, note.noteNumber)
                     : 1;
        channelOf[i] = channel;

        const auto number = static_cast<std::uint8_t>(std::clamp(note.noteNumber, 0, 127));
        // A note-on of velocity zero reads as a note-off, so the floor is one.
        const auto velocity = static_cast<std::uint8_t>(std::clamp(note.velocity, 1, 127));

        const auto pairId = static_cast<int>(i);

        // Its own pitch struck again before this note ends: the note-off is
        // dropped, because emitting it would cut the second note short. The
        // fork's `useNoteUp = false`. What ends such a note is the pass or the
        // span it sits in.
        auto endedBySelf = true;
        for (std::size_t j = i + 1; j < notes.size(); ++j) {
            if (notes[j].startBeat >= note.startBeat + note.lengthBeats)
                break;
            if (notes[j].noteNumber == note.noteNumber && notes[j].lengthBeats > 0.0 &&
                (!list.mpe || channelOf[j] == channel)) {
                endedBySelf = false;
                break;
            }
        }

        pending.push_back(PendingEvent{
            MidiClipEvent{note.startBeat, statusFor(kNoteOn, channel), number, velocity, -1},
            pairId});

        if (endedBySelf)
            pending.push_back(
                PendingEvent{MidiClipEvent{note.startBeat + note.lengthBeats,
                                           statusFor(kNoteOff, channel), number, 0, -1},
                             pairId});

        list.longestNoteBeats = std::max(list.longestNoteBeats, note.lengthBeats);

        // ---- Per-note pitch expression -------------------------------------
        //
        // Densified like any other curve rather than on the sync layer's
        // 1/16-beat grid: that grid is not TE's rule, TE emits one raw wheel per
        // expression point with no interpolation at all. Expression is pitch,
        // which is where a coarse grid is heard most directly.
        if (list.mpe && note.hasPitchExpression()) {
            auto points = note.pitchExpression;
            std::stable_sort(points.begin(), points.end(),
                             [](const auto& a, const auto& b) { return a.beat < b.beat; });

            const auto valueAt = [&points](double beat) {
                if (beat <= points.front().beat)
                    return points.front().semitones;
                if (beat >= points.back().beat)
                    return points.back().semitones;
                for (std::size_t p = 0; p + 1 < points.size(); ++p) {
                    const auto& a = points[p];
                    const auto& b = points[p + 1];
                    if (beat >= a.beat && beat <= b.beat) {
                        const auto span = b.beat - a.beat;
                        if (span <= 0.0)
                            return b.semitones;
                        return a.semitones + (beat - a.beat) / span * (b.semitones - a.semitones);
                    }
                }
                return points.back().semitones;
            };

            auto lastValue = std::numeric_limits<int>::min();
            auto lastBeat = -std::numeric_limits<double>::max();

            const auto put = [&](double relativeBeat, bool authored) {
                const auto value = wheelForSemitones(valueAt(relativeBeat));
                if (value == lastValue)
                    return;
                if (!authored && relativeBeat - lastBeat < curveFloorBeats)
                    return;

                pending.push_back(PendingEvent{
                    MidiClipEvent{note.startBeat + relativeBeat, statusFor(kPitchWheel, channel),
                                  static_cast<std::uint8_t>(value & 0x7f),
                                  static_cast<std::uint8_t>((value >> 7) & 0x7f), -1},
                    -1});
                lastValue = value;
                lastBeat = relativeBeat;
            };

            // The value at the note's own start, so playback begins on the
            // curve rather than at rest and glides onto it.
            put(0.0, true);

            for (std::size_t p = 0; p + 1 < points.size(); ++p) {
                const auto from = std::max(0.0, points[p].beat);
                const auto to = std::min(note.lengthBeats, points[p + 1].beat);
                if (to <= from)
                    continue;

                const auto byFloor =
                    curveFloorBeats > 0.0
                        ? static_cast<int>(std::ceil((to - from) / curveFloorBeats))
                        : kMaxCurveStepsPerSegment;
                const auto steps = std::clamp(byFloor, 1, kMaxCurveStepsPerSegment);

                for (auto k = 0; k <= steps; ++k)
                    put(from + (to - from) * k / steps, k == 0);
            }
        }
    }

    // ---- Controllers -------------------------------------------------------

    {
        std::map<int, std::vector<MidiCCData>> byController;
        for (const auto& cc : clip.midiCCData)
            byController[cc.controller].push_back(cc);

        for (auto& [controller, events] : byController) {
            const auto number = static_cast<std::uint8_t>(std::clamp(controller, 0, 127));
            densify(std::move(events), 127, curveFloorBeats, [&](double beat, int value) {
                pending.push_back(
                    PendingEvent{MidiClipEvent{beat, statusFor(kControlChange, 1), number,
                                               static_cast<std::uint8_t>(value), -1},
                                 -1});
            });
        }
    }

    {
        const auto& bend = clip.midiPitchBendData;
        const auto allAtRest =
            !bend.empty() &&
            std::all_of(bend.begin(), bend.end(), [](const MidiPitchBendData& event) {
                return event.value == kPitchWheelRest;
            });

        if (!allAtRest) {
            densify(bend, 16383, curveFloorBeats, [&](double beat, int value) {
                pending.push_back(
                    PendingEvent{MidiClipEvent{beat, statusFor(kPitchWheel, 1),
                                               static_cast<std::uint8_t>(value & 0x7f),
                                               static_cast<std::uint8_t>((value >> 7) & 0x7f), -1},
                                 -1});
            });
        }
    }

    // ---- Sort, then pair the notes up again ---------------------------------

    std::stable_sort(pending.begin(), pending.end(),
                     [](const PendingEvent& a, const PendingEvent& b) {
                         if (a.event.beat != b.event.beat)
                             return a.event.beat < b.event.beat;
                         return rankOf(a.event.status) < rankOf(b.event.status);
                     });

    list.events.reserve(pending.size());
    for (const auto& entry : pending)
        list.events.push_back(entry.event);

    std::map<int, std::int32_t> noteOffOf;
    for (std::size_t i = 0; i < pending.size(); ++i)
        if (pending[i].pairId >= 0 && list.events[i].isNoteOff())
            noteOffOf[pending[i].pairId] = static_cast<std::int32_t>(i);

    for (std::size_t i = 0; i < pending.size(); ++i) {
        if (pending[i].pairId < 0 || !list.events[i].isNoteOn())
            continue;

        const auto found = noteOffOf.find(pending[i].pairId);
        list.events[i].endsAt = found == noteOffOf.end() ? -1 : found->second;
    }

    // ---- Index the controllers ----------------------------------------------

    for (std::size_t i = 0; i < list.events.size(); ++i) {
        const auto& event = list.events[i];
        const auto kind = event.kind();
        if (kind != kControlChange && kind != kPitchWheel)
            continue;

        const auto controller =
            kind == kPitchWheel ? MidiControllerStream::kPitchBend : static_cast<int>(event.data1);

        auto found = std::find_if(list.controllers.begin(), list.controllers.end(),
                                  [&](const MidiControllerStream& stream) {
                                      return stream.channel == event.channel() &&
                                             stream.controller == controller;
                                  });

        if (found == list.controllers.end()) {
            list.controllers.push_back(MidiControllerStream{event.channel(), controller, {}});
            found = std::prev(list.controllers.end());
        }

        found->events.push_back(static_cast<std::int32_t>(i));
    }

    return list;
}

}  // namespace magda::engine
