#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>

#include "../../core/controllers/BindingRegistry.hpp"
#include "../../core/controllers/BindingTransform.hpp"
#include "../../core/controllers/ControllerRegistry.hpp"
#include "MidiBridge.hpp"
#include "controllers/ControllerFeedback.hpp"
#include "controllers/ControllerParamWriter.hpp"
#include "midi/MidiLearnSession.hpp"

namespace magda {

// ============================================================================
// ControllerRouter
// ============================================================================

/**
 * @brief Routes raw MIDI from controller ports to plugin parameters.
 *
 * Singleton. Listens to ControllerRegistry and BindingRegistry for
 * reconfiguration signals. When a MIDI message arrives from a controller
 * port, it:
 *   1. Looks up matching bindings via BindingRegistry::findForSource().
 *   2. Applies the binding's mode transform on the calling (MIDI) thread.
 *   3. For relative bindings: updates per-binding accumulators.
 *   4. Hops to the message thread via callAsync.
 *   5. Resolves the target and calls ControllerParamWriter::write().
 *   6. Emits a FeedbackEvent to the feedback sink.
 *
 * Phase C: real MidiBridge hook is added in Phase D. Use
 * injectMessageForTest() as the primary entry point.
 */
class ControllerRouter : public ControllerRegistryListener,
                         public BindingRegistryListener,
                         public RawMidiListener {
  public:
    static ControllerRouter& getInstance();

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * @brief Prepare the router (Phase C: no MidiBridge wiring).
     *
     * Subscribes to registries for reconfiguration.
     */
    void reconfigure();

    /**
     * @brief Wire to MidiBridge for real MIDI input (Phase D).
     *
     * Registers as a RawMidiListener on the bridge. Call after reconfigure().
     * Passing nullptr unsubscribes from any previously set bridge.
     */
    void setMidiBridge(MidiBridge* bridge);

    /**
     * @brief Cleanly shut down the router.
     *
     * Unregisters from MidiBridge and registry listeners. Must be called before
     * MidiBridge is destroyed.
     */
    void shutdown();

    // ========================================================================
    // Injection (for tests and Phase C without real MidiBridge)
    // ========================================================================

    /**
     * @brief Inject a MIDI message as if it arrived from the given port.
     *
     * Thread-safe: may be called from any thread (mimics the real MIDI callback).
     * portName defaults to empty; supply it to exercise the identifier-vs-name
     * match fallback.
     */
    void injectMessageForTest(const juce::String& portId, const juce::MidiMessage& msg,
                              const juce::String& portName = {});

    // ========================================================================
    // Dependency injection (tests)
    // ========================================================================

    /** Replace the parameter writer (default: DefaultControllerParamWriter). */
    void setParamWriter(std::unique_ptr<ControllerParamWriter> writer);

    /** Replace the feedback sink (default: NullSink). */
    void setFeedbackSink(std::unique_ptr<ControllerFeedbackSink> sink);

    // ========================================================================
    // MIDI Learn session
    // ========================================================================

    /** Callback type fired on the message thread when a qualifying message is captured. */
    using LearnCallback = std::function<void(const LearnCapture&)>;

    /**
     * @brief Start a MIDI learn session.
     *
     * Any in-progress session is cancelled first. The next qualifying MIDI
     * message (CC, Note-on, or Pitch-wheel) fires onCaptured on the message
     * thread. Note-off messages arriving within captureDebounceMs of the
     * corresponding Note-on are silently discarded.
     *
     * May be called from any thread.
     */
    void beginLearnSession(LearnSessionConfig cfg, LearnCallback onCaptured);

    /**
     * @brief Cancel any active learn session without firing the callback.
     *
     * Safe to call even when no session is active. May be called from any thread.
     */
    void cancelLearnSession();

    /** Returns true when a learn session is currently active. Thread-safe. */
    bool isLearning() const;

    // ========================================================================
    // Registry listener callbacks
    // ========================================================================

    void controllerRegistryChanged() override;
    void bindingRegistryChanged(BindingScope scope) override;

    // ========================================================================
    // Raw MIDI entry point (called from MIDI thread)
    // ========================================================================

    /**
     * @brief Called when a raw MIDI message arrives from a controller port.
     *
     * Must be safe to call from the MIDI thread (lock-free read path + callAsync hop).
     * Accepts both the OS-native identifier and the display name so that stored
     * Controller.inputPort values written as either form resolve consistently
     * via magda::midi::matches.
     */
    void onMidiFromControllerPort(const juce::String& portId, const juce::String& portName,
                                  const juce::MidiMessage& msg);

    // RawMidiListener implementation (wired via setMidiBridge)
    void onRawMidi(const juce::String& deviceId, const juce::String& deviceName,
                   const juce::MidiMessage& msg) override;

  private:
    ControllerRouter() = default;

    MidiBridge* midiBridge_ = nullptr;

    // ---- MIDI Learn ----
    struct LearnState {
        LearnSessionConfig cfg;
        LearnCallback cb;
        std::atomic<bool> active{false};
        juce::int64 armedAtMs = 0;     // Time beginLearnSession was called
        juce::int64 lastNoteOnMs = 0;  // Time of last Note-on captured
    };

    std::unique_ptr<LearnState> learn_;
    juce::CriticalSection learnLock_;  // guards learn_ pointer swap

    // Per-binding runtime state (message-thread only after callAsync hop)
    struct BindingRuntimeState {
        float accumulator = 0.0f;  // for relative modes: accumulated normalized value
        ToggleState toggleState;   // for toggle mode
    };

    // Per-binding pending write state (updated from MIDI thread, consumed on message thread).
    // Raw integer value is stored so mode computation (including accumulation) happens
    // on the message thread where per-binding state lives.
    struct PendingWrite {
        std::atomic<int> pendingRaw{0};
        std::atomic<int> pendingRawMax{127};
        std::atomic<bool> inFlight{false};
    };

    // Schedule a parameter write for a binding.
    // Called from MIDI thread (or test thread). Stores raw MIDI value and
    // schedules a callAsync if not already in flight.
    void scheduleWrite(const BindingId& bindingId, int rawValue, int rawMax,
                       const Binding& binding);

    // Execute pending write on the message thread.
    void executeWrite(const BindingId& bindingId, int rawValue, int rawMax, const Binding& binding);

    std::unique_ptr<ControllerParamWriter> paramWriter_;
    std::unique_ptr<ControllerFeedbackSink> feedbackSink_{std::make_unique<NullSink>()};

    // Per-binding message-thread runtime state (keyed by BindingId string)
    std::unordered_map<juce::String, BindingRuntimeState> runtimeState_;

    // Per-binding pending write (keyed by BindingId string)
    // Stored as shared_ptr so the callAsync lambda captures it safely
    std::unordered_map<juce::String, std::shared_ptr<PendingWrite>> pendingWrites_;

    bool configured_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllerRouter)
};

}  // namespace magda
