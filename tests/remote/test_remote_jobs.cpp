#include <atomic>
#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_jobs.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/api/remote_subscriptions.hpp"

namespace {

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> properties = {}) {
    auto value = juce::var(new juce::DynamicObject());
    for (const auto& [name, property] : properties)
        value.getDynamicObject()->setProperty(name, property);
    return value;
}

RequestContext context(const juce::String& clientId, ScopeSet scopes = allScopes()) {
    RequestContext result;
    result.clientId = clientId;
    result.clientName = "job-test";
    result.transport = "test";
    result.scopes = scopes;
    return result;
}

Response run(RemoteApiService& service, const char* operation, juce::var input,
             RequestContext request) {
    Response response;
    service.dispatch(operation, input, request,
                     [&](Response completed) { response = std::move(completed); });
    return response;
}

RemoteJobSpec spec(const juce::String& owner, Revision revision = 0, Scope scope = Scope::Edit) {
    return {.kind = "project.open",
            .ownerClientId = owner,
            .requiredScope = scope,
            .acceptedRevision = revision};
}

}  // namespace

TEST_CASE("Remote jobs use one bounded legal state machine", "[remote][jobs][2834]") {
    RemoteJobManager jobs;
    const auto id = jobs.accept(spec("client-a", 7));
    REQUIRE(id.startsWith("job_"));
    REQUIRE(jobs.markRunning(id));
    REQUIRE(jobs.reportProgress(id, 0.4));
    CHECK_FALSE(jobs.reportProgress(id, 0.2));
    CHECK_FALSE(jobs.reportProgress(id, 1.1));

    Error error;
    CHECK_FALSE(jobs.get(id, "client-b", allScopes(), error).has_value());
    CHECK(error.code == ErrorCode::NotFound);
    CHECK_FALSE(jobs.get(id, "client-a", ScopeSet{Scope::Read}, error).has_value());
    CHECK(error.code == ErrorCode::PermissionDenied);

    REQUIRE(jobs.complete(id, object({{"opened", true}}), 7,
                          {{"artifact_opaque", "project-report", "application/json"}}));
    const auto completed = jobs.get(id, "client-a", allScopes(), error);
    REQUIRE(completed.has_value());
    CHECK(completed->state == RemoteJobState::Completed);
    CHECK(completed->progress == 1.0);
    CHECK(completed->completionRevision == 7);
    REQUIRE(completed->artifacts.size() == 1);
    CHECK(completed->artifacts.front().id.startsWith("artifact_"));
    CHECK_FALSE(jobs.complete(id, object(), 7));

    const auto* operation = OperationRegistry::instance().find("jobs.get");
    REQUIRE(operation != nullptr);
    const auto issues = validateJson(toJson(*completed), operation->outputSchema);
    juce::String diagnostics;
    for (const auto& issue : issues)
        diagnostics << issue.path << ": " << issue.code << " " << issue.message << "\n";
    INFO(diagnostics);
    CHECK(issues.empty());
}

TEST_CASE("Job operations publish closed shared schemas", "[remote-api][jobs][2834]") {
    const auto& registry = OperationRegistry::instance();
    const auto* list = registry.find("jobs.list");
    const auto* get = registry.find("jobs.get");
    const auto* cancel = registry.find("jobs.cancel");
    REQUIRE(list != nullptr);
    REQUIRE(get != nullptr);
    REQUIRE(cancel != nullptr);
    CHECK(list->access == OperationAccess::Read);
    CHECK(get->access == OperationAccess::Read);
    CHECK(cancel->access == OperationAccess::Control);
    CHECK(cancel->requiredScope == Scope::Read);
    CHECK_FALSE(validateOperationInput(*get, object({{"jobId", "job_safe"}})).has_value());
    CHECK(validateOperationInput(*get, object({{"jobId", "job_safe"}, {"path", "/tmp/x"}}))
              .has_value());
}

TEST_CASE("Cancellation is terminal and invokes engine cleanup once", "[remote][jobs][2834]") {
    RemoteJobManager jobs;
    std::atomic<int> cancellations{0};
    const auto id = jobs.accept(spec("owner"), [&] { ++cancellations; });
    REQUIRE(jobs.markRunning(id));

    Error error;
    REQUIRE(jobs.cancel(id, "owner", allScopes(), error));
    CHECK(cancellations.load() == 1);
    CHECK_FALSE(jobs.complete(id, object({{"late", true}}), 0));
    CHECK_FALSE(jobs.cancel(id, "owner", allScopes(), error));
    CHECK(error.code == ErrorCode::Conflict);

    const auto cancelled = jobs.get(id, "owner", allScopes(), error);
    REQUIRE(cancelled.has_value());
    CHECK(cancelled->state == RemoteJobState::Cancelled);
    CHECK(cancelled->cancelRequested);
    REQUIRE(cancelled->error.has_value());
    CHECK(cancelled->error->code == ErrorCode::Cancelled);
}

