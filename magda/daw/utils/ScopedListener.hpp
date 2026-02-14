#pragma once

namespace magda {

/**
 * RAII guard for Broadcaster/Listener registration.
 *
 * Prevents use-after-free bugs caused by C++ member destruction order:
 * declare the ScopedListener *before* the data it guards so it is
 * destroyed (and unregisters) first.
 *
 * Non-copyable, non-movable — one guard per registration.
 */
template <typename Broadcaster, typename Listener> class ScopedListener {
  public:
    // Empty — no registration yet
    explicit ScopedListener(Listener* listener) : listener_(listener) {}

    // Register immediately
    ScopedListener(Broadcaster& broadcaster, Listener* listener)
        : broadcaster_(&broadcaster), listener_(listener) {
        broadcaster_->addListener(listener_);
    }

    ~ScopedListener() {
        if (broadcaster_)
            broadcaster_->removeListener(listener_);
    }

    // Swap broadcaster (for setController-style APIs)
    void reset(Broadcaster* b = nullptr) {
        if (broadcaster_)
            broadcaster_->removeListener(listener_);
        broadcaster_ = b;
        if (broadcaster_)
            broadcaster_->addListener(listener_);
    }

    void reset(Broadcaster& b) {
        reset(&b);
    }

    // Access the current broadcaster (may be nullptr)
    Broadcaster* get() const {
        return broadcaster_;
    }

    // Non-copyable, non-movable
    ScopedListener(const ScopedListener&) = delete;
    ScopedListener& operator=(const ScopedListener&) = delete;
    ScopedListener(ScopedListener&&) = delete;
    ScopedListener& operator=(ScopedListener&&) = delete;

  private:
    Broadcaster* broadcaster_ = nullptr;
    Listener* listener_;
};

}  // namespace magda
