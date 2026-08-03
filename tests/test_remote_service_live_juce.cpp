#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/api/remote_model_bridge.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;
using namespace magda::remote;

namespace {

/// Drives the dispatcher against the real facade and the real singletons, so
/// these assert on what actually happens to the project rather than on what a
/// mock recorded.
struct LiveFixture {
    MagdaApiLive api;
    RemoteApiService service{api};
    std::unique_ptr<ModelChangeBridge> bridge;

    LiveFixture() {
        TrackManager::getInstance().clearAllTracks();
        UndoManager::getInstance().clearHistory();
        bridge = std::make_unique<ModelChangeBridge>(service);
    }

    ~LiveFixture() {
        bridge.reset();
        TrackManager::getInstance().clearAllTracks();
        UndoManager::getInstance().clearHistory();
    }

    Response run(const juce::String& name, const juce::var& input, RequestContext context = {}) {
        Response captured;
        int completions = 0;
        service.dispatch(name, input, context, [&](Response response) {
            captured = std::move(response);
            ++completions;
        });
        REQUIRE(completions == 1);
        return captured;
    }
};

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

}  // namespace

TEST_CASE("A live write advances the revision exactly once", "[remote][live][revisions]") {
    const ScopedMessageThreadAssertionDisabler disabler;
    LiveFixture fixture;

    const auto before = fixture.service.currentRevision();
    const auto response =
        fixture.run("tracks.create", object({{"name", "Live"}, {"type", "audio"}}));
    REQUIRE(response.ok);

    // The live facade notifies TrackManager listeners synchronously from inside
    // the handler, so the bridge sees tracksChanged() while the request is
    // still executing. Without the re-entrancy guard that callback would bump
    // the revision too, and one request would advance it twice.
    REQUIRE(fixture.service.currentRevision() == before + 1);
    REQUIRE(response.revision == before + 1);
}

TEST_CASE("A live multi-field update advances the revision exactly once",
          "[remote][live][revisions]") {
    const ScopedMessageThreadAssertionDisabler disabler;
    LiveFixture fixture;

    const auto created =
        fixture.run("tracks.create", object({{"name", "Strip"}, {"type", "audio"}}));
    REQUIRE(created.ok);
    const auto trackId = static_cast<int>(created.result["id"]);
    const auto before = fixture.service.currentRevision();

    // Each setter fires its own trackPropertyChanged.
    const auto updated = fixture.run("tracks.update", object({{"trackId", trackId},
                                                              {"name", "Renamed"},
                                                              {"volume", 0.5},
                                                              {"pan", -0.25},
                                                              {"muted", true},
                                                              {"soloed", false}}));
    REQUIRE(updated.ok);
    REQUIRE(fixture.service.currentRevision() == before + 1);
}

TEST_CASE("A UI-originated change still advances the revision", "[remote][live][revisions]") {
    const ScopedMessageThreadAssertionDisabler disabler;
    LiveFixture fixture;

    const auto before = fixture.service.currentRevision();
    // Not routed through the dispatcher, so the re-entrancy guard must not
    // suppress it — this is exactly the case the bridge exists for.
    TrackManager::getInstance().createTrack("By hand", TrackType::Audio);

    REQUIRE(fixture.service.currentRevision() > before);
}

TEST_CASE("A live write is reachable through the real facade", "[remote][live]") {
    const ScopedMessageThreadAssertionDisabler disabler;
    LiveFixture fixture;

    const auto response =
        fixture.run("tracks.create", object({{"name", "Verified"}, {"type", "audio"}}));
    REQUIRE(response.ok);

    const auto trackId = static_cast<TrackId>(static_cast<int>(response.result["id"]));
    const auto* track = TrackManager::getInstance().getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->name == "Verified");

    // And the read side agrees, through the same dispatcher.
    const auto fetched =
        fixture.run("tracks.get", object({{"trackId", static_cast<int>(trackId)}}));
    REQUIRE(fetched.ok);
    REQUIRE(fetched.result["name"].toString() == "Verified");
}

TEST_CASE("A cascading write notifies every affected topic", "[remote][live][changes]") {
    const ScopedMessageThreadAssertionDisabler disabler;
    LiveFixture fixture;

    const auto created =
        fixture.run("tracks.create", object({{"name", "Doomed"}, {"type", "audio"}}));
    REQUIRE(created.ok);
    const auto trackId = static_cast<int>(created.result["id"]);

    std::vector<ChangeSource::Change> seen;
    fixture.service.changes().addListener(
        [&seen](const std::vector<ChangeSource::Change>& changes) {
            seen.insert(seen.end(), changes.begin(), changes.end());
        });

    REQUIRE(fixture.run("tracks.delete", object({{"trackId", trackId}})).ok);
    fixture.service.changes().flush();

    // Deleting a track takes its clips and devices with it, so a subscriber
    // watching only clips must still hear about it.
    std::vector<Topic> topics;
    for (const auto& change : seen)
        topics.push_back(change.topic);
    REQUIRE(std::find(topics.begin(), topics.end(), Topic::Tracks) != topics.end());
    REQUIRE(std::find(topics.begin(), topics.end(), Topic::Clips) != topics.end());
    REQUIRE(std::find(topics.begin(), topics.end(), Topic::Devices) != topics.end());
}

