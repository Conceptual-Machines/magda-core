// Subscriptions, revisioned events, and backpressure (#1857).
//
// Everything here is hub behaviour with no socket involved: what a subscriber
// receives, when a delta becomes a snapshot, and what happens to a client that
// stops consuming. How those events reach a WebSocket client is
// test_remote_websocket_server.cpp.
//
// The Catch2 runner has no MessageManager, so there is no flush timer and no
// sampling timer. `flush()` and `sampleNow()` are called directly, which makes
// every ordering assertion here deterministic rather than timing-dependent.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/api/remote_subscriptions.hpp"

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

namespace {

/// Reads MagdaApi live state, which asserts the message thread. The same
/// accommodation every other remote test makes.
struct MessageThreadRelaxation {
    ScopedMessageThreadAssertionDisabler disabler;
};

/// A subscriber that remembers what it was sent and can be told to stop taking
/// events, which is how backpressure is provoked without a real socket.
struct Recorder {
    std::vector<SubscriptionEvent> events;
    bool accepting = true;
    int refused = 0;
    int disconnects = 0;
    juce::String lastReason;

    SubscriptionHub::Sink sink() {
        return [this](const SubscriptionEvent& event) {
            if (!accepting) {
                ++refused;
                return false;
            }
            events.push_back(event);
            return true;
        };
    }

    SubscriptionHub::Disconnect disconnect() {
        return [this](const juce::String& reason) {
            ++disconnects;
            lastReason = reason;
        };
    }

    std::vector<SubscriptionEvent> forTopic(Topic topic) const {
        std::vector<SubscriptionEvent> matching;
        for (const auto& event : events)
            if (event.topic == topic)
                matching.push_back(event);
        return matching;
    }

    void clear() {
        events.clear();
    }
};

class FakeMeters : public MeterSource {
  public:
    std::vector<TrackLevels> levels;
    int calls = 0;

