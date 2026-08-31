#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <string>
#include <vector>

#include "NullDiffCase.hpp"
#include "NullDiffMaterial.hpp"
#include "core/Source.hpp"

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
 *
 * So is a project whose sources cannot be told apart by the only part of a path
 * a manifest can name. See @ref refuseIndistinguishableSources.
 *
 * A project that hosts an external plugin is not refused any more (#2175), and
 * what replaced the refusal is a declaration: @ref MgdFixture::hostedPlugins
 * names them, the tier has to be @c AudioTier::Invariants, and the runner does
 * not render the case at all on a machine that has not scanned one of them. The
 * refusal was right while nothing hosted a VST3 in the engine and the verdict
 * could only be a fact about the machine; what makes it a fixture now is that
 * the fact about the machine is asked first and answered out loud.
 *
 * ## A loaded fixture keeps its ids
 *
 * A `Case` carries source ids and nothing else; the legs resolve them through
 * the global `SourcePool`. The app's install clears that pool and resets its id
 * allocator, which is right for an app with one project open and fatal for a
 * corpus holding several cases: two fixtures would both come back holding id 1
 * for different files. The rig therefore puts back what the install cleared and
 * moves this project's sources to ids nothing else is using, so every case
 * loaded stays resolvable while the others are.
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
    /// Relative to the corpus root, e.g. "legacy/projects/0.15.0-demo.mgd".
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

    /// The external plugins this project hosts, by the name the project gives
    /// them, in no order (#2175).
    ///
    /// Declared for the same reason the sources are, and checked the same way in
    /// both directions: a plugin in the project that the manifest does not name
    /// is refused, and a name in the manifest no device claims is refused. A
    /// project acquires a plugin the day somebody replaces its bytes, which is
    /// exactly the day nobody is reading the manifest.
    ///
    /// Non-empty demands @c AudioTier::Invariants. A plugin is entitled to
    /// frame its own work -- to dither, to hold state from however it was last
    /// called -- so there is no null to ask of a project hosting one, and a
    /// residual measured across one is a number about the plugin rather than
    /// about either engine.
    std::vector<const char*> hostedPlugins;
};

/// What loading one fixture produced, or why it could not be loaded.
struct FixtureLoad {
    bool ok = false;

    /// Why not. Empty on success, and the only thing a caller should print.
    std::string failure;

    /// The case, ready for either leg. Meaningless unless @c ok.
    Case value;

    /// The stand-in written for each source the manifest names, keyed by that
    /// name. Carried so a caller can check the files back without reproducing
    /// the naming, and keyed rather than ordered because the order a project
    /// stages its sources in is not the order a manifest lists them.
    ///
    /// One entry per manifest source, never per staged source. A project may
    /// name one file twice -- a v2 table entry beside a v1 clip that migrated
    /// to the same path -- and those are one sound sharing one stand-in.
    std::map<juce::String, juce::File> written;
};

/**
 * @brief Put the pool back the way it was found.
 *
 * The rig drives the app's own source install, and that install clears the pool
 * before repopulating it: correctly, because loading a project is exactly when
 * the previous project's sources stop being pooled. In a test binary the pool is
 * shared with everything else in the run, so a fixture loaded there would
 * otherwise pull the code-built corpus's sources out from under it and leave
 * every SourceFact in it pointing at an id nobody holds.
 *
 * Snapshot and restore rather than clear, because clearing is what causes the
 * problem rather than what fixes it, and `insert` preserves ids so what goes
 * back is what was there. Ordering is not a defence: the corpus builds itself
 * lazily and once.
 *
 * Here rather than in one test file because every caller of loadFixture needs
 * it, and a correctness guard nobody can see from the function it guards is one
 * that gets copied slightly wrong.
 */
class PooledSourcesUnwind {
  public:
    PooledSourcesUnwind();
    ~PooledSourcesUnwind();

    PooledSourcesUnwind(const PooledSourcesUnwind&) = delete;
    PooledSourcesUnwind& operator=(const PooledSourcesUnwind&) = delete;
    PooledSourcesUnwind(PooledSourcesUnwind&&) = delete;
    PooledSourcesUnwind& operator=(PooledSourcesUnwind&&) = delete;

  private:
    std::vector<magda::Source> held_;
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

/**
 * @brief Why @p paths cannot be stood in for, or empty if they can.
 *
 * A manifest names a source by the last component of its path, because the rest
 * of it names a volume that is gone and a manifest carrying somebody's home
 * directory would have to be rewritten the day the project moved -- which is
 * exactly what a migration fixture may never do.
 *
 * That leaves one thing a project can do that a manifest cannot express: play
 * two different files that happen to share a name. `/packA/loop.wav` and
 * `/packB/loop.wav` are two sounds, and every part of the rig would treat them
 * as one. They would match the same declaration, be written to the same file,
 * and then collapse for real in the source install, which dedups by canonical
 * path: the two clips would come out playing the same thing, and the case would
 * render something the project never was.
 *
 * Compared with case folded, because whether two names can name one file is the
 * filesystem's question and macOS and Windows both answer yes by default. That
 * is conservative on a case-sensitive filesystem on purpose: a corpus that
 * refuses a fixture on Linux and accepts it on macOS is worse than one that
 * refuses it everywhere, because the failure would arrive on somebody else's
 * machine.
 *
 * Folding is all a comparison of strings can do, and it is not everything the
 * filesystem does: macOS stores some names decomposed and some composed, so two
 * paths can differ as strings, fold differently, and still name one file. The
 * load catches that by counting what its own directory holds after each write
 * rather than by comparing another pair of strings, which would miss it for the
 * same reason this does.
 *
 * Refused rather than papered over with a unique suffix, and the difference
 * matters. Making the writes distinct would stop the collapse and leave the
 * manifest still unable to say which of the two is the drum loop and which is
 * the riser, so both would silently take one MaterialSpec. A corpus that cannot
 * describe a project should say so, and a fixture that needs this is a reason
 * to extend the key rather than to let one stand in for both.
 *
 * Exposed rather than kept inside the load because it is a rule about what a
 * fixture can be, not a step in reading one, and because provoking it through a
 * load would mean checking in a project built to break it.
 */
std::string refuseIndistinguishableSources(const std::vector<juce::String>& paths);

/// Where the fixtures live, as configured by CMake.
juce::File fixtureCorpusDir();

/// Every fixture the corpus declares.
const std::vector<MgdFixture>& mgdFixtures();

}  // namespace magda::nulldiff
