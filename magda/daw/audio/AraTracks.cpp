#include "AraTracks.hpp"

#include "core/ChainWalk.hpp"
#include "core/ClipManager.hpp"
#include "core/TrackManager.hpp"
#include "engine/PluginService.hpp"

namespace magda {

namespace {

/** @brief The installed plugin @p device names, if the scan says it offers ARA. */
std::optional<juce::String> araName(const DeviceInfo& device) {
    const auto* known = PluginService::getInstance().knownList();
    if (known == nullptr || device.format == PluginFormat::Internal || device.uniqueId.isEmpty())
        return std::nullopt;
    if (const auto type = known->getTypeForIdentifierString(device.uniqueId);
        type != nullptr && type->hasARAExtension)
        return type->name;
    return std::nullopt;
}

}  // namespace

std::optional<juce::String> araPluginOnTrack(TrackId trackId) {
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    if (track == nullptr)
        return std::nullopt;

    std::optional<juce::String> found;
    const auto visit = [&found](const DeviceInfo& device, const ChainNodePath&) {
        found = araName(device);
        return !found.has_value();
    };
    chain_walk::forEachDevice(track->chain.fxChainElements, ChainNodePath::trackLevel(trackId),
                              chain_walk::Pads::Skip, visit);
    for (const auto& element : track->chain.postFxChainElements)
        if (!found)
            found = araName(element.device);
    return found;
}

std::optional<juce::String> araPluginForClip(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || !clip->isAudio())
        return std::nullopt;
    return araPluginOnTrack(clip->trackId);
}

}  // namespace magda