    std::vector<TrackLevels> sample() override {
        ++calls;
        return levels;
    }
};

juce::var stringArray(const std::vector<const char*>& values) {
    juce::Array<juce::var> array;
    for (const auto* value : values)
        array.add(juce::String(value));
    return array;
}

juce::var params(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

/// The hub answers through a callback because it hops to the message thread for
/// anything that reads the model. With no MessageManager in this runner there is
/// nowhere to hop, so the job runs inline and this is synchronous — the same
/// accommodation `RemoteApiService::dispatch` makes.
Response call(SubscriptionHub& hub, SubscriptionHub::ClientId client, const char* method,
              const juce::var& input) {
    Response captured;
    int completions = 0;
    const bool claimed = hub.handle(client, juce::String(method), input, [&](Response response) {
        captured = std::move(response);
        ++completions;
    });
    REQUIRE(claimed);
    REQUIRE(completions == 1);
    return captured;
}

Response subscribe(SubscriptionHub& hub, SubscriptionHub::ClientId client,
                   const std::vector<const char*>& topics) {
    return call(hub, client, "subscriptions.subscribe", params({{"topics", stringArray(topics)}}));
}

int snapshotCount(const Response& response) {
    const auto* snapshots = response.result["snapshots"].getArray();
    return snapshots == nullptr ? -1 : snapshots->size();
}

std::vector<juce::String> topicsOf(const Response& response) {
    std::vector<juce::String> names;
    if (const auto* array = response.result["topics"].getArray())
        for (const auto& entry : *array)
            names.push_back(entry.toString());
    return names;
}

TrackInfo makeTrack(TrackId id, const juce::String& name) {
    TrackInfo track;
    track.id = id;
    track.name = name;
    track.type = TrackType::Audio;
    return track;
}

/// One committed edit: bump the revision, mark the topic, and deliver.
void commit(RemoteApiService& service, Topic topic) {
    service.noteModelChanged(topic);
    service.changes().flush();
}

}  // namespace

TEST_CASE("Subscribing returns a snapshot for every requested model topic",
          "[remote][subscriptions]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    const auto response = subscribe(hub, client, {"tracks", "transport"});

    REQUIRE(response.ok);
    REQUIRE(topicsOf(response) == std::vector<juce::String>{"tracks", "transport"});
    REQUIRE(snapshotCount(response) == 2);

    // The snapshot is the read operation's own output, not a second projection
    // of it: a client that polled tracks.list would have received this exactly.
    const auto* snapshots = response.result["snapshots"].getArray();
    const auto tracks = (*snapshots)[0];
    REQUIRE(tracks["topic"].toString() == "tracks");
    REQUIRE(tracks["type"].toString() == "snapshot");
    REQUIRE(tracks["payload"].getArray()->size() == 1);
    REQUIRE(tracks["payload"][0]["name"].toString() == "Drums");

    // Snapshots ride in the reply rather than arriving as events, so there is
    // no window in which a client is subscribed and has no state.
    REQUIRE(recorder.events.empty());
}

TEST_CASE("The continuous topics have no snapshot to deliver", "[remote][subscriptions][meters]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    const auto response = subscribe(hub, client, {"meters", "playhead"});

    REQUIRE(response.ok);
    // A meter reading is a sample, not state: there is no "current value since
    // revision N" to hand over, only the next one along.
    REQUIRE(snapshotCount(response) == 0);
    REQUIRE(topicsOf(response).size() == 2);
}

TEST_CASE("A committed change delivers only what changed", "[remote][subscriptions][delta]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));
    api.tracks_.tracks.push_back(makeTrack(2, "Bass"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});

    api.tracks_.tracks[1].name = "Sub Bass";
    api.tracks_.tracks.push_back(makeTrack(3, "Keys"));
    api.tracks_.tracks.erase(api.tracks_.tracks.begin());
    commit(service, Topic::Tracks);

    REQUIRE(recorder.events.size() == 1);
    const auto& event = recorder.events.front();
    REQUIRE(event.topic == Topic::Tracks);
    REQUIRE(event.type == SubscriptionEvent::Type::Delta);
    REQUIRE(event.revision == service.currentRevision());

    REQUIRE(event.payload["added"].getArray()->size() == 1);
    REQUIRE(event.payload["added"][0]["id"].toString() == "3");
    REQUIRE(event.payload["updated"].getArray()->size() == 1);
    REQUIRE(event.payload["updated"][0]["name"].toString() == "Sub Bass");
    // Removals carry the identity alone. Sending the whole departed object
    // would be sending state the client is about to throw away.
    REQUIRE(event.payload["removed"].getArray()->size() == 1);
    REQUIRE(static_cast<int>(event.payload["removed"][0]) == 1);
}

TEST_CASE("A topic with no element identity delivers its whole state",
          "[remote][subscriptions][delta]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.tempo = 120.0;

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"project"});

    api.project_.info.tempo = 140.0;
    commit(service, Topic::Project);

    REQUIRE(recorder.events.size() == 1);
    // `project` is one small object with nothing to diff, so the delta is the
    // object. The type still says delta: this is a change, not a subscription.
    REQUIRE(recorder.events[0].type == SubscriptionEvent::Type::Delta);
    REQUIRE(static_cast<double>(recorder.events[0].payload["tempo"]) == 140.0);
}

TEST_CASE("A notification that changed nothing observable sends nothing",
          "[remote][subscriptions][coalescing]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});

    // The model says the topic changed, and by the projection's reckoning it did
    // not — a value that came back to where it started, or state this DTO does
    // not expose. The cheapest event is the one never sent.
    api.tracks_.tracks[0].name = "Temporary";
    api.tracks_.tracks[0].name = "Drums";
    commit(service, Topic::Tracks);

    REQUIRE(recorder.events.empty());
}

TEST_CASE("A burst of model notifications coalesces to one event",
          "[remote][subscriptions][coalescing]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});

    // The acceptance criterion: sustained 100 Hz control changes must not become
    // 100 callbacks per second per client. Marking is what a fader move does;
    // one flush is what a client sees.
    for (int i = 0; i < 100; ++i) {
        api.tracks_.tracks[0].volume = 0.5f + static_cast<float>(i) * 0.001f;
        service.noteModelChanged(Topic::Tracks);
    }
    service.changes().flush();

    REQUIRE(recorder.events.size() == 1);
    REQUIRE(recorder.events[0].revision == service.currentRevision());
}

