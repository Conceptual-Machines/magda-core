#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <set>
#include <vector>

#include "MgdFixture.hpp"
#include "core/SourcePool.hpp"

/**
 * @file test_mgd_fixture_rig.cpp
 * @brief The .mgd fixture rig (#2173).
 *
 * This slice renders nothing. What it asserts is that a fixture is a case: that
 * it loads, that the case it produces is the project on disk plus the manifest's
 * declarations and nothing else, that every source reads back as what the
 * manifest asked for, and that a source the manifest failed to name is a loud
 * failure rather than a quiet one.
 *
 * That last one is the reason the rig exists in this shape. Every path in a
 * saved project points at a machine that is gone, so a source nobody stood in
 * for reaches a leg as a file that is not there, renders silence, and nulls
 * against the other leg's silence at the ordinary floor. A corpus cannot see
 * that in its own report: it looks exactly like a case that passed.
 */

using namespace magda;
using namespace magda::nulldiff;

namespace {

juce::File scratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_mgd_fixture_rig");
    root.createDirectory();
    return root;
}

/// Put the pool back the way it was found.
///
/// The rig drives the app's own source install, and that install clears the
/// pool before repopulating it -- correctly, because loading a project is
/// exactly when the previous project's sources stop being pooled. In a test
/// binary the pool is shared with everything else in the run, so a fixture
/// loaded here would otherwise pull the null-diff corpus's sources out from
/// under it and leave every SourceFact in it pointing at an id nobody holds.
///
/// Snapshot and restore rather than clear, because clearing is what causes the
/// problem rather than what fixes it, and `insert` preserves ids so what goes
/// back is what was there. Ordering is not a defence: the corpus builds itself
/// lazily and once, so whether it survives would depend on which test ran
/// first.
struct PoolUnwind {
    PoolUnwind() : held_(SourcePool::getInstance().snapshot()) {}

    ~PoolUnwind() {
        auto& pool = SourcePool::getInstance();
        pool.clear();
        for (const auto& source : held_)
            pool.insert(source);
    }

    PoolUnwind(const PoolUnwind&) = delete;
    PoolUnwind& operator=(const PoolUnwind&) = delete;

  private:
    std::vector<Source> held_;
};

}  // namespace

TEST_CASE("Every fixture names a file that is there", "[nulldiff][fixture]") {
    REQUIRE_FALSE(mgdFixtures().empty());

    std::set<std::string> names;
    for (const auto& fixture : mgdFixtures()) {
        INFO(fixture.file);
        CHECK(fixtureCorpusDir().getChildFile(fixture.file).existsAsFile());

        // The case name is what a report prints, so two fixtures sharing one
        // would make a failure name a case the reader cannot find.
        CHECK(names.insert(fixture.declaration.name).second);
        CHECK_FALSE(fixture.declaration.name.empty());
        CHECK_FALSE(fixture.declaration.covers.empty());
    }
}

TEST_CASE("A fixture loads into a case", "[nulldiff][fixture]") {
    const PoolUnwind unwind;

    for (const auto& fixture : mgdFixtures()) {
        INFO(fixture.declaration.name);

        const auto loaded = loadFixture(fixture, scratch());
        INFO(loaded.failure);
        REQUIRE(loaded.ok);

        // The project half came off disk. A fixture whose file stopped
        // containing a project would still "load" into an empty case, and an
        // empty case renders silence.
        CHECK_FALSE(loaded.value.tracks.empty());
        CHECK_FALSE(loaded.value.clips.empty());
        CHECK(loaded.value.sources.size() == fixture.sources.size());

        // The declaration half came from the manifest, untouched by the load.
        CHECK(loaded.value.name == fixture.declaration.name);
        CHECK(loaded.value.covers == fixture.declaration.covers);
        CHECK(loaded.value.tier == fixture.declaration.tier);
        CHECK(loaded.value.startBeat == fixture.declaration.startBeat);
        CHECK(loaded.value.endBeat == fixture.declaration.endBeat);

        // The tempo is the project's, not the corpus default. Rendering a real
        // arrangement at 120 would put every clip somewhere nobody heard it.
        REQUIRE(loaded.value.tempo.size() == 1);
        CHECK(loaded.value.tempo.front().beat == 0.0);
        CHECK(loaded.value.tempo.front().bpm > 0.0);
    }
}

