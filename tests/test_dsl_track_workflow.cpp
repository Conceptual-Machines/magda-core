#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/agents/dsl_interpreter.hpp"
#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

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

// Find the first top-level group track whose name matches.
const TrackInfo* findGroup(const juce::String& name) {
    auto& tm = TrackManager::getInstance();
    for (auto id : tm.getTopLevelTracks()) {
        const auto* t = tm.getTrack(id);
        if (t && t->isGroup() && t->name == name)
            return t;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("DSL filter(tracks) groups every track and is undoable", "[dsl][tracks][group]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();
    UndoManager::getInstance().clearHistory();
    auto k = tm.createTrack("Kick", TrackType::Audio);
    auto s = tm.createTrack("Snare", TrackType::Audio);
    auto b = tm.createTrack("Bass", TrackType::Audio);

    MagdaApiLive api;
    dsl::Interpreter interp(api);
    REQUIRE(interp.execute("filter(tracks).track.group(name=\"All Tracks\")"));

    const auto* group = findGroup("All Tracks");
    REQUIRE(group != nullptr);
    CHECK(group->childIds == std::vector<TrackId>{k, s, b});

    // Undo dissolves the group, restoring the three top-level tracks.
    REQUIRE(UndoManager::getInstance().undo());
    CHECK(tm.getTopLevelTracks() == std::vector<TrackId>{k, s, b});

    tm.clearAllTracks();
    UndoManager::getInstance().clearHistory();
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

TEST_CASE("DSL track.move without index errors", "[dsl][tracks][reorder]") {
    test::MockMagdaApi api;
    addTrack(api, 10, "Kick");

    dsl::Interpreter interp(api);
    REQUIRE_FALSE(interp.execute("track(id=1).track.move()"));
}

TEST_CASE("DSL groups explicit track ids and chains colour onto the group",
          "[dsl][tracks][group]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();
    UndoManager::getInstance().clearHistory();
    auto k = tm.createTrack("Kick", TrackType::Audio);
    auto s = tm.createTrack("Snare", TrackType::Audio);
    auto h = tm.createTrack("Hat", TrackType::Audio);

    MagdaApiLive api;
    dsl::Interpreter interp(api);
    REQUIRE(interp.execute(
        "track(id=1).track.group(name=\"Drums\", tracks=\"1,2,3\").track.set(colour=\"#ff5a36\")"));

    const auto* group = findGroup("Drums");
    REQUIRE(group != nullptr);
    CHECK(group->childIds == std::vector<TrackId>{k, s, h});
    // The chained .track.set targets the freshly-created group.
    CHECK(group->colour == juce::Colour(0xffff5a36));

    tm.clearAllTracks();
    UndoManager::getInstance().clearHistory();
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
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();
    UndoManager::getInstance().clearHistory();
    auto k = tm.createTrack("Kick", TrackType::Audio);
    auto s = tm.createTrack("Snare", TrackType::Audio);
    auto b = tm.createTrack("Bass", TrackType::Audio);
    auto lv = tm.createTrack("Lead Vox", TrackType::Audio);
    auto dv = tm.createTrack("Double Vox", TrackType::Audio);

    MagdaApiLive api;
    dsl::Interpreter interp(api);
    // Ids resolve against a snapshot taken before execution, so the second
    // group's ids still map to the vocal tracks even though the first group
    // reordered the track list.
    REQUIRE(interp.execute("track(id=1).track.group(name=\"Rhythm\", tracks=\"1,2,3\")\n"
                           "track(id=4).track.group(name=\"Vocals\", tracks=\"4,5\")"));

    const auto* rhythm = findGroup("Rhythm");
    const auto* vocals = findGroup("Vocals");
    REQUIRE(rhythm != nullptr);
    REQUIRE(vocals != nullptr);
    CHECK(rhythm->childIds == std::vector<TrackId>{k, s, b});
    CHECK(vocals->childIds == std::vector<TrackId>{lv, dv});

    tm.clearAllTracks();
    UndoManager::getInstance().clearHistory();
}
