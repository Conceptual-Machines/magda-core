#include <catch2/catch_test_macros.hpp>

#include "magda/daw/api/track_api_live.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

namespace {

struct RoutingApiFixture {
    RoutingApiFixture() {
        tracks.setAudioEngine(nullptr);
        tracks.clearAllTracks();
        undo.clearHistory();
    }
    ~RoutingApiFixture() {
        undo.clearHistory();
        tracks.clearAllTracks();
        tracks.setAudioEngine(nullptr);
    }

    TrackId add(const juce::String& name, TrackType type = TrackType::Media) {
        return tracks.createTrack(name, type);
    }

    TrackManager& tracks = TrackManager::getInstance();
    UndoManager& undo = UndoManager::getInstance();
    TrackApiLive api;
};

juce::String noneId(RoutingMedia media, RoutingDirection direction) {
    const auto mediaName = media == RoutingMedia::Audio ? "audio" : "midi";
    const auto directionName = direction == RoutingDirection::Input ? "input" : "output";
    return "none:" + juce::String(mediaName) + ":" + directionName;
}

}  // namespace

TEST_CASE("Routing endpoint ids hide backend identifiers", "[routing][api][2832]") {
    const auto token = routingEndpointId(RoutingMedia::Midi, RoutingDirection::Output,
                                         "/private/backend/device-42");
    REQUIRE(token.startsWith("route:midi:output:"));
    REQUIRE_FALSE(token.contains("private"));
    REQUIRE_FALSE(token.contains("device-42"));
    REQUIRE(token == routingEndpointId(RoutingMedia::Midi, RoutingDirection::Output,
                                       "/private/backend/device-42"));
    REQUIRE(token != routingEndpointId(RoutingMedia::Midi, RoutingDirection::Input,
                                       "/private/backend/device-42"));
}

TEST_CASE("Current empty routes project to discoverable None endpoints", "[routing][api][2832]") {
    RoutingApiFixture fixture;
    const auto track = fixture.add("Track");

    const auto routing = fixture.api.getRouting(track);
    REQUIRE(routing.has_value());
    CHECK(routing->audioInputEndpointId == "none:audio:input");
    CHECK(routing->audioOutputEndpointId == "master");
    CHECK(routing->midiOutputEndpointId == "none:midi:output");

    const auto endpoints = fixture.api.getRoutingEndpoints();
    for (const auto& id : {routing->audioInputEndpointId, routing->audioOutputEndpointId,
                           routing->midiInputEndpointId, routing->midiOutputEndpointId})
        CHECK(std::ranges::contains(endpoints, id, &RoutingEndpoint::id));
}

TEST_CASE("A selected route without engine support remains visible but unavailable",
          "[routing][api][2832]") {
    RoutingApiFixture fixture;
    const auto trackId = fixture.add("Track");
    auto* track = fixture.tracks.getTrack(trackId);
    REQUIRE(track != nullptr);
    track->audioInputDevice = "default";

    const auto endpoints = fixture.api.getRoutingEndpoints();
    const auto endpoint = std::ranges::find_if(endpoints, [](const auto& candidate) {
        return candidate.id == "default" && candidate.media == RoutingMedia::Audio &&
               candidate.direction == RoutingDirection::Input;
    });
    REQUIRE(endpoint != endpoints.end());
    CHECK_FALSE(endpoint->available);

    TrackRoutingPatch patch;
    patch.audioInputEndpointId = "default";
    CHECK(fixture.api.setRouting(trackId, patch).status == SetTrackRoutingStatus::EndpointNotFound);
    CHECK(track->audioInputDevice == "default");
    CHECK_FALSE(fixture.undo.canUndo());
}

TEST_CASE("Track routing applies cascades as one undoable edit", "[routing][api][2832]") {
    RoutingApiFixture fixture;
    const auto source = fixture.add("Source");
    const auto destination = fixture.add("Destination");
    REQUIRE(fixture.tracks.getTrack(destination)->midiInputDevice == "all");

    TrackRoutingPatch patch;
    patch.audioInputEndpointId = "track:" + juce::String(source);
    const auto result = fixture.api.setRouting(destination, patch);

    REQUIRE(result.status == SetTrackRoutingStatus::Applied);
    REQUIRE(result.droppedConnections.size() == 1);
    CHECK(result.droppedConnections.front().trackId == destination);
    CHECK(result.droppedConnections.front().field == "midiInputEndpointId");
    CHECK(result.droppedConnections.front().endpointId == "all");
    CHECK(fixture.tracks.getTrack(destination)->audioInputDevice ==
          "track:" + juce::String(source));
    CHECK(fixture.tracks.getTrack(destination)->midiInputDevice.isEmpty());

    REQUIRE(fixture.undo.undo());
    CHECK(fixture.tracks.getTrack(destination)->audioInputDevice.isEmpty());
    CHECK(fixture.tracks.getTrack(destination)->midiInputDevice == "all");
    REQUIRE(fixture.undo.redo());
    CHECK(fixture.tracks.getTrack(destination)->audioInputDevice ==
          "track:" + juce::String(source));
    CHECK(fixture.tracks.getTrack(destination)->midiInputDevice.isEmpty());
}

