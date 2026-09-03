#pragma once

#include <cmath>
#include <compare>
#include <cstdint>

/**
 * @file TimeDomains.hpp
 * @brief The four domains the engine says "when" in, and the samples under them.
 *
 * Beats and seconds, each in a timeline form that goes backwards at a loop wrap
 * or a locate and a monotonic form that never does (RenderContext.hpp).
 *
 * Two types rather than one shape reused, so the compiler catches a beat passed
 * where a second was meant. No conversion between them lives here: a tempo map
 * converts a position, not an elapsed duration (#2324).
 *
 * Then the samples, which are what the four domains are interpretations of: a
 * position on the transport's own count, a number of them, and the two things a
 * sample offset inside a block can mean (#2336).
 */

namespace magda::engine {

/**
 * @brief A hundredth of a sample: the slack in a conversion to one.
 *
 * Beats and seconds are doubles and the arithmetic between them is not exact,
 * so a moment that is a sample exactly can come out a billionth of a sample
 * either side of it. Anything that rounds in one direction has to be told, or
 * it answers with the sample next door: the first sample at or after a beat
 * becomes the one after that, and the sample a moment sits inside becomes the
 * one before it.
 *
 * Small enough not to move anything real. A hundredth of a sample is two tenths
 * of a microsecond at 48 kHz.
 */
inline constexpr double kSampleEpsilon = 1.0e-2;

/// A stretch of beats, half open.
struct BeatRange {
    double start = 0.0;
    double end = 0.0;

    double length() const {
        return end - start;
    }

    bool empty() const {
        return end <= start;
    }

    bool operator==(const BeatRange&) const = default;
};

/// A stretch of seconds, half open.
struct SecondsRange {
    double start = 0.0;
    double end = 0.0;

    double length() const {
        return end - start;
    }

    bool empty() const {
        return end <= start;
    }

    bool operator==(const SecondsRange&) const = default;
};

/**
 * @brief A number of samples: how long something lasts, or how far apart two
 * moments are.
 *
 * Separate from a position for the reason a duration is separate from a date.
 * Two positions can be subtracted and cannot be added; a duration can be added
 * to a position and to another duration. A single integer standing for both
 * lets every one of those through.
 *
 * Signed, because the distance between two moments is asked for in both orders
 * and a run can be nudged backwards.
 */
struct SampleDuration {
    std::int64_t samples = 0;

    /**
     * @brief How long this lasts at @p sampleRate.
     *
     * Exact between rate changes and meaningless across one: a count that spans
     * a rate change counts samples that were not all worth the same amount of
     * time, and no single rate turns it back into seconds (#2336).
     */
    double seconds(double sampleRate) const {
        return sampleRate > 0.0 ? static_cast<double>(samples) / sampleRate : 0.0;
    }

    /// The samples @p seconds is worth at @p sampleRate, to the nearest one.
    static SampleDuration ofSeconds(double seconds, double sampleRate) {
        return SampleDuration{static_cast<std::int64_t>(std::llround(seconds * sampleRate))};
    }

    bool operator==(const SampleDuration&) const = default;
    auto operator<=>(const SampleDuration&) const = default;
};

inline SampleDuration operator+(SampleDuration a, SampleDuration b) {
    return SampleDuration{a.samples + b.samples};
}

inline SampleDuration operator-(SampleDuration a, SampleDuration b) {
    return SampleDuration{a.samples - b.samples};
}

/**
 * @brief A point on the transport's own sample count.
 *
 * The count never goes back. A locate does not move it, a loop wrap does not
 * take it back, a tempo edit does not rescale it, and re-anchoring the cursor
 * does not reset it: it is how many samples the transport has rolled, and the
 * device only ever hands over more of them.
 *
 * Which is what makes it the coordinate the others are derived from rather than
 * one more of them. A beat is where something sits and a second is how long
 * something lasted, and both are answers a tempo map gives; a sample is what
 * played. Two beats a thousand blocks apart can be the same beat and two
 * elapsed seconds can disagree about which cycle they are in, but no two
 * samples are the same sample.
 */
struct SamplePosition {
    std::int64_t sample = 0;

