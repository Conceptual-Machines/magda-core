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
        for (auto split = pending_.find("\n\n"); split != std::string::npos;
             split = pending_.find("\n\n")) {
            const auto frame = pending_.substr(0, split);
            pending_.erase(0, split + 2);
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

    /// Bytes currently retained against the possibility that this response is
    /// an error body rather than a stream. Exposed so the bound is assertable:
    /// it is the difference between a subscription that costs nothing to keep
    /// open and one that grows for the life of the process.
    std::size_t retainedBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return raw_.size();
    }

  private:
    /// Enough for any JSON-RPC error body a refusal can carry, and far below
    /// what a stream would reach.
    static constexpr std::size_t kMaxRetainedRaw = 64 * 1024;

    mutable std::mutex mutex_;
    bool retainRaw_ = true;
    std::string raw_;
    std::string pending_;
};

}  // namespace magda::mcp
