#include "param/ModSources.hpp"

namespace magda::engine {

void collectModulationSources(const std::vector<magda::ChainElement>& elements,
                              magda::TrackId ownTrack, std::set<magda::TrackId>& out) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            collectModulationSources(device.mods, sidechainSourceOf(device.sidechain), ownTrack,
                                     out);
        } else if (magda::isRack(element)) {
            const auto& rack = magda::getRack(element);
            const auto source = sidechainSourceOf(rack.sidechain);

            // The rack's own modifiers hear the rack's sidechain. This is the
            // edge the plan compiler used to report as unrepresentable: a rack
            // sidechain is a modulation source rather than a signal one, and
            // "modulation is not topology" was true of the mix and false of the
            // dependency, which is what carrying it here settles.
            collectModulationSources(rack.mods, source, ownTrack, out);

            // A rack's chains inherit nothing: a device inside a rack hears its
            // own sidechain, and the rack's is the rack's. The fork resolves it
            // the same way, per scope rather than per subtree.
            for (const auto& chain : rack.chains)
                collectModulationSources(chain.elements, ownTrack, out);
        }
    }
}

void collectModulationSources(const magda::TrackInfo& track, std::set<magda::TrackId>& out) {
    // The track's own modifiers have no sidechain of their own to read: a
    // sidechain is a property of a device or a rack, and a track scope is
    // neither, so they listen to the track they live on.
    collectModulationSources(track.mods, magda::INVALID_TRACK_ID, track.id, out);

    collectModulationSources(track.chain.fxChainElements, track.id, out);

    for (const auto& element : track.chain.postFxChainElements)
        collectModulationSources(element.device.mods, sidechainSourceOf(element.device.sidechain),
                                 track.id, out);

    for (const auto& element : track.chain.mixerAnalysisElements)
        collectModulationSources(element.device.mods, sidechainSourceOf(element.device.sidechain),
                                 track.id, out);
}

}  // namespace magda::engine
