#include "SamplerMedia.hpp"

#include "core/ChainWalk.hpp"
#include "core/DeviceState.hpp"
#include "core/SamplerDevices.hpp"
#include "core/TrackManager.hpp"

namespace magda {

SamplerMedia& SamplerMedia::getInstance() {
    static SamplerMedia media;
    return media;
}

namespace {

/// Point the root's @p key (@p zone -1) or the @p zone th zone's at @p file.
void repoint(device_state::Doc& doc, const juce::String& key, int zone, const juce::File& file) {
    if (zone < 0) {
        doc.root.props.set(key, file.getFullPathName());
        return;
    }
    for (auto& child : doc.root.children)
        if (child.type == "zone" && zone-- == 0) {
            child.props.set(key, file.getFullPathName());
            return;
        }
}

}  // namespace

std::vector<SamplerMediaReference> modelSamplerMedia() {
    std::vector<SamplerMediaReference> references;
    auto& tracks = TrackManager::getInstance();
    std::vector<TrackId> ids{MASTER_TRACK_ID};
    for (const auto& track : tracks.getTracks())
        ids.push_back(track.id);

    for (const auto trackId : ids) {
        auto* track = tracks.getTrack(trackId);
        if (track == nullptr)
            continue;
        chain_walk::forEachDevice(
            track->chain.fxChainElements, ChainNodePath::trackLevel(trackId),
            chain_walk::Pads::Enter,
            [&references](const DeviceInfo& device, const ChainNodePath& path) {
                if (!sampler_devices::isSampler(device.pluginId))
                    return true;
                const auto doc = device_state::decode(device.pluginState);
                if (!doc)
                    return true;
                // The root's own source and the instrument it was imported from (#3099), then
                // each zone's source (#3098); -1 names the root.
                const auto add = [&references, &path](const device_state::Node& node,
                                                      const juce::String& key, int zone) {
                    const auto source = node.props[juce::Identifier(key)].toString();
                    if (source.isEmpty() || !juce::File::isAbsolutePath(source))
                        return;
                    references.push_back(
                        {juce::File(source), [path, key, zone](const juce::File& file) {
                             TrackManager::getInstance().updateDeviceAuthoredState(
                                 path, [&file, &key, zone](device_state::Doc& doc) {
                                     repoint(doc, key, zone, file);
                                 });
                         }});
                };
                add(doc->root, "source", -1);
                add(doc->root, "instrument", -1);
                int zone = 0;
                for (const auto& child : doc->root.children)
                    if (child.type == "zone")
                        add(child, "source", zone++);
                return true;
            });
    }
    return references;
}

}  // namespace magda
