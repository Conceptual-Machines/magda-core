#include "SamplerMedia.hpp"

#include "core/ChainWalk.hpp"
#include "core/DeviceState.hpp"
#include "core/TrackManager.hpp"

namespace magda {

SamplerMedia& SamplerMedia::getInstance() {
    static SamplerMedia media;
    return media;
}

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
                const auto source = doc ? doc->root.props["source"].toString() : juce::String();
                if (source.isNotEmpty() && juce::File::isAbsolutePath(source))
                    references.push_back({juce::File(source), [path](const juce::File& file) {
                                              TrackManager::getInstance().updateDeviceAuthoredState(
                                                  path, [&file](device_state::Doc& doc) {
                                                      doc.root.props.set("source",
                                                                         file.getFullPathName());
                                                  });
                                          }});
                return true;
            });
    }
    return references;
}

}  // namespace magda