TEST_CASE("Events arrive in revision order across topics", "[remote][subscriptions][ordering]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks", "project", "transport"});

    api.tracks_.tracks[0].name = "Kit";
    commit(service, Topic::Tracks);
    api.project_.info.tempo = 128.0;
    commit(service, Topic::Project);
    api.transport_.playing = true;
    commit(service, Topic::Transport);

    REQUIRE(recorder.events.size() == 3);
    REQUIRE(recorder.events[0].topic == Topic::Tracks);
    REQUIRE(recorder.events[1].topic == Topic::Project);
    REQUIRE(recorder.events[2].topic == Topic::Transport);
    for (std::size_t i = 1; i < recorder.events.size(); ++i)
        REQUIRE(recorder.events[i].revision > recorder.events[i - 1].revision);
}

TEST_CASE("A client that refuses an event is resynced rather than patched",
          "[remote][subscriptions][backpressure]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});

    // The client stops reading. Its delta is dropped, not queued: holding it
    // would be memory spent on a client that is not listening.
    recorder.accepting = false;
    api.tracks_.tracks[0].name = "Kit";
    commit(service, Topic::Tracks);
    REQUIRE(recorder.refused == 1);
    REQUIRE(recorder.events.empty());

    // It starts reading again. It has no baseline for the delta it missed, so
    // what it gets is complete state rather than the next increment.
    recorder.accepting = true;
    api.tracks_.tracks.push_back(makeTrack(2, "Bass"));
    commit(service, Topic::Tracks);

    REQUIRE(recorder.events.size() == 1);
    REQUIRE(recorder.events[0].type == SubscriptionEvent::Type::Snapshot);
    REQUIRE(recorder.events[0].payload.getArray()->size() == 2);

    // And it is back on deltas from there.
    recorder.clear();
    api.tracks_.tracks.push_back(makeTrack(3, "Keys"));
    commit(service, Topic::Tracks);
    REQUIRE(recorder.events.size() == 1);
    REQUIRE(recorder.events[0].type == SubscriptionEvent::Type::Delta);
}

TEST_CASE("A client that never resumes is disconnected", "[remote][subscriptions][backpressure]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub::Options options;
    options.droppedFlushesBeforeDisconnect = 3;
    SubscriptionHub hub(api, service, options);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});
    REQUIRE(hub.clientCount() == 1);

    recorder.accepting = false;
    for (int i = 0; i < 3; ++i) {
        api.tracks_.tracks[0].volume = 0.1f * static_cast<float>(i + 1);
        commit(service, Topic::Tracks);
    }

    // It is holding a connection, a thread, and a place in every fan-out while
    // consuming none of it.
    REQUIRE(recorder.disconnects == 1);
    REQUIRE(recorder.lastReason.isNotEmpty());
    REQUIRE(hub.clientCount() == 0);
}

TEST_CASE("A dropped sample is not a client falling behind",
          "[remote][subscriptions][backpressure][meters]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub::Options options;
    options.droppedFlushesBeforeDisconnect = 2;
    SubscriptionHub hub(api, service, options);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"playhead"});

    recorder.accepting = false;
    for (int i = 0; i < 20; ++i)
        hub.sampleNow();

    // Latest-value-wins means a discarded sample is the policy working. Counting
    // it against the client would disconnect every subscriber that briefly
    // stalled on a meter frame it did not need.
    REQUIRE(recorder.refused == 20);
    REQUIRE(recorder.disconnects == 0);
    REQUIRE(hub.clientCount() == 1);
}

TEST_CASE("Meters and the playhead are sampled from their own sources",
          "[remote][subscriptions][meters]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.transport_.positionBeats = 12.5;
    api.transport_.playing = true;

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    auto meters = std::make_unique<FakeMeters>();
    auto* metersRaw = meters.get();
    metersRaw->levels = {{1, 0.5f, 0.25f, false}, {MASTER_TRACK_ID, 0.9f, 0.9f, true}};
    hub.setMeterSource(std::move(meters));

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"meters", "playhead"});

    hub.sampleNow();

    const auto meterEvents = recorder.forTopic(Topic::Meters);
    REQUIRE(meterEvents.size() == 1);
    REQUIRE(meterEvents[0].type == SubscriptionEvent::Type::Sample);
    REQUIRE(meterEvents[0].payload["tracks"].getArray()->size() == 2);
    REQUIRE(static_cast<int>(meterEvents[0].payload["tracks"][1]["trackId"]) == MASTER_TRACK_ID);
    REQUIRE(static_cast<bool>(meterEvents[0].payload["tracks"][1]["clipped"]));

    const auto playhead = recorder.forTopic(Topic::Playhead);
    REQUIRE(playhead.size() == 1);
    REQUIRE(static_cast<double>(playhead[0].payload["positionBeats"]) == 12.5);
    REQUIRE(static_cast<bool>(playhead[0].payload["playing"]));
}

