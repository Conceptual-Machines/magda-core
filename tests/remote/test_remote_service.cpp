#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "MockMagdaApi.hpp"
#include "RemoteTestScopes.hpp"
#include "magda/daw/api/remote_service.hpp"

using namespace magda;
using namespace magda::remote;
using magda::test::fullyGrantedContext;
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
             RequestContext context = fullyGrantedContext()) {
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
    //
    // Transport-scoped operations (#1857) are the one deliberate exception, and
    // the assertion is two-way rather than a carve-out: an operation with no
    // handler must say it is the transport's, and one that claims to be the
    // transport's must not also carry a handler here.
    const auto& operations = OperationRegistry::instance().operations();
    REQUIRE_FALSE(operations.empty());
    for (const auto& operation : operations) {
        INFO("operation: " << operation.name);
        REQUIRE((operation.handler != nullptr) == !operation.transportScoped);
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

TEST_CASE("One mutating request opens one named undo step", "[remote][service][undo]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    // Uses a write with no command behind it, so this stays a test of the
    // dispatcher's compound bracketing. That a command-backed write is actually
    // undoable is asserted against the real UndoManager in the live tests — a
    // stub cannot answer that question, since it does not run commands.
    const auto response = run(service, "project.setTempo", object({{"tempo", 90.0}}));

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

    auto stale = fullyGrantedContext();
    stale.expectedRevision = INITIAL_REVISION;  // what the client saw before the write above
    const auto response = run(service, "project.setTempo", object({{"tempo", 140.0}}), stale);

    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "conflict");
    REQUIRE(api.project_.info.tempo == 90.0);

    // The matching revision succeeds, so a client can recover by re-reading.
    auto current = fullyGrantedContext();
    current.expectedRevision = service.currentRevision();
    REQUIRE(run(service, "project.setTempo", object({{"tempo", 140.0}}), current).ok);
    REQUIRE(api.project_.info.tempo == 140.0);
}

TEST_CASE("Retrying a completed write does not apply it twice", "[remote][service][idempotency]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto context = fullyGrantedContext();
    context.clientId = "client-a";
    context.requestId = "req-1";

    const auto first = run(service, "project.setTempo", object({{"tempo", 90.0}}), context);
    REQUIRE(first.ok);
    REQUIRE(api.project_.info.tempo == 90.0);

    // The client never saw the response and retried the same request id. The
    // payload differs to make replay observable: if this executed rather than
    // replaying, the tempo would move.
    const auto retry = run(service, "project.setTempo", object({{"tempo", 140.0}}), context);
    REQUIRE(retry.ok);
    REQUIRE(api.project_.info.tempo == 90.0);
    // The replay is the original response verbatim, so the client's view stays
    // consistent.
    REQUIRE(static_cast<double>(retry.result["tempo"]) == 90.0);
    REQUIRE(retry.revision == first.revision);
    REQUIRE(service.currentRevision() == first.revision);
}

TEST_CASE("Idempotency keys are scoped per client", "[remote][service][idempotency]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto first = fullyGrantedContext();
    first.clientId = "client-a";
    first.requestId = "req-1";
    auto second = fullyGrantedContext();
    second.clientId = "client-b";
    second.requestId = "req-1";  // same id, different client

    REQUIRE(run(service, "project.setTempo", object({{"tempo", 90.0}}), first).ok);
    REQUIRE(run(service, "project.setTempo", object({{"tempo", 140.0}}), second).ok);

    // Both executed: one client must never replay another's response.
    REQUIRE(api.project_.info.tempo == 140.0);
    REQUIRE(service.currentRevision() == INITIAL_REVISION + 2);
}

TEST_CASE("Reads are not served from the idempotency cache", "[remote][service][idempotency]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.tempo = 120.0;
    RemoteApiService service(api);

    auto context = fullyGrantedContext();
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

    const auto writeWithId = [&](const juce::String& requestId, double tempo) {
        auto context = fullyGrantedContext();
        context.clientId = "client-a";
        context.requestId = requestId;
        return run(service, "project.setTempo", object({{"tempo", tempo}}), context);
    };

    writeWithId("r1", 90.0);
    writeWithId("r2", 100.0);
    writeWithId("r3", 110.0);  // evicts r1
    REQUIRE(api.project_.info.tempo == 110.0);
    REQUIRE(service.currentRevision() == INITIAL_REVISION + 3);

    // r1 has aged out, so its retry re-executes rather than replaying. That is
    // the accepted trade: a bounded cache cannot promise idempotency forever,
    // and unbounded growth is the worse failure for a long-lived session.
    writeWithId("r1", 120.0);
    REQUIRE(api.project_.info.tempo == 120.0);
    REQUIRE(service.currentRevision() == INITIAL_REVISION + 4);

    // r3 is still cached, so this replays instead of applying 130.
    writeWithId("r3", 130.0);
    REQUIRE(api.project_.info.tempo == 120.0);
    REQUIRE(service.currentRevision() == INITIAL_REVISION + 4);
}

TEST_CASE("Concurrent retries of one request id apply the mutation once",
          "[remote][service][idempotency][stress]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    // The pre-queue cache lookup cannot catch this on its own: both requests can
    // pass it before either has finished. The recheck inside the serialized
    // execution path is what makes the duplicate replay instead of re-applying.
    constexpr int threadCount = 8;
    std::atomic<int> okCount{0};
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (int worker = 0; worker < threadCount; ++worker) {
        workers.emplace_back([&] {
            auto context = fullyGrantedContext();
            context.clientId = "client-a";
            context.requestId = "req-1";
            service.dispatch("project.setTempo", object({{"tempo", 90.0}}), context,
                             [&](Response response) {
                                 if (response.ok)
                                     ++okCount;
                             });
        });
    }
    for (auto& worker : workers)
        worker.join();

    // Every caller gets a success, but only one of them was a real mutation.
    REQUIRE(okCount.load() == threadCount);
    REQUIRE(service.currentRevision() == INITIAL_REVISION + 1);
}

TEST_CASE("Selection ids are checked against the model", "[remote][service][selection]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto selection = [](std::initializer_list<std::pair<const char*, juce::var>> overrides) {
        auto* result = new juce::DynamicObject();
        result->setProperty("trackId", juce::var());
        result->setProperty("clipId", juce::var());
        result->setProperty("clipIds", juce::Array<juce::var>{});
        result->setProperty("automationLaneId", juce::var());
        result->setProperty("automationClipId", juce::var());
        result->setProperty("noteClipId", juce::var());
        result->setProperty("noteIndices", juce::Array<juce::var>{});
        for (const auto& [key, value] : overrides)
            result->setProperty(key, value);
        return juce::var(result);
    };

    // SelectionManager accepts ids without checking they exist, so an
    // unvalidated request would leave the session pointing at nothing and still
    // report success.
    const auto badTrack = run(service, "selection.set", selection({{"trackId", 999}}));
    REQUIRE_FALSE(badTrack.ok);
    REQUIRE(errorCodeOf(badTrack) == "not_found");

    const auto badClip = run(service, "selection.set", selection({{"clipId", 777}}));
    REQUIRE_FALSE(badClip.ok);
    REQUIRE(errorCodeOf(badClip) == "not_found");

    // Nothing was applied on the way to the error.
    REQUIRE(api.selection_.trackSelections.empty());
    REQUIRE(api.selection_.clipSelections.empty());
    REQUIRE(service.currentRevision() == INITIAL_REVISION);
}

