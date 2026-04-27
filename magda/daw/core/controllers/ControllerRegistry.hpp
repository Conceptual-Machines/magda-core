#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <vector>

#include "Controller.hpp"

namespace magda {

// ============================================================================
// ControllerRegistryListener
// ============================================================================

class ControllerRegistry;

class ControllerRegistryListener {
  public:
    virtual ~ControllerRegistryListener() = default;
    virtual void controllerRegistryChanged() = 0;
};

// ============================================================================
// ControllerRegistry
// ============================================================================

/**
 * @brief Singleton registry of MIDI controller devices.
 *
 * Thread-safety: mutations happen on the message thread; reads may happen on
 * the MIDI thread via an atomic snapshot pointer that is swapped on every
 * mutation.
 */
class ControllerRegistry {
  public:
    static ControllerRegistry& getInstance();

    // ========================================================================
    // CRUD
    // ========================================================================

    /** Add a controller (or update if id already exists). */
    void add(const Controller& c);

    /** Update an existing controller by id. No-op if not found. */
    void update(const Controller& c);

    /** Remove a controller by id. No-op if not found. */
    void remove(const ControllerId& id);

    /**
     * @brief Re-resolve stored inputPort identifiers against a live MIDI input list.
     *
     * For each controller whose inputPort is not in the live list, try to match its
     * inputPortName against a live device display name and rewrite inputPort to the
     * current identifier. Returns true if any controller was updated.
     *
     * Caller should persist (saveToConfig → Config::save) when this returns true.
     */
    bool rematchInputPorts(const juce::Array<juce::MidiDeviceInfo>& liveInputs);

    // ========================================================================
    // Queries (message-thread)
    // ========================================================================

    /** Return all controllers (message thread copy). */
    std::vector<Controller> all() const;

    /** Find a controller by id. */
    std::optional<Controller> find(const ControllerId& id) const;

    /** Find a controller by MIDI input port identifier. */
    std::optional<Controller> findByInputPort(const juce::String& portId) const;

    /**
     * @brief Return true if portId is registered as a controller input port.
     *
     * Used by AudioBridge to exclude controller ports from instrument routing,
     * and by ControllerRouter to filter MidiBridge callbacks.
     *
     * Thread-safe: reads from atomic snapshot.
     */
    bool isControllerInputPort(const juce::String& portId) const;

    /**
     * @brief Overload matching a live MIDI device by identifier OR display name.
     *
     * Matching is done via magda::midi::matches so a stored entry that holds
     * the device name (e.g. after moving a project between machines or OSes
     * where JUCE identifier formats differ) still resolves correctly.
     *
     * Thread-safe: reads from atomic snapshot.
     */
    bool isControllerInputPort(const juce::String& liveIdentifier,
                               const juce::String& liveName) const;

    // ========================================================================
    // Persistence
    // ========================================================================

    /** Load from the "controllers" array in config (juce::var array of objects). */
    void loadFromConfig(const juce::var& json);

    /** Serialize to a juce::var array suitable for Config storage. */
    juce::var saveToConfig() const;

    // ========================================================================
    // Listeners
    // ========================================================================

    void addListener(ControllerRegistryListener* l);
    void removeListener(ControllerRegistryListener* l);

  private:
    ControllerRegistry() = default;

    void rebuildSnapshot();
    void notifyListeners();

    // Message-thread storage
    std::vector<Controller> controllers_;

    // Lock-free read snapshot for MIDI thread.
    // Use std::shared_ptr with std::atomic_store/atomic_load (C++11/14 free
    // functions) since std::atomic<std::shared_ptr<T>> requires C++20.
    std::shared_ptr<const std::vector<Controller>> snapshot_{
        std::make_shared<const std::vector<Controller>>()};

    std::vector<ControllerRegistryListener*> listeners_;
};

}  // namespace magda
