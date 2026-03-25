#pragma once

#include <atomic>
#include <memory>
#include <type_traits>

namespace magda {

/**
 * Shared flag that tracks whether an object is still alive.
 *
 * The owner creates a LifetimeFlag and exposes it via getLifetimeFlag().
 * Observers hold a copy (shared_ptr) and check *flag_ before accessing
 * the owner. When the owner is destroyed, the flag is set to false
 * automatically — no manual cleanup needed.
 *
 * This prevents use-after-free in listener/observer patterns where
 * destruction order between broadcaster and listener is unpredictable.
 */
class LifetimeFlag {
  public:
    LifetimeFlag() : flag_(std::make_shared<std::atomic<bool>>(true)) {}
    ~LifetimeFlag() {
        *flag_ = false;
    }

    /** Get the shared flag. Observers hold this to check aliveness. */
    std::shared_ptr<std::atomic<bool>> getFlag() const {
        return flag_;
    }

    LifetimeFlag(const LifetimeFlag&) = delete;
    LifetimeFlag& operator=(const LifetimeFlag&) = delete;
    LifetimeFlag(LifetimeFlag&&) = delete;
    LifetimeFlag& operator=(LifetimeFlag&&) = delete;

  private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

namespace detail {
// SFINAE detection for getLifetimeFlag() method
template <typename T, typename = void> struct HasLifetimeFlag : std::false_type {};

template <typename T>
struct HasLifetimeFlag<T, std::void_t<decltype(std::declval<T>().getLifetimeFlag())>>
    : std::true_type {};
}  // namespace detail

/**
 * RAII guard for Broadcaster/Listener registration.
 *
 * If the Broadcaster has a LifetimeFlag (detected via getLifetimeFlag()),
 * the guard acquires it and checks aliveness before calling removeListener
 * in the destructor. This makes destruction-order bugs impossible.
 *
 * For broadcasters without a LifetimeFlag, falls back to raw pointer
 * behavior (still better than manual removeListener calls).
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
        acquireLifetimeFlag();
        broadcaster_->addListener(listener_);
    }

    ~ScopedListener() {
        unregister();
    }

    // Swap broadcaster (for setController-style APIs)
    void reset(Broadcaster* b = nullptr) {
        unregister();
        broadcaster_ = b;
        aliveFlag_.reset();
        if (broadcaster_) {
            acquireLifetimeFlag();
            broadcaster_->addListener(listener_);
        }
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
    void unregister() {
        if (!broadcaster_)
            return;
        // If we have a lifetime flag, only touch broadcaster if it's alive
        if (aliveFlag_) {
            if (*aliveFlag_)
                broadcaster_->removeListener(listener_);
        } else {
            broadcaster_->removeListener(listener_);
        }
        broadcaster_ = nullptr;
    }

    void acquireLifetimeFlag() {
        if constexpr (detail::HasLifetimeFlag<Broadcaster>::value) {
            if (broadcaster_)
                aliveFlag_ = broadcaster_->getLifetimeFlag().getFlag();
        }
    }

    Broadcaster* broadcaster_ = nullptr;
    Listener* listener_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_;
};

}  // namespace magda
