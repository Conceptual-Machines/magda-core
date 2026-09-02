#pragma once

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