TEST_CASE("A client can create a track, add a clip, and add notes to it",
          "[remote][live][integration]") {
    const ScopedMessageThreadAssertionDisabler disabler;
    LiveFixture fixture;

    // The #701 acceptance path, end to end through the dispatcher against the
    // real model.
    const auto track = fixture.run("tracks.create", object({{"name", "Lead"}, {"type", "audio"}}));
    REQUIRE(track.ok);
    const auto trackId = static_cast<int>(track.result["id"]);

    const auto clip = fixture.run("clips.createMidi", object({{"trackId", trackId},
                                                              {"startBeat", 0.0},
                                                              {"lengthBeats", 4.0},
                                                              {"view", "arrangement"}}));
    REQUIRE(clip.ok);
    const auto clipId = static_cast<int>(clip.result["id"]);

    const auto note = fixture.run("clips.addMidiNote", object({{"clipId", clipId},
                                                               {"note", 60},
                                                               {"velocity", 100},
                                                               {"startBeat", 0.0},
                                                               {"lengthBeats", 1.0}}));
    REQUIRE(note.ok);
    REQUIRE(note.result["notes"].getArray() != nullptr);
    REQUIRE(note.result["notes"].getArray()->size() == 1);

    const auto listed = fixture.run("clips.list", object({{"trackId", trackId}}));
    REQUIRE(listed.ok);
    REQUIRE(listed.result.getArray() != nullptr);
    REQUIRE(listed.result.getArray()->size() == 1);

    // Three mutations, three revisions, and each one undoable on its own.
    REQUIRE(fixture.service.currentRevision() == 3);
    REQUIRE(UndoManager::getInstance().canUndo());
}

TEST_CASE("Adding a point to a clip-based lane is refused", "[remote][live][automation]") {
    const ScopedMessageThreadAssertionDisabler disabler;
    LiveFixture fixture;
    AutomationManager::getInstance().clearAll();

    const auto trackId = TrackManager::getInstance().createTrack("Auto", TrackType::Audio);
    AutomationTarget target;
    target.kind = ControlTarget::Kind::TrackVolume;
    target.devicePath = ChainNodePath::trackLevel(trackId);
    const auto laneId =
        AutomationManager::getInstance().createLane(target, AutomationLaneType::ClipBased);
    REQUIRE(laneId != INVALID_AUTOMATION_LANE_ID);

    const auto before = fixture.service.currentRevision();
    const auto response =
        fixture.run("automation.addPoint", object({{"laneId", static_cast<int>(laneId)},
                                                   {"beatPosition", 0.0},
                                                   {"value", 0.5},
                                                   {"curve", "linear"}}));

    // Points on a clip-based lane live on its clips. Reporting success here
    // would advance the revision for a lane that gained nothing.
    REQUIRE_FALSE(response.ok);
    REQUIRE(toString(response.error.code) == "conflict");
    REQUIRE(fixture.service.currentRevision() == before);

    AutomationManager::getInstance().clearAll();
}

TEST_CASE("Clearing an already-empty lane does not advance the revision",
          "[remote][live][automation]") {
    const ScopedMessageThreadAssertionDisabler disabler;
    LiveFixture fixture;
    AutomationManager::getInstance().clearAll();

    const auto trackId = TrackManager::getInstance().createTrack("Auto", TrackType::Audio);
    AutomationTarget target;
    target.kind = ControlTarget::Kind::TrackVolume;
    target.devicePath = ChainNodePath::trackLevel(trackId);
    const auto laneId =
        AutomationManager::getInstance().createLane(target, AutomationLaneType::Absolute);
    REQUIRE(laneId != INVALID_AUTOMATION_LANE_ID);

    // A freshly created lane is not necessarily empty, so clear it once to
    // reach a known state. That first clear is a real mutation.
    REQUIRE(fixture.run("automation.clearLane", object({{"laneId", static_cast<int>(laneId)}})).ok);

    const auto before = fixture.service.currentRevision();
    const auto response =
        fixture.run("automation.clearLane", object({{"laneId", static_cast<int>(laneId)}}));

    // Succeeds — the lane is in the requested state — but nothing changed, so
    // holding the revision avoids invalidating other clients for nothing.
    REQUIRE(response.ok);
    REQUIRE(fixture.service.currentRevision() == before);

    AutomationManager::getInstance().clearAll();
}

TEST_CASE("A remote mutation is undoable as one step", "[remote][live][undo]") {
    const ScopedMessageThreadAssertionDisabler disabler;
    LiveFixture fixture;

    REQUIRE_FALSE(UndoManager::getInstance().canUndo());

    const auto response =
        fixture.run("tracks.create", object({{"name", "Undoable"}, {"type", "audio"}}));
    REQUIRE(response.ok);
    const auto trackId = static_cast<TrackId>(static_cast<int>(response.result["id"]));
    REQUIRE(TrackManager::getInstance().getTrack(trackId) != nullptr);

    // The acceptance criterion: one remote mutation is reversed by one Undo.
    REQUIRE(UndoManager::getInstance().canUndo());
    UndoManager::getInstance().undo();
    REQUIRE(TrackManager::getInstance().getTrack(trackId) == nullptr);
}
