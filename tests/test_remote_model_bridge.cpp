#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "MockMagdaApi.hpp"
#include "RemoteTestScopes.hpp"
#include "magda/daw/api/remote_model_bridge.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;
using namespace magda::remote;
using magda::test::fullyGrantedContext;
using magda::test::MockMagdaApi;

namespace {

/// Owns the service, the bridge, and a subscription, so each test starts from a
/// clean project and a clean listener set.
struct BridgeFixture {
    MockMagdaApi api;
    RemoteApiService service{api};
    std::vector<ChangeSource::Change> seen;
    std::unique_ptr<ModelChangeBridge> bridge;

    BridgeFixture() {
        TrackManager::getInstance().clearAllTracks();
        UndoManager::getInstance().clearHistory();
        service.changes().addListener([this](const std::vector<ChangeSource::Change>& changes) {
            seen.insert(seen.end(), changes.begin(), changes.end());
        });
        bridge = std::make_unique<ModelChangeBridge>(service);
    }

    ~BridgeFixture() {
        bridge.reset();
        TrackManager::getInstance().clearAllTracks();
    }

    bool sawTopic(Topic topic) const {
        return std::any_of(seen.begin(), seen.end(),
                           [topic](const auto& change) { return change.topic == topic; });
    }
};

}  // namespace

TEST_CASE("A track created outside the remote API reaches subscribers", "[remote][bridge]") {
    BridgeFixture fixture;
    const auto before = fixture.service.currentRevision();

    // The case the bridge exists for: this is what the MAGDA UI does, with no
    // remote client involved. Without the bridge a subscriber would never hear
    // about it.
    TrackManager::getInstance().createTrack("From the UI", TrackType::Audio);
    fixture.service.changes().flush();

    REQUIRE(fixture.sawTopic(Topic::Tracks));
    REQUIRE(fixture.service.currentRevision() > before);
}

TEST_CASE("A locally committed change makes a client's expected revision stale",
          "[remote][bridge][revisions]") {
    BridgeFixture fixture;

    // A client reads the revision, the user then edits in the UI, and the
    // client's write must not silently land on changed state.
    const auto clientView = fixture.service.currentRevision();
    TrackManager::getInstance().createTrack("Meanwhile", TrackType::Audio);

    auto context = fullyGrantedContext();
    context.expectedRevision = clientView;
    Response response;
    fixture.service.dispatch(
        "project.setTempo",
        [] {
            auto* input = new juce::DynamicObject();
            input->setProperty("tempo", 90.0);
            return juce::var(input);
        }(),
        context, [&](Response result) { response = std::move(result); });

    REQUIRE_FALSE(response.ok);
    REQUIRE(toString(response.error.code) == "conflict");
}

TEST_CASE("Device notifications land on the devices topic", "[remote][bridge]") {
    BridgeFixture fixture;
    const auto trackId = TrackManager::getInstance().createTrack("FX", TrackType::Audio);
    fixture.service.changes().flush();
    fixture.seen.clear();

    TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
    fixture.service.changes().flush();

    REQUIRE(fixture.sawTopic(Topic::Devices));
}

TEST_CASE("A burst of model notifications coalesces to one delivery", "[remote][bridge]") {
    BridgeFixture fixture;
    const auto trackId = TrackManager::getInstance().createTrack("Busy", TrackType::Audio);
    fixture.service.changes().flush();
    fixture.seen.clear();

    for (int index = 0; index < 50; ++index)
        TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
    fixture.service.changes().flush();

    // 50 model callbacks, one notification — the property #1857 needs so a
    // subscriber is not woken once per callback.
    const auto deviceChanges =
        std::count_if(fixture.seen.begin(), fixture.seen.end(),
                      [](const auto& change) { return change.topic == Topic::Devices; });
    REQUIRE(deviceChanges == 1);
}

TEST_CASE("Destroying the bridge detaches it from the model", "[remote][bridge][lifecycle]") {
    BridgeFixture fixture;
    fixture.bridge.reset();
    fixture.seen.clear();

    // Must not notify, and must not touch the destroyed listener.
    TrackManager::getInstance().createTrack("After detach", TrackType::Audio);
    fixture.service.changes().flush();

    REQUIRE(fixture.seen.empty());
}

TEST_CASE("A shut-down service ignores model notifications", "[remote][bridge][lifecycle]") {
    BridgeFixture fixture;
    fixture.service.shutdown();
    fixture.seen.clear();

    TrackManager::getInstance().createTrack("After shutdown", TrackType::Audio);
    fixture.service.changes().flush();

    REQUIRE(fixture.seen.empty());
}

TEST_CASE("Clip and track notifications also invalidate the session grid", "[remote][bridge]") {
    BridgeFixture fixture;
    const auto before = fixture.service.currentRevision();

    // The session grid is projected out of the clips and keyed by track, so a
    // subscriber watching only `session` has to hear about both. Marking only
    // the obvious topic would leave it showing a grid the project no longer has.
    TrackManager::getInstance().createTrack("Drums", TrackType::Audio);
    ClipManager::getInstance().clearAllClips();
    fixture.service.changes().flush();

    REQUIRE(fixture.sawTopic(Topic::Tracks));
    REQUIRE(fixture.sawTopic(Topic::Clips));
    REQUIRE(fixture.sawTopic(Topic::Session));

    // Two edits, two revisions — not four. A change that invalidates several
    // topics is still one change, and advancing the counter per topic would make
    // every multi-topic edit look like several to an optimistic writer.
    REQUIRE(fixture.service.currentRevision() == before + 2);
}