TEST_CASE("Sampling costs nothing when nobody is subscribed to it",
          "[remote][subscriptions][meters]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    auto meters = std::make_unique<FakeMeters>();
    auto* metersRaw = meters.get();
    hub.setMeterSource(std::move(meters));

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});

    hub.sampleNow();
    hub.sampleNow();

    // The meter source is never read on behalf of a subscriber that did not ask
    // for meters — the whole point of making them opt-in.
    REQUIRE(metersRaw->calls == 0);
    REQUIRE(recorder.events.empty());
}

TEST_CASE("A subscriber only hears about topics it asked for", "[remote][subscriptions]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder tracksOnly;
    Recorder projectOnly;
    SubscriptionHub hub(api, service);

    const auto a = hub.addClient(tracksOnly.sink(), tracksOnly.disconnect());
    const auto b = hub.addClient(projectOnly.sink(), projectOnly.disconnect());
    subscribe(hub, a, {"tracks"});
    subscribe(hub, b, {"project"});

    api.tracks_.tracks[0].name = "Kit";
    commit(service, Topic::Tracks);

    REQUIRE(tracksOnly.events.size() == 1);
    REQUIRE(projectOnly.events.empty());
}

TEST_CASE("Unsubscribing stops delivery", "[remote][subscriptions]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks", "project"});

    const auto narrowed = call(hub, client, "subscriptions.unsubscribe",
                               params({{"topics", stringArray({"tracks"})}}));
    REQUIRE(narrowed.ok);
    REQUIRE(topicsOf(narrowed) == std::vector<juce::String>{"project"});

    api.tracks_.tracks[0].name = "Kit";
    commit(service, Topic::Tracks);
    REQUIRE(recorder.events.empty());

    // No topics means all of them: the shape a client uses when it is going away
    // rather than narrowing what it watches.
    const auto all = call(hub, client, "subscriptions.unsubscribe", params({}));
    REQUIRE(topicsOf(all).empty());

    api.project_.info.tempo = 90.0;
    commit(service, Topic::Project);
    REQUIRE(recorder.events.empty());
}

TEST_CASE("subscriptions.list reports what this connection watches", "[remote][subscriptions]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    REQUIRE(topicsOf(call(hub, client, "subscriptions.list", params({}))).empty());

    subscribe(hub, client, {"clips", "tracks"});
    const auto listed = topicsOf(call(hub, client, "subscriptions.list", params({})));
    // Reported in topic order rather than subscription order, so a client can
    // compare two listings without sorting them first.
    REQUIRE(listed == std::vector<juce::String>{"tracks", "clips"});
}

TEST_CASE("Resync hands back complete state and clears the client's arrears",
          "[remote][subscriptions][resync]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub::Options options;
    options.droppedFlushesBeforeDisconnect = 3;
    SubscriptionHub hub(api, service, options);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks", "project"});

    recorder.accepting = false;
    api.tracks_.tracks[0].name = "Kit";
    commit(service, Topic::Tracks);
    api.tracks_.tracks[0].name = "Kit 2";
    commit(service, Topic::Tracks);
    recorder.accepting = true;

    const auto resync =
        call(hub, client, "subscriptions.resync", params({{"topics", stringArray({"tracks"})}}));
    REQUIRE(resync.ok);
    REQUIRE(snapshotCount(resync) == 1);
    const auto* snapshots = resync.result["snapshots"].getArray();
    REQUIRE((*snapshots)[0]["topic"].toString() == "tracks");
    REQUIRE((*snapshots)[0]["payload"][0]["name"].toString() == "Kit 2");

    // Two flushes were missed out of an allowance of three. Having been handed
    // complete state, what it missed no longer matters, so the count starts over
    // rather than carrying it to a disconnect.
    api.tracks_.tracks[0].volume = 0.2f;
    commit(service, Topic::Tracks);
    api.tracks_.tracks[0].volume = 0.3f;
    commit(service, Topic::Tracks);
    REQUIRE(recorder.disconnects == 0);
}

