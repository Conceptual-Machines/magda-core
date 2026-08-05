#pragma once

#include <cstdint>

#include "transport/TempoMap.hpp"

/**
 * @file TransportState.hpp
 * @brief Everything the transport reads, as one immutable value.
 *
 * Published the way values are: resolved on the message thread, swapped in
 * whole, read on the audio thread without a lock. A tempo edit, a loop drag and
 * a metronome toggle are all the same kind of event as a fader move, and none
 * of them compiles a plan.
 *
 * Play, stop and locate travel the same way, as a request the clock applies
 * once. They are state rather than events on purpose: two locates arriving
 * inside one block should leave the cursor where the second one asked for, and
 * a queue that replayed both would have it seek twice to get there.
 */

namespace magda::engine {

/** The stretch of timeline playback returns to, in beats. */
struct LoopRange {
    bool enabled = false;
    double startBeat = 0.0;
    double endBeat = 0.0;

    /// A range playback can actually run through. A backwards or empty one is
    /// not looped rather than being repaired into something the user did not
    /// ask for.
    bool valid() const {
        return enabled && endBeat > startBeat;
    }

    bool operator==(const LoopRange&) const = default;
};

/** The metronome. */
struct ClickSettings {
    bool enabled = false;

    /// Accent the first beat of every bar.
    bool emphasiseBars = true;

    /// Linear gain, applied to both channels.
    float gain = 0.5f;

    bool operator==(const ClickSettings&) const = default;
};

/**
 * @brief Where the transport has been asked to be.
 *
 * @ref generation is what makes it a request rather than a description: the
 * clock applies a snapshot whose generation it has not seen and ignores the
 * position in every other one, so republishing the same transport because the
 * tempo changed does not drag the cursor back to where playback started.
 * Whoever publishes bumps it for every play, stop and locate, and for nothing
 * else.
 */
struct TransportRequest {
    std::uint64_t generation = 0;
    bool playing = false;
    double positionBeat = 0.0;

    /**
     * @brief Beats of count-in before @ref positionBeat.
     *
     * Resolved by the caller rather than named as a mode here: how many beats a
     * bar is worth is the tempo map's business, and whether a plain play counts
     * in at all is the application's. What reaches the engine is a number of
     * beats to roll in for.
     *
     * The cursor starts that far before the play position and reaches it
     * playing, so material before the start is heard rather than skipped, and
     * the loop does not wrap the roll-in.
     */
    double countInBeats = 0.0;
};

/** Tempo, loop, metronome and the play request, as one published value. */
struct TransportSnapshot {
    TempoMap tempo;
    LoopRange loop;
    ClickSettings click;
    TransportRequest request;
};

}  // namespace magda::engine
