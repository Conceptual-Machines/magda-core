#pragma once

#include <map>
#include <string>

namespace magda {

/**
 * @brief What the user calls one direction of an interface's channels (#2259).
 *
 * Mono channels and stereo pairs are named apart: "Mic 1" on channel 3 and "Drum OH" on 3-4
 * can both exist. Channels are zero-based, a pair keyed by its first channel.
 */
struct ChannelNames {
    std::map<int, std::string> mono;
    std::map<int, std::string> pairs;

    /** @brief The name of @p firstChannel, or of the pair it starts; empty when unnamed. */
    std::string nameOf(int firstChannel, bool stereo) const {
        const auto& names = stereo ? pairs : mono;
        const auto it = names.find(firstChannel);
        return it != names.end() ? it->second : std::string();
    }

    /** @brief Name @p firstChannel, or the pair it starts; an empty name clears it. */
    void setName(int firstChannel, bool stereo, const std::string& name) {
        auto& names = stereo ? pairs : mono;
        if (name.empty())
            names.erase(firstChannel);
        else
            names[firstChannel] = name;
    }

    bool empty() const {
        return mono.empty() && pairs.empty();
    }

    bool operator==(const ChannelNames&) const = default;
};

}  // namespace magda
