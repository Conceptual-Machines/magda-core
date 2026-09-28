#include "param/ModSources.hpp"

#include "plan/RackNesting.hpp"

namespace magda::engine {
namespace {

void collectModulationTaps(const std::vector<magda::ChainElement>& elements,
                           magda::TrackId ownTrack, std::set<ModTap>& out,
                           std::set<magda::TrackId>& notes, RackNesting& nesting) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            const auto source = sidechainSourceOf(device.sidechain);
            collectModulationTaps(device.mods, source, ownTrack, out);
            collectNoteSources(device.mods, source, ownTrack, notes);

            // A pad-per-chain device's pads hold devices like any other rack's
            // chains, and each of those is an ordinary DeviceInfo that can own
            // macros and modifiers since #2207. Not collecting them dropped
            // nothing while a pad device had no DeviceInfo at all; now it
            // would drop a modifier the parameter table has an address for
            // (#2211).
            //
            // Under `ownTrack`, not the grid's sidechain source: a device
            // inside a rack hears its own sidechain and the rack's is the
            // rack's, which is the rule the chain descent below follows. The
            // pad rack itself is synthesized and owns no modifiers.
            if (device.pads)
                for (const auto& pad : device.pads->chains)
                    collectModulationTaps(pad.elements, ownTrack, out, notes, nesting);
        } else if (magda::isRack(element)) {
            const auto& rack = magda::getRack(element);

            // Nothing under an instance that contains itself is compiled or
            // carried, so a tap collected for one is an ordering edge to a
            // track nothing reads.
            if (nesting.encloses(rack.id))
                continue;

            const RackNesting::Scope scope{nesting, rack.id};
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
                collectModulationTaps(chain.elements, ownTrack, out, notes, nesting);
        }
    }
}

}  // namespace

void collectModulationTaps(const std::vector<magda::ChainElement>& elements,
                           magda::TrackId ownTrack, std::set<ModTap>& out,
                           std::set<magda::TrackId>& notes) {
    RackNesting nesting;
    collectModulationTaps(elements, ownTrack, out, notes, nesting);
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
