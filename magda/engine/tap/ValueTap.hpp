#pragma once

#include <atomic>
#include <bit>
#include <cstdint>

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
 * ## What no writes means
 *
 * A count of zero is the one reading with a meaning of its own: nothing in the
 * engine publishes this value, so the model's own is the answer and the tap is
 * not part of it. That is not only the moment before the first block. The
 * parameter table carries a mixer value, a macro or a modifier's rate only when
 * something reaches it, because a project's faders and its sixteen macros per
 * scope are overwhelmingly numbers nothing moves; a tap on one of those has no
 * publisher and says so.
 *
 * Which means it has to be able to go back. A lane deleted off a fader takes
 * that fader out of the table, and a tap left counting from before would freeze
 * at the last automated position while claiming the engine had stopped. So a
 * publish that binds nothing to a tap clears it (see @ref clear), and the host
 * reads what is true: the engine has no opinion, ask the model.
 *
 * The count therefore never wraps to zero on its own. Thirty-three days of
 * continuous rendering at 96 kHz and 64 samples a block is not a number to
 * dismiss for an installation, and reaching it must not turn a live parameter
 * into one nothing publishes.
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
        /// Normalised, 0 to 1. Zero where nothing has published, which is not a
        /// parameter whose value is zero; @ref writes is what tells those
        /// apart.
        float value = 0.0f;

        /// Blocks published. Zero means nothing in the engine publishes this
        /// value and the model's own is the answer; anything else is meant to
        /// be compared with the last count seen rather than read as a total.
        /// It wraps past its own maximum without passing through zero, so the
        /// comparison stays right and the zero keeps its one meaning.
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
        state_.store(pack(value, nextWriteCount(writes)), std::memory_order_release);
    }

    /**
     * @brief Back to nothing published. Off the audio thread.
     *
     * For a tap the engine has stopped having an opinion about: a publish that
     * bound it to nothing, because what it names is no longer a value the
     * parameter table carries. The alternative is a tap frozen at its last
     * value under a count that has stopped moving, which a host reads as an
     * engine that has stopped rather than as a value it now owns outright.
     *
     * Only ever after the swap that made such a plan live. A tap cleared while
     * the epoch it replaces is still rendering would be written again by that
     * epoch's next block and left counting from one.
     */
    void clear() {
        state_.store(0, std::memory_order_release);
    }

    /**
     * @brief The count after @p writes.
     *
     * Public because it is a rule of the contract above rather than an
     * implementation detail: the count skips zero on the way round, so the one
     * reading that means "nothing publishes this" cannot be reached by a rig
     * that has simply been up for a long time. Thirty-three days of continuous
     * rendering at 96 kHz and 64 samples a block is not a number to dismiss for
     * an installation, and it is not a number a test can reach either, which is
     * the other half of why this is nameable.
     */
    static constexpr std::uint32_t nextWriteCount(std::uint32_t writes) {
        return writes + 1 == 0 ? 1 : writes + 1;
    }

  private:
    /// The count in the high half, the float's bits in the low one. A zeroed
    /// word is therefore no writes and a value of zero, which is what a tap
    /// nothing publishes reads as.
    static constexpr std::uint64_t pack(float value, std::uint32_t writes) {
        return (static_cast<std::uint64_t>(writes) << 32U) | std::bit_cast<std::uint32_t>(value);
    }

    static constexpr Reading unpack(std::uint64_t state) {
        return Reading{std::bit_cast<float>(static_cast<std::uint32_t>(state & 0xFFFFFFFFULL)),
                       static_cast<std::uint32_t>(state >> 32U)};
    }

    std::atomic<std::uint64_t> state_{0};
};

}  // namespace magda::engine
