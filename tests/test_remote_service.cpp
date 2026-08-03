#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_service.hpp"

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

namespace {

/// Reads MagdaApi live state, which asserts the message thread. The Catch2
/// runner has no MessageManager, so suspend that assertion for the file — the
/// same accommodation the existing projection tests make.
struct MessageThreadRelaxation {
    ScopedMessageThreadAssertionDisabler disabler;
};

juce::var emptyInput() {
    return juce::var(new juce::DynamicObject());
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

/// Runs one operation to completion and returns the response. Dispatch executes
/// inline when there is no message thread to hop to, so this is synchronous.
Response run(RemoteApiService& service, const juce::String& name, const juce::var& input,
             RequestContext context = {}) {
    Response captured;
    int completions = 0;
    service.dispatch(name, input, context, [&](Response response) {
        captured = std::move(response);
        ++completions;
    });
    REQUIRE(completions == 1);
    return captured;
}

juce::String errorCodeOf(const Response& response) {
    return toString(response.error.code);
}

}  // namespace

TEST_CASE("Every declared operation has a handler", "[remote][service][registry]") {
    // The property the handler-on-descriptor design exists to guarantee: before
    // it, the registry declared 36 operations and implemented none, and nothing
    // detected that.
    const auto& operations = OperationRegistry::instance().operations();
    REQUIRE_FALSE(operations.empty());
    for (const auto& operation : operations) {
        INFO("operation: " << operation.name);
        REQUIRE(operation.handler != nullptr);
    }
}

TEST_CASE("An unknown operation is rejected without touching the model",
          "[remote][service][errors]") {
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto response = run(service, "tracks.summonDragon", emptyInput());

    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "unknown_operation");
    REQUIRE(api.tracks_.created.empty());
    REQUIRE(service.currentRevision() == INITIAL_REVISION);
}

TEST_CASE("Schema-invalid input is rejected before execution", "[remote][service][errors]") {
    MockMagdaApi api;
    RemoteApiService service(api);

    // tempo is required and bounded to 20..400 by the operation's input schema.
    const auto missing = run(service, "project.setTempo", emptyInput());
    REQUIRE_FALSE(missing.ok);
    REQUIRE(errorCodeOf(missing) == "validation_failed");

    const auto outOfRange = run(service, "project.setTempo", object({{"tempo", 10000.0}}));
    REQUIRE_FALSE(outOfRange.ok);
    REQUIRE(errorCodeOf(outOfRange) == "validation_failed");

    // Nothing reached the facade, and no revision was burned on a bad request.
    REQUIRE(api.project_.info.tempo != 10000.0);
    REQUIRE(service.currentRevision() == INITIAL_REVISION);
}

TEST_CASE("A read executes and leaves the revision alone", "[remote][service]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.name = "Demo";
    api.project_.info.tempo = 128.0;
    RemoteApiService service(api);

    const auto response = run(service, "project.get", emptyInput());

    REQUIRE(response.ok);
    REQUIRE(response.result["name"].toString() == "Demo");
    REQUIRE(static_cast<double>(response.result["tempo"]) == 128.0);
    REQUIRE(response.revision == INITIAL_REVISION);
    // A read must not open an undo step.
    REQUIRE(api.undo_.compoundDescriptions.empty());
}

TEST_CASE("A committed write advances the revision by exactly one", "[remote][service]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto first = run(service, "project.setTempo", object({{"tempo", 90.0}}));
    REQUIRE(first.ok);
    REQUIRE(first.revision == INITIAL_REVISION + 1);
    REQUIRE(api.project_.info.tempo == 90.0);

    const auto second = run(service, "project.setTempo", object({{"tempo", 100.0}}));
    REQUIRE(second.revision == INITIAL_REVISION + 2);
    REQUIRE(service.currentRevision() == INITIAL_REVISION + 2);
}

TEST_CASE("A failed write does not advance the revision", "[remote][service]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    // Schema-valid but semantically wrong: no such track.
    const auto response = run(service, "tracks.delete", object({{"trackId", 999}}));

    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "not_found");
    REQUIRE(service.currentRevision() == INITIAL_REVISION);
    // Otherwise every rejected request would invalidate other clients'
    // expectedRevision for no reason.
    REQUIRE(response.revision == INITIAL_REVISION);
}

