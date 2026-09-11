#pragma once

#include <vector>

#include "FourOscTranslation.hpp"

namespace magda {
struct TrackInfo;
class TrackManager;
}  // namespace magda

/**
 * @file FourOscMigration.hpp
 * @brief Converting a whole project's 4OSC devices at once (#2437).
 *
 * The translation itself is FourOscTranslation.hpp. This is the project-level
 * half: what to ask about, and what the answer changes.
 */

namespace magda::daw::audio {

/** @brief One device the project would convert. */
struct FourOscCandidate {
    ChainNodePath path;
    juce::String deviceName;
    std::vector<FourOscGap> gaps;
};

/// Every 4OSC in @p tracks, with what each one would lose. Empty means there
/// is nothing to ask about.
std::vector<FourOscCandidate> findFourOscDevices(const std::vector<TrackInfo>& tracks,
                                                 const TrackInfo& master);

/// Where the pre-conversion project is written, beside @p project. A project
/// that has never been saved has no file and gets no copy.
juce::File backupFileFor(const juce::File& project);

/// What the dialog says, built from @p candidates. Names the gaps when there
/// are any and promises nothing when there are not.
juce::String describeConversion(const std::vector<FourOscCandidate>& candidates);

/**
 * @brief Convert every 4OSC in the project, in place.
 *
 * Write @ref backupFileFor first: this does not keep the old devices.
 *
 * @return how many devices changed. Message thread.
 */
int convertFourOscDevices(TrackManager& tracks);

}  // namespace magda::daw::audio
