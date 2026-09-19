#include "HardwareInputMap.hpp"

namespace magda::daw::engine_host {

HardwareInputMap resolveHardwareInputs(const juce::BigInteger& enabled,
                                       const std::map<int, juce::String>& namesByChannel,
                                       const juce::BigInteger& active) {
    const auto& usable = enabled.isZero() ? active : enabled;

    std::vector<int> physical;
    for (auto channel = 0; channel <= active.getHighestBit(); ++channel)
        if (active[channel] && usable[channel])
            physical.push_back(channel);

    const auto packed = [&active](int channel) {
        auto index = 0;
        for (auto below = 0; below < channel; ++below)
            if (active[below])
                ++index;
        return index;
    };
    const auto fallbackName = [](int channel) { return "In " + juce::String(channel + 1); };
    const auto nameOf = [&](int channel) {
        const auto found = namesByChannel.find(channel);
        return found != namesByChannel.end() ? found->second : fallbackName(channel);
    };
    const auto indices = [&packed](std::initializer_list<int> channels) {
        std::vector<int> result;
        for (const auto channel : channels)
            result.push_back(packed(channel));
        return result;
    };

    HardwareInputMap resolved;

    // Wave-ins first, so a device's own name wins over a positional pair or a
    // fallback spelling the same thing.
    for (std::size_t i = 0; i < physical.size();) {
        const auto first = physical[i];
        const auto name = nameOf(first);
        const auto pair = i + 1 < physical.size() && nameOf(physical[i + 1]) == name;

        const auto channels = pair ? indices({first, physical[i + 1]}) : indices({first});
        resolved.emplace(name, channels);
        if (pair)
            resolved.emplace("stereo:" + name, channels);
        i += pair ? 2 : 1;
    }

    for (std::size_t i = 0; i + 1 < physical.size(); i += 2) {
        const auto channels = indices({physical[i], physical[i + 1]});
        resolved.emplace("stereo:" + nameOf(physical[i]), channels);
        resolved.emplace("stereo:" + fallbackName(physical[i]), channels);
    }

    for (const auto channel : physical)
        resolved.emplace(fallbackName(channel), indices({channel}));

    return resolved;
}

}  // namespace magda::daw::engine_host