TEST_CASE("One mutating request is one named undo step", "[remote][service][undo]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto response =
        run(service, "tracks.create", object({{"name", "Bass"}, {"type", "audio"}}));

    REQUIRE(response.ok);
    REQUIRE(api.undo_.compoundDescriptions.size() == 1);
    REQUIRE(api.undo_.maxCompoundDepth == 1);
    // Balanced: a leaked compound would swallow the user's next edits.
    REQUIRE(api.undo_.compoundDepth == 0);
    // Named, so the user sees what the remote client did rather than "Undo".
    REQUIRE(api.undo_.compoundDescriptions[0].isNotEmpty());
}

TEST_CASE("A write that fails inside the handler still closes its undo step",
          "[remote][service][undo]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto response = run(service, "tracks.delete", object({{"trackId", 4242}}));

    REQUIRE_FALSE(response.ok);
    REQUIRE(api.undo_.compoundDepth == 0);
}

TEST_CASE("A stale expected revision is rejected as a conflict", "[remote][service][revisions]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    REQUIRE(run(service, "project.setTempo", object({{"tempo", 90.0}})).ok);

    RequestContext stale;
    stale.expectedRevision = INITIAL_REVISION;  // what the client saw before the write above
    const auto response = run(service, "project.setTempo", object({{"tempo", 140.0}}), stale);

    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "conflict");
    REQUIRE(api.project_.info.tempo == 90.0);

    // The matching revision succeeds, so a client can recover by re-reading.
    RequestContext current;
    current.expectedRevision = service.currentRevision();
    REQUIRE(run(service, "project.setTempo", object({{"tempo", 140.0}}), current).ok);
    REQUIRE(api.project_.info.tempo == 140.0);
}

TEST_CASE("Retrying a completed write does not apply it twice", "[remote][service][idempotency]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    RequestContext context;
    context.clientId = "client-a";
    context.requestId = "req-1";

    const auto first =
        run(service, "tracks.create", object({{"name", "Keys"}, {"type", "audio"}}), context);
    REQUIRE(first.ok);
    REQUIRE(api.tracks_.created.size() == 1);

    // The client never saw the response and retried with the same request id.
    const auto retry =
        run(service, "tracks.create", object({{"name", "Keys"}, {"type", "audio"}}), context);
    REQUIRE(retry.ok);
    REQUIRE(api.tracks_.created.size() == 1);
    // The replay is the original response, ids included, so the client's view
    // stays consistent.
    REQUIRE(static_cast<int>(retry.result["id"]) == static_cast<int>(first.result["id"]));
    REQUIRE(retry.revision == first.revision);
    REQUIRE(service.currentRevision() == first.revision);
}

TEST_CASE("Idempotency keys are scoped per client", "[remote][service][idempotency]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    RequestContext first;
    first.clientId = "client-a";
    first.requestId = "req-1";
    RequestContext second;
    second.clientId = "client-b";
    second.requestId = "req-1";  // same id, different client

    REQUIRE(run(service, "tracks.create", object({{"name", "A"}, {"type", "audio"}}), first).ok);
    REQUIRE(run(service, "tracks.create", object({{"name", "B"}, {"type", "audio"}}), second).ok);

    // Two distinct tracks: one client must never replay another's response.
    REQUIRE(api.tracks_.created.size() == 2);
}

TEST_CASE("Reads are not served from the idempotency cache", "[remote][service][idempotency]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.tempo = 120.0;
    RemoteApiService service(api);

    RequestContext context;
    context.clientId = "client-a";
    context.requestId = "req-read";

    const auto first = run(service, "project.get", emptyInput(), context);
    REQUIRE(static_cast<double>(first.result["tempo"]) == 120.0);

    api.project_.info.tempo = 145.0;

    // Caching a read would serve state that has since changed.
    const auto second = run(service, "project.get", emptyInput(), context);
    REQUIRE(static_cast<double>(second.result["tempo"]) == 145.0);
}

TEST_CASE("The idempotency cache is bounded", "[remote][service][idempotency]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    service.setIdempotencyCacheCapacity(2);

    const auto writeWithId = [&](const juce::String& requestId) {
        RequestContext context;
        context.clientId = "client-a";
        context.requestId = requestId;
        return run(service, "tracks.create", object({{"name", "T"}, {"type", "audio"}}), context);
    };

    writeWithId("r1");
    writeWithId("r2");
    writeWithId("r3");  // evicts r1
    REQUIRE(api.tracks_.created.size() == 3);

    // r1 has aged out, so its retry re-executes rather than replaying. That is
    // the accepted trade: a bounded cache cannot promise idempotency forever,
    // and unbounded growth is the worse failure for a long-lived session.
    writeWithId("r1");
    REQUIRE(api.tracks_.created.size() == 4);

    // r3 is still cached and still replays.
    writeWithId("r3");
    REQUIRE(api.tracks_.created.size() == 4);
}

