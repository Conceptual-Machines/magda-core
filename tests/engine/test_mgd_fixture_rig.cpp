#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
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
    const PooledSourcesUnwind unwind;

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
    const PooledSourcesUnwind unwind;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (const auto& fixture : mgdFixtures()) {
        INFO(fixture.declaration.name);

        const auto loaded = loadFixture(fixture, scratch());
        INFO(loaded.failure);
        REQUIRE(loaded.ok);
        // One stand-in per source the manifest names, whatever the project did.
        // A project may name one file twice and those share a stand-in, so this
        // counts declarations rather than staged sources.
        REQUIRE(loaded.written.size() == fixture.sources.size());

        for (const auto& declared : fixture.sources) {
            INFO(declared.fileName << ": " << declared.covers);

            // Looked up by name rather than by position. The order a project
            // stages its sources in is the order it was saved in, and the order
            // a manifest lists them is whatever reads best; pairing the two by
            // index is a coincidence that holds until a fixture is reordered.
            const auto found = loaded.written.find(juce::String(declared.fileName));
            REQUIRE(found != loaded.written.end());

            const auto& file = found->second;
            REQUIRE(file.existsAsFile());

            const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            REQUIRE(reader != nullptr);
            CHECK(reader->sampleRate == declared.material.sampleRate);

            // Whole samples rather than seconds. A duration that does not land
            // on a sample boundary is rounded by the writer, and asserting the
            // seconds back would be asserting the rounding.
            const auto expected = static_cast<juce::int64>(declared.material.durationSeconds *
                                                           declared.material.sampleRate);
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

TEST_CASE("Two fixtures held at once both stay resolvable", "[nulldiff][fixture]") {
    const PooledSourcesUnwind unwind;

    // A Case carries source ids and nothing else, and the legs resolve them
    // through the global pool. The app's install clears that pool and resets its
    // allocator to one, which is right for an app with one project open: load a
    // second fixture and the first would come back holding ids that now name the
    // second's audio, silently, with every clip still pointing somewhere.
    //
    // Two fixtures, held together, is the only arrangement that can see it.
    REQUIRE(mgdFixtures().size() >= 2);

    const auto first = loadFixture(mgdFixtures()[0], scratch());
    INFO(first.failure);
    REQUIRE(first.ok);

    // What the first case claims, recorded before anything else is loaded.
    std::map<SourceId, juce::String> claimed;
    for (const auto& source : first.value.sources)
        claimed[source.id] = source.path;
    REQUIRE_FALSE(claimed.empty());

    const auto second = loadFixture(mgdFixtures()[1], scratch());
    INFO(second.failure);
    REQUIRE(second.ok);

    auto& pool = SourcePool::getInstance();

    // Every id the first case holds still names the file it named. This is the
    // assertion: not that the pool has something under that id, but that it has
    // the same thing.
    for (const auto& [id, path] : claimed) {
        INFO(path);
        const auto* pooled = pool.get(id);
        REQUIRE(pooled != nullptr);
        CHECK(pooled->filePath == path);
    }

    // And the second case's ids are its own rather than the first's reissued.
    for (const auto& source : second.value.sources) {
        INFO(source.path);
        const auto* pooled = pool.get(source.id);
        REQUIRE(pooled != nullptr);
        CHECK(pooled->filePath == source.path);
        CHECK(claimed.find(source.id) == claimed.end());
    }

    // The clips agree with the sources: every event resolves to a file the case
    // says it plays. An id that survived while its clip moved would pass the
    // checks above and still render the wrong audio.
    const auto eventsResolve = [&pool](const Case& value) {
        std::set<juce::String> declared;
        for (const auto& source : value.sources)
            declared.insert(source.path);

        auto ok = true;
        for (const auto& clip : value.clips) {
            if (!clip.isAudio())
                continue;
            for (const auto& event : clip.audio().events) {
                const auto* pooled = pool.get(event.sourceId);
                ok = ok && pooled != nullptr && declared.count(pooled->filePath) == 1;
            }
        }
        return ok;
    };

    CHECK(eventsResolve(first.value));
    CHECK(eventsResolve(second.value));
}

TEST_CASE("A project that hosts a plugin has to declare it", "[nulldiff][fixture]") {
    const PooledSourcesUnwind unwind;

    // A plugin in a project used to be an outright refusal (#2175 lifted it),
    // and what replaced it is a declaration checked in both directions. The
    // specimen is the real project rather than a described one, because the rule
    // is about what is in the file and the file is right here.
    const auto retrospect =
        std::find_if(mgdFixtures().begin(), mgdFixtures().end(),
                     [](const auto& f) { return f.declaration.name == "project.retrospect"; });
    REQUIRE(retrospect != mgdFixtures().end());
    REQUIRE_FALSE(retrospect->hostedPlugins.empty());

    SECTION("a plugin the manifest does not name is refused") {
        // The failure this rule is built around: a project acquires a plugin the
        // day somebody replaces its bytes, and that is exactly the day nobody
        // rereads the manifest.
        auto undeclared = *retrospect;
        undeclared.hostedPlugins.clear();

        const auto loaded = loadFixture(undeclared, scratch());
        CHECK_FALSE(loaded.ok);
        CHECK(loaded.failure.find("Retrospect") != std::string::npos);
    }

    SECTION("a name no device claims is refused") {
        // The mirror, and the rule the source declarations already live by: a
        // declaration that has stopped being true is worse than one nobody
        // wrote, because it reads as coverage.
        auto stale = *retrospect;
        stale.hostedPlugins.push_back("A Plugin Nobody Shipped");

        const auto loaded = loadFixture(stale, scratch());
        CHECK_FALSE(loaded.ok);
        CHECK(loaded.failure.find("A Plugin Nobody Shipped") != std::string::npos);
    }

    SECTION("hosting one and asking for a null is refused") {
        // The tier is checked rather than left to a reviewer's eye. A plugin
        // frames its own work, so a residual measured across one is a number
        // about the plugin, and a tolerance wide enough to pass it passes
        // anything.
        auto nulled = *retrospect;
        nulled.declaration.tier = AudioTier::Exact;

        const auto loaded = loadFixture(nulled, scratch());
        CHECK_FALSE(loaded.ok);
        CHECK(loaded.failure.find("invariants tier") != std::string::npos);
    }

    SECTION("every fixture the corpus carries holds the rule") {
        for (const auto& fixture : mgdFixtures()) {
            INFO(fixture.file);
            const auto good = loadFixture(fixture, scratch());
            INFO(good.failure);
            CHECK(good.ok);

            // And the half that cannot be checked from inside the load: a
            // fixture that declares a plugin declares the tier that goes with
            // it, which is what the corpus reads to decide what to assert.
            if (!fixture.hostedPlugins.empty())
                CHECK(fixture.declaration.tier == AudioTier::Invariants);
        }
    }
}

TEST_CASE("A source the manifest does not name fails rather than passing quietly",
          "[nulldiff][fixture]") {
    const PooledSourcesUnwind unwind;

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

    // Both separators, on every host. A path in a saved project belongs to the
    // machine that saved it, and juce::File cuts on the separator of the machine
    // reading it: a Windows runner asked for the name of a macOS path handed
    // back the whole path, so every manifest lookup missed and the load refused
    // a fixture that was fine. CI found that; these are what would have.
    //
    // Asserted in both directions rather than the one that broke, because the
    // corpus is checked in on macOS and read on Windows and Linux, and a project
    // saved on Windows is a fixture somebody will add.
    CHECK_FALSE(refuseIndistinguishableSources({"/packs/a/loop.wav", "/packs/b/loop.wav"}).empty());
    CHECK_FALSE(refuseIndistinguishableSources({"C:\\packs\\a\\loop.wav", "C:\\packs\\b\\loop.wav"})
                    .empty());
    CHECK_FALSE(
        refuseIndistinguishableSources({"/packs/a/loop.wav", "C:\\packs\\b\\loop.wav"}).empty());

    // And two names that really are different stay different under both.
    CHECK(refuseIndistinguishableSources({"C:\\packs\\a\\loop.wav", "C:\\packs\\a\\kick.wav"})
              .empty());
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
    const PooledSourcesUnwind unwind;

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

        for (const auto& [name, file] : loaded.written) {
            INFO(name << " -> " << file.getFullPathName());
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
    const PooledSourcesUnwind unwind;

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
