#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>

namespace magda::mcp {

/**
 * @brief One response's SSE framing, and the raw bytes behind it.
 *
 * Owned by the request that opened the stream rather than by the bridge: a
 * content receiver is called on the connection's own thread, so two concurrent
 * `subscriptions/listen` requests interleave, and a single buffer would splice
 * a half-received frame from one response onto the next.
 *
 * `raw` is kept because a content receiver is also handed the body of a refused
 * request, which is an ordinary JSON-RPC error rather than an event stream —
 * and by then `Result::body` is empty, so this is the only copy left.
 */
class SseParser {
  public:
    using Emit = std::function<void(const std::string&)>;

    void consume(const std::string& chunk, const Emit& emit) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Retained only while the response might still turn out to be an
        // ordinary error body. A subscription stream stays open for as long as
        // the host cares to listen, so keeping every byte of one would grow
        // without limit for the life of the process. The cap covers the window
        // before that is known — an error body is small, and anything larger is
        // not one.
        if (retainRaw_) {
            if (raw_.size() + chunk.size() <= kMaxRetainedRaw)
                raw_ += chunk;
            else
                retainRaw_ = false;
        }

        pending_ += chunk;

        // The incomplete frame is bounded too. Capping only the raw copy left
        // this one unlimited, and it is fed by the same bytes: a peer that never
        // sends a frame separator grows it forever, which is the identical leak
        // one buffer further along.
        //
        // Past the cap the frame being accumulated can never be one of ours —
        // these are single JSON-RPC notifications — so it is abandoned. What
        // matters is abandoning *only* it: the chunk that crossed the limit may
        // also carry that frame's terminator and several perfectly good frames
        // behind it, and clearing the buffer wholesale would discard those
        // separators along with the oversized frame, then drop the next valid
        // frame as well when the resync flag consumed it.
        //
        // So: if a separator is already buffered, the oversized frame ends
        // there. Drop through it and let the loop below parse everything after
        // it normally. Only when none has arrived is there nothing to keep, and
        // the flag then makes the eventual terminating segment — the tail of
        // something already truncated — the one that gets dropped.
        if (pending_.size() > kMaxFrameBytes) {
            if (const auto split = pending_.find(kFrameSeparator); split != std::string::npos) {
                pending_.erase(0, split + kFrameSeparatorLength);
                resynchronising_ = false;
            } else {
                pending_.clear();
                pending_.shrink_to_fit();
                resynchronising_ = true;
            }
        }

        for (auto split = pending_.find(kFrameSeparator); split != std::string::npos;
             split = pending_.find(kFrameSeparator)) {
            const auto frame = pending_.substr(0, split);
            pending_.erase(0, split + kFrameSeparatorLength);

            if (resynchronising_) {
                resynchronising_ = false;
                continue;
            }

            // A line beginning with a colon is a keep-alive comment, which the
            // SSE grammar says to ignore.
            if (frame.rfind("data: ", 0) == 0) {
                // A parsed frame proves this is an event stream, so the raw copy
                // can never be needed and is released rather than merely capped.
                if (retainRaw_) {
                    retainRaw_ = false;
                    raw_.clear();
                    raw_.shrink_to_fit();
                }
                emit(frame.substr(6));
            }
        }
    }

    std::string raw() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return raw_;
    }

    /**
     * @brief Every byte this parser is holding, across both buffers.
     *
     * Deliberately the total. An earlier version reported only the raw copy,
     * and a test asserting on it passed while the incomplete-frame buffer grew
     * to megabytes alongside — the accessor measured the buffer that had been
     * fixed rather than the memory the object was using.
     */
    std::size_t retainedBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return raw_.size() + pending_.size();
    }

  private:
    /// Enough for any JSON-RPC error body a refusal can carry, and far below
    /// what a stream would reach.
    static constexpr std::size_t kMaxRetainedRaw = 64 * 1024;

    /// A frame here is one JSON-RPC notification. This is orders of magnitude
    /// above the largest of those and still bounded.
    static constexpr std::size_t kMaxFrameBytes = std::size_t{1024} * 1024;

    static constexpr const char* kFrameSeparator = "\n\n";
    static constexpr std::size_t kFrameSeparatorLength = 2;

    mutable std::mutex mutex_;
    bool retainRaw_ = true;
    bool resynchronising_ = false;
    std::string raw_;
    std::string pending_;
};

}  // namespace magda::mcp
