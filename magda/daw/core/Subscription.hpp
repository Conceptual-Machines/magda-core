// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <utility>

namespace magda {

/**
 * @brief RAII subscription handle - automatically unsubscribes on destruction
 *
 * Like Python's context manager - cleanup is automatic when scope ends.
 * Move-only to prevent double-unsubscribe.
 *
 * Usage:
 *   Subscription sub = observable.onChange([this]() { repaint(); });
 *   // ... sub auto-unsubscribes when destroyed
 */
class Subscription {
  public:
    Subscription() = default;

    explicit Subscription(std::function<void()> unsubscribe)
        : unsubscribe_(std::move(unsubscribe)) {}

    ~Subscription() {
        reset();
    }

    // Move-only
    Subscription(Subscription&& other) noexcept
        : unsubscribe_(std::exchange(other.unsubscribe_, nullptr)) {}

    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            reset();
            unsubscribe_ = std::exchange(other.unsubscribe_, nullptr);
        }
        return *this;
    }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    // Manually unsubscribe (also called by destructor)
    void reset() {
        if (unsubscribe_) {
            unsubscribe_();
            unsubscribe_ = nullptr;
        }
    }

    // Check if subscription is active
    explicit operator bool() const {
        return unsubscribe_ != nullptr;
    }

  private:
    std::function<void()> unsubscribe_;
};

}  // namespace magda
