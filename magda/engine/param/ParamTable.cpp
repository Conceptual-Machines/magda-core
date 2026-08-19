#include "param/ParamTable.hpp"

#include <algorithm>

namespace magda::engine {

std::uint64_t paramLayoutFingerprint(const std::vector<ParamKey>& keys) {
    // FNV-1a, as the plan's fingerprint is: it has to separate layouts that
    // differ, and it runs off the audio thread once per compile.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xff;
            hash *= 1099511628211ULL;
        }
    };

    mix(keys.size());

    for (const auto& key : keys) {
        mix(static_cast<std::uint64_t>(key.kind));
        mix(static_cast<std::uint64_t>(key.scope));
        mix(static_cast<std::uint64_t>(key.trackId));
        mix(static_cast<std::uint64_t>(key.rackId));
        mix(static_cast<std::uint64_t>(key.device.segment));
        mix(static_cast<std::uint64_t>(key.device.deviceId));
        mix(static_cast<std::uint64_t>(key.modId));
        mix(static_cast<std::uint64_t>(key.index));
    }

    return hash;
}

std::span<const ParamLink> ParamTable::linksFor(ParamId param) const {
    if (param < 0 || param + 1 >= static_cast<ParamId>(linkOffsets.size()))
        return {};

    const auto first = static_cast<std::size_t>(linkOffsets[static_cast<std::size_t>(param)]);
    const auto last = static_cast<std::size_t>(linkOffsets[static_cast<std::size_t>(param) + 1]);
    if (last <= first || last > links.size())
        return {};

    return std::span<const ParamLink>{links}.subspan(first, last - first);
}

}  // namespace magda::engine
