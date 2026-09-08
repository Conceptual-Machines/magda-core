#pragma once

#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <vector>

#include "exec/RenderContext.hpp"
#include "tap/RecordTap.hpp"
#include "transport/TransportState.hpp"

/**
 * @file RecordingFeed.hpp
 * @brief What the callback records into, whatever kind of material it is.
 *
 * A feed rather than part of a plan: arming a track compiles a plan, but
 * starting a recording is not a structural edit and must not cost one.
 */

namespace magda::engine {

/**
 * @brief One take being fed, block by block, on the audio thread.
 *
 * Audio (io/TakeRecorder.hpp) and MIDI (io/MidiTakeRecorder.hpp) are one call,
 * so a track recording both is two takes rather than a special case.
 */
class TakeCapture {
  public:
    virtual ~TakeCapture() = default;

    /**
     * @brief Take what this block carried. On the audio thread, once a block.
     *
     * @p loop tells a wrap from a locate: a wrap opens the next pass, anything
     * else ends the take.
     */
    virtual void capture(const BlockInfo& block, bool countingIn, const LoopRange& loop) = 0;

    /// Where the take publishes the pass in flight (#2463). Read from any
    /// thread, for as long as whoever owns the take keeps it.
    virtual const RecordTap& tap() const = 0;
};

/// The takes a callback feeds. Not owned: whoever publishes them keeps them
/// alive until it has published something that does not name them.
using RecordingTakes = std::vector<TakeCapture*>;

/**
 * @brief What the audio thread records into, replaced on the publishing thread.
 *
 * Publishing waits for the block the callback is in, so a take taken out of the
 * set is safe to finish once the call returns.
 */
class RecordingFeed {
    using Published = farbot::RealtimeObject<std::shared_ptr<const RecordingTakes>,
                                             farbot::RealtimeObjectOptions::nonRealtimeMutatable>;

  public:
    /// On the publishing thread.
    void publish(std::shared_ptr<const RecordingTakes> takes) {
        published_.nonRealtimeReplace(std::move(takes));
    }

    /// What is live, for as long as this exists. On the audio thread. Null
    /// until something is published, which is a session recording nothing.
    class Reader {
      public:
        explicit Reader(RecordingFeed& feed) : access_(feed.published_) {}

        const RecordingTakes* get() const noexcept {
            return (*access_).get();
        }
        explicit operator bool() const noexcept {
            return get() != nullptr;
        }

      private:
        Published::ScopedAccess<farbot::ThreadType::realtime> access_;
    };

  private:
    Published published_;
};

}  // namespace magda::engine
