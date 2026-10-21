#include "SamplerMedia.hpp"

#include "core/ChainWalk.hpp"
#include "core/DeviceState.hpp"
#include "core/TrackManager.hpp"

namespace magda {

SamplerMedia& SamplerMedia::getInstance() {
    static SamplerMedia media;
    return media;
}

namespace {

/// Point the root's source (@p zone -1) or the @p zone th zone's at @p file.
void repoint(device_state::Doc& doc, int zone, const juce::File& file) {
    if (zone < 0) {
        doc.root.props.set("source", file.getFullPathName());
        return;
    }
    for (auto& child : doc.root.children)
        if (child.type == "zone" && zone-- == 0) {
            child.props.set("source", file.getFullPathName());
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
                if (device.pluginId != "magdasampler")
                    return true;
                const auto doc = device_state::decode(device.pluginState);
                if (!doc)
                    return true;
                // The root's own source, then each zone's (#3098); -1 names the root.
                const auto add = [&references, &path](const juce::String& source, int zone) {
                    if (source.isEmpty() || !juce::File::isAbsolutePath(source))
                        return;
                    references.push_back({juce::File(source), [path, zone](const juce::File& file) {
                                              TrackManager::getInstance().updateDeviceAuthoredState(
                                                  path, [&file, zone](device_state::Doc& doc) {
                                                      repoint(doc, zone, file);
                                                  });
                                          }});
                };
                add(doc->root.props["source"].toString(), -1);
                int zone = 0;
                for (const auto& child : doc->root.children)
                    if (child.type == "zone")
                        add(child.props["source"].toString(), zone++);
                return true;
            });
    }
    return references;
}

}  // namespace magda
