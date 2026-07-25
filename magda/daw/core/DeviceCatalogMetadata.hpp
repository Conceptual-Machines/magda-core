#pragma once

#include <juce_core/juce_core.h>

namespace magda::daw {

/**
 * User-facing catalog metadata shared by the device inspector and other host UI.
 *
 * The backing strings are owned by the process-wide device registries and remain
 * valid for the lifetime of the application.
 */
struct DeviceCatalogMetadata {
    const char* displayName = "";
    const char* browserCategory = "";
    const char* description = "";
    bool found = false;

    explicit operator bool() const {
        return found;
    }
};

/**
 * Resolves metadata from either the compiled-device catalog or a registered
 * device pack. Returns an empty value for external/unknown plugins.
 */
DeviceCatalogMetadata findDeviceCatalogMetadata(const juce::String& pluginId);

}  // namespace magda::daw
