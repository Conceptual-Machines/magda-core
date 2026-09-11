#pragma once

#include <vector>

#include "AudioEngine.hpp"

namespace magda {

/**
 * @brief Every parameter a registered MAGDA device declares, read off the
 * device itself.
 *
 * Neither engine is asked. The device is built from the catalog, questioned and
 * dropped, so the answer is the same under both -- and needs no Edit to build a
 * plugin in, which the native engine does not have (#2601).
 *
 * Empty when no registered device answers to @p pluginId, or when the one that
 * does is still a host plugin rather than a MagdaDevice.
 */
std::vector<ScannedPluginParameter> scanDeviceParameters(const juce::String& pluginId);

}  // namespace magda
