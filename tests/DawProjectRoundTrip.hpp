#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <string>
#include <vector>

#include "NullDiffCase.hpp"

/**
 * @file DawProjectRoundTrip.hpp
 * @brief A corpus project through DAWproject and back, as model values (#2080).
 *
 * The mechanism only. What is asserted about the result belongs to the runner
 * (test_dawproject_round_trip.cpp), for the same reason a plan golden's fixture
 * does not know about its golden file: a round trip that decided what counted
 * as a successful round trip could be written to succeed.
 *
 * The path is the real one, archive included, rather than the XML adapter on
 * its own. Audio arrives at the importer as a path inside a zip, and it is
 * DawProjectArchive that extracts it, repoints the event and rescales its
 * source-domain anchors for whatever rate the extracted file turns out to run
 * at. A round trip that stopped at the XML would skip all three and then render
 * whatever the original happened to still have pooled, which is a test of the
 * corpus rather than of the export.
 *
 * ## What comes back, and what is carried over
 *
 * A Case is a project plus the environment it is rendered in. Only the project
 * makes the trip: tracks, the master, and clips. Sample rate, block size, the
 * beat range, the tier and its allowances are how the corpus renders a case
 * rather than anything DAWproject has an opinion about, so they are copied
 * from the original unchanged.
 *
 * The groove document is carried over on the same grounds, and it is the one
 * that could be argued: `Case::grooveXml` is the template library both engines
 * are handed, not a property of the project, while the clip's reference to a
 * template by name IS project data and is declared as a loss below.
 *
 * ## Identity
 *
 * The importer renumbers. Tracks and clips are assigned ids as they are parsed,
 * so nothing that came out carries the id that went in, and the master arrives
 * as an ordinary track whose channel says `role="master"`.
 *
 * Ids are therefore mapped back by name before anything is compiled, which the
 * corpus can afford because its track and clip names are unique within a case
 * (the runner asserts it rather than assuming it). Mapping by name and not by
 * position is what keeps the mapping from hiding a reordering: the rebuilt case
 * keeps the imported document's order, so a project whose tracks came back in a
 * different order compiles to a plan whose ops are in a different order, and
 * the golden comparison says so.
 *
 * A name that matches nothing, or matches twice, is a refusal rather than a
 * residual. See RoundTrip::refusal.
 */

namespace magda::nulldiff {

/**
 * @brief One thing DAWproject cannot carry, declared against the case that
 *        carries it.
 *
 * The bar is one thing and not two: a field this case sets, which went out and
 * did not come back. Not "a field the format has no attribute for", which would
 * be a list about DAWproject rather than about this corpus, and not "a field
 * the engine reads", which would leave a real loss undeclared whenever the
 * corpus happened to carry it somewhere nothing reads. AudioClipModel::takes is
 * that second case: neither engine plays a take list, and it is declared here
 * anyway, because it went out and did not come back.
 *
 * Two things change across the trip and are not losses. Ids are reassigned by
 * the importer, and the trip maps them back before anything is compiled, for
 * the reasons above. Colours, names and the rest of what the format does carry
 * come back as they went, so there is nothing to declare.
 *
 * @p restore puts the field back from the original and says whether it had to.
 * That single answer is what keeps the list honest in both directions. The
 * runner requires every declaration to fire, so a field the adapter starts
 * carrying turns its declaration into a failure rather than into a comment that
 * is quietly no longer true; and because the restore touches that field and
 * nothing else, everything the format was supposed to carry is still being
 * compared afterwards. That is the "and nothing more" half.
 */
struct Loss {
    /// The model field, spelled as it appears in the header that declares it.
    std::string field;

    /// Why the format cannot hold it. Read by whoever the failure lands on.
    std::string reason;

    /// Put @p field back on @p imported from @p original.
    /// @return whether anything actually had to be restored.
    std::function<bool(Case& imported, const Case& original)> restore;
};

/// The losses declared for @p caseName, or an empty list.
const std::vector<Loss>& declaredLosses(const std::string& caseName);

/// Every case name the loss table has an entry for, so the runner can refuse a
/// declaration that no longer names a case in the corpus.
std::vector<std::string> namesWithDeclaredLosses();

/// What one declared loss did on one case.
struct LossOutcome {
    std::string field;
    std::string reason;

    /// False when the restore found nothing to put back, which means the field
    /// survived the trip and the declaration is stale.
    bool occurred = false;
};

struct RoundTrip {
    /// The reimported project, renumbered back onto the original's ids and with
    /// every declared loss restored.
    Case value;

    std::vector<LossOutcome> losses;

    /// Why the trip could not be made at all: an export the schema refused, an
    /// archive that would not read, a name the mapping could not resolve. Never
    /// reported as a difference, because a harness failure that reads like an
    /// engine failure costs somebody a day.
    std::string refusal;
};

/**
 * @brief Export @p original to a .dawproject under @p scratchDirectory,
 *        reimport it, and rebuild a renderable case from what came back.
 */
RoundTrip exportAndReimport(const Case& original, const juce::File& scratchDirectory);

}  // namespace magda::nulldiff