TEST_CASE("Resync with no topics covers everything subscribed", "[remote][subscriptions][resync]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks", "project", "meters"});

    const auto resync = call(hub, client, "subscriptions.resync", params({}));
    // Two, not three: `meters` is subscribed but has no state to resync to.
    REQUIRE(snapshotCount(resync) == 2);
}

TEST_CASE("Reconnecting at the current revision skips the snapshots",
          "[remote][subscriptions][resync]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    commit(service, Topic::Tracks);
    const auto revision = service.currentRevision();
    REQUIRE(revision > INITIAL_REVISION);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    const auto resumed = call(hub, client, "subscriptions.subscribe",
                              params({{"topics", stringArray({"tracks"})},
                                      {"fromRevision", static_cast<juce::int64>(revision)}}));
    REQUIRE(resumed.ok);
    // Nothing has been committed since the client last looked, so re-sending
    // every track would be a full transfer to say "no change".
    REQUIRE(snapshotCount(resumed) == 0);

    // And it is a live subscriber from there, on deltas.
    api.tracks_.tracks.push_back(makeTrack(2, "Bass"));
    commit(service, Topic::Tracks);
    REQUIRE(recorder.events.size() == 1);
    REQUIRE(recorder.events[0].type == SubscriptionEvent::Type::Delta);
    REQUIRE(recorder.events[0].payload["added"].getArray()->size() == 1);
}

TEST_CASE("Reconnecting at a revision that has moved on forces a full resync",
          "[remote][subscriptions][resync]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    commit(service, Topic::Tracks);
    const auto stale = service.currentRevision();
    commit(service, Topic::Tracks);
    REQUIRE(service.currentRevision() > stale);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    const auto resumed = call(hub, client, "subscriptions.subscribe",
                              params({{"topics", stringArray({"tracks"})},
                                      {"fromRevision", static_cast<juce::int64>(stale)}}));

    // No per-revision history is kept, so a client that is behind by any amount
    // cannot be caught up incrementally. Saying so explicitly beats replaying
    // what happens to still be in memory.
    REQUIRE(snapshotCount(resumed) == 1);
}

TEST_CASE("A second subscriber does not cost the first one its next delta",
          "[remote][subscriptions][resync]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder first;
    Recorder second;
    SubscriptionHub hub(api, service);

    const auto a = hub.addClient(first.sink(), first.disconnect());
    subscribe(hub, a, {"tracks"});

    // A change lands, and a new client subscribes before the flush that would
    // have told the first one about it. Moving the shared baseline forward for
    // the newcomer would swallow that change for the incumbent.
    api.tracks_.tracks.push_back(makeTrack(2, "Bass"));
    service.noteModelChanged(Topic::Tracks);

    const auto b = hub.addClient(second.sink(), second.disconnect());
    const auto joined = subscribe(hub, b, {"tracks"});
    REQUIRE(snapshotCount(joined) == 1);
    REQUIRE((*joined.result["snapshots"].getArray())[0]["payload"].getArray()->size() == 2);

    service.changes().flush();

    REQUIRE(first.events.size() == 1);
    REQUIRE(first.events[0].payload["added"].getArray()->size() == 1);
    // The newcomer sees a delta it already had. Added and updated are upserts
    // keyed by id, so applying one twice lands in the same place.
    REQUIRE(second.events.size() == 1);
}

TEST_CASE("Subscription input is validated against the declared schema",
          "[remote][subscriptions][errors]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());

    const auto unknown = call(hub, client, "subscriptions.subscribe",
                              params({{"topics", stringArray({"telemetry"})}}));
    REQUIRE_FALSE(unknown.ok);
    REQUIRE(unknown.error.code == ErrorCode::ValidationFailed);

    const auto empty = call(hub, client, "subscriptions.subscribe",
                            params({{"topics", juce::var(juce::Array<juce::var>{})}}));
    REQUIRE_FALSE(empty.ok);
    REQUIRE(empty.error.code == ErrorCode::ValidationFailed);

    const auto missing = call(hub, client, "subscriptions.subscribe", params({}));
    REQUIRE_FALSE(missing.ok);
    REQUIRE(missing.error.code == ErrorCode::ValidationFailed);

    const auto extra = call(hub, client, "subscriptions.subscribe",
                            params({{"topics", stringArray({"tracks"})}, {"forever", true}}));
    REQUIRE_FALSE(extra.ok);
    REQUIRE(extra.error.code == ErrorCode::ValidationFailed);
}

