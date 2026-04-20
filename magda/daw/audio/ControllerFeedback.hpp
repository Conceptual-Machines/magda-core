#pragma once

#include "../core/controllers/Binding.hpp"

namespace magda {

// ============================================================================
// FeedbackEvent
// ============================================================================

/**
 * @brief Emitted by ControllerRouter after a successful parameter write.
 *
 * Reserved for future LED / motor-fader feedback implementations.
 * For 0.6.0 only NullSink is wired.
 */
struct FeedbackEvent {
    BindingId bindingId;
    float value;  // final normalized value in [0,1]
};

// ============================================================================
// ControllerFeedbackSink
// ============================================================================

/**
 * @brief Abstract sink for controller feedback events.
 */
class ControllerFeedbackSink {
  public:
    virtual ~ControllerFeedbackSink() = default;
    virtual void send(const FeedbackEvent&) = 0;
};

// ============================================================================
// NullSink
// ============================================================================

/**
 * @brief No-op feedback sink (default).
 */
class NullSink : public ControllerFeedbackSink {
  public:
    void send(const FeedbackEvent&) override {}
};

}  // namespace magda
