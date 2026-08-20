#include "param/ModSources.hpp"

namespace magda::engine {

void collectModulationTaps(const std::vector<magda::ChainElement>& elements,
                           magda::TrackId ownTrack, std::set<ModTap>& out,
                           std::set<magda::TrackId>& notes) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            const auto source = sidechainSourceOf(device.sidechain);
            collectModulationTaps(device.mods, source, ownTrack, out);
            collectNoteSources(device.mods, source, ownTrack, notes);
        } else if (magda::isRack(element)) {
            const auto& rack = magda::getRack(element);
            const auto source = sidechainSourceOf(rack.sidechain);

            // The rack's own modifiers hear the rack's sidechain. This is the
            // edge the plan compiler used to report as unrepresentable: a rack
            // sidechain is a modulation source rather than a signal one, and
            // "modulation is not topology" was true of the mix and false of the
            // dependency, which is what carrying it here settles.
            collectModulationTaps(rack.mods, source, ownTrack, out);
            collectNoteSources(rack.mods, source, ownTrack, notes);

            // A rack's chains inherit nothing: a device inside a rack hears its
            // own sidechain, and the rack's is the rack's. The fork resolves it
            // the same way, per scope rather than per subtree.
            for (const auto& chain : rack.chains)
                collectModulationTaps(chain.elements, ownTrack, out, notes);
        }
    }
}

void collectModulationTaps(const magda::TrackInfo& track, std::set<ModTap>& out,
                           std::set<magda::TrackId>& notes) {
    // The track's own modifiers have no sidechain of their own to read: a
    // sidechain is a property of a device or a rack, and a track scope is
    // neither, so they listen to the track they live on.
    collectModulationTaps(track.mods, magda::INVALID_TRACK_ID, track.id, out);
    collectNoteSources(track.mods, magda::INVALID_TRACK_ID, track.id, notes);

    collectModulationTaps(track.chain.fxChainElements, track.id, out, notes);

    const auto flat = [&](const auto& section) {
        for (const auto& element : section) {
            const auto source = sidechainSourceOf(element.device.sidechain);
            collectModulationTaps(element.device.mods, source, track.id, out);
            collectNoteSources(element.device.mods, source, track.id, notes);
        }
    };

    flat(track.chain.postFxChainElements);
    flat(track.chain.mixerAnalysisElements);
}

}  // namespace magda::engine
