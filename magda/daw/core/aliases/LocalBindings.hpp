#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>

#include "../SelectionManager.hpp"
#include "../TypeIds.hpp"

namespace magda {

/**
 * @brief Plan-local DSL name bindings (the '$' sigil namespace).
 *
 * Holds short, user-facing names bound to concrete paths/IDs during the
 * execution of a single automation plan or DSL script. Bindings do not
 * persist across plan executions.
 *
 * Usage:
 *   bindings.bindDevice("mysynth", ChainNodePath::chainDevice(...));
 *   bindings.bindTrack("lead", trackId);
 *   auto path = bindings.lookupDevice("mysynth");
 */
class LocalBindings {
  public:
    // ========================================================================
    // Device bindings (by name -> ChainNodePath)
    // ========================================================================

    void bindDevice(const juce::String& name, const ChainNodePath& path) {
        deviceBindings_[name] = path;
    }

    std::optional<ChainNodePath> lookupDevice(const juce::String& name) const {
        auto it = deviceBindings_.find(name);
        if (it != deviceBindings_.end())
            return it->second;
        return std::nullopt;
    }

    // ========================================================================
    // Track bindings (by name -> TrackId)
    // ========================================================================

    void bindTrack(const juce::String& name, TrackId id) {
        trackBindings_[name] = id;
    }

    std::optional<TrackId> lookupTrack(const juce::String& name) const {
        auto it = trackBindings_.find(name);
        if (it != trackBindings_.end())
            return it->second;
        return std::nullopt;
    }

    // ========================================================================
    // Housekeeping
    // ========================================================================

    void clear() {
        deviceBindings_.clear();
        trackBindings_.clear();
    }

    bool hasDevice(const juce::String& name) const {
        return deviceBindings_.count(name) > 0;
    }

    bool hasTrack(const juce::String& name) const {
        return trackBindings_.count(name) > 0;
    }

    int deviceCount() const {
        return static_cast<int>(deviceBindings_.size());
    }

    int trackCount() const {
        return static_cast<int>(trackBindings_.size());
    }

  private:
    std::map<juce::String, ChainNodePath> deviceBindings_;
    std::map<juce::String, TrackId> trackBindings_;
};

}  // namespace magda
