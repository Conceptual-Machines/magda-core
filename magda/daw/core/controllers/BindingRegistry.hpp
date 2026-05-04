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
 * across both scopes.
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
     * Resolves each binding's target using a DefaultChainContext + TargetResolver.
     * Must be called on the message thread.
     *
     * Kind-filtered so a macro-targeted binding at (path, 0, DeviceMacro) does NOT
     * match a plugin-param query at (path, 0, PluginParam).
     *
     * @return All matching bindings from both Global and Project scopes.
     */
    std::vector<Binding> findFor(const ControlTarget& target) const;

    /**
     * @brief Remove all bindings whose target resolves to the given ControlTarget.
     *
     * Convenience wrapper: calls findFor, then removes each match from its scope.
     * Must be called on the message thread.
     *
     * @return Number of bindings removed.
     */
    int removeFor(const ControlTarget& target);

    /**
     * @brief Return true if any binding (Global + Project) resolves to a target
     * on this devicePath with the given owner kind AND has an active source.
     *
     * Used by device-header indicators to answer "is this device actively
     * being driven by a controller?". Bindings whose source controller is
     * registered but disabled are skipped so the indicator matches the
     * ControllerRouter's actual routing behavior. Port-only bindings (no
     * controllerId, from the Learn path) always count.
     *
     * Must be called on the message thread.
     */
    bool hasBindingForDevice(const ChainNodePath& devicePath, ControlTarget::Kind owner) const;

    /**
     * @brief Return true if any active focused-device-macro resolver binding
     * currently resolves to this device — i.e. the device has automap-profile
     * coverage, regardless of any user Learn'd bindings on top.
     */
    bool hasResolverBindingForDevice(const ChainNodePath& devicePath) const;

    /**
     * @brief Return true if any active explicit user mapping (ControlTarget or
     * AliasRef) targets a parameter / macro / mod on this device. Excludes
     * resolver-based automap-profile bindings.
     */
    bool hasUserMappingForDevice(const ChainNodePath& devicePath) const;

    /**
     * @brief Return true if any active binding (Global + Project) resolves to the
     * given ControlTarget.
     *
     * Same "active" semantics as hasBindingForDevice — bindings whose source
     * controller is registered but disabled are skipped. Use this for
     * per-parameter indicators (macro knobs, param slots, linkable sliders)
     * instead of `!findFor(...).empty()` so the indicator disappears when the
     * binding's controller is disabled.
     *
     * Must be called on the message thread.
     */
    bool hasActiveBindingFor(const ControlTarget& target) const;

    /**
     * @brief Return true if any active binding (Global + Project) for this macro
     * is an explicit static target (ControlTarget owner=DeviceMacro) — i.e. came
     * from a user MIDI Learn gesture, not an automap profile resolver.
     *
     * Used to paint the macro indicator orange (Learn override) vs green (profile
     * default). Same "active" semantics as hasActiveBindingFor.
     */
    bool hasActiveStaticBindingForMacro(const ChainNodePath& devicePath, int macroIndex) const;

    /**
     * @brief Remove only user Learn'd static DeviceMacro bindings on (path, macroIndex).
     *
     * Mirrors hasActiveStaticBindingForMacro: leaves focused-device-macro
     * resolver bindings (automap profile defaults) alone so the macro falls
     * back to its profile mapping after the user clears their override.
     */
    int removeStaticBindingsForMacro(const ChainNodePath& devicePath, int macroIndex);

    /**
     * @brief Return true when an automap profile binding (focused-device-macro
     * resolver) targeting this (devicePath, macroIndex) is shadowed by an
     * overlapping static PluginParam binding — i.e. a MIDI Learn override is
     * stealing the CC and the green automap dot should drop.
     *
     * Must be called on the message thread.
     */
    bool isAutomapShadowedForMacro(const ChainNodePath& devicePath, int macroIndex) const;

    /**
     * @brief Return true when a static PluginParam binding at (devicePath,
     * paramIndex) is overriding an overlapping focused-device-macro resolver
     * binding — i.e. the param is stealing the CC from a profile-mapped macro
     * and should paint a red override dot.
     *
     * The resolver-side binding is matched by its declared kind, regardless of
     * whether it currently resolves to a focused device, so the indicator is
     * stable across focus changes.
     *
     * Must be called on the message thread.
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