TEST_CASE("An empty selection clears the whole selection", "[remote][service][selection]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto* empty = new juce::DynamicObject();
    empty->setProperty("trackId", juce::var());
    empty->setProperty("clipId", juce::var());
    empty->setProperty("clipIds", juce::Array<juce::var>{});
    empty->setProperty("automationLaneId", juce::var());
    empty->setProperty("automationClipId", juce::var());
    empty->setProperty("noteClipId", juce::var());
    empty->setProperty("noteIndices", juce::Array<juce::var>{});

    const auto response = run(service, "selection.set", juce::var(empty));

    REQUIRE(response.ok);
    // clearNoteSelection alone is a no-op outside note mode, so it cannot
    // express "select nothing" for a track or clip selection.
    REQUIRE(api.selection_.clearSelectionCalls == 1);
}

TEST_CASE("An expired deadline fails with timeout instead of executing late",
          "[remote][service][errors]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto context = fullyGrantedContext();
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
    auto context = fullyGrantedContext();
    context.expectedRevision = before;
    const auto response = run(service, "project.setTempo", object({{"tempo", 90.0}}), context);
    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "conflict");
}

TEST_CASE("Replacing the project clears the idempotency cache", "[remote][service][lifecycle]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto context = fullyGrantedContext();
    context.clientId = "client-a";
    context.requestId = "req-1";
    REQUIRE(run(service, "project.setTempo", object({{"tempo", 90.0}}), context).ok);
    REQUIRE(api.project_.info.tempo == 90.0);

    service.projectReplaced();

    // The cached response describes the outgoing project, so replaying it would
    // answer for state that no longer exists. This must execute instead.
    REQUIRE(run(service, "project.setTempo", object({{"tempo", 140.0}}), context).ok);
    REQUIRE(api.project_.info.tempo == 140.0);
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

    const auto response = service.dispatchSync("project.get", emptyInput(), fullyGrantedContext());

    REQUIRE(response.ok);
    REQUIRE(static_cast<double>(response.result["tempo"]) == 111.0);
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
    auto context = fullyGrantedContext();
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
                auto context = fullyGrantedContext();
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

namespace {

/// An external plugin with two parameters, only the first opted in to AI
/// control — the shape devices.setParameter's allowlist gate cares about.
DeviceInfo makeFilterDevice() {
    DeviceInfo device;
    device.id = 5;
    device.name = "Filter";
    device.format = PluginFormat::VST3;
    device.parameters.emplace_back(0, "Cutoff", "Hz", 20.0f, 20000.0f, 800.0f);
    device.parameters.emplace_back(1, "Drive", "%", 0.0f, 100.0f, 0.0f);
    device.parameters[0].currentValue = 800.0f;
    device.aiSoundDesignerParameters = {0};
    return device;
}

juce::var pathInput(const ChainNodePath& path) {
    return object({{"devicePath", toJson(makeDevicePathDto(path))}});
}

}  // namespace

TEST_CASE("devices.listParameters returns the device's parameters", "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    api.devices_.devices[path] = makeFilterDevice();
    RemoteApiService service(api);

    const auto response = run(service, "devices.listParameters", pathInput(path));

    REQUIRE(response.ok);
    const auto* items = response.result.getArray();
    REQUIRE(items != nullptr);
    REQUIRE(items->size() == 2);
    REQUIRE((*items)[0]["name"].toString() == "Cutoff");
    REQUIRE(static_cast<bool>((*items)[0]["aiAgentEnabled"]));
    REQUIRE_FALSE(static_cast<bool>((*items)[1]["aiAgentEnabled"]));
    // A read burns no revision.
    REQUIRE(service.currentRevision() == INITIAL_REVISION);

    const auto missing =
        run(service, "devices.listParameters", pathInput(ChainNodePath::topLevelDevice(1, 99)));
    REQUIRE_FALSE(missing.ok);
    REQUIRE(errorCodeOf(missing) == "not_found");
}

TEST_CASE("devices.setParameter writes an allowed parameter and echoes the model",
          "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    api.devices_.devices[path] = makeFilterDevice();
    RemoteApiService service(api);

    auto input = pathInput(path);
    input.getDynamicObject()->setProperty("parameterIndex", 0);
    input.getDynamicObject()->setProperty("value", 1200.0);
    const auto response = run(service, "devices.setParameter", input);

    REQUIRE(response.ok);
    REQUIRE(static_cast<double>(response.result["currentValue"]) == 1200.0);
    REQUIRE(api.devices_.parameterWrites.size() == 1);
    REQUIRE(std::get<2>(api.devices_.parameterWrites.front()) == 1200.0f);
}

TEST_CASE("devices.setParameter refuses a parameter the user did not enable",
          "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    api.devices_.devices[path] = makeFilterDevice();
    RemoteApiService service(api);

    // Drive exists but is not on the device's AI allowlist: the remote surface
    // must refuse rather than bypass the in-app safeguard.
    auto input = pathInput(path);
    input.getDynamicObject()->setProperty("parameterIndex", 1);
    input.getDynamicObject()->setProperty("value", 50.0);
    const auto response = run(service, "devices.setParameter", input);

    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "permission_denied");
    REQUIRE(api.devices_.parameterWrites.empty());
}

