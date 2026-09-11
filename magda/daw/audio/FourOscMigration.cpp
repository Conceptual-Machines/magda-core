#include "FourOscMigration.hpp"

#include <set>

#include "../core/ChainWalk.hpp"
#include "../core/TrackInfo.hpp"

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

    text += " The project as it is now is saved alongside first, so nothing is overwritten.";

    return text;
}

}  // namespace magda::daw::audio