TEST_CASE("An expired deadline fails with timeout instead of executing late",
          "[remote][service][errors]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    RequestContext context;
    context.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);

    const auto response = run(service, "project.setTempo", object({{"tempo", 90.0}}), context);

    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "timeout");
    REQUIRE(api.project_.info.tempo != 90.0);
}

TEST_CASE("A shut-down service accepts nothing", "[remote][service][lifecycle]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    service.shutdown();
    REQUIRE(service.isShutdown());

    const auto response = run(service, "project.setTempo", object({{"tempo", 90.0}}));
    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "cancelled");
    REQUIRE(api.project_.info.tempo != 90.0);
}

TEST_CASE("Shutdown is idempotent", "[remote][service][lifecycle]") {
    MockMagdaApi api;
    RemoteApiService service(api);

    service.shutdown();
    service.shutdown();
    REQUIRE(service.isShutdown());
}

TEST_CASE("Replacing the project invalidates outstanding revisions",
          "[remote][service][lifecycle]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto before = service.currentRevision();
    service.projectReplaced();
    REQUIRE(service.currentRevision() > before);

    // A client holding the pre-swap revision is now stale by construction,
    // rather than writing into a project that no longer exists.
    RequestContext context;
    context.expectedRevision = before;
    const auto response = run(service, "project.setTempo", object({{"tempo", 90.0}}), context);
    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "conflict");
}

TEST_CASE("Replacing the project clears the idempotency cache", "[remote][service][lifecycle]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    RequestContext context;
    context.clientId = "client-a";
    context.requestId = "req-1";
    REQUIRE(run(service, "tracks.create", object({{"name", "A"}, {"type", "audio"}}), context).ok);
    REQUIRE(api.tracks_.created.size() == 1);

    service.projectReplaced();

    // The cached response describes the old project's ids, so replaying it
    // would hand the client an id that means nothing now.
    run(service, "tracks.create", object({{"name", "A"}, {"type", "audio"}}), context);
    REQUIRE(api.tracks_.created.size() == 2);
}

TEST_CASE("Committed writes notify the matching change topic", "[remote][service][changes]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    std::vector<ChangeSource::Change> seen;
    service.changes().addListener([&](const std::vector<ChangeSource::Change>& changes) {
        seen.insert(seen.end(), changes.begin(), changes.end());
    });

    REQUIRE(run(service, "project.setTempo", object({{"tempo", 90.0}})).ok);
    service.changes().flush();

    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0].topic == Topic::Project);
    REQUIRE(seen[0].revision == service.currentRevision());
}

TEST_CASE("A cascading write notifies every affected topic", "[remote][service][changes]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    REQUIRE(run(service, "tracks.create", object({{"name", "A"}, {"type", "audio"}})).ok);

    std::vector<ChangeSource::Change> seen;
    service.changes().addListener([&](const std::vector<ChangeSource::Change>& changes) {
        seen.insert(seen.end(), changes.begin(), changes.end());
    });

    const auto trackId = api.tracks_.created.front().id;
    REQUIRE(run(service, "tracks.delete", object({{"trackId", trackId}})).ok);
    service.changes().flush();

    // Deleting a track takes its clips and devices with it, so a subscriber
    // watching only clips must still hear about it.
    std::vector<Topic> topics;
    for (const auto& change : seen)
        topics.push_back(change.topic);
    REQUIRE(std::find(topics.begin(), topics.end(), Topic::Tracks) != topics.end());
    REQUIRE(std::find(topics.begin(), topics.end(), Topic::Clips) != topics.end());
    REQUIRE(std::find(topics.begin(), topics.end(), Topic::Devices) != topics.end());
}

TEST_CASE("Reads emit no change notifications", "[remote][service][changes]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    std::vector<ChangeSource::Change> seen;
    service.changes().addListener([&](const std::vector<ChangeSource::Change>& changes) {
        seen.insert(seen.end(), changes.begin(), changes.end());
    });

    REQUIRE(run(service, "project.get", emptyInput()).ok);
    REQUIRE(run(service, "tracks.list", emptyInput()).ok);
    service.changes().flush();

    REQUIRE(seen.empty());
}

