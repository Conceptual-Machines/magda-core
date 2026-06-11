#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/agents/dsl_interpreter.hpp"

using namespace magda;

namespace {

void addTrack(test::MockMagdaApi& api, TrackId id, const juce::String& name) {
    TrackInfo t;
    t.id = id;
    t.name = name;
    t.type = TrackType::Audio;
    api.tracks_.tracks.push_back(t);
    api.tracks_.nextId = std::max(api.tracks_.nextId, id + 1);
}

}  // namespace

TEST_CASE("DSL filter(tracks) with no condition groups every track", "[dsl][tracks][group]") {
    test::MockMagdaApi api;
    addTrack(api, 10, "Kick");
    addTrack(api, 20, "Snare");
    addTrack(api, 30, "Bass");

    dsl::Interpreter interp(api);

    // master-selected "group all tracks"
    REQUIRE(interp.execute("filter(tracks).track.group(name=\"All Tracks\")"));

    REQUIRE(api.tracks_.groupWrites.size() == 1);
    CHECK(api.tracks_.groupWrites[0].ids == std::vector<TrackId>{10, 20, 30});
    CHECK(api.tracks_.groupWrites[0].name == "All Tracks");
}

TEST_CASE("DSL filter(tracks) with no condition fans a set across every track", "[dsl][tracks]") {
    test::MockMagdaApi api;
    addTrack(api, 10, "Kick");
    addTrack(api, 20, "Snare");

    dsl::Interpreter interp(api);

    REQUIRE(interp.execute("filter(tracks).track.set(mute=true)"));

    REQUIRE(api.tracks_.muteWrites.size() == 2);
    CHECK(api.tracks_.muteWrites[0].id == 10);
    CHECK(api.tracks_.muteWrites[1].id == 20);
    CHECK(api.tracks_.muteWrites[0].value);
    CHECK(api.tracks_.muteWrites[1].value);
}

TEST_CASE("DSL track.move forwards a 1-based position to the facade", "[dsl][tracks][reorder]") {
    test::MockMagdaApi api;
    addTrack(api, 10, "Kick");
    addTrack(api, 20, "Snare");
    addTrack(api, 30, "Bass");

    dsl::Interpreter interp(api);

    REQUIRE(interp.execute("track(id=3).track.move(index=1)"));

    REQUIRE(api.tracks_.moveWrites.size() == 1);
    CHECK(api.tracks_.moveWrites[0].id == 30);
    CHECK(api.tracks_.moveWrites[0].position == 1);
}

TEST_CASE("DSL track.move without index errors", "[dsl][tracks][reorder]") {
    test::MockMagdaApi api;
    addTrack(api, 10, "Kick");

    dsl::Interpreter interp(api);
    REQUIRE_FALSE(interp.execute("track(id=1).track.move()"));
}

TEST_CASE("DSL groups explicit track ids and chains colour onto group", "[dsl][tracks][group]") {
    test::MockMagdaApi api;
    addTrack(api, 10, "Kick");
    addTrack(api, 20, "Snare");
    addTrack(api, 30, "Hat");

    dsl::Interpreter interp(api);

    REQUIRE(interp.execute(
        "track(id=1).track.group(name=\"Drums\", tracks=\"1,2,3\").track.set(colour=\"#ff5a36\")"));

    REQUIRE(api.tracks_.groupWrites.size() == 1);
    CHECK(api.tracks_.groupWrites[0].ids == std::vector<TrackId>{10, 20, 30});
    CHECK(api.tracks_.groupWrites[0].name == "Drums");

    REQUIRE(api.tracks_.colourWrites.size() == 1);
    CHECK(api.tracks_.colourWrites[0].id == api.tracks_.groupWrites[0].groupId);
    CHECK(api.tracks_.colourWrites[0].value == juce::Colour(0xffff5a36));
}

TEST_CASE("DSL can rename and colour code tracks consistently", "[dsl][tracks][colour]") {
    test::MockMagdaApi api;
    addTrack(api, 10, "Audio 1");
    addTrack(api, 20, "Audio 2");

    dsl::Interpreter interp(api);

    REQUIRE(interp.execute("track(id=1).track.set(name=\"Drums - Kick\", color=\"#ff5a36\")\n"
                           "track(id=2).track.set(name=\"Drums - Snare\", colour=\"44c7ff\")"));

    REQUIRE(api.tracks_.nameWrites.size() == 2);
    CHECK(api.tracks_.nameWrites[0].value == "Drums - Kick");
    CHECK(api.tracks_.nameWrites[1].value == "Drums - Snare");

    REQUIRE(api.tracks_.colourWrites.size() == 2);
    CHECK(api.tracks_.colourWrites[0].value == juce::Colour(0xffff5a36));
    CHECK(api.tracks_.colourWrites[1].value == juce::Colour(0xff44c7ff));
}

TEST_CASE("DSL explicit track ids stay stable after grouping reorders tracks",
          "[dsl][tracks][group]") {
    test::MockMagdaApi api;
    addTrack(api, 10, "Kick");
    addTrack(api, 20, "Snare");
    addTrack(api, 30, "Bass");
    addTrack(api, 40, "Lead Vox");
    addTrack(api, 50, "Double Vox");

    dsl::Interpreter interp(api);

    REQUIRE(interp.execute("track(id=1).track.group(name=\"Rhythm\", tracks=\"1,2,3\")\n"
                           "track(id=4).track.group(name=\"Vocals\", tracks=\"4,5\")"));

    REQUIRE(api.tracks_.groupWrites.size() == 2);
    CHECK(api.tracks_.groupWrites[0].ids == std::vector<TrackId>{10, 20, 30});
    CHECK(api.tracks_.groupWrites[1].ids == std::vector<TrackId>{40, 50});
}
