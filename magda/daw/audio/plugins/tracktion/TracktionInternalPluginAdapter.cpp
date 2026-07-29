#include <utility>

#include "plugins/InternalPluginRegistry.hpp"
#include "processors/base/DeviceProcessor.hpp"

namespace magda::daw::audio {

std::unique_ptr<DeviceProcessor> createInternalPluginProcessor(const InternalPluginSpec& spec,
                                                               DeviceId deviceId,
                                                               DevicePluginPtr plugin) {
    if (!plugin || spec.createProcessor == nullptr)
        return nullptr;

    if (spec.matchesPlugin != nullptr && !spec.matchesPlugin(plugin.ref()))
        return nullptr;

    return spec.createProcessor(deviceId, std::move(plugin));
}

}  // namespace magda::daw::audio