TEST_CASE("devices.setParameter rejects an out-of-range value rather than clamping",
          "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    api.devices_.devices[path] = makeFilterDevice();
    RemoteApiService service(api);

    auto input = pathInput(path);
    input.getDynamicObject()->setProperty("parameterIndex", 0);
    input.getDynamicObject()->setProperty("value", 99999.0);
    const auto response = run(service, "devices.setParameter", input);

    REQUIRE_FALSE(response.ok);
    REQUIRE(errorCodeOf(response) == "validation_failed");
    REQUIRE(api.devices_.parameterWrites.empty());
}

TEST_CASE("devices.openEditor opens the editor without burning a revision",
          "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    api.devices_.devices[path] = makeFilterDevice();
    RemoteApiService service(api);

    const auto response = run(service, "devices.openEditor", pathInput(path));

    REQUIRE(response.ok);
    REQUIRE(static_cast<bool>(response.result["accepted"]));
    REQUIRE(api.devices_.openedEditors.size() == 1);
    // A window is not project content; the revision must not move.
    REQUIRE(service.currentRevision() == INITIAL_REVISION);

    const auto missing =
        run(service, "devices.openEditor", pathInput(ChainNodePath::topLevelDevice(1, 99)));
    REQUIRE_FALSE(missing.ok);
    REQUIRE(errorCodeOf(missing) == "not_found");
}

