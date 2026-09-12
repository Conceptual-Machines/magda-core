#pragma once

#include <map>
#include <span>
#include <vector>

#include "AutomationInfo.hpp"
#include "ChainNodePath.hpp"
#include "ControlTarget.hpp"
#include "TrackInfo.hpp"

/**
 * @file AddressedParameters.hpp
 * @brief Which of a device's parameters something addresses (#2630).
 *
 * A hosted plugin's value is mirrored only for a slot something addresses; the
 * chunk carries the rest (#2629).
 */

namespace magda {

struct AutomationLaneInfo;
struct TrackInfo;

/** @brief Everything that can address a parameter of a device. */
struct AddressingSources {
    std::span<const TrackInfo> tracks;
    const TrackInfo* master = nullptr;

    /// Every lane the project holds, playing or not: one that is not playing
    /// still holds the value the user drew against.
    std::span<const AutomationLaneInfo> lanes;

    /// Controller bindings, MIDI learn and the AI's aliases, resolved to
    /// targets by the registries that own them.
    std::span<const ControlTarget> bound;
};

/** @brief The addressed slots of every device in one project. */
class AddressedParameters {
  public:
    static AddressedParameters from(const AddressingSources& sources);

    /// @p devicePath's addressed slots, ascending and without repeats.
    std::span<const int> forDevice(const ChainNodePath& devicePath) const;

    bool addresses(const ChainNodePath& devicePath, int paramIndex) const;

    /// How many devices have at least one addressed slot.
    int numDevices() const {
        return static_cast<int>(byDevice_.size());
    }

  private:
    std::map<ChainNodePath, std::vector<int>> byDevice_;
};

}  // namespace magda