    bool operator==(const SamplePosition&) const = default;
    auto operator<=>(const SamplePosition&) const = default;
};

inline SampleDuration operator-(SamplePosition a, SamplePosition b) {
    return SampleDuration{a.sample - b.sample};
}

inline SamplePosition operator+(SamplePosition position, SampleDuration duration) {
    return SamplePosition{position.sample + duration.samples};
}

inline SamplePosition operator-(SamplePosition position, SampleDuration duration) {
    return SamplePosition{position.sample - duration.samples};
}

inline SamplePosition& operator+=(SamplePosition& position, SampleDuration duration) {
    position.sample += duration.samples;
    return position;
}

/// A stretch of the transport's count, half open.
struct SampleRange {
    SamplePosition start;
    SamplePosition end;

    SampleDuration length() const {
        return end - start;
    }

    bool empty() const {
        return end <= start;
    }

    bool operator==(const SampleRange&) const = default;
};

/**
 * @brief A sample of a block that something happens on, from 0 to N-1.
 *
 * A note-on, a click, the start of a parameter segment: one moment, on one
 * sample the block actually plays. N is not one of them. It is the boundary
 * after the block's last sample, so a message written there is written into
 * nobody's buffer, and the block that owns that sample is the next one, where
 * the same moment is sample zero.
 *
 * @see EdgeSample, which is the other thing a sample offset can mean.
 */
struct EventSample {
    int value = 0;

    bool operator==(const EventSample&) const = default;
    auto operator<=>(const EventSample&) const = default;
};

/**
 * @brief Where a stretch of a block begins or ends, from 0 to N.
 *
 * A fade bound, a region's ends, the edges of a hole. Stretches are half open,
 * so N is a legal answer and a common one: "this fade runs to the end of the
 * block" is exactly an end edge of N.
 *
 * The distinction from @ref EventSample is meaning rather than axis, which is
 * why it is a type and not a naming convention. A note-off arrives as a beat,
 * the way a note-on does, but it is where a note's stretch ends, and a note-off
 * on the block boundary is the one case where the two rules differ: as an event
 * it would be written nowhere and the note would hang. What it needs is to be
 * heard one sample early, which is @ref BlockInfo::soundsAt, in one place and
 * under a name, rather than an unexplained clamp at each site that emits one.
 */
struct EdgeSample {
    int value = 0;

    bool operator==(const EdgeSample&) const = default;
    auto operator<=>(const EdgeSample&) const = default;
};

/// How many samples lie between two edges. An int rather than an edge: the
/// distance between two bounds is a count, and it is what a buffer loop runs
/// over.
inline int operator-(EdgeSample a, EdgeSample b) {
    return a.value - b.value;
}

/// The edge @p count samples past @p edge, which is how a region's far end is
/// named once its near end and its length are known.
inline EdgeSample operator+(EdgeSample edge, int count) {
    return EdgeSample{edge.value + count};
}

/**
 * @brief One instant inside a block, in every domain the engine names it in.
 *
 * Built from one coordinate and one only: the sample. Every face below is
 * derived from it through the tempo map, so the five of them are one instant
 * rather than five answers that happen to be near each other.
 *
 * That is the whole point of the type. The domains are related by the map,
 * which is not a straight line, and projecting an instant onto each axis
 * separately gives a set of faces no single moment has: over a steep ramp the
 * beat face and the seconds face of one "instant" can be 163 samples apart.
 * Anything built from those faces inherits the gap, and a run whose origin has
 * it starts its material somewhere it was never placed (#2330).
 *
 * The sample is canonical because it is the one coordinate that is not a
 * matter of interpretation: it is what plays.
 */
struct BlockInstant {
    /// Offset within the block. What actually sounds at this instant.
    int sample = 0;

    double timelineBeat = 0.0;
    double timelineSeconds = 0.0;

    /// The faces that never go back, which is what a queued launch is named in
    /// and what a run's elapsed material is measured in (#2324).
    double monotonicBeat = 0.0;
    double monotonicSeconds = 0.0;
};

/**
 * @brief Where a run began, in the two domains a source reads it against.
 *
 * One value, because a run whose faces came from different instants would put a
 * clip's notes and its samples in different places. The two are not one number
 * converted: a map says where a beat sits, not how long a run has lasted, so
 * each face is projected from its own monotonic domain (#2324).
 *
 * Here rather than with the launcher because it is what shifts a block onto a
 * run's own axes, and the block is not a launcher concept.
 */
struct RunOrigin {
    double beat = 0.0;
    double seconds = 0.0;

    bool operator==(const RunOrigin&) const = default;
};

}  // namespace magda::engine
