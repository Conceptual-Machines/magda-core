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

/**
 * @brief Where the converted project is saved, as a new project beside the old.
 *
 * A MAGDA project is a folder holding its .mgd and its media, so this names a
 * sibling of that folder rather than a file inside it. Pass the result to
 * ProjectManager::saveProjectAs(), which builds the folder. Nothing for a
 * project that has never been saved and so has nowhere to sit beside.
 */
juce::File convertedProjectFileFor(const juce::File& project);

/// What the dialog says. @p candidates may be empty: a project with no 4OSC
/// in it is still worth copying, because the engines do not render every
/// device identically.
juce::String describeMigration(const std::vector<FourOscCandidate>& candidates);

/**
 * @brief Convert every 4OSC in the project, in place.
 *
 * Write @ref backupFileFor first: this does not keep the old devices.
 *
 * @return how many devices changed. Message thread.
 */
int convertFourOscDevices(TrackManager& tracks);

}  // namespace magda::daw::audio