TEST_CASE("Every source reads back as what the manifest asked for", "[nulldiff][fixture]") {
    const PoolUnwind unwind;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (const auto& fixture : mgdFixtures()) {
        INFO(fixture.declaration.name);

        const auto loaded = loadFixture(fixture, scratch());
        INFO(loaded.failure);
        REQUIRE(loaded.ok);
        REQUIRE(loaded.written.size() == fixture.sources.size());

        for (std::size_t i = 0; i < loaded.written.size(); ++i) {
            const auto& file = loaded.written[i];
            const auto& declared = fixture.sources[i].material;
            INFO(fixture.sources[i].fileName << ": " << fixture.sources[i].covers);

            REQUIRE(file.existsAsFile());

            const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            REQUIRE(reader != nullptr);
            CHECK(reader->sampleRate == declared.sampleRate);

            // Whole samples rather than seconds. A duration that does not land
            // on a sample boundary is rounded by the writer, and asserting the
            // seconds back would be asserting the rounding.
            const auto expected =
                static_cast<juce::int64>(declared.durationSeconds * declared.sampleRate);
            CHECK(reader->lengthInSamples == expected);
        }

        // Every source in the case points at what was written, not at the dead
        // path the project was saved with.
        for (const auto& source : loaded.value.sources) {
            INFO(source.path);
            CHECK(juce::File(source.path).existsAsFile());
            CHECK(source.sampleRate > 0.0);
            CHECK(source.durationSeconds > 0.0);
        }
    }
}

TEST_CASE("A source the manifest does not name fails rather than passing quietly",
          "[nulldiff][fixture]") {
    const PoolUnwind unwind;

    // The failure this rig is built around, provoked rather than described: drop
    // one declaration and the load has to refuse. Without the check the case
    // would still be produced, that source would point at a volume this machine
    // has never had, and the clip reading it would render silence against the
    // other leg's silence.
    REQUIRE_FALSE(mgdFixtures().front().sources.empty());

    auto starved = mgdFixtures().front();
    const auto dropped = starved.sources.back().fileName;
    starved.sources.pop_back();

    const auto loaded = loadFixture(starved, scratch());
    CHECK_FALSE(loaded.ok);
    CHECK(loaded.failure.find(dropped) != std::string::npos);
}

TEST_CASE("Two project sources that share a name are refused", "[nulldiff][fixture]") {
    // The collision a manifest cannot express. A manifest names a source by the
    // last component of its path, so two different files called loop.wav are one
    // declaration, one written file, and -- because the source install dedups by
    // canonical path -- one pooled source. Two clips would come out playing the
    // same sound and the case would render a project that never existed, at the
    // ordinary floor, against an incumbent doing the same thing.
    //
    // Checked against paths rather than through a load, because provoking it
    // through one would mean checking in a project built to break the rig, and
    // the rule is about what a fixture can be rather than about reading one.
    CHECK(refuseIndistinguishableSources({}).empty());
    CHECK(refuseIndistinguishableSources({"/packs/a/loop.wav", "/packs/a/kick.wav"}).empty());

    // The same file named twice is not a collision. A v1 project stages one
    // entry per clip, so two clips playing one file arrive as two paths, and
    // refusing that would refuse the ordinary case.
    CHECK(refuseIndistinguishableSources({"/packs/a/loop.wav", "/packs/a/loop.wav"}).empty());

    const auto refusal = refuseIndistinguishableSources({"/packs/a/loop.wav", "/packs/b/loop.wav"});
    CHECK_FALSE(refusal.empty());
    CHECK(refusal.find("loop.wav") != std::string::npos);
    CHECK(refusal.find("/packs/a/") != std::string::npos);
    CHECK(refusal.find("/packs/b/") != std::string::npos);

    // Case folded, because whether two names are one file belongs to the
    // filesystem the stand-ins are written to and macOS and Windows both say
    // yes. Compared exactly, this pair passed the guard and then wrote the
    // second file over the first: two sources, one sound, and a null against an
    // incumbent doing the same thing.
    CHECK_FALSE(refuseIndistinguishableSources({"/packs/a/Loop.wav", "/packs/b/loop.wav"}).empty());
    CHECK_FALSE(refuseIndistinguishableSources({"/packs/a/LOOP.WAV", "/packs/b/loop.wav"}).empty());

    // Still not a collision when it is one file: the same path twice, whatever
    // case it is written in, is what a v1 project produces for two clips.
    CHECK(refuseIndistinguishableSources({"/packs/a/Loop.wav", "/packs/a/Loop.wav"}).empty());
}

