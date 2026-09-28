#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

#include "LiveMidiQueue.hpp"
#include "LiveMidiSources.hpp"
#include "io/LiveInput.hpp"

/**
 * @file LiveMidiCollector.hpp
 * @brief The queue's events as the per-source streams a block renders (#2579).
 *
 * One buffer per slot the registry hands out, filled from the queue once per
 * callback. Its own unit because what it has to get right is not obvious: a
 * queued event outlives the model, so the slot it names can have changed hands
 * between the push and this, and the answer is neither to mix the two owners'
 * notes in one buffer nor to hand the first owner's bytes over under the
 * second's name (#2590).
 */

namespace magda::daw::engine_host {

class LiveMidiCollector {
  public:
    /// Room for every slot, taken off the audio thread because it allocates.
    void prepare();

    /**
     * @brief Everything @p queue holds, as one stream per slot that had any.
     *
     * Audio thread, once per callback. The span is valid until the next call.
     *
     * An event is kept only while @p sources still says its slot answers to
     * the id it was pushed under. The id that passed that test is what the
     * slot carries for the rest of the block: a slot that changes hands
     * mid-callback keeps its first owner's notes and drops the second's, since
     * one buffer cannot be two tracks' and relabelling it would play the first
     * track's notes on the second.
     */
    std::span<const engine::LiveMidiStream> collect(LiveMidiQueue& queue,
                                                    const LiveMidiSources& sources);

    /// Events this could not place: a source the room had no slot for, one
    /// whose slot changed hands while it was queued, or a slot already holding
    /// its whole budget. Any thread; the trace's.
    std::uint32_t dropped() const {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    void drop() {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    std::vector<juce::MidiBuffer> midiBySlot_;

    /// The id that filled each slot this block, which is what its stream is
    /// named by. Only slots in @ref written_ hold a live answer.
    std::vector<int> ownerOfSlot_;

    /// The slots this block wrote to, so the next one clears those and not all
    /// of them: the room is a project's worth and most of it is quiet.
    std::vector<int> written_;

    std::vector<engine::LiveMidiStream> streams_;
    std::atomic<std::uint32_t> dropped_{0};
};

}  // namespace magda::daw::engine_host
