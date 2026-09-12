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
 * Documents written before #2317 duplicate the parameters as `Doc::params`, in
 * a domain that depends on which build captured them. This module applies that
 * chronology once at load, filling only entries the model array is missing.
 */

/// What the container that carried the saved state says about the build that
/// wrote it, for documents predating the `paramsAreDisplayDomain` marker.
struct Provenance {
    /// True when the container's `magdaVersion` is older than 0.20; an unmarked
    /// document from it holds values in the capturing plugin's display range.
    bool savedBeforeWrapperCutover = false;
};

/// Provenance from a container's saved MAGDA version string ("0.19.2").
/// Unparseable or missing versions are read as old.
Provenance provenanceFromMagdaVersion(const juce::String& version);

/// Fill `device.parameters` entries missing from the model out of the pre-#2317
/// duplicate record in `device.pluginState`, in the display domain. No-op for
/// external devices and unreadable state. Returns true when the device changed.
bool hydrateParametersFromDeviceState(DeviceInfo& device, const Provenance& provenance = {});

/// Hydrate, then seed the parameters the device declares but nothing saved (#2613).
/// Seeding first would make a saved value look like one the model already had.
void completeDeviceParameters(DeviceInfo& device, const Provenance& provenance = {});

/// Every internal device in a chain-element list, racks and pad chains included.
void hydrateChainElements(std::vector<ChainElement>& elements, const Provenance& provenance = {});
void hydrateRack(RackInfo& rack, const Provenance& provenance = {});

/// Every internal device staged for a project load. Runs after the retired-device
/// aliases and the param-index migrations, before any engine projection exists.
void hydrateStagedProject(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                          const juce::String& magdaVersion);

}  // namespace magda::daw::audio::device_state_hydration