TEST_CASE("devices.setParameterConfig updates the customization and echoes the parameters",
          "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    api.devices_.devices[path] = makeFilterDevice();
    RemoteApiService service(api);

    auto input = pathInput(path);
    juce::Array<juce::var> aiSelection;
    aiSelection.add(0);
    aiSelection.add(1);
    input.getDynamicObject()->setProperty("aiAgentParameters", aiSelection);
    input.getDynamicObject()->setProperty("aiPrompt", "Deep bass");
    const auto response = run(service, "devices.setParameterConfig", input);

    REQUIRE(response.ok);
    const auto* items = response.result.getArray();
    REQUIRE(items != nullptr);
    REQUIRE(items->size() == 2);
    // Both parameters are now agent-controllable, and the device holds the
    // update — the next setParameter on Drive must succeed.
    REQUIRE(static_cast<bool>((*items)[1]["aiAgentEnabled"]));
    const auto& device = api.devices_.devices.at(path);
    REQUIRE(device.aiSoundDesignerParameters == std::vector<int>{0, 1});
    REQUIRE(device.aiSoundDesignerPrompt == "Deep bass");
    REQUIRE(api.devices_.configUpdates.size() == 1);
}

TEST_CASE("devices.setParameterConfig refuses unknown indices and internal devices",
          "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    api.devices_.devices[path] = makeFilterDevice();
    const auto internalPath = ChainNodePath::topLevelDevice(1, 6);
    auto internalDevice = makeFilterDevice();
    internalDevice.format = PluginFormat::Internal;
    api.devices_.devices[internalPath] = internalDevice;
    RemoteApiService service(api);

    auto unknownIndex = pathInput(path);
    juce::Array<juce::var> selection;
    selection.add(7);
    unknownIndex.getDynamicObject()->setProperty("aiAgentParameters", selection);
    const auto badIndex = run(service, "devices.setParameterConfig", unknownIndex);
    REQUIRE_FALSE(badIndex.ok);
    REQUIRE(errorCodeOf(badIndex) == "validation_failed");

    const auto empty = run(service, "devices.setParameterConfig", pathInput(path));
    REQUIRE_FALSE(empty.ok);
    REQUIRE(errorCodeOf(empty) == "validation_failed");

    auto internalInput = pathInput(internalPath);
    internalInput.getDynamicObject()->setProperty("aiAgentParameters", juce::Array<juce::var>());
    const auto internalResponse = run(service, "devices.setParameterConfig", internalInput);
    REQUIRE_FALSE(internalResponse.ok);
    REQUIRE(errorCodeOf(internalResponse) == "validation_failed");

    REQUIRE(api.devices_.configUpdates.empty());
}

TEST_CASE("devices.setParameter converts display units to the model domain",
          "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    DeviceInfo device;
    device.id = 5;
    device.format = PluginFormat::VST3;
    ParameterInfo gain(0, "Gain", "dB", -24.0f, 24.0f, 0.0f);
    gain.teMinValue = 0.0f;
    gain.teMaxValue = 1.0f;
    gain.displayText = std::make_shared<ParameterInfo::DisplayTextProvider>();
    gain.currentValue = 0.5f;
    device.parameters.push_back(gain);
    device.aiSoundDesignerParameters = {0};
    api.devices_.devices[path] = device;
    RemoteApiService service(api);

    auto input = pathInput(path);
    input.getDynamicObject()->setProperty("parameterIndex", 0);
    input.getDynamicObject()->setProperty("value", 12.0);
    const auto response = run(service, "devices.setParameter", input);

    REQUIRE(response.ok);
    // The echo speaks display units; the model write is TE-native.
    REQUIRE(std::abs(static_cast<double>(response.result["currentValue"]) - 12.0) < 1e-4);
    REQUIRE(std::abs(static_cast<double>(response.result["normalizedValue"]) - 0.75) < 1e-6);
    REQUIRE(api.devices_.parameterWrites.size() == 1);
    REQUIRE(std::abs(std::get<2>(api.devices_.parameterWrites.front()) - 0.75f) < 1e-6f);
}

