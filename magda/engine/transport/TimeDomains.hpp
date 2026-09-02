#pragma once

/**
 * @file TimeDomains.hpp
 * @brief The four ways the engine says when, and why they are four types.
 *
 * An instant has two faces, beats and seconds, and each of those has a timeline
 * form and a monotonic one. Beats say where the material is; seconds say how
 * much audio went by. The timeline forms go backwards every time a loop wraps
 * or somebody locates; the monotonic forms only ever move forwards, which is
 * what a queued launch and a run's own length are named in (RenderContext.hpp).
 *
 * A beat and a second are separate types here rather than two names for a
 * double, and the reason is the one the DAW layer already gives for its own
 * pair (core/TimeTypes.hpp): the bug is not arithmetic, it is a value from one
 * domain arriving where the other was meant, and no amount of care at the call
 * site catches that as reliably as the compiler does. Both reviews of the
 * session launcher found exactly that bug, in a place where the two were plain
 * doubles.
 *
 * There is deliberately no conversion between them here. A tempo map converts a
 * position, and how long a run has been going is not a position: ask a map how
 * far apart two beats are and the answer changes with every tempo edit between
 * them, and after a wrap the two beats are not even in the same cycle (#2324).
 * Whatever needs both faces is handed both, derived together, by the one thing
 * that owns a tempo map.
 */

namespace magda::engine {

/// A stretch of beats. Half open: a block ending at 4.0 and one starting there
/// do not both contain beat 4.
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

/// A stretch of seconds, half open in the same way. A separate type from
/// BeatRange rather than the same shape reused: see the file comment.
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

}  // namespace magda::engine