TEST_CASE("dispatchSync returns the response directly", "[remote][service]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.tempo = 111.0;
    RemoteApiService service(api);

    const auto response = service.dispatchSync("project.get", emptyInput(), {});

    REQUIRE(response.ok);
    REQUIRE(static_cast<double>(response.result["tempo"]) == 111.0);
}

TEST_CASE("A client can create a track, add a clip, and add notes to it",
          "[remote][service][integration]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    // The #701 acceptance path, exercised through the dispatcher rather than
    // against the facade directly.
    const auto track = run(service, "tracks.create", object({{"name", "Lead"}, {"type", "audio"}}));
    REQUIRE(track.ok);
    const auto trackId = static_cast<int>(track.result["id"]);

    const auto clip = run(service, "clips.createMidi",
                          object({{"trackId", trackId},
                                  {"startBeat", 0.0},
                                  {"lengthBeats", 4.0},
                                  {"view", "arrangement"}}));
    REQUIRE(clip.ok);
    REQUIRE(api.clips_.midiCreations.size() == 1);
    REQUIRE(api.clips_.midiCreations[0].trackId == trackId);
    REQUIRE(api.clips_.midiCreations[0].lengthBeats == 4.0);

    // MockClipApi records creations rather than materialising the clip, so seed
    // the one the handler just asked for before reading it back.
    const auto clipId = static_cast<ClipId>(static_cast<int>(clip.result["id"]));
    ClipInfo seeded;
    seeded.id = clipId;
    seeded.trackId = static_cast<TrackId>(trackId);  // ClipInfo defaults to MIDI content
    seeded.placement.startBeat = 0.0;
    seeded.placement.lengthBeats = 4.0;
    api.clips_.clips[clipId] = seeded;
    api.clips_.clipsOnTrack[static_cast<TrackId>(trackId)] = {clipId};

    const auto note = run(service, "clips.addMidiNote",
                          object({{"clipId", static_cast<int>(clipId)},
                                  {"note", 60},
                                  {"velocity", 100},
                                  {"startBeat", 0.0},
                                  {"lengthBeats", 1.0}}));
    REQUIRE(note.ok);
    REQUIRE(note.result["notes"].getArray() != nullptr);
    REQUIRE(note.result["notes"].getArray()->size() == 1);

    const auto listed = run(service, "clips.list", object({{"trackId", trackId}}));
    REQUIRE(listed.ok);
    REQUIRE(listed.result.getArray() != nullptr);
    REQUIRE(listed.result.getArray()->size() == 1);

    // Four mutations, four revisions, and one undo step each.
    REQUIRE(service.currentRevision() == 3);
    REQUIRE(api.undo_.compoundDescriptions.size() == 3);
    REQUIRE(api.undo_.compoundDepth == 0);
}

TEST_CASE("A local committed change bumps the revision", "[remote][service][changes]") {
    MockMagdaApi api;
    RemoteApiService service(api);

    std::vector<ChangeSource::Change> seen;
    service.changes().addListener([&](const std::vector<ChangeSource::Change>& changes) {
        seen.insert(seen.end(), changes.begin(), changes.end());
    });

    service.noteModelChanged(Topic::Tracks);
    service.changes().flush();

    REQUIRE(service.currentRevision() == INITIAL_REVISION + 1);
    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0].topic == Topic::Tracks);
}

TEST_CASE("Continuous motion notifies without bumping the revision", "[remote][service][changes]") {
    MockMagdaApi api;
    RemoteApiService service(api);

    std::vector<ChangeSource::Change> seen;
    service.changes().addListener([&](const std::vector<ChangeSource::Change>& changes) {
        seen.insert(seen.end(), changes.begin(), changes.end());
    });

    // A parameter following an LFO fires continuously while the transport
    // rolls. Bumping the revision for it would leave every client's
    // expectedRevision permanently stale during playback.
    for (int index = 0; index < 200; ++index)
        service.noteModelActivity(Topic::Devices);
    service.changes().flush();

    REQUIRE(service.currentRevision() == INITIAL_REVISION);
    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0].topic == Topic::Devices);

    // And a write with the pre-motion revision still succeeds, which is the
    // property that makes remote control usable during playback.
    RequestContext context;
    context.expectedRevision = INITIAL_REVISION;
    const ScopedMessageThreadAssertionDisabler disabler;
    REQUIRE(run(service, "project.setTempo", object({{"tempo", 90.0}}), context).ok);
}

