#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <vector>

#include "../aliases/Target.hpp"
#include "Binding.hpp"

namespace magda {

// ============================================================================
// BindingScope
// ============================================================================

/**
 * @brief Scope for binding storage.
 *
 * Global bindings are saved in the user config file.
 * Project bindings are saved in the .mgd project file.
 */
enum class BindingScope { Global, Project };

// ============================================================================
// BindingRegistryListener
// ============================================================================

class BindingRegistry;

class BindingRegistryListener {
  public:
    virtual ~BindingRegistryListener() = default;
    virtual void bindingRegistryChanged(BindingScope scope) = 0;
};

// ============================================================================
// BindingRegistry
// ============================================================================

/**
 * @brief Singleton registry of controller-to-parameter bindings.
 *
 * Two scopes: Global (persisted in config) and Project (persisted in .mgd).
 *
 * Thread-safety: mutations happen on the message thread; reads from the MIDI
 * thread are served via an atomic snapshot that is swapped on every mutation
 * across both scopes. Except where a query is explicitly marked MIDI-thread
 * safe below, all queries run on the message thread.
 */
class BindingRegistry {
  public:
    static BindingRegistry& getInstance();

    // ========================================================================
    // CRUD (per scope)
    // ========================================================================

    /** Add a binding to the given scope (updates if id already exists). */
    void add(BindingScope scope, const Binding& b);

    /** Update a binding in the given scope. No-op if not found. */
    void update(BindingScope scope, const Binding& b);

    /** Remove a binding from the given scope. No-op if not found. */
    void remove(BindingScope scope, const BindingId& id);

    /**
     * @brief Remove all bindings in a scope whose source.controllerId matches.
     *
     * Single snapshot rebuild + listener notification for the batch, rather
     * than once per binding. Returns the number removed.
     */
    int removeAllForController(BindingScope scope, const ControllerId& controllerId);

    /** True when any binding in any scope is keyed to this controllerId. */
    bool hasAnyBindingForController(const ControllerId& controllerId) const;

    // ========================================================================
    // Queries (message-thread)
    // ========================================================================

    /** Return all bindings in a scope. */
    std::vector<Binding> bindings(BindingScope scope) const;

    /**
     * @brief Find all bindings that match a given source.
     *
     * When a stored binding has channel == 0, it matches any channel.
     * When channel is specified (1..16), it matches only that channel.
     *
     * Thread-safe: reads from atomic snapshot -- safe to call from the MIDI thread.
     */
    std::vector<Binding> findForSource(const ControllerId& controllerId, BindingMsgType msgType,
                                       int channel, int number) const;

    /**
     * @brief Find all bindings whose source.portKey matches a live MIDI port.
     *
     * Matching uses magda::midi::matches so a stored portKey holding either
     * a JUCE identifier or a display name resolves against the live device.
     * This is the MIDI Learn dispatch path — bindings attach directly to a
     * port without needing a ControllerRegistry entry.
     *
     * Thread-safe: reads from atomic snapshot -- safe to call from the MIDI thread.
     */
    std::vector<Binding> findForPort(const juce::String& liveIdentifier,
                                     const juce::String& liveName, BindingMsgType msgType,
                                     int channel, int number) const;

    /**
     * @brief Find all bindings whose target resolves to a given ControlTarget.
     *
     * Resolves each binding's target using a DefaultChainContext +
     * TargetResolver, kind-filtered so a macro-targeted binding at (path, 0,
     * DeviceMacro) does not match a plugin-param query at (path, 0,
     * PluginParam).
     *
     * @return All matching bindings from both Global and Project scopes.
     */
    std::vector<Binding> findFor(const ControlTarget& target) const;

    /**
     * @brief Remove all bindings whose target resolves to the given ControlTarget.
     * @return Number of bindings removed.
     */
    int removeFor(const ControlTarget& target);

    /**
     * @brief True if any binding (Global + Project) resolves to a target on
     * this devicePath with the given owner kind AND has an active source.
     *
     * For the "is this device actively driven by a controller?" device-header
     * indicator. Bindings whose source controller is registered but disabled
     * are skipped, to match ControllerRouter's actual routing. Port-only
     * bindings (no controllerId, from the Learn path) always count.
     */
    bool hasBindingForDevice(const ChainNodePath& devicePath, ControlTarget::Kind owner) const;

    /**
     * @brief True if an active focused-device-macro resolver binding
     * resolves to this device: automap-profile coverage, regardless of user
     * Learn'd bindings on top.
     */
    bool hasResolverBindingForDevice(const ChainNodePath& devicePath) const;

