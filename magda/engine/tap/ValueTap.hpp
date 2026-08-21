#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

/**
 * @file ValueTap.hpp
 * @brief What a block settled a value at, for whoever draws it.
 *
 * The third tap, and the one that reads back rather than measures (#2122). A
 * LevelTap answers how loud a point in the signal was and a MidiTap answers
 * what reached one; this answers what a number is. Two numbers, in fact, and
 * they are the same shape, so they are the same tap:
 *
 *  - what a modifier published this block, which is what a modifier editor
 *    animates, and
 *  - what a parameter resolved to once its base, its lane and its links had
 *    been settled against each other, which is what a device UI draws when
 *    something is modulating a knob, and what touch and latch would read at the
 *    moment a gesture starts.
 *
 * A normalised position, 0 to 1, in both cases. The parameter's own units are a
 * function of that position and its scale, and the scale belongs to the model,
 * which already has it; a tap that converted would be a second place the
 * conversion could be wrong.
 *
 * Bound by ParamKey, because that is the address of both: a modifier taken as a
 * source is a key with no parameter index and a parameter is a key with one,
 * and neither needs anything the other does not have (ParamKey.hpp).
 *
 * Owned by the host, for the reason a LevelTap is: it outlives the plans that
 * reference it, so an edit somewhere else in the project does not restate a
 * value something was reading.
 *
 * ## Why it is not a meter
 *
 * A read here is not destructive, and that is the whole of the difference. A
 * meter reports the loudest thing since it was last looked at because a
 * transient falling between two frames is a transient it must not miss. A
 * position has nothing of the kind to miss: what a knob draws is where the value
 * is, and where it was halfway between two frames is not a fact anybody is owed.
 * So the value stands until a block replaces it, any number of readers get the
 * same answer rather than taking it from each other, and a reader arriving late
 * reads the current position rather than a zero.
 *
 * ## Nothing simulates it
 *
 * A tap holds while the engine is not rendering, and holding is the right
 * answer: an LFO nothing is advancing is not turning, and an editor that
 * animated one anyway would be drawing a second LFO that agrees with the audible
 * one by accident and diverges from it under every retrigger. That second LFO is
 * what the incumbent engine needs, because there the value is polled off the
 * engine at frame rate and a poll between two audio callbacks has nothing to
 * report. Here the value is published by the block that produced it, so the
 * simulation has nothing left to do and does not survive the port.
 *
 * @ref Reading::writes is what distinguishes a value that is holding from a
 * value that is not moving. It counts blocks published rather than changes, so
 * an editor with nothing to redraw has something to stop on, and a UI that
 * wants to know whether the engine is running at all can ask the value it is
 * already reading rather than the transport.
 *
 * ## Why the two of them are one word
 *
 * The count is what a reader gates a repaint on, so a reading that pairs one
 * block's count with another block's value is not a stale frame but a value
 * that never arrives. Read the value first and a reader can see the old value
 * beside the new count: it redraws with the old one, and at the next poll the
 * count has not moved again, so it decides nothing changed and the value it
 * never drew stays undrawn until something else writes. Read the count first
 * and the mirror image happens, with the fresher value thrown away for a count
 * that had not caught up. Neither order is a repaint's worth of wrong.
 *
 * So they are not two atomics. A float and a 32-bit count fit in one 64-bit
 * word, which every platform this builds for stores and loads in one
 * instruction, and a reading is then always one block's own: one store on the
 * audio thread, one load off it, no sequence lock and nothing to retry.
 *
 * One writer, the audio thread. Any number of readers.
 */

namespace magda::engine {

class ValueTap {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "a value tap is written on the audio thread and must not take a lock");

  public:
    /** @brief The value, and how many blocks have published one. */
    struct Reading {
        /// Normalised, 0 to 1. Zero before any block has published, which is a
        /// tap that has been bound and not yet written rather than a parameter
        /// whose value is zero; @ref writes is what tells those apart.
        float value = 0.0f;

        /// Blocks published since the tap was made. Wraps, and is meant to be
        /// compared with the last one seen rather than read as a total: a
        /// reader asking whether this differs from what it drew last frame gets
        /// the right answer across the wrap, and one asking how many blocks
        /// have gone by does not.
        std::uint32_t writes = 0;
    };

    /**
     * @brief What the last block to publish settled the value at.
     *
     * Off the audio thread, from as many readers as like, as often as they
     * like. Nothing is consumed and nothing is reset.
     *
     * One load, so the pair is one block's own: the count belongs to the block
     * that wrote the value beside it, and a reader gating a repaint on the
     * count is gating on the value it is about to draw.
     */
    Reading read() const {
        return unpack(state_.load(std::memory_order_acquire));
    }

    /// Just the value, for a reader that only draws it.
    float value() const {
        return read().value;
    }

    /**
     * @brief Publish what this block settled the value at. Audio thread.
     *
     * Once per block per tap, from where the value was settled: a modifier's
     * output as it was advanced, a parameter's position as it was resolved.
     * Unconditional rather than only on a change, because the count is of
     * blocks published and a tap that fell silent on a value that stopped
     * moving would be indistinguishable from one whose engine had stopped.
     *
     * The count comes off the word this is about to replace rather than out of
     * a counter of its own, which is what makes the pair inseparable. Loading
     * it needs no ordering and no compare-and-swap: there is one writer, and it
     * is the only thing that ever changes this.
     */
    void write(float value) {
        const auto writes = unpack(state_.load(std::memory_order_relaxed)).writes;
        state_.store(pack(value, writes + 1), std::memory_order_release);
    }

  private:
    /// The count in the high half, the float's bits in the low one. A zeroed
    /// word is therefore no writes and a value of zero, which is what a tap
    /// nothing has published reads as.
    static std::uint64_t pack(float value, std::uint32_t writes) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return (static_cast<std::uint64_t>(writes) << 32U) | bits;
    }

    static Reading unpack(std::uint64_t state) {
        Reading reading;
        const auto bits = static_cast<std::uint32_t>(state & 0xFFFFFFFFULL);
        std::memcpy(&reading.value, &bits, sizeof(bits));
        reading.writes = static_cast<std::uint32_t>(state >> 32U);
        return reading;
    }

    std::atomic<std::uint64_t> state_{0};
};

}  // namespace magda::engine
