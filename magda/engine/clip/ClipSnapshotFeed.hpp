#pragma once

#include <farbot/RealtimeObject.hpp>
#include <memory>
#include <utility>

#include "clip/ClipSnapshot.hpp"

/**
 * @file ClipSnapshotFeed.hpp
 * @brief The live clip snapshot: replaced on one thread, read on the other.
 *
 * A snapshot travels the way a plan does and for the same reason: it is
 * immutable once published, so an edit does not mutate what a callback is
 * reading, it makes a new one and swaps it in. The one it replaces is destroyed
 * on the publishing thread, which is what keeps the audio thread from ever
 * freeing a clip list.
 *
 * It is separate from the session on purpose. A clip source needs the snapshot
 * and nothing else the session owns, so it takes one of these; that is also
 * what lets a source be tested with no session at all.
 *
 * One publishing thread. The swap keeps the audio thread out of the publisher's
 * way, not two publishers out of each other's, and publishing waits for the
 * block the callback is in the way a plan swap does: it is the reading side
 * that never waits, not this one.
 */

namespace magda::engine {

class ClipSnapshotFeed {
    using Published = farbot::RealtimeObject<std::shared_ptr<const ClipSnapshot>,
                                             farbot::RealtimeObjectOptions::nonRealtimeMutatable>;

  public:
    /// On the publishing thread.
    void publish(std::shared_ptr<const ClipSnapshot> snapshot) {
        published_.nonRealtimeReplace(std::move(snapshot));
    }

    /**
     * @brief What is live, for as long as this exists. On the audio thread.
     *
     * Null until something has been published, which is a track with nothing to
     * play rather than an error: a session renders silence before its first
     * snapshot arrives, exactly as it renders silence before its first plan.
     */
    class Reader {
      public:
        explicit Reader(ClipSnapshotFeed& feed) : access_(feed.published_) {}

        const ClipSnapshot* get() const noexcept {
            return (*access_).get();
        }
        const ClipSnapshot* operator->() const noexcept {
            return get();
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