TEST_CASE("Stable jobs cannot publish success after the project moves",
          "[remote][jobs][revision][2834]") {
    RemoteJobManager jobs;
    const auto id = jobs.accept(spec("owner", 12));
    REQUIRE(jobs.markRunning(id));
    REQUIRE(
        jobs.complete(id, object({{"opened", true}}), 13, {{"artifact", "render", "audio/wav"}}));

    Error error;
    const auto failed = jobs.get(id, "owner", allScopes(), error);
    REQUIRE(failed.has_value());
    CHECK(failed->state == RemoteJobState::Failed);
    REQUIRE(failed->error.has_value());
    CHECK(failed->error->code == ErrorCode::Conflict);
    CHECK(failed->result.isVoid());
    CHECK(failed->artifacts.empty());

    const auto explicitlyFailed = jobs.accept(spec("owner", 12));
    REQUIRE(jobs.fail(explicitlyFailed, Error{ErrorCode::InternalError, "engine failed", {}}, 13));
    const auto engineFailure = jobs.get(explicitlyFailed, "owner", allScopes(), error);
    REQUIRE(engineFailure.has_value());
    REQUIRE(engineFailure->error.has_value());
    CHECK(engineFailure->error->code == ErrorCode::InternalError);
}

TEST_CASE("Terminal retention and disconnect cleanup are bounded", "[remote][jobs][2834]") {
    RemoteJobManager::Options options;
    options.maxRetainedTerminalJobs = 1;
    options.terminalRetention = std::chrono::hours(1);
    RemoteJobManager jobs(options);

    const auto first = jobs.accept(spec("owner", 0, Scope::Read));
    REQUIRE(jobs.complete(first, object(), 0));
    const auto second = jobs.accept(spec("owner", 0, Scope::Read));
    REQUIRE(jobs.complete(second, object(), 0));
    REQUIRE(jobs.list("owner", allScopes()).size() == 1);
    CHECK(jobs.list("owner", allScopes()).front().id == second);

    std::atomic<bool> cancelled{false};
    jobs.accept(spec("departing"), [&] { cancelled = true; });
    jobs.ownerDisconnected("departing");
    CHECK(cancelled.load());
    CHECK(jobs.list("departing", allScopes()).empty());
}

TEST_CASE("Job operations are owner scoped and revision neutral", "[remote][jobs][service][2834]") {
    ScopedMessageThreadAssertionDisabler relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    const auto id = service.jobs().accept(spec("client-a", service.currentRevision()));

    auto own = context("client-a");
    const auto listed = run(service, "jobs.list", object(), own);
    REQUIRE(listed.ok);
    REQUIRE(listed.result.getArray()->size() == 1);
    CHECK(listed.result[0]["id"].toString() == id);
    CHECK(listed.revision == INITIAL_REVISION);

    const auto hidden = run(service, "jobs.get", object({{"jobId", id}}), context("client-b"));
    CHECK_FALSE(hidden.ok);
    CHECK(hidden.error.code == ErrorCode::NotFound);

    own.requestId = "cancel-job";
    const auto cancelled = run(service, "jobs.cancel", object({{"jobId", id}}), own);
    REQUIRE(cancelled.ok);
    CHECK(cancelled.result["state"].toString() == "cancelled");
    CHECK(cancelled.revision == INITIAL_REVISION);
    CHECK(service.currentRevision() == INITIAL_REVISION);

    // Control calls participate in idempotency even though they do not edit the
    // project: retrying the cancellation returns the first success.
    const auto replay = run(service, "jobs.cancel", object({{"jobId", id}}), own);
    REQUIRE(replay.ok);
    CHECK(replay.result["state"].toString() == "cancelled");
}

TEST_CASE("Job subscriptions project only the connection owner's jobs",
          "[remote][jobs][subscriptions][2834]") {
    ScopedMessageThreadAssertionDisabler relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    SubscriptionHub hub(api, service);

    service.jobs().accept(spec("owner-a", 0, Scope::Read));
    service.jobs().accept(spec("owner-b", 0, Scope::Read));

    std::vector<SubscriptionEvent> events;
    const auto subscriber = hub.addClient(
        [&](const SubscriptionEvent& event) {
            events.push_back(event);
            return true;
        },
        [](const juce::String&) {}, "owner-a", [] { return allScopes(); });

    juce::Array<juce::var> topics;
    topics.add("jobs");
    Response subscribed;
    REQUIRE(hub.handle(subscriber, "subscriptions.subscribe", object({{"topics", topics}}),
                       [&](Response response) { subscribed = std::move(response); }));
    REQUIRE(subscribed.ok);
    const auto* snapshots = subscribed.result["snapshots"].getArray();
    REQUIRE(snapshots != nullptr);
    REQUIRE(snapshots->size() == 1);
    const auto* payload = snapshots->getReference(0)["payload"].getArray();
    REQUIRE(payload != nullptr);
    REQUIRE(payload->size() == 1);
    CHECK(payload->getReference(0)["kind"].toString() == "project.open");

    const auto ownedId = payload->getReference(0)["id"].toString();
    REQUIRE(service.jobs().markRunning(ownedId));
    service.changes().flush();
    REQUIRE(events.size() == 1);
    CHECK(events.front().topic == Topic::Jobs);
    REQUIRE(events.front().payload.getArray() != nullptr);
    REQUIRE(events.front().payload.getArray()->size() == 1);
    CHECK(events.front().payload[0]["state"].toString() == "running");
}
