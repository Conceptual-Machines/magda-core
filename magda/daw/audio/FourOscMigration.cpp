#include "FourOscMigration.hpp"

#include <set>

#include "../core/ChainWalk.hpp"
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
 */
int convertIn(std::vector<ChainElement>& elements, TrackManager& tracks) {
    auto converted = 0;

    for (auto index = 0; index < static_cast<int>(elements.size()); ++index) {
        auto& element = elements[static_cast<std::size_t>(index)];

        if (isRack(element)) {
            for (auto& chain : getRack(element).chains)
                converted += convertIn(chain.elements, tracks);

            continue;
        }

        if (!isDevice(element))
            continue;

        auto& device = getDevice(element);

        if (device.pads)
            for (auto& pad : device.pads->chains)
                converted += convertIn(pad.elements, tracks);

        if (!isFourOscDevice(device))
            continue;

        auto translated = translateFourOsc(device, [&tracks] { return tracks.allocateDeviceId(); });
        device = std::move(translated.device);
        ++converted;

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

juce::File backupFileFor(const juce::File& project) {
    if (project == juce::File{})
        return {};

    // A sibling rather than a hidden file: this is the copy the user goes back
    // to if the conversion was not what they wanted, so it belongs where they
    // keep their projects. Nonexistent, so converting twice does not overwrite
    // the first copy.
    return project
        .getSiblingFile(project.getFileNameWithoutExtension() + " (4OSC)" +
                        project.getFileExtension())
        .getNonexistentSibling();
}

juce::String describeConversion(const std::vector<FourOscCandidate>& candidates) {
    if (candidates.empty())
        return {};

    const auto count = static_cast<int>(candidates.size());
    juce::String text = count == 1 ? "This project uses 4OSC, which the MAGDA engine does not have."
                                   : "This project uses 4OSC on " + juce::String(count) +
                                         " devices, and the MAGDA engine does not have it.";

    text += " It can be converted to Poly Synth, which carries the oscillators, the filter, both "
            "envelopes and the voice settings.";

    const auto gaps = distinctGaps(candidates);
    if (gaps.empty()) {
        text += " Everything this patch uses has somewhere to go.";
    } else {
        // Named rather than summarised. "Some settings will be lost" tells
        // somebody to expect a difference without telling them what to listen
        // for.
        text += " These have no equivalent and will be lost: ";
        for (auto index = 0; index < static_cast<int>(gaps.size()); ++index)
            text += (index > 0 ? ", " : "") + gaps[static_cast<std::size_t>(index)];
        text += ".";
    }

    text += " Your project is saved as a new file first. The converted one cannot be turned "
            "back into a 4OSC project.";

    return text;
}

int convertFourOscDevices(TrackManager& tracks) {
    auto converted = 0;

    tracks.forEachTrackIncludingMaster([&tracks, &converted](TrackInfo& track) {
        const auto onThisTrack = convertIn(track.chain.fxChainElements, tracks);
        if (onThisTrack == 0)
            return;

        converted += onThisTrack;
        tracks.notifyTrackDevicesChanged(track.id);
    });

    return converted;
}

}  // namespace magda::daw::audio