TEST_CASE("Routing preflight rejects feedback without mutation", "[routing][api][2832]") {
    RoutingApiFixture fixture;
    const auto first = fixture.add("First");
    const auto second = fixture.add("Second");

    TrackRoutingPatch firstPatch;
    firstPatch.audioInputEndpointId = "track:" + juce::String(first);
    REQUIRE(fixture.api.setRouting(second, firstPatch).status == SetTrackRoutingStatus::Applied);
    const auto beforeFirst = fixture.tracks.getTrack(first)->routingState();
    const auto beforeSecond = fixture.tracks.getTrack(second)->routingState();

    TrackRoutingPatch cycle;
    cycle.audioInputEndpointId = "track:" + juce::String(second);
    const auto rejected = fixture.api.setRouting(first, cycle);
    REQUIRE(rejected.status == SetTrackRoutingStatus::FeedbackCycle);
    CHECK(fixture.tracks.getTrack(first)->routingState() == beforeFirst);
    CHECK(fixture.tracks.getTrack(second)->routingState() == beforeSecond);

    // Only the successful setup edit exists on the stack.
    REQUIRE(fixture.undo.undo());
    CHECK(fixture.tracks.getTrack(second)->audioInputDevice.isEmpty());
    CHECK_FALSE(fixture.undo.undo());
}

TEST_CASE("A MIDI Out to a track is the sender's, and several may name one track",
          "[routing][api][2832][2209]") {
    RoutingApiFixture fixture;
    const auto first = fixture.add("First");
    const auto second = fixture.add("Second");
    const auto destination = fixture.add("Destination");
    fixture.tracks.setTrackAudioInput(destination, "default");
    fixture.undo.clearHistory();

    TrackRoutingPatch patch;
    patch.midiOutputEndpointId = "track:" + juce::String(destination);
    patch.midiOutputChannel = 2;
    REQUIRE(fixture.api.setRouting(first, patch).status == SetTrackRoutingStatus::Applied);
    patch.midiOutputChannel = 3;
    const auto result = fixture.api.setRouting(second, patch);
    REQUIRE(result.status == SetTrackRoutingStatus::Applied);

    // The destination keeps its own input.
    CHECK(result.droppedConnections.empty());
    CHECK(fixture.tracks.getTrack(destination)->audioInputDevice == "default");
    CHECK(fixture.tracks.getTrack(first)->midiOutputDevice == "track:" + juce::String(destination));
    CHECK(fixture.tracks.getTrack(second)->midiOutputChannel == 3);

    const auto projected = fixture.api.getRouting(first);
    REQUIRE(projected.has_value());
    CHECK(projected->midiOutputEndpointId == "track:" + juce::String(destination));
    CHECK(projected->midiOutputChannel == 2);

    // One undo step per edit, channel and route together.
    REQUIRE(fixture.undo.undo());
    CHECK(fixture.tracks.getTrack(second)->midiOutputDevice.isEmpty());
    CHECK(fixture.tracks.getTrack(second)->midiOutputChannel == 0);
    CHECK(fixture.tracks.getTrack(first)->midiOutputChannel == 2);

    TrackRoutingPatch clear;
    clear.midiOutputEndpointId = noneId(RoutingMedia::Midi, RoutingDirection::Output);
    REQUIRE(fixture.api.setRouting(first, clear).status == SetTrackRoutingStatus::Applied);
    CHECK(fixture.tracks.getTrack(first)->midiOutputDevice.isEmpty());
}

TEST_CASE("A MIDI channel outside 0-16 is refused", "[routing][api][2209]") {
    RoutingApiFixture fixture;
    const auto track = fixture.add("Track");

    TrackRoutingPatch patch;
    patch.midiInputChannel = 17;
    CHECK(fixture.api.setRouting(track, patch).status == SetTrackRoutingStatus::Incompatible);

    patch.midiInputChannel = 10;
    REQUIRE(fixture.api.setRouting(track, patch).status == SetTrackRoutingStatus::Applied);
    CHECK(fixture.tracks.getTrack(track)->midiInputChannel == 10);
}

TEST_CASE("Routing rejects unavailable and input-incompatible endpoints", "[routing][api][2832]") {
    RoutingApiFixture fixture;
    const auto media = fixture.add("Media");
    const auto group = fixture.add("Group", TrackType::Group);

    TrackRoutingPatch unavailable;
    unavailable.audioInputEndpointId = "route:audio:input:missing";
    CHECK(fixture.api.setRouting(media, unavailable).status ==
          SetTrackRoutingStatus::EndpointNotFound);

    TrackRoutingPatch groupInput;
    groupInput.midiInputEndpointId = "all";
    CHECK(fixture.api.setRouting(group, groupInput).status == SetTrackRoutingStatus::Incompatible);
}

TEST_CASE("Atomic routing state replacement validates every target before commit",
          "[routing][api][2832]") {
    RoutingApiFixture fixture;
    const auto track = fixture.add("Track");
    const auto before = fixture.tracks.getTrack(track)->routingState();
    auto changed = before;
    changed.audioOutput = {};

    CHECK_FALSE(fixture.tracks.applyTrackRoutingStates(
        {changed, TrackRoutingState{99999, {}, {}, {}, {}}}));
    CHECK(fixture.tracks.getTrack(track)->routingState() == before);
}
