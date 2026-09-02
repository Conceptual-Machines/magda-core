#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "core/DeviceInfo.hpp"
#include "core/RackInfo.hpp"

namespace magda {
struct TrackInfo;
}

namespace magda::daw::audio::device_state_hydration {

/**
 * One-time load migration for the retired dual parameter authority (#2317).
 *
 * `DeviceInfo::parameters` is the sole persisted authority for an internal
 * device's automatable parameters, in the device's DISPLAY domain. Documents
 * written since #2317 carry no parameter record at all. Documents written
 * before it duplicate the parameters as `Doc::params`, in a domain that
 * depends on which build captured them; this module is the one place that
 * still knows that chronology, and it applies it exactly once - at load,
 * before either engine projection is constructed.
 *
 * Hydration never overwrites a parameter the model already carries: the model
 * array is the authority and the duplicate record is only consulted for
 * entries the array is missing (a state-only device preset, an imported
 * chain, a hand-edited file). The duplicate itself is not deleted here; it
 * disappears the next time the device is captured, because capture no longer
 * writes one.
 */

/// What the container that carried the saved state says about the build that
/// wrote it. Documents predating the `paramsAreDisplayDomain` marker cannot
/// say on their own which domain they used; the container sometimes can.
struct Provenance {
    /// True when the container is known to have been written before the
    /// wrapper cutover (a project whose `magdaVersion` is older than 0.20).
    /// An unmarked document from such a container can only hold values in the
    /// capturing plugin's display range.
    bool savedBeforeWrapperCutover = false;
};

/// Provenance from a container's saved MAGDA version string ("0.19.2").
/// Unparseable or missing versions are read as old: every build that wrote a
/// version at all predates the cutover or wrote the marker.
Provenance provenanceFromMagdaVersion(const juce::String& version);

/// Hydrate `device.parameters` entries missing from the model out of the
/// pre-#2317 duplicate parameter record in `device.pluginState`, converting to
/// the display domain. No-op for external devices, empty or unreadable state,
/// documents without a parameter record, and parameters the model already
/// carries. Returns true when the device was changed.
bool hydrateParametersFromDeviceState(DeviceInfo& device, const Provenance& provenance = {});

/// Every internal device in a chain-element list (racks and pad chains
/// included), for presets and imported chains. RackInfo overload for rack
/// presets.
void hydrateChainElements(std::vector<ChainElement>& elements, const Provenance& provenance = {});
void hydrateRack(RackInfo& rack, const Provenance& provenance = {});

/// Every internal device staged for a project load, with provenance from the
/// file's `magdaVersion`. Belongs at the end of the load-time migration chain:
/// after the retired-device aliases (so ids are canonical) and the param-index
/// migrations (so the array hydration merges against is current), before any
/// engine projection exists.
void hydrateStagedProject(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                          const juce::String& magdaVersion);

}  // namespace magda::daw::audio::device_state_hydration
