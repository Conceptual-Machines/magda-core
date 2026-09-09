#pragma once

#include <algorithm>

namespace magda {

/** @brief Enable each wave device that carries a channel the predicate accepts.
 *
 *  A TE wave device owns a slice of the interface's channels, so it belongs in
 *  the enabled set when any one of its channels does (#2148).
 */
void enableDevicesForChannels(const auto& devices, auto wantsChannel) {
    for (auto* device : devices) {
        if (device == nullptr)
            continue;
        const auto isWanted = [&wantsChannel](const auto& channel) {
            return wantsChannel(channel.indexInDevice);
        };
        const bool shouldEnable = std::ranges::any_of(device->getChannels(), isWanted);
        if (device->isEnabled() != shouldEnable)
            device->setEnabled(shouldEnable);
    }
}

}  // namespace magda
