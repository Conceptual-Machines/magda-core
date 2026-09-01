#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "DawProjectRoundTrip.hpp"
#include "NullDiffCompare.hpp"
#include "NullDiffHostedPlugin.hpp"
#include "NullDiffNativeLeg.hpp"
#include "TextDifference.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDump.hpp"

/**
 * @file test_dawproject_round_trip.cpp
 * @brief DAWproject as an engine-facing cross-check (#2080).
 *
 * Every corpus project is exported to a .dawproject, reimported, and required
 * to compile to the same plan and render to the same samples.
 *
 * What this catches is not a serialization bug in the ordinary sense.
 * test_project_document_adapters.cpp already asserts, field by field, that the
 * round trip preserves what it was asked to preserve, and it does. What it
 * cannot assert is the facts the engine depends on that nobody thought to put
 * on the list, because the list was written by reading the model rather than by
 * rendering it. A property the plan compiler reads and the adapter drops passes
 * every test in that file today and fails here.
 *
 * ## Native against native
 *
 * This is a semantic cross-check, not a parity test. Both sides are the native
 * engine, the fork has no part in it, and the two models are supposed to be the
 * same model. So the bar is bit identity rather than a floor: there is no
 * mechanism by which one deterministic graph fed one timeline twice could
 * produce anything else, and a floor would let a real difference hide under it
 * until it grew. The corpus's tiers are about what stands between two different
 * engines and do not apply here; a Spectral case runs the same stretcher primed
 * the same way on both sides of this comparison, so it owes the same bits as
 * everything else.
 *
 * ## Declared losses
 *
 * DAWproject cannot hold everything MAGDA's model holds, and where it cannot,
 * the case says so: the field, the reason, and a restore that puts it back from
 * the original before anything is compiled or rendered. See
 * DawProjectRoundTrip.hpp for why a restore is stronger than simply erasing the
 * field from both sides.
 *
 * Every declaration has to fire. A restore that finds nothing to put back means
 * the format carried the field after all, and the declaration is a comment that
 * has stopped being true; it fails here rather than sitting in the table. And
 * because a restore touches its own field and nothing else, everything the
 * format was supposed to carry is still under comparison afterwards. A lossy
 * mapping is a fine thing to have written down and a bad thing to discover.
 */

using namespace magda;
using namespace magda::nulldiff;

namespace {

/// Where the corpus's own material lives, per the one-directory-per-test-file
/// convention the other null-diff runners follow.
juce::File corpusScratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_null_diff_dawproject");
    root.createDirectory();
    return root;
}

/// Where the archives and the audio extracted back out of them go. A sibling of
/// the corpus material rather than a child of it, because the exporter embeds
/// those files and the importer extracts copies under the same names: nesting
/// the two would put a case's original and its round-tripped copy in one tree,
/// where the next reader has to work out which is which.
juce::File tripScratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_null_diff_dawproject_trips");
    root.createDirectory();
    return root;
}

/// Bit identity, written as a level so it goes through the comparator
/// everything else in the corpus goes through. compareAudio's floor is a linear
/// threshold and fromDb takes negative infinity to exactly zero, so `nulled()`
/// here means the peak difference was zero and `firstDivergence` is the first
/// sample that differed at all.
constexpr double kBitIdenticalDb = -std::numeric_limits<double>::infinity();

std::string join(const std::vector<std::string>& parts) {
    std::string joined;
    for (const auto& part : parts)
        joined += (joined.empty() ? "" : ", ") + part;
    return joined;
}

std::string dumpFor(const Case& value) {
    engine::CompileOptions options;

    // The same switch the native leg compiles under. A plan compared under
    // different options from the one that is rendered would be pinning a graph
    // nothing plays.
    options.deviceMeters = false;

    return engine::dumpPlan(engine::compileRenderPlan(value.tracks, value.master, options));
}

/// The captured streams, side by side, as one comparable text.
///
/// Compared literally rather than through compareMidi, which exists to hold a
/// grid sampler against a curve and allows for the slack between two different
/// engines. There is no slack to allow for here: the same compiler ran twice
/// over what is supposed to be the same model, so every message has to land on
/// the same sample carrying the same bytes.
std::string dumpMidi(const std::map<TrackId, MidiStream>& byTrack) {
    std::string text;

    for (const auto& [trackId, stream] : byTrack) {
        text += "T" + std::to_string(trackId) + " events=" + std::to_string(stream.size()) + "\n";

        for (const auto& event : stream)
            text += "  " + std::to_string(event.sample) + " " + std::to_string(event.status) + " " +
                    std::to_string(event.data1) + " " + std::to_string(event.data2) + "\n";
    }

    return text;
}

/// Every declaration on this case fired.
///
/// A restore that found nothing to put back means the format carried the field
/// after all, so the declaration has stopped being true and says so here rather
/// than sitting in the table describing a loss that no longer happens.
bool declarationsHold(const RoundTrip& trip) {
    bool held = true;

    for (const auto& loss : trip.losses) {
        INFO("declared loss: " << loss.field);
        INFO(loss.reason);
        INFO("nothing had to be restored, so the format carries this field after all and "
             "the declaration is stale");
        CHECK(loss.occurred);
        held = held && loss.occurred;
    }

    return held;
}

/// The two models compile to the same graph.
bool planHolds(const Case& value, const Case& imported) {
    const auto difference = firstDifference(dumpFor(value), dumpFor(imported));

    INFO("the reimported project compiles to a different plan:\n" << difference);
    CHECK(difference.empty());
    return difference.empty();
}

