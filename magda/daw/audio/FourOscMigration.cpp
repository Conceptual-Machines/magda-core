#include "FourOscMigration.hpp"

#include <set>

#include "../core/AutomationManager.hpp"
#include "../core/ChainWalk.hpp"
#include "../core/DeviceParamMigrations.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/TrackManager.hpp"

namespace magda::daw::audio {

namespace {

void collectFrom(const std::vector<ChainElement>& elements, const ChainNodePath& parentPath,
                 std::vector<FourOscCandidate>& found) {
    chain_walk::forEachDevice(elements, parentPath, chain_walk::Pads::Enter,
                              [&found](const DeviceInfo& device, const ChainNodePath& path) {
                                  if (!isFourOscDevice(device))
                                      return;

                                  found.push_back({.path = path,
                                                   .deviceName = device.name,
                                                   .gaps = translateFourOsc(device).gaps});
                              });
}

/// The gap reasons across every device, said once each. Four oscillators on
/// three devices all losing unison is one thing to tell someone, not twelve.
std::vector<juce::String> distinctGaps(const std::vector<FourOscCandidate>& candidates) {
    std::vector<juce::String> ordered;
    std::set<juce::String> seen;

    for (const auto& candidate : candidates)
        for (const auto& gap : candidate.gaps)
            if (seen.insert(gap.control).second)
                ordered.push_back(gap.control);

    return ordered;
}

/**
 * @brief Replace every 4OSC in @p elements, and in any rack or pad under it.
 *
 * The rack of effects goes in directly after the synth it came from, so it
 * reaches the same signal 4OSC's own effects did.
 *
 * @p replaced collects the path of every device converted, which is what the
 * links and lanes still addressing 4OSC's parameters are found by.
 */
int convertIn(std::vector<ChainElement>& elements, const ChainNodePath& parentPath,
              TrackManager& tracks, std::set<ChainNodePath>& replaced) {
    auto converted = 0;

    for (auto index = 0; index < static_cast<int>(elements.size()); ++index) {
        auto& element = elements[static_cast<std::size_t>(index)];

        if (isRack(element)) {
            auto& rack = getRack(element);
            const auto rackPath = chain_walk::rackIn(parentPath, rack.id);
            for (auto& chain : rack.chains)
                converted +=
                    convertIn(chain.elements, rackPath.withChain(chain.id), tracks, replaced);

            continue;
        }

        if (!isDevice(element))
            continue;

        auto& device = getDevice(element);
        const auto devicePath = chain_walk::deviceIn(parentPath, device.id);

        if (device.pads)
            for (auto& pad : device.pads->chains)
                converted += convertIn(
                    pad.elements, ChainNodePath::padChain(parentPath.trackId, device.id, pad.id),
                    tracks, replaced);

        if (!isFourOscDevice(device))
            continue;

        auto translated = translateFourOsc(device, [&tracks] { return tracks.allocateDeviceId(); });
        device = std::move(translated.device);
        ++converted;
        replaced.insert(devicePath);

        if (translated.effects == nullptr)
            continue;

        translated.effects->id = tracks.allocateRackId();
        translated.effects->chains.front().id = tracks.allocateChainId();

        elements.insert(elements.begin() + index + 1, ChainElement{std::move(translated.effects)});
        ++index;
    }

    return converted;
}

}  // namespace

std::vector<FourOscCandidate> findFourOscDevices(const std::vector<TrackInfo>& tracks,
                                                 const TrackInfo& master) {
    std::vector<FourOscCandidate> found;

    const auto collectTrack = [&found](const TrackInfo& track) {
        const auto path = ChainNodePath::trackLevel(track.id);
        collectFrom(track.chain.fxChainElements, path, found);
    };

    for (const auto& track : tracks)
        collectTrack(track);

    collectTrack(master);

    return found;
}

juce::File convertedProjectFileFor(const juce::File& project) {
    if (project == juce::File{})
        return {};

    // Up out of the project's own folder, so the new one is its neighbour
    // rather than a stray .mgd inside it.
    const auto projects = project.getParentDirectory().getParentDirectory();
    if (!projects.isDirectory())
        return {};

    // Unwrapped, and deliberately: saveProjectAs() builds the folder itself,
    // and only when the file is not already sitting in one of its own name.
    // Handing it the wrapped path meant it created nothing and wrote into a
    // directory that was not there.
    const auto folder =
        projects.getChildFile(project.getFileNameWithoutExtension() + " (magda engine)")
            .getNonexistentSibling();

    return projects.getChildFile(folder.getFileName() + project.getFileExtension());
}

juce::String describeMigration(const std::vector<FourOscCandidate>& candidates) {
    juce::String text =
        "This project was made with the Tracktion engine. MAGDA's engine does not render every "
        "device the same way, so it opens as a copy and the original is left alone.";

    if (!candidates.empty()) {
        const auto count = static_cast<int>(candidates.size());
        text += count == 1 ? " Its 4OSC becomes a Poly Synth"
                           : " Its " + juce::String(count) + " 4OSC devices become Poly Synths";
        text += ", carrying the oscillators, the filter, both envelopes, the voice settings and "
                "the built-in effects.";

        const auto gaps = distinctGaps(candidates);
        if (!gaps.empty()) {
            // Named rather than summarised. "Some settings may change" tells
            // somebody to expect a difference without telling them what to
            // listen for.
            text += " These have no equivalent and will be lost: ";
            for (auto index = 0; index < static_cast<int>(gaps.size()); ++index)
                text += (index > 0 ? ", " : "") + gaps[static_cast<std::size_t>(index)];
            text += ".";
        }
    }

    return text;
}

int convertFourOscDevices(TrackManager& tracks) {
    auto converted = 0;
    std::set<ChainNodePath> replaced;

    tracks.forEachTrackIncludingMaster([&tracks, &converted, &replaced](TrackInfo& track) {
        const auto onThisTrack = convertIn(track.chain.fxChainElements,
                                           ChainNodePath::trackLevel(track.id), tracks, replaced);
        if (onThisTrack == 0)
            return;

        converted += onThisTrack;
        tracks.notifyTrackDevicesChanged(track.id);
    });

    if (replaced.empty())
        return converted;

    // Poly Synth's parameters are different controls in different units at the
    // same indices, and the path a link names still resolves. A macro left
    // pointing at 4OSC's Tune 1 would drive Poly Synth's Osc 1 Wave.
    tracks.forEachTrackIncludingMaster([&replaced](TrackInfo& track) {
        device_param_migrations::dropParamLinksInTrack(track, replaced);
    });

    auto& automation = AutomationManager::getInstance();
    for (const auto lane :
         device_param_migrations::lanesAddressing(automation.getLanes(), replaced))
        automation.deleteLane(lane);

    return converted;
}

}  // namespace magda::daw::audio