TEST_CASE("devices.add creates a device and returns its address", "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    TrackInfo track;
    track.id = 1;
    api.tracks_.tracks.push_back(track);
    api.devices_.catalog.push_back({"internal.filter", "Filter", "", "", "", PluginFormat::Internal,
                                    DeviceType::Effect, false});
    RemoteApiService service(api);

    const auto response =
        run(service, "devices.add", object({{"trackId", 1}, {"catalogId", "internal.filter"}}));

    REQUIRE(response.ok);
    const auto id = static_cast<int>(response.result["id"]);
    REQUIRE(id != INVALID_DEVICE_ID);
    // The returned address is the canonical top-level spelling.
    REQUIRE(static_cast<int>(response.result["devicePath"]["topLevelDeviceId"]) == id);
    REQUIRE(response.result["devicePath"]["section"].toString() == "fx");
    REQUIRE(api.devices_.added.size() == 1);
    REQUIRE(api.devices_.added.front().catalogId == "internal.filter");

    const auto unknown =
        run(service, "devices.add", object({{"trackId", 1}, {"catalogId", "no.such.device"}}));
    REQUIRE_FALSE(unknown.ok);
    REQUIRE(errorCodeOf(unknown) == "not_found");
}

TEST_CASE("devices.remove and devices.move act on resolvable paths only",
          "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    api.devices_.devices[path] = makeFilterDevice();
    RemoteApiService service(api);

    auto move = pathInput(path);
    move.getDynamicObject()->setProperty("toIndex", 2);
    const auto moved = run(service, "devices.move", move);
    REQUIRE(moved.ok);
    REQUIRE(api.devices_.moved.size() == 1);

    const auto removed = run(service, "devices.remove", pathInput(path));
    REQUIRE(removed.ok);
    REQUIRE(static_cast<bool>(removed.result["accepted"]));
    REQUIRE(api.devices_.removed.size() == 1);

    const auto missing =
        run(service, "devices.remove", pathInput(ChainNodePath::topLevelDevice(1, 99)));
    REQUIRE_FALSE(missing.ok);
    REQUIRE(errorCodeOf(missing) == "not_found");
}

TEST_CASE("devices.setParameterConfig applies detection overrides", "[remote][service][devices]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    const auto path = ChainNodePath::topLevelDevice(1, 5);
    api.devices_.devices[path] = makeFilterDevice();
    RemoteApiService service(api);

    auto* override_ = new juce::DynamicObject();
    override_->setProperty("index", 1);
    override_->setProperty("unit", "dB");
    override_->setProperty("scale", "logarithmic");
    override_->setProperty("minValue", 1.0);
    override_->setProperty("maxValue", 100.0);
    juce::Array<juce::var> overrides;
    overrides.add(juce::var(override_));
    auto input = pathInput(path);
    input.getDynamicObject()->setProperty("parameterOverrides", overrides);
    const auto response = run(service, "devices.setParameterConfig", input);

    REQUIRE(response.ok);
    const auto* items = response.result.getArray();
    REQUIRE(items != nullptr);
    // The echo speaks the new detection data immediately.
    REQUIRE((*items)[1]["unit"].toString() == "dB");
    REQUIRE((*items)[1]["scale"].toString() == "logarithmic");
    REQUIRE(static_cast<double>((*items)[1]["minValue"]) == 1.0);
    REQUIRE(static_cast<double>((*items)[1]["maxValue"]) == 100.0);
    REQUIRE(api.devices_.configUpdates.size() == 1);

    // An empty or inverted display range is refused before anything persists.
    auto* bad = new juce::DynamicObject();
    bad->setProperty("index", 0);
    bad->setProperty("minValue", 500.0);
    bad->setProperty("maxValue", 100.0);
    juce::Array<juce::var> badOverrides;
    badOverrides.add(juce::var(bad));
    auto badInput = pathInput(path);
    badInput.getDynamicObject()->setProperty("parameterOverrides", badOverrides);
    const auto refused = run(service, "devices.setParameterConfig", badInput);
    REQUIRE_FALSE(refused.ok);
    REQUIRE(errorCodeOf(refused) == "validation_failed");
    REQUIRE(api.devices_.configUpdates.size() == 1);
}
