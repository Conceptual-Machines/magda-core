#pragma once

#include <cstdint>

/**
 * @file TimeDomains.hpp
 * @brief The four domains the engine says "when" in.
 *
 * Beats and seconds, each in a timeline form that goes backwards at a loop wrap
 * or a locate and a monotonic form that never does (RenderContext.hpp).
 *
 * Two types rather than one shape reused, so the compiler catches a beat passed
 * where a second was meant. No conversion between them lives here: a tempo map
 * converts a position, not an elapsed duration (#2324).
 */

namespace magda::engine {

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
 * @brief A stretch of the transport's own sample count, half open.
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
struct SampleRange {
    std::int64_t start = 0;
    std::int64_t end = 0;

    std::int64_t length() const {
        return end - start;
    }

    bool empty() const {
        return end <= start;
    }

    bool operator==(const SampleRange&) const = default;
};

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
