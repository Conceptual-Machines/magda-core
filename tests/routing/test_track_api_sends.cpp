#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/track_api_live.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

namespace {

struct SendApiFixture {
    SendApiFixture() {
        tracks.setAudioEngine(nullptr);
        tracks.clearAllTracks();
        AutomationManager::getInstance().clearAll();
        undo.clearHistory();
    }

    ~SendApiFixture() {
        undo.clearHistory();
        AutomationManager::getInstance().clearAll();
        tracks.clearAllTracks();
        tracks.setAudioEngine(nullptr);
    }

    TrackId add(const juce::String& name, TrackType type = TrackType::Media) {
        return tracks.createTrack(name, type);
    }

    static juce::String endpoint(TrackId id) {
        return "track:" + juce::String(id);
    }

    TrackManager& tracks = TrackManager::getInstance();
    UndoManager& undo = UndoManager::getInstance();
    TrackApiLive api;
};

}  // namespace

TEST_CASE("Send lifecycle preserves safe identity through undo and redo",
          "[routing][api][sends][2837]") {
    SendApiFixture fixture;
    const auto source = fixture.add("Source");
    const auto firstDestination = fixture.add("Delay");
    const auto secondDestination = fixture.add("Reverb");

    TrackSendPatch create;
    create.destinationEndpointId = SendApiFixture::endpoint(firstDestination);
    create.level = 0.4f;
    create.enabled = false;
    create.preFader = true;
    const auto created = fixture.api.createSend(source, create);
    REQUIRE(created.status == TrackSendMutationStatus::Applied);
    REQUIRE(created.send.has_value());
    const auto sendId = created.send->id;
    CHECK(sendId.startsWith("send:"));
    CHECK(created.send->sourceTrackId == source);
    CHECK(created.send->destinationEndpointId == SendApiFixture::endpoint(firstDestination));
    CHECK(created.send->level == Catch::Approx(0.4f));
    CHECK_FALSE(created.send->enabled);
    CHECK(created.send->preFader);

    REQUIRE(fixture.undo.undo());
    CHECK(fixture.api.getSends(source).empty());
    REQUIRE(fixture.undo.redo());
    REQUIRE(fixture.api.getSends(source).size() == 1);
    CHECK(fixture.api.getSends(source).front().id == sendId);

    TrackSendPatch update;
    update.destinationEndpointId = SendApiFixture::endpoint(secondDestination);
    update.level = 0.75f;
    update.enabled = true;
    update.preFader = false;
    const auto updated = fixture.api.updateSend(sendId, update);
    REQUIRE(updated.status == TrackSendMutationStatus::Applied);
    REQUIRE(updated.send.has_value());
    CHECK(updated.send->id == sendId);
    CHECK(updated.send->destinationEndpointId == SendApiFixture::endpoint(secondDestination));
    CHECK(updated.send->level == Catch::Approx(0.75f));
    CHECK(updated.send->enabled);
    CHECK_FALSE(updated.send->preFader);
    REQUIRE(updated.invalidatedConnections.size() == 1);
    CHECK(updated.invalidatedConnections.front().destinationEndpointId ==
          SendApiFixture::endpoint(firstDestination));
    CHECK(updated.invalidatedConnections.front().reason == "destination_replaced");

    TrackSendPatch noChange;
    noChange.level = 0.75f;
    CHECK(fixture.api.updateSend(sendId, noChange).status == TrackSendMutationStatus::Unchanged);
    REQUIRE(fixture.undo.undo());
    const auto restored = fixture.api.getSends(source);
    REQUIRE(restored.size() == 1);
    CHECK(restored.front().id == sendId);
    CHECK(restored.front().destinationEndpointId == SendApiFixture::endpoint(firstDestination));
    CHECK(restored.front().level == Catch::Approx(0.4f));

    REQUIRE(fixture.undo.redo());
    const auto removed = fixture.api.removeSend(sendId);
    REQUIRE(removed.status == TrackSendMutationStatus::Applied);
    REQUIRE(removed.invalidatedConnections.size() == 1);
    CHECK(removed.invalidatedConnections.front().reason == "send_removed");
    CHECK(fixture.api.getSends(source).empty());
    REQUIRE(fixture.undo.undo());
    REQUIRE(fixture.api.getSends(source).size() == 1);
    CHECK(fixture.api.getSends(source).front().id == sendId);
}