TEST_CASE("A method that is not a subscription method is not claimed",
          "[remote][subscriptions][errors]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    // Returning false without answering is the transport's signal to route it to
    // the dispatcher instead.
    int completions = 0;
    REQUIRE_FALSE(hub.handle(client, "tracks.list", params({}), [&](Response) { ++completions; }));
    REQUIRE(completions == 0);
    REQUIRE_FALSE(SubscriptionHub::isSubscriptionMethod("tracks.list"));
    REQUIRE(SubscriptionHub::isSubscriptionMethod("subscriptions.subscribe"));
}

TEST_CASE("A subscription method reaching the dispatcher is refused with a reason",
          "[remote][subscriptions][errors]") {
    MockMagdaApi api;
    RemoteApiService service(api);

    Response captured;
    int completions = 0;
    service.dispatch("subscriptions.subscribe", juce::var(new juce::DynamicObject()), {},
                     [&](Response response) {
                         captured = std::move(response);
                         ++completions;
                     });

    REQUIRE(completions == 1);
    REQUIRE_FALSE(captured.ok);
    // Not UnknownOperation: the operation is real and the request is well formed.
    // The only thing wrong is the transport it arrived on.
    REQUIRE(captured.error.code == ErrorCode::InvalidRequest);
    REQUIRE(captured.error.message.contains("subscriptions"));
}

TEST_CASE("The registry declares the subscription methods it cannot execute",
          "[remote][subscriptions][registry]") {
    const auto& registry = OperationRegistry::instance();
    for (const auto* name : {"subscriptions.subscribe", "subscriptions.unsubscribe",
                             "subscriptions.list", "subscriptions.resync"}) {
        INFO("operation: " << name);
        const auto* operation = registry.find(name);
        REQUIRE(operation != nullptr);
        // Declared here so both adapters share one contract; implemented in the
        // adapter because a connection is the one thing the registry has no
        // notion of.
        REQUIRE(operation->transportScoped);
        REQUIRE(operation->handler == nullptr);
        REQUIRE(operation->access == OperationAccess::Read);
    }

    const auto described = registry.describe();
    const auto* operations = described["operations"].getArray();
    REQUIRE(operations != nullptr);
    bool sawSubscribe = false;
    for (const auto& operation : *operations) {
        // Present on every entry rather than only where true: a client deciding
        // what it can call should read a field, not infer one from silence.
        REQUIRE(operation["transportScoped"].isBool());
        if (operation["name"].toString() == "subscriptions.subscribe")
            sawSubscribe = static_cast<bool>(operation["transportScoped"]);
    }
    REQUIRE(sawSubscribe);
}

TEST_CASE("Removing a client stops delivery and releases its topics",
          "[remote][subscriptions][lifetime]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});
    hub.removeClient(client);
    REQUIRE(hub.clientCount() == 0);

    api.tracks_.tracks[0].name = "Kit";
    commit(service, Topic::Tracks);
    REQUIRE(recorder.events.empty());

    // Rejoining gets a snapshot rather than a delta against a baseline that was
    // released with the last subscriber.
    Recorder rejoined;
    const auto next = hub.addClient(rejoined.sink(), rejoined.disconnect());
    REQUIRE(snapshotCount(subscribe(hub, next, {"tracks"})) == 1);
}

TEST_CASE("Shutdown stops delivery and survives a later flush",
          "[remote][subscriptions][lifetime]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});
    hub.shutdown();

    api.tracks_.tracks[0].name = "Kit";
    commit(service, Topic::Tracks);
    REQUIRE(recorder.events.empty());

    // Idempotent, and every method fails closed afterwards rather than
    // registering a subscriber nothing will ever feed.
    hub.shutdown();
    REQUIRE(hub.addClient(recorder.sink(), recorder.disconnect()) == 0);
    REQUIRE_FALSE(call(hub, client, "subscriptions.list", params({})).ok);
}

