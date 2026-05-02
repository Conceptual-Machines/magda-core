#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

#include "../../audio/midi/MidiLearnSession.hpp"
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
     * @param paramIndex Parameter / macro / mod-param index — see owner for what this means.
     * @param owner      Target owner kind. Listeners interested in plugin params should
     *                   ignore events with owner != PluginParam, etc. The (path, paramIndex)
     *                   tuple alone is ambiguous because a macro index can collide with a
     *                   plugin-param index on the same device path.
     * @param learning   true = session started; false = session ended (captured or cancelled).
     */
    virtual void midiLearnStateChanged(const ChainNodePath& path, int paramIndex,
                                       ControlTarget::Kind owner, bool learning) = 0;

    /**
     * @brief Called when a learn session completes and a binding was created.
     *
     * The binding has already been added to BindingRegistry at this point.
     */
    virtual void midiLearnCompleted(const ChainNodePath& /*path*/, int /*paramIndex*/,
                                    ControlTarget::Kind /*owner*/, const Binding& /*binding*/) {}

    /**
     * @brief Called when clearMappings() removes one or more bindings.
     *
     * @param numRemoved Number of bindings removed (always >= 1 when called).
     */
    virtual void midiLearnCleared(const ChainNodePath& /*path*/, int /*paramIndex*/,
                                  ControlTarget::Kind /*owner*/, int /*numRemoved*/) {}
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
     * @brief Start a MIDI learn session for any control target.
     *
     * If a session is already active (for any target), it is cancelled first.
     * Notifies listeners with midiLearnStateChanged(true) for the new target.
     *
     * For DeviceMacro the path must be the macro's owning ChainNodePath
     * (Track / Rack / Device). For ModParam the path is the modifier's owning
     * scope.
     */
    void beginLearn(const ControlTarget& target, const juce::String& displayName);

    /**
     * @brief Cancel any active learn session without creating a binding.
     *
     * No-op if no session is active.
     */
    void cancelLearn();

    /**
     * @brief Return true if there is an active learn session targeting `target`.
     *
     * Compares the armed session against the supplied ControlTarget — kind,
     * path, and the kind-specific identity fields (paramIndex / modId+modParamIndex).
     */
    bool isLearning(const ControlTarget& target) const;

    /**
     * @brief Remove all bindings mapping to `target`.
     *
     * For PluginParam / ModParam this delegates to BindingRegistry::removeFor.
     * For DeviceMacro it delegates to BindingRegistry::removeStaticBindingsForMacro
     * so focused-device-macro automap-profile bindings stay intact and the macro
     * falls back to its profile mapping after the user clears their override.
     *
     * @return Number of bindings removed.
     */
    int clearMappings(const ControlTarget& target);

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

    void notifyStateChanged(const ChainNodePath& path, int paramIndex, ControlTarget::Kind owner,
                            bool learning);

    ControllerRouter* router_ = nullptr;

    // Armed session state (valid while a learn is in progress)
    bool armed_ = false;
    ChainNodePath armedPath_;
    int armedParam_ = -1;
    ControlTarget::Kind armedOwner_ = ControlTarget::Kind::PluginParam;
    ModId armedModId_ = INVALID_MOD_ID;
    int armedModParamIndex_ = -1;
    juce::String armedDisplayName_;

    // Internal helpers ---------------------------------------------------
    void armSession(const ChainNodePath& path, int paramIndex, ControlTarget::Kind owner,
                    ModId modId, int modParamIndex, const juce::String& displayName);

    BindingScope scope_ = BindingScope::Project;

    std::vector<MidiLearnCoordinatorListener*> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnCoordinator)
};

}  // namespace magda