TEST_CASE("Two names the filesystem calls one file are counted, not compared",
          "[nulldiff][fixture]") {
    // The premise the load's collision check rests on, asserted against a real
    // directory rather than described in a comment.
    //
    // "cafe" with an acute accent has two spellings in Unicode: one code point,
    // or an "e" followed by a combining accent. macOS stores whichever it was
    // given, so a project referencing one pack that used each produces two paths
    // that differ as strings, fold differently, and name one file. Every string
    // comparison misses that, including a comparison of the paths the writer
    // returned, which is what this used to do.
    //
    // So the load counts what its own directory holds after each write instead.
    // This is what says the count can tell them apart: whatever the filesystem
    // does with the pair, the number of files is the number of distinct sounds.
    auto directory = scratch().getChildFile("normalisation");
    directory.deleteRecursively();
    directory.createDirectory();

    const auto composed = juce::String::fromUTF8("caf\xc3\xa9");
    const auto decomposed = juce::String::fromUTF8("cafe\xcc\x81");
    REQUIRE(composed != decomposed);
    CHECK(composed.toLowerCase() != decomposed.toLowerCase());

    MaterialSpec spec;
    spec.kind = MaterialKind::Impulses;
    spec.durationSeconds = 0.25;

    const auto first = writeMaterial(directory, composed, spec);
    const auto second = writeMaterial(directory, decomposed, spec);
    REQUIRE(first.existsAsFile());
    REQUIRE(second.existsAsFile());

    // Whatever this machine did, the count says it: one file where the two
    // spellings met, two where they did not. The load refuses on the first.
    const auto held = directory.getNumberOfChildFiles(juce::File::findFiles);
    REQUIRE((held == 1 || held == 2));

    // Where they met, the two paths the writer handed back are still two
    // different strings, and that is the whole finding. The check that compared
    // those paths saw two files and let the pair through; only the count knew
    // better. On a filesystem that keeps them apart there is nothing here to
    // catch, which is why this asserts what happened rather than which.
    if (held == 1)
        CHECK(first.getFullPathName() != second.getFullPathName());

    directory.deleteRecursively();
}

TEST_CASE("Each fixture's material is its own", "[nulldiff][fixture]") {
    const PoolUnwind unwind;

    // Two fixtures are allowed to reference files with the same name: they are
    // different projects and nothing ties their sources together. A flat scratch
    // directory would let the second overwrite the first's material, leaving the
    // first case holding a path that now plays the other project's sound.
    //
    // Loading them one at a time hides that, which is what this asserts against:
    // every file any fixture wrote is still there, and still unique, after all of
    // them have been loaded.
    std::set<juce::String> everyPath;
    std::vector<juce::File> all;

    for (const auto& fixture : mgdFixtures()) {
        INFO(fixture.declaration.name);
        const auto loaded = loadFixture(fixture, scratch());
        INFO(loaded.failure);
        REQUIRE(loaded.ok);

        for (const auto& file : loaded.written) {
            INFO(file.getFullPathName());
            CHECK(everyPath.insert(file.getFullPathName()).second);
            all.push_back(file);
        }
    }

    for (const auto& file : all) {
        INFO(file.getFullPathName());
        CHECK(file.existsAsFile());
    }
}

TEST_CASE("A declaration nothing claims fails too", "[nulldiff][fixture]") {
    const PoolUnwind unwind;

    // The other direction, and it is not symmetry for its own sake. A manifest
    // entry no source claims is a declaration that has stopped being true, which
    // reads like coverage and is not: the same rule the DAWproject loss table
    // lives by, where a restore that finds nothing to put back fails rather than
    // sitting in the table.
    auto extra = mgdFixtures().front();
    extra.sources.push_back({.fileName = "a-source-this-project-never-had.wav",
                             .material = extra.sources.front().material,
                             .covers = "nothing, which is the point"});

    const auto loaded = loadFixture(extra, scratch());
    CHECK_FALSE(loaded.ok);
    CHECK(loaded.failure.find("a-source-this-project-never-had.wav") != std::string::npos);
}