// ===========================================================================
// Load
// ===========================================================================

TEST_CASE("One flush projects each topic once, however many clients are watching",
          "[remote][subscriptions][load]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    for (TrackId id = 1; id <= 50; ++id)
        api.tracks_.tracks.push_back(makeTrack(id, "Track " + juce::String(id)));

    RemoteApiService service(api);
    SubscriptionHub hub(api, service);

    constexpr int kClients = 32;
    std::vector<std::unique_ptr<Recorder>> recorders;
    for (int i = 0; i < kClients; ++i) {
        recorders.push_back(std::make_unique<Recorder>());
        subscribe(hub, hub.addClient(recorders.back()->sink(), recorders.back()->disconnect()),
                  {"tracks"});
    }

    api.tracks_.getTracksCalls = 0;
    api.tracks_.tracks[0].name = "Renamed";
    commit(service, Topic::Tracks);

    // The projection is the expensive half — every track, every field — and it
    // is shared. Doing it per subscriber would make a busy project quadratic in
    // clients, which is the failure this design exists to avoid.
    REQUIRE(api.tracks_.getTracksCalls == 1);
    for (const auto& recorder : recorders) {
        REQUIRE(recorder->events.size() == 1);
        REQUIRE(recorder->events[0].type == SubscriptionEvent::Type::Delta);
    }
}

TEST_CASE("Sustained change is one event per client per flush", "[remote][subscriptions][load]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});

    // A hundred flushes of a fader that is moving at a hundred times that rate.
    // The acceptance criterion is about what reaches the client, and it is the
    // flush count that bounds it, not the notification count.
    constexpr int kFlushes = 100;
    constexpr int kMarksPerFlush = 100;
    for (int flush = 0; flush < kFlushes; ++flush) {
        for (int mark = 0; mark < kMarksPerFlush; ++mark) {
            api.tracks_.tracks[0].volume =
                static_cast<float>(flush * kMarksPerFlush + mark) * 0.0001f;
            service.noteModelActivity(Topic::Tracks);
        }
        service.changes().flush();
    }

    REQUIRE(recorder.events.size() == kFlushes);
    // Motion, not commits: `noteModelActivity` deliberately leaves the revision
    // alone, or optimistic concurrency would be unusable during playback.
    REQUIRE(service.currentRevision() == INITIAL_REVISION);
}

TEST_CASE("A client that has stopped reading does not hold up the others",
          "[remote][subscriptions][load][backpressure]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder stalled;
    Recorder healthy;
    SubscriptionHub::Options options;
    options.droppedFlushesBeforeDisconnect = 5;
    SubscriptionHub hub(api, service, options);

    subscribe(hub, hub.addClient(stalled.sink(), stalled.disconnect()), {"tracks"});
    subscribe(hub, hub.addClient(healthy.sink(), healthy.disconnect()), {"tracks"});

    stalled.accepting = false;
    for (int i = 0; i < 4; ++i) {
        api.tracks_.tracks[0].volume = 0.1f * static_cast<float>(i + 1);
        commit(service, Topic::Tracks);
    }

    // Nothing was queued for the stalled client and nothing was retried, so its
    // neighbour's stream is exactly what it would have been alone.
    REQUIRE(stalled.events.empty());
    REQUIRE(healthy.events.size() == 4);
    for (const auto& event : healthy.events)
        REQUIRE(event.type == SubscriptionEvent::Type::Delta);
    REQUIRE(hub.clientCount() == 2);
}

// ===========================================================================
// Recovery and invalidation
// ===========================================================================