TEST_CASE("A shut-down service records no further model changes", "[remote][service][lifecycle]") {
    MockMagdaApi api;
    RemoteApiService service(api);
    service.shutdown();

    std::vector<ChangeSource::Change> seen;
    service.changes().addListener([&](const std::vector<ChangeSource::Change>& changes) {
        seen.insert(seen.end(), changes.begin(), changes.end());
    });

    service.noteModelChanged(Topic::Tracks);
    service.noteModelActivity(Topic::Devices);
    service.changes().flush();

    REQUIRE(seen.empty());
    REQUIRE(service.currentRevision() == INITIAL_REVISION);
}

TEST_CASE("The response envelope carries the revision", "[remote][service]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto response = run(service, "project.setTempo", object({{"tempo", 90.0}}));
    const auto envelope = response.toEnvelope();

    REQUIRE(static_cast<bool>(envelope["ok"]));
    REQUIRE(static_cast<juce::int64>(envelope["revision"]) == 1);
    REQUIRE(envelope["apiVersion"].toString() == juce::String(API_VERSION.data()));

    const auto failed = run(service, "tracks.delete", object({{"trackId", 999}}));
    const auto failureEnvelope = failed.toEnvelope();
    REQUIRE_FALSE(static_cast<bool>(failureEnvelope["ok"]));
    REQUIRE(failureEnvelope["error"]["code"].toString() == "not_found");
    REQUIRE(static_cast<juce::int64>(failureEnvelope["revision"]) == 1);
}

TEST_CASE("Concurrent dispatch from many threads keeps revisions unique",
          "[remote][service][stress]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    constexpr int threadCount = 8;
    constexpr int perThread = 25;

    std::mutex revisionMutex;
    std::vector<Revision> revisions;
    std::atomic<int> failures{0};

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (int worker = 0; worker < threadCount; ++worker) {
        workers.emplace_back([&, worker] {
            for (int index = 0; index < perThread; ++index) {
                RequestContext context;
                context.clientId = "client-" + juce::String(worker);
                context.requestId = juce::String(index);
                service.dispatch("project.setTempo", object({{"tempo", 100.0}}), context,
                                 [&](Response response) {
                                     if (!response.ok) {
                                         ++failures;
                                         return;
                                     }
                                     const std::lock_guard<std::mutex> lock(revisionMutex);
                                     revisions.push_back(response.revision);
                                 });
            }
        });
    }
    for (auto& worker : workers)
        worker.join();

    REQUIRE(failures.load() == 0);
    REQUIRE(revisions.size() == static_cast<std::size_t>(threadCount * perThread));

    // Every committed write got its own revision — none reused, none skipped.
    std::sort(revisions.begin(), revisions.end());
    REQUIRE(std::adjacent_find(revisions.begin(), revisions.end()) == revisions.end());
    REQUIRE(revisions.front() == 1);
    REQUIRE(revisions.back() == static_cast<Revision>(threadCount * perThread));
    REQUIRE(service.currentRevision() == static_cast<Revision>(threadCount * perThread));
}

TEST_CASE("Concurrent change marks never lose the newest revision",
          "[remote][service][stress][changes]") {
    ChangeSource changes;
    std::vector<ChangeSource::Change> seen;
    changes.addListener([&](const std::vector<ChangeSource::Change>& batch) {
        seen.insert(seen.end(), batch.begin(), batch.end());
    });

    constexpr int threadCount = 4;
    constexpr Revision perThread = 500;

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (int worker = 0; worker < threadCount; ++worker) {
        workers.emplace_back([&changes, worker] {
            for (Revision revision = 1; revision <= perThread; ++revision)
                changes.markChanged(Topic::Meters,
                                    revision + static_cast<Revision>(worker) * perThread);
        });
    }
    for (auto& worker : workers)
        worker.join();

    changes.flush();

    REQUIRE(seen.size() == 1);
    REQUIRE(seen[0].topic == Topic::Meters);
    // Latest-value-wins has to hold under contention, not just single-threaded.
    REQUIRE(seen[0].revision == perThread * threadCount);
}
