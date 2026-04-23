#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

#include "../../audio/MidiLearnSession.hpp"
#include "Binding.hpp"
#include "BindingRegistry.hpp"

namespace magda {

class ControllerRouter;

// ============================================================================
// MidiLearnCoordinatorListener
// ============================================================================

/**
 * @brief Observer interface for MIDI Learn state changes.
 *
 * All callbacks are delivered on the JUCE message thread.
 */
class MidiLearnCoordinatorListener {
  public:
    virtual ~MidiLearnCoordinatorListener() = default;

    /**
     * @brief Called when a learn session starts or ends for a specific parameter.
     *
     * @param path       Device path of the parameter being learned (or that was learned).
     * @param paramIndex Parameter index within the device.
     * @param learning   true = session started; false = session ended (captured or cancelled).
     */
    virtual void midiLearnStateChanged(const ChainNodePath& path, int paramIndex,
                                       bool learning) = 0;

    /**
     * @brief Called when a learn session completes and a binding was created.
     *
     * The binding has already been added to BindingRegistry at this point.
     */
    virtual void midiLearnCompleted(const ChainNodePath& /*path*/, int /*paramIndex*/,
                                    const Binding& /*binding*/) {}

    /**
     * @brief Called when clearMappings() removes one or more bindings.
     *
     * @param numRemoved Number of bindings removed (always >= 1 when called).
     */
    virtual void midiLearnCleared(const ChainNodePath& /*path*/, int /*paramIndex*/,
                                  int /*numRemoved*/) {}
};

// ============================================================================
// MidiLearnCoordinator
// ============================================================================

/**
 * @brief Application-level coordinator for MIDI Learn workflows.
 *
 * Singleton. Bridges the UI (param right-click menu, pulsing border) with the
 * headless ControllerRouter learn session API.
 *
 * One-at-a-time semantics: starting a new learn session implicitly cancels the
 * previous one. Listeners are notified of state transitions on the message thread.
 *
 * Usage:
 *   1. Call attach(ControllerRouter::getInstance()) during startup.
 *   2. Call setScope() to seed the default scope from Config.
 *   3. UI components call beginLearn() on right-click "Learn MIDI".
 *   4. Listeners receive midiLearnStateChanged(true) -> pulse border.
 *   5. On capture: midiLearnCompleted() -> stop pulsing; show toast.
 *   6. On cancelLearn(): midiLearnStateChanged(false).
 */
class MidiLearnCoordinator {
  public:
    static MidiLearnCoordinator& getInstance();

    // ========================================================================
    // Setup
    // ========================================================================

    /** Wire the coordinator to a ControllerRouter. Must be called before any learn
     *  sessions begin. Non-owning; the router must outlive the coordinator. */
    void attach(ControllerRouter& router);

    // ========================================================================
    // Listener management
    // ========================================================================

    void addListener(MidiLearnCoordinatorListener* l);
    void removeListener(MidiLearnCoordinatorListener* l);

    // ========================================================================
    // Learn control
    // ========================================================================

    /**
     * @brief Start a MIDI learn session for a parameter.
     *
     * If a session is already active (for any parameter), it is cancelled first.
     * Notifies listeners with midiLearnStateChanged(true) for the new param.
     *
     * @param path        Device path of the target parameter.
     * @param paramIndex  Parameter index within the device.
     * @param displayName Human-readable name used in toast messages.
     */
    void beginLearn(const ChainNodePath& path, int paramIndex, const juce::String& displayName);

    /**
     * @brief Cancel any active learn session without creating a binding.
     *
     * No-op if no session is active.
     */
    void cancelLearn();

    /**
     * @brief Return true if there is an active learn session for (path, paramIndex).
     */
    bool isLearning(const ChainNodePath& path, int paramIndex) const;

    /**
     * @brief Remove all bindings mapping to (path, paramIndex).
     *
     * Calls BindingRegistry::removeForTarget() and notifies listeners.
     *
     * @return Number of bindings removed.
     */
    int clearMappings(const ChainNodePath& path, int paramIndex);

    // ========================================================================
    // Scope
    // ========================================================================

    /**
     * @brief Set the scope used when adding new bindings.
     *
     * Default is BindingScope::Project.
     */
    void setScope(BindingScope scope) {
        scope_ = scope;
    }

    BindingScope getScope() const {
        return scope_;
    }

  private:
    MidiLearnCoordinator() = default;

    // Called from router's callAsync (message thread) when a MIDI event is captured.
    void onCapture(const LearnCapture& capture);

    void notifyStateChanged(const ChainNodePath& path, int paramIndex, bool learning);

    ControllerRouter* router_ = nullptr;

    // Armed session state (valid while a learn is in progress)
    bool armed_ = false;
    ChainNodePath armedPath_;
    int armedParam_ = -1;
    juce::String armedDisplayName_;

    BindingScope scope_ = BindingScope::Project;

    std::vector<MidiLearnCoordinatorListener*> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnCoordinator)
};

}  // namespace magda
