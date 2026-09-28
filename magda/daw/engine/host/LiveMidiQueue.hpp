#pragma once

// farbot leaves including the standard library to whoever uses it: its headers
// drop into a project that has already done so and do not compile alone.
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <farbot/fifo.hpp>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

/**
 * @file LiveMidiQueue.hpp
 * @brief How a live note crosses into the audio callback (#2579).
 *
 * Both the MIDI callback thread and the message thread play notes -- hardware
 * keys arrive on one, the piano roll and the QWERTY keyboard on the other --
 * and only the callback reads them, so this is many producers and one
 * consumer.
 */

namespace magda::daw::engine_host {

class LiveMidiQueue {
  public:
    /// One short message, already resolved to the source it belongs to and the
    /// buffer that source was holding when it was resolved.
    struct Event {
        int source = 0;
        int slot = -1;
        juce::uint8 bytes[3]{};
        int size = 0;
    };

    /**
     * @brief Queue @p message under @p source. Any thread but the audio one.
     *
     * SysEx and anything longer than three bytes is dropped and counted: a
     * live note fits, and carrying a variable-length message would mean
     * allocating for it.
     *
     * @p slot travels with it because a queued event outlives the model: by
     * the time the callback reads this, @p source may be a track nobody has
     * any more and @p slot may belong to another. Carrying both is what lets
     * the callback see that they no longer agree (LiveMidiSources.hpp).
     */
    void push(int source, int slot, const juce::MidiMessage& message);

    /// Everything pushed since the last call, in order. Audio thread.
    template <typename Fn> void drain(Fn&& consume) {
        Event event;
        while (fifo_.pop(event))
            consume(event);
    }

    /// Messages too long to carry. Read from any thread; the trace's.
    std::uint32_t oversized() const {
        return oversized_.load(std::memory_order_relaxed);
    }

    /// Events lost to a full queue, which is a callback that stopped running.
    std::uint32_t overflowed() const {
        return overflowed_.load(std::memory_order_relaxed);
    }

  private:
    /// Two callbacks' worth of dense playing at any usable block size. Must be
    /// a power of two, which farbot asserts.
    static constexpr int kCapacity = 1024;

    farbot::fifo<Event, farbot::fifo_options::concurrency::single,
                 farbot::fifo_options::concurrency::multiple>
        fifo_{kCapacity};

    std::atomic<std::uint32_t> oversized_{0};
    std::atomic<std::uint32_t> overflowed_{0};
};

}  // namespace magda::daw::engine_host