TEST_CASE("Send preflight rejects duplicates feedback and unsafe destinations",
          "[routing][api][sends][2837]") {
    SendApiFixture fixture;
    const auto first = fixture.add("First");
    const auto second = fixture.add("Second");
    const auto chord = fixture.add("Chord", TrackType::Chord);

    TrackSendPatch toSecond;
    toSecond.destinationEndpointId = SendApiFixture::endpoint(second);
    REQUIRE(fixture.api.createSend(first, toSecond).status == TrackSendMutationStatus::Applied);
    const auto before = fixture.api.getSends(first);

    CHECK(fixture.api.createSend(first, toSecond).status == TrackSendMutationStatus::Duplicate);

    TrackSendPatch self;
    self.destinationEndpointId = SendApiFixture::endpoint(first);
    CHECK(fixture.api.createSend(first, self).status == TrackSendMutationStatus::FeedbackCycle);

    TrackSendPatch cycle;
    cycle.destinationEndpointId = SendApiFixture::endpoint(first);
    CHECK(fixture.api.createSend(second, cycle).status == TrackSendMutationStatus::FeedbackCycle);

    TrackSendPatch unavailable;
    unavailable.destinationEndpointId = "route:audio:input:not-a-real-endpoint";
    CHECK(fixture.api.createSend(first, unavailable).status ==
          TrackSendMutationStatus::EndpointNotFound);
    CHECK(fixture.api.createSend(chord, toSecond).status == TrackSendMutationStatus::Incompatible);
    CHECK(fixture.api.getSends(first) == before);

    // Only the successful setup mutation reached the undo stack.
    REQUIRE(fixture.undo.undo());
    CHECK(fixture.api.getSends(first).empty());
    CHECK_FALSE(fixture.undo.undo());
}

TEST_CASE("Disabled sends do not introduce feedback until enabled", "[routing][api][sends][2837]") {
    SendApiFixture fixture;
    const auto first = fixture.add("First");
    const auto second = fixture.add("Second");

    TrackSendPatch forward;
    forward.destinationEndpointId = SendApiFixture::endpoint(second);
    REQUIRE(fixture.api.createSend(first, forward).status == TrackSendMutationStatus::Applied);

    TrackSendPatch disabledReturn;
    disabledReturn.destinationEndpointId = SendApiFixture::endpoint(first);
    disabledReturn.enabled = false;
    const auto created = fixture.api.createSend(second, disabledReturn);
    REQUIRE(created.status == TrackSendMutationStatus::Applied);
    REQUIRE(created.send.has_value());

    TrackSendPatch enable;
    enable.enabled = true;
    CHECK(fixture.api.updateSend(created.send->id, enable).status ==
          TrackSendMutationStatus::FeedbackCycle);
    CHECK_FALSE(fixture.api.getSends(second).front().enabled);
}

TEST_CASE("Send creation enforces the engine send limit before mutation",
          "[routing][api][sends][2837]") {
    SendApiFixture fixture;
    const auto source = fixture.add("Source");
    for (int index = 0; index < TrackManager::MAX_SENDS_PER_TRACK; ++index) {
        const auto destination = fixture.add("Destination " + juce::String(index));
        TrackSendPatch create;
        create.destinationEndpointId = SendApiFixture::endpoint(destination);
        REQUIRE(fixture.api.createSend(source, create).status == TrackSendMutationStatus::Applied);
    }
    REQUIRE(static_cast<int>(fixture.api.getSends(source).size()) ==
            TrackManager::MAX_SENDS_PER_TRACK);

    const auto extraDestination = fixture.add("One too many");
    TrackSendPatch extra;
    extra.destinationEndpointId = SendApiFixture::endpoint(extraDestination);
    CHECK(fixture.api.createSend(source, extra).status == TrackSendMutationStatus::LimitReached);
    CHECK(static_cast<int>(fixture.api.getSends(source).size()) ==
          TrackManager::MAX_SENDS_PER_TRACK);
}

TEST_CASE("Destination changes remap durable send references and removal protects them",
          "[routing][api][sends][2837]") {
    SendApiFixture fixture;
    const auto source = fixture.add("Source");
    const auto firstDestination = fixture.add("Delay");
    const auto secondDestination = fixture.add("Reverb");

    TrackSendPatch create;
    create.destinationEndpointId = SendApiFixture::endpoint(firstDestination);
    const auto created = fixture.api.createSend(source, create);
    REQUIRE(created.send.has_value());
    const auto sendId = created.send->id;
    const auto oldBus = fixture.tracks.getTrack(source)->sends.front().busIndex;
    auto& automation = AutomationManager::getInstance();
    const auto laneId = automation.createLane(ControlTarget::sendLevel(source, oldBus),
                                              AutomationLaneType::Absolute);
    REQUIRE(laneId != INVALID_AUTOMATION_LANE_ID);

    TrackSendPatch update;
    update.destinationEndpointId = SendApiFixture::endpoint(secondDestination);
    REQUIRE(fixture.api.updateSend(sendId, update).status == TrackSendMutationStatus::Applied);
    const auto newBus = fixture.tracks.getTrack(source)->sends.front().busIndex;
    CHECK(newBus != oldBus);
    REQUIRE(automation.getLane(laneId) != nullptr);
    CHECK(automation.getLane(laneId)->target == ControlTarget::sendLevel(source, newBus));

    CHECK(fixture.api.removeSend(sendId).status == TrackSendMutationStatus::Referenced);
    REQUIRE(fixture.api.getSends(source).size() == 1);
    automation.deleteLane(laneId);
    CHECK(fixture.api.removeSend(sendId).status == TrackSendMutationStatus::Applied);
}
