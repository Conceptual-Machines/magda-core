#pragma once

#include <juce_core/juce_core.h>

#include <magda/sdk/device/ParameterManifest.hpp>
#include <optional>
#include <vector>

namespace magda::daw::audio {

/** @brief One registered device's manifest, or why it has none. */
struct DeviceManifestEntry {
    juce::String pluginId;

    /// Null when the device is skipped.
    std::optional<sdk::DeviceManifest> manifest;
    juce::String skipReason;
};

/**
 * @brief The parameter manifest of every device the catalog can build detached (#2939).
 *
 * Each device is instantiated and asked to describe itself. Catalog entries the host builds
 * itself (no device factory, such as Drum Grid) are listed with the reason they are skipped.
 */
std::vector<DeviceManifestEntry> buildBasePackManifests();

}  // namespace magda::daw::audio
