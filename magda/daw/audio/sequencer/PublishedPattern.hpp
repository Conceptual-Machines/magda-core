#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <thread>

namespace magda::daw::audio::sequencer {

/**
 * @brief A pattern the model publishes and one audio thread plays.
 *
 * The model owns a step pattern (#2313) and the device is handed it, so every
 * edit crosses from the message thread to the audio thread while a block may
 * be reading. Two slots and an index are not enough on their own: an index
 * says which slot is current, not whether the audio thread still holds a
 * reference to the other one, so two publishes inside one block rewrote the
 * pattern being played and tore it (#2335).
 *
 * So the reader acknowledges. `liveSlot_` names the slot the audio thread may
 * read, and Hold TAKES it - exchanging it for kNoSlot - for as long as the
 * block needs it, putting it back on the way out. publish() fills the other
 * slot and hands it over with a compare-exchange that only succeeds while the
 * audio thread is holding nothing.
 *
 * The waiting is all on the message thread, and it is bounded: the audio
 * thread holds a slot for one device's process() and never waits for anything
 * itself. Nothing here allocates.
 */
template <typename PatternT> class PublishedPattern {
  public:
    /**
     * @brief The audio thread's borrow of the live pattern, for one block.
     *
     * Construct one at the top of process() and read pattern() through it. The
     * reference it returns stays valid until it goes out of scope, whatever
     * the message thread is trying to publish meanwhile.
     */
    class Hold {
      public:
        explicit Hold(PublishedPattern& owner)
            : owner_(owner), slot_(owner.liveSlot_.exchange(kNoSlot, std::memory_order_acquire)) {}

        ~Hold() {
            if (slot_ != kNoSlot)
                owner_.liveSlot_.store(slot_, std::memory_order_release);
        }

        Hold(const Hold&) = delete;
        Hold& operator=(const Hold&) = delete;
        Hold(Hold&&) = delete;
        Hold& operator=(Hold&&) = delete;

        /// False only when the slot was already taken, which the one audio
        /// thread cannot do to itself. Read pattern() only when this is true.
        bool isValid() const {
            return slot_ != kNoSlot;
        }

        const PatternT& pattern() const {
            return owner_.slots_[static_cast<std::size_t>(slot_)];
        }

      private:
        PublishedPattern& owner_;
        int slot_;
    };

    /// Hand @p pattern to the audio thread. Message thread only; waits out a
    /// block that is holding the slot it is about to fill.
    void publish(const PatternT& pattern) {
        slots_[static_cast<std::size_t>(writeSlot_)] = pattern;

        int offered = liveSlot_.load(std::memory_order_relaxed);
        while (offered == kNoSlot ||
               !liveSlot_.compare_exchange_weak(offered, writeSlot_, std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
            if (offered == kNoSlot) {
                std::this_thread::yield();
                offered = liveSlot_.load(std::memory_order_relaxed);
            }
        }

        // The slot the audio thread was reading until a moment ago is the one
        // the next publish may fill.
        writeSlot_ = offered;
    }

    /// The pattern last published - the slot publish() is not about to fill.
    /// Message thread only, and a plain read: the audio thread only ever reads
    /// the same slot.
    PatternT current() const {
        return slots_[static_cast<std::size_t>(1 - writeSlot_)];
    }

  private:
    static constexpr int kNoSlot = -1;

    std::array<PatternT, 2> slots_{};
    std::atomic<int> liveSlot_{0};
    /// The slot publish() may fill. Message thread only; always the one
    /// `liveSlot_` is not offering.
    int writeSlot_ = 1;
};

}  // namespace magda::daw::audio::sequencer