bool audioHolds(const NativeRender& before, const NativeRender& after, double sampleRate) {
    AudioCompareOptions options;
    options.sampleRate = sampleRate;
    options.floorDb = kBitIdenticalDb;

    const auto residual = compareAudio(before.audio, after.audio, options);

    INFO("audio: peak " << formatDb(residual.peakDb) << ", rms " << formatDb(residual.rmsDb)
                        << ", first difference at sample " << residual.firstDivergence
                        << ", length difference " << residual.lengthDifference << " samples");
    INFO(residual.refusal);
    CHECK(residual.nulled());
    return residual.nulled();
}

bool midiHolds(const NativeRender& before, const NativeRender& after) {
    const auto difference =
        firstDifference(dumpMidi(before.midiByTrack), dumpMidi(after.midiByTrack));

    INFO("the reimported project sends different MIDI:\n" << difference);
    CHECK(difference.empty());
    return difference.empty();
}

/// The two models render the same samples, and send the same MIDI where the
/// case captures any.
///
/// Both renders are handed the corpus's own plugins (#2246). Without them a case
/// that hosts one binds a passthrough on both sides, reports the same missing
/// plugin twice, and nulls against itself: a round trip certified for a project
/// whose plugin never ran, which is exactly the shape of pass this file exists
/// to refuse elsewhere.
bool renderHolds(const Case& value, const Case& imported) {
    HostedScan scan;
    const InstalledPlugins installed{.formats = &scan.formats, .knownPlugins = &scan.knownPlugins};

    const auto before = renderNative(value, installed);
    const auto after = renderNative(imported, installed);

    {
        INFO("the original would not render: " << before.failure);
        REQUIRE(before.failure.empty());
    }
    {
        INFO("the reimported project would not render: " << after.failure);
        REQUIRE(after.failure.empty());
    }

    bool held = true;

    {
        // A diagnostic on one side and not the other is a finding whether or
        // not the audio matched: a snapshot that dropped a clip and a render
        // that matched it anyway are two bugs, not none.
        INFO("the two renders reported different diagnostics");
        CHECK(before.diagnostics == after.diagnostics);
        held = held && before.diagnostics == after.diagnostics;
    }

    held = audioHolds(before, after, value.sampleRate) && held;

    if (value.capturesMidi())
        held = midiHolds(before, after) && held;

    return held;
}

/// One project through the trip, with everything asserted about it.
///
/// Each question is asked inside its own scope so that its context lands on its
/// own failure and nowhere else. Catch2's INFO lives to the end of the block it
/// is written in, so a flat body would print "the reimported project compiles
/// to a different plan:" with nothing after it beside an audio residual, which
/// reads like two findings where there is one.
///
/// @return whether it held.
bool holds(const Case& value) {
    INFO(value.name);
    INFO(value.covers);

    const auto trip = exportAndReimport(value, tripScratch());

    {
        // Never reported as a difference. An export the schema refused and a
        // render that came back wrong are different findings, and one reading
        // like the other is what costs a day.
        INFO("the round trip could not be made: " << trip.refusal);
        REQUIRE(trip.refusal.empty());
    }

    // Written out rather than short circuited: every question is worth an
    // answer on a case that has already failed one of them, because "the plan
    // is the same and the audio is not" and "neither is" are different
    // diagnoses.
    const auto declarations = declarationsHold(trip);
    const auto plan = planHolds(value, trip.value);
    const auto render = renderHolds(value, trip.value);

    return declarations && plan && render;
}

}  // namespace

TEST_CASE("Every project survives a DAWproject round trip", "[nulldiff][dawproject]") {
    // No list of known-lossy cases, because there are none: the corpus went
    // through this on the first run with every declaration in the table already
    // needed and nothing left over. A suppression list written before anything
    // needed suppressing is somewhere for a real finding to be filed.
    std::vector<std::string> failed;

    for (const auto& value : sharedCorpus(corpusScratch()))
        if (!holds(value))
            failed.push_back(value.name);

    // The roll call. A run that broke several projects says which in one line
    // rather than only in the six screens of context above it.
    INFO("projects that did not survive the trip: " << join(failed));
    CHECK(failed.empty());
}

TEST_CASE("Every declared loss names a case in the corpus", "[nulldiff][dawproject]") {
    // A renamed or deleted case leaves its declarations behind, and a loss
    // declared against nothing is a paragraph of documentation that nothing
    // holds to the model.
    std::set<std::string> corpus;
    for (const auto& value : sharedCorpus(corpusScratch()))
        corpus.insert(value.name);

    for (const auto& name : namesWithDeclaredLosses()) {
        INFO("declared losses for a case that is not in the corpus: " << name);
        CHECK(corpus.count(name) == 1);
    }
}

TEST_CASE("Corpus names are unique within a case", "[nulldiff][dawproject]") {
    // The round trip maps the importer's fresh ids back onto the originals by
    // name. Two tracks or two clips sharing one inside a case would make that
    // mapping a guess, and a guess could put a clip on the wrong track and then
    // certify the plan it compiled to.
    for (const auto& value : sharedCorpus(corpusScratch())) {
        INFO(value.name);

        std::set<juce::String> tracks{value.master.name};
        for (const auto& track : value.tracks) {
            INFO("track name: " << track.name);
            CHECK(tracks.insert(track.name).second);
        }

        std::set<juce::String> clips;
        for (const auto& clip : value.clips) {
            INFO("clip name: " << clip.name);
            CHECK(clips.insert(clip.name).second);
        }
    }
}