TEST_CASE("The stalled client is the one disconnected, not whoever follows it",
          "[remote][subscriptions][backpressure]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder stalled;
    Recorder healthy;
    SubscriptionHub::Options options;
    options.droppedFlushesBeforeDisconnect = 2;
    SubscriptionHub hub(api, service, options);

    // Order matters. Removing the stalled client compacts the healthy one over
    // the top of it, so a disconnect callback read from the tail afterwards
    // belongs to a moved-from copy rather than to the client that stalled.
    subscribe(hub, hub.addClient(stalled.sink(), stalled.disconnect()), {"tracks"});
    subscribe(hub, hub.addClient(healthy.sink(), healthy.disconnect()), {"tracks"});

    stalled.accepting = false;
    for (int i = 0; i < 2; ++i) {
        api.tracks_.tracks[0].volume = 0.1f * static_cast<float>(i + 1);
        commit(service, Topic::Tracks);
    }

    // The one that stopped consuming is told to go, and it is the only one:
    // dropping it while leaving its socket open would strand a thread and a
    // connection slot for the life of the process.
    REQUIRE(stalled.disconnects == 1);
    REQUIRE(healthy.disconnects == 0);
    REQUIRE(hub.clientCount() == 1);
    REQUIRE_FALSE(healthy.events.empty());
}

TEST_CASE("A refused event is followed by a snapshot even if nothing else changes",
          "[remote][subscriptions][backpressure][resync]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder recorder;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(recorder.sink(), recorder.disconnect());
    subscribe(hub, client, {"tracks"});

    recorder.accepting = false;
    api.tracks_.tracks[0].name = "Kit";
    commit(service, Topic::Tracks);
    REQUIRE(recorder.refused == 1);

    // The project goes quiet. Nothing will ever change this topic again, so a
    // recovery that waited for the next observable change would wait forever —
    // the client would be silently stale until it thought to ask for a resync.
    recorder.accepting = true;
    service.changes().flush();

    REQUIRE(recorder.events.size() == 1);
    REQUIRE(recorder.events[0].type == SubscriptionEvent::Type::Snapshot);
    REQUIRE(recorder.events[0].payload[0]["name"].toString() == "Kit");

    // And the retry stops once the debt is paid, rather than republishing on
    // every flush from then on.
    recorder.clear();
    service.changes().flush();
    REQUIRE(recorder.events.empty());
}

TEST_CASE("Opening a different project reaches subscribers of every topic",
          "[remote][subscriptions][resync]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Old project"));

    RemoteApiService service(api);
    Recorder tracksOnly;
    SubscriptionHub hub(api, service);

    // Deliberately not subscribed to `project`. A client watching one topic
    // would otherwise keep showing the outgoing project's contents until
    // something happened to change that same topic in the new one.
    const auto client = hub.addClient(tracksOnly.sink(), tracksOnly.disconnect());
    subscribe(hub, client, {"tracks"});

    api.tracks_.tracks.clear();
    api.tracks_.tracks.push_back(makeTrack(9, "New project"));
    service.projectReplaced();
    service.changes().flush();

    REQUIRE(tracksOnly.events.size() == 1);
    const auto& payload = tracksOnly.events[0].payload;
    REQUIRE(payload["removed"].getArray()->size() == 1);
    REQUIRE(static_cast<int>(payload["removed"][0]) == 1);
    REQUIRE(payload["added"].getArray()->size() == 1);
    REQUIRE(payload["added"][0]["name"].toString() == "New project");
}

TEST_CASE("A session subscriber hears about a clip that changes the grid",
          "[remote][subscriptions][delta]") {
    MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.tracks_.tracks.push_back(makeTrack(1, "Drums"));

    RemoteApiService service(api);
    Recorder sessionOnly;
    SubscriptionHub hub(api, service);

    const auto client = hub.addClient(sessionOnly.sink(), sessionOnly.disconnect());
    subscribe(hub, client, {"session"});

    // `session.get` projects its slots out of the clips rather than storing them
    // beside them, so a clip operation that marked only `clips` would leave this
    // subscriber stale for as long as the session grid went untouched.
    ClipInfo clip;
    clip.id = 50;
    clip.trackId = 1;
    clip.name = "Loop";
    clip.setMidiContent();
    clip.view = ClipView::Session;
    clip.sceneIndex = 0;
    api.clips_.clips.emplace(clip.id, clip);
    api.clips_.clipsOnTrack[1] = {clip.id};

    for (const auto topic : {Topic::Clips, Topic::Session})
        service.noteModelChanged(topic);
    service.changes().flush();

    REQUIRE(sessionOnly.events.size() == 1);
    REQUIRE(sessionOnly.events[0].topic == Topic::Session);
    REQUIRE(sessionOnly.events[0].payload["added"].getArray()->size() == 1);
}