    /**
     * @brief The owning controller of the first resolver (automap) binding
     * that resolves to this device, or nullopt.
     *
     * Lets the caller gate the automap indicator on that controller's live
     * connection/activation state rather than just config having a profile
     * mapping. See magda::controllers::isDeviceAutomapLive.
     */
    std::optional<ControllerId> resolverControllerForDevice(const ChainNodePath& devicePath) const;

    /**
     * @brief True if any active explicit user mapping (ControlTarget or
     * AliasRef) targets a parameter/macro/mod on this device. Excludes
     * resolver-based automap-profile bindings.
     */
    bool hasUserMappingForDevice(const ChainNodePath& devicePath) const;

    /**
     * @brief The owning controller of the first explicit user mapping
     * (excluding resolver automap) targeting this device, or nullopt.
     * Counterpart to resolverControllerForDevice, for the pinned
     * (user-mapped) indicator.
     */
    std::optional<ControllerId> userMappingControllerForDevice(
        const ChainNodePath& devicePath) const;

    /**
     * @brief True if any active binding (Global + Project) resolves to the
     * given ControlTarget.
     *
     * Same "active" semantics as hasBindingForDevice. Use for per-parameter
     * indicators (macro knobs, param slots, linkable sliders) instead of
     * `!findFor(...).empty()`, so the indicator drops when the binding's
     * controller is disabled.
     */
    bool hasActiveBindingFor(const ControlTarget& target) const;

    /**
     * @brief True if any active binding for this macro is an explicit static
     * target (owner=DeviceMacro) from a user Learn gesture, rather than an
     * automap resolver. Paints the macro indicator orange (Learn override)
     * vs green (profile default).
     */
    bool hasActiveStaticBindingForMacro(const ChainNodePath& devicePath, int macroIndex) const;

    /**
     * @brief Remove only user Learn'd static DeviceMacro bindings on (path,
     * macroIndex), leaving resolver (automap profile) bindings alone so the
     * macro falls back to its profile mapping.
     */
    int removeStaticBindingsForMacro(const ChainNodePath& devicePath, int macroIndex);

    /**
     * @brief True when an automap resolver binding at (devicePath,
     * macroIndex) is shadowed by an overlapping static PluginParam binding:
     * a Learn override is stealing the CC and the green automap dot should
     * drop.
     */
    bool isAutomapShadowedForMacro(const ChainNodePath& devicePath, int macroIndex) const;

    /**
     * @brief True when a static PluginParam binding at (devicePath,
     * paramIndex) overrides an overlapping resolver binding: the param is
     * stealing the CC from a profile-mapped macro and should paint a red
     * override dot. The resolver side is matched by declared kind regardless
     * of whether it currently resolves to a focused device, so this stays
     * stable across focus changes.
     */
    bool isPluginParamOverridingMacro(const ChainNodePath& devicePath, int paramIndex) const;

    // ========================================================================
    // Persistence
    // ========================================================================

    /** Load Global scope from config "globalBindings" juce::var array. */
    void loadGlobal(const juce::var& json);

    /** Serialize Global scope to a juce::var array. */
    juce::var saveGlobal() const;

    /** Load Project scope from project "projectBindings" juce::var array. */
    void loadProject(const juce::var& json);

    /** Serialize Project scope to a juce::var array. */
    juce::var saveProject() const;

    /** Clear all project-scope bindings (called on project close). */
    void clearProject();

    // ========================================================================
    // Listeners
    // ========================================================================

    void addListener(BindingRegistryListener* l);
    void removeListener(BindingRegistryListener* l);

  private:
    BindingRegistry() = default;

    static std::vector<Binding> decodeArray(const juce::var& json);
    static juce::var encodeArray(const std::vector<Binding>& bindings);

    void rebuildSnapshot();
    void notifyListeners(BindingScope scope);

    // Message-thread storage
    std::vector<Binding> globalBindings_;
    std::vector<Binding> projectBindings_;

    // Lock-free read snapshot combining both scopes for MIDI thread.
    // Use std::atomic_store/atomic_load free functions (C++11/14) since
    // std::atomic<std::shared_ptr<T>> requires C++20.
    std::shared_ptr<const std::vector<Binding>> snapshot_{
        std::make_shared<const std::vector<Binding>>()};

    std::vector<BindingRegistryListener*> listeners_;
};

}  // namespace magda
