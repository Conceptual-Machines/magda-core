#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

#include "NullDiffCase.hpp"
#include "NullDiffMaterial.hpp"

/**
 * @file MgdFixture.hpp
 * @brief A real project turned into a null-diff case (#2173).
 *
 * Every case in the corpus today is built in code, and #2040 settled that
 * argument for the cases it covered: a load adds a step neither leg is testing
 * and a binary to the repository, and thirty lines of value initialisation is
 * reviewable as a diff. That reasoning still holds there and this does not
 * replace it.
 *
 * It stops holding for a project somebody actually made, because what those are
 * worth testing for is the combinations nobody would have thought to write
 * down: a rack inside a rack with a modifier on a send, sixty tracks, an
 * arrangement forty minutes long. A case built in code tests the rules whoever
 * wrote it knew about.
 *
 * ## A fixture is a file plus a manifest
 *
 * The load already produces model values. `ProjectSerializer::loadAndStage`
 * fills a `StagedProjectData` on any thread without touching a singleton, and
 * what it fills is close to the half of a `Case` that describes the project.
 * What is missing is everything a case declares on top of one: the range to
 * render, the tier, the figures that tier needs, and the environment.
 *
 * None of that can live in the file. A `.mgd` is what the app writes and there
 * is nowhere in it to say what two engines owe each other. So the manifest
 * carries a `Case` holding only its declarations, and the load fills in the
 * project half. A fixture is the file plus the half a `.mgd` cannot hold.
 *
 * ## The audio is generated rather than checked in
 *
 * A saved project references its sources by path and those paths point at
 * machines that are gone: an external SSD, somebody's Music folder, a bounces
 * directory under a project that was deleted. Every one of the projects in the
 * legacy corpus is like this and always will be, because the bytes must never
 * be rewritten.
 *
 * So the manifest names, per source, the `MaterialSpec` that stands in for it,
 * and the rig writes it into the scratch directory and repoints the source
 * before either leg sees the case. Same rule as the rest of the corpus and the
 * same two reasons: a fixture recorded once is a fixture nobody can regenerate
 * when a case needs one bar more, and a corpus carrying a sample library grows
 * a megabyte per project.
 *
 * Choosing the kind is a judgement about what stands between the two engines on
 * that source's path, the way #2040 chooses it per case. A stretched clip fed
 * impulses reports the distance between two interpolators rather than anything
 * about placement.
 *
 * ## What the rig refuses
 *
 * A source the project references that the manifest does not name is a failure,
 * not a gap to fill in later. That case would reach a leg with a source pointing
 * at a path that does not exist, so it would render silence, and silence nulls
 * against silence perfectly well. It is the failure this rig is built around.
 *
 * The reverse is refused too: a manifest entry no source in the project claims
 * is a declaration that has stopped being true. That is the rule the DAWproject
 * loss table already lives by, and for the same reason -- a lossy mapping is a
 * fine thing to have written down and a bad thing to discover.
 */

namespace magda::nulldiff {

/**
 * @brief What stands in for one source the project references.
 *
 * Matched on the file name rather than the whole path, and that is not a
 * shortcut. The path in a saved project names a volume that is gone, so the
 * only part of it still meaningful is the last component; and a manifest
 * carrying somebody's home directory would have to be rewritten the day the
 * project moved, which is exactly what these files may never do.
 */
struct FixtureSource {
    /// The file name the project's own path ends in, e.g. "kick.wav".
    const char* fileName = "";

    /// What is written in its place, and why that kind. The rig checks the
    /// written file back against this rather than trusting it.
    MaterialSpec material;

    /// What this source is in the project, in a phrase. Printed beside a
    /// failure, because "source 3 disagreed" is only useful to whoever wrote it.
    const char* covers = "";
};

/**
 * @brief One real project, with everything the file cannot say about it.
 *
 * @c declaration is a `Case` carrying only its declarations: the name, what it
 * covers, the tier and its figures, the range and the environment. Its project
 * half is left empty and filled by the load, so a fixture cannot quietly assert
 * a project different from the one on disk.
 */
struct MgdFixture {
    /// Relative to the corpus root, e.g. "legacy/projects/0.13.0-retrovid.mgd".
    const char* file = "";

    /// The version string the saving build wrote, as a fact about the file
    /// rather than something the rig reads. A fixture whose bytes were replaced
    /// by a newer save would still load; this is what says it should not have
    /// been.
    const char* savedBy = "";

    /// Whether these bytes are a migration fixture (#2079) and therefore
    /// unrewritable, or a project saved for this corpus and free to be resaved
    /// when a case needs one bar more. See tests/corpus/legacy/README.md.
    bool isMigrationFixture = true;

    Case declaration;
    std::vector<FixtureSource> sources;
};

/// What loading one fixture produced, or why it could not be loaded.
struct FixtureLoad {
    bool ok = false;

    /// Why not. Empty on success, and the only thing a caller should print.
    std::string failure;

    /// The case, ready for either leg. Meaningless unless @c ok.
    Case value;

    /// What the rig wrote, in the order the manifest names them. Carried so a
    /// test can check the files back without reproducing the naming.
    std::vector<juce::File> written;
};

/**
 * @brief Load @p fixture, writing its material into @p scratchDirectory.
 *
 * Drives the app's own load and its own source install rather than a copy of
 * them, for the reason the incumbent leg drives `PluginManager`: a second
 * implementation is something that can agree with itself while both are wrong.
 *
 * Touches `SourcePool`, which the install path clears. Not thread safe, and
 * neither is anything else that reads the pool while it runs.
 */
FixtureLoad loadFixture(const MgdFixture& fixture, const juce::File& scratchDirectory);

/// Where the fixtures live, as configured by CMake.
juce::File fixtureCorpusDir();

/// Every fixture the corpus declares.
const std::vector<MgdFixture>& mgdFixtures();

}  // namespace magda::nulldiff
