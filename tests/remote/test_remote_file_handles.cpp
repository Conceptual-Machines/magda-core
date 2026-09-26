#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_file_handles.hpp"
#include "magda/daw/api/remote_service.hpp"

namespace {

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

struct TempPath {
    explicit TempPath(const juce::String& suffix) : file(juce::File::createTempFile(suffix)) {}
    ~TempPath() {
        file.deleteRecursively();
    }
    juce::File file;
};

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields = {}) {
    auto value = juce::var(new juce::DynamicObject());
    for (const auto& [name, field] : fields)
        value.getDynamicObject()->setProperty(name, field);
    return value;
}

RequestContext context(const juce::String& clientId = "connection-a") {
    RequestContext result;
    result.clientId = clientId;
    result.clientName = "file-test";
    result.transport = "test";
    result.scopes = allScopes();
    return result;
}

Response run(RemoteApiService& service, const char* operation, juce::var input,
             RequestContext request = context()) {
    Response response;
    int completions = 0;
    service.dispatch(operation, input, request, [&](Response completed) {
        response = std::move(completed);
        ++completions;
    });
    REQUIRE(completions == 1);
    return response;
}

juce::var openInput(const juce::String& handle, const char* dirtyPolicy = "fail") {
    return object({{"sourceHandle", handle},
                   {"dirtyPolicy", dirtyPolicy},
                   {"autosavePolicy", "fail"},
                   {"missingMediaPolicy", "fail"},
                   {"unavailableDevicePolicy", "fail"}});
}

juce::var saveAsInput(const juce::String& handle, const char* overwritePolicy = "fail") {
    return object({{"destinationHandle", handle},
                   {"overwritePolicy", overwritePolicy},
                   {"mediaPolicy", "copy"}});
}

}  // namespace

TEST_CASE("Remote file handles are opaque owner-scoped capabilities",
          "[remote][file-handles][2847]") {
    TempPath source(".mgd");
    REQUIRE(source.file.replaceWithText("project"));
    RemoteFileHandleRegistry handles;

    const auto id = handles.approve("connection-a", "file-test",
                                    RemoteFileCapability::ProjectSource, source.file);
    REQUIRE(id.startsWith("file_"));
    const auto listed = handles.list("connection-a");
    REQUIRE(listed.size() == 1);
    CHECK(listed.front().name == source.file.getFileName());
    const auto wire = juce::JSON::toString(toJson(listed.front()), true);
    CHECK_FALSE(wire.contains(source.file.getParentDirectory().getFullPathName()));

    Error error;
    const auto resolved =
        handles.resolve(id, "connection-a", RemoteFileCapability::ProjectSource, error);
    REQUIRE(resolved.has_value());
    CHECK(resolved->file == source.file);

    CHECK_FALSE(handles.resolve(id, "connection-b", RemoteFileCapability::ProjectSource, error));
    CHECK(error.code == ErrorCode::NotFound);
    CHECK_FALSE(
        handles.resolve(id, "connection-a", RemoteFileCapability::ProjectDestination, error));
    CHECK(error.code == ErrorCode::PermissionDenied);

    handles.ownerDisconnected("connection-a");
    CHECK(handles.list("connection-a").empty());
}

TEST_CASE("Remote file handles expire and can be revoked by connection or client",
          "[remote][file-handles][2847]") {
    TempPath source(".mgd");
    RemoteFileHandleRegistry expired({.maxHandles = 4, .lifetime = std::chrono::milliseconds(-1)});
    const auto expiredId = expired.approve("connection-a", "client-a",
                                           RemoteFileCapability::ProjectSource, source.file);
    Error error;
    CHECK_FALSE(
        expired.resolve(expiredId, "connection-a", RemoteFileCapability::ProjectSource, error));
    CHECK(error.code == ErrorCode::NotFound);

    RemoteFileHandleRegistry handles;
    const auto first = handles.approve("connection-a", "client-a",
                                       RemoteFileCapability::ProjectSource, source.file);
    handles.approve("connection-b", "client-a", RemoteFileCapability::ProjectSource, source.file);
    handles.approve("connection-c", "client-c", RemoteFileCapability::ProjectSource, source.file);
    CHECK(handles.countForClient("client-a") == 2);
    REQUIRE(handles.revoke(first, "connection-a", error));
    CHECK(handles.countForClient("client-a") == 1);
    handles.revokeClient("client-a");
    CHECK(handles.countForClient("client-a") == 0);
    CHECK(handles.countForClient("client-c") == 1);
}

TEST_CASE("Project file operations publish closed explicit schemas",
          "[remote-api][contract][project-file][2847]") {
    const auto& registry = OperationRegistry::instance();
    const auto* list = registry.find("fileHandles.list");
    const auto* revoke = registry.find("fileHandles.revoke");
    const auto* open = registry.find("project.open");
    const auto* saveAs = registry.find("project.saveAs");
    REQUIRE(list != nullptr);
    REQUIRE(revoke != nullptr);
    REQUIRE(open != nullptr);
    REQUIRE(saveAs != nullptr);
    CHECK(list->access == OperationAccess::Read);
    CHECK(revoke->access == OperationAccess::Control);
    CHECK(revoke->requiredScope == Scope::Edit);
    CHECK(open->access == OperationAccess::Control);
    CHECK(open->requiredScope == Scope::Edit);
    CHECK(saveAs->access == OperationAccess::Control);
    CHECK(saveAs->requiredScope == Scope::Edit);

    CHECK_FALSE(validateOperationInput(*open, openInput("file_safe")).has_value());
    CHECK(validateOperationInput(*open, object({{"sourceHandle", "file_safe"},
                                                {"dirtyPolicy", "fail"},
                                                {"autosavePolicy", "fail"},
                                                {"missingMediaPolicy", "fail"},
                                                {"unavailableDevicePolicy", "fail"},
                                                {"path", "/tmp/project.mgd"}}))
              .has_value());
    CHECK(validateOperationInput(*open, object({{"sourceHandle", "file_safe"}})).has_value());
    CHECK_FALSE(validateOperationInput(*saveAs, saveAsInput("file_safe")).has_value());
    CHECK(validateOperationInput(*saveAs, object({{"destinationHandle", "file_safe"},
                                                  {"overwritePolicy", "fail"},
                                                  {"mediaPolicy", "copy"},
                                                  {"path", "/tmp/project.mgd"}}))
              .has_value());
}

TEST_CASE("Project open consumes a local capability and explicit policies",
          "[remote][service][project-file][2847]") {
    TempPath source(".mgd");
    REQUIRE(source.file.replaceWithText("project"));
    MockMagdaApi api;
    RemoteApiService service(api);
    const auto handle = service.fileHandles().approve(
        "connection-a", "file-test", RemoteFileCapability::ProjectSource, source.file);

    const auto response = run(service, "project.open", openInput(handle));
    REQUIRE(response.ok);
    CHECK(response.result["kind"].toString() == "project.open");
    CHECK(response.result["state"].toString() == "completed");
    CHECK(api.project_.openAsyncCalls == 1);
    CHECK(api.project_.lastOpenSource == source.file);
    CHECK_FALSE(api.project_.lastOpenOptions.discardUnsavedChanges);
    CHECK_FALSE(juce::JSON::toString(response.toEnvelope(), true)
                    .contains(source.file.getParentDirectory().getFullPathName()));

    const auto otherOwner =
        run(service, "project.open", openInput(handle), context("connection-b"));
    CHECK_FALSE(otherOwner.ok);
    CHECK(otherOwner.error.code == ErrorCode::NotFound);

    api.project_.dirty = true;
    const auto dirty = run(service, "project.open", openInput(handle));
    CHECK_FALSE(dirty.ok);
    CHECK(dirty.error.code == ErrorCode::Conflict);
    CHECK(api.project_.openAsyncCalls == 1);
}

TEST_CASE("Project file job cancellation is terminal and reaches the loader",
          "[remote][service][project-file][2847]") {
    TempPath source(".mgd");
    REQUIRE(source.file.replaceWithText("project"));
    MockMagdaApi api;
    api.project_.deferOpenAsync = true;
    RemoteApiService service(api);
    const auto handle = service.fileHandles().approve(
        "connection-a", "file-test", RemoteFileCapability::ProjectSource, source.file);

    const auto accepted = run(service, "project.open", openInput(handle));
    REQUIRE(accepted.ok);
    CHECK(accepted.result["state"].toString() == "running");
    const auto jobId = accepted.result["id"].toString();
    REQUIRE(api.project_.lastOpenOptions.cancelled != nullptr);
    CHECK_FALSE(api.project_.lastOpenOptions.cancelled->load());

    const auto cancelled = run(service, "jobs.cancel", object({{"jobId", jobId}}));
    REQUIRE(cancelled.ok);
    CHECK(cancelled.result["state"].toString() == "cancelled");
    CHECK(api.project_.lastOpenOptions.cancelled->load());

    REQUIRE(api.project_.pendingOpenCallback != nullptr);
    ProjectFileOperationResult lateSuccess;
    lateSuccess.status = ProjectFileOperationStatus::Succeeded;
    api.project_.pendingOpenCallback(lateSuccess);
    const auto afterLateCompletion = run(service, "jobs.get", object({{"jobId", jobId}}));
    REQUIRE(afterLateCompletion.ok);
    CHECK(afterLateCompletion.result["state"].toString() == "cancelled");
}

TEST_CASE("Project save-as enforces destination and overwrite capabilities",
          "[remote][service][project-file][2847]") {
    TempPath destination(".mgd");
    MockMagdaApi api;
    RemoteApiService service(api);
    const auto handle = service.fileHandles().approve(
        "connection-a", "file-test", RemoteFileCapability::ProjectDestination, destination.file);

    const auto saved = run(service, "project.saveAs", saveAsInput(handle));
    REQUIRE(saved.ok);
    CHECK(saved.result["state"].toString() == "completed");
    CHECK(api.project_.saveAsAsyncCalls == 1);
    CHECK(api.project_.lastSaveAsDestination == destination.file);

    REQUIRE(destination.file.replaceWithText("existing"));
    const auto denied = run(service, "project.saveAs", saveAsInput(handle, "replace"));
    CHECK_FALSE(denied.ok);
    CHECK(denied.error.code == ErrorCode::PermissionDenied);

    const auto overwriteHandle = service.fileHandles().approve(
        "connection-a", "file-test", RemoteFileCapability::ProjectDestination, destination.file,
        true);
    const auto overwritten =
        run(service, "project.saveAs", saveAsInput(overwriteHandle, "replace"));
    REQUIRE(overwritten.ok);
    CHECK(overwritten.result["state"].toString() == "completed");
}

TEST_CASE("Project save-as fails when the project changes before completion",
          "[remote][service][project-file][2847]") {
    TempPath destination(".mgd");
    MockMagdaApi api;
    api.project_.deferSaveAsAsync = true;
    RemoteApiService service(api);
    const auto handle = service.fileHandles().approve(
        "connection-a", "file-test", RemoteFileCapability::ProjectDestination, destination.file);

    const auto accepted = run(service, "project.saveAs", saveAsInput(handle));
    REQUIRE(accepted.ok);
    const auto jobId = accepted.result["id"].toString();
    service.noteModelChanged(Topic::Tracks);

    REQUIRE(api.project_.pendingSaveAsCallback != nullptr);
    ProjectFileOperationResult lateSuccess;
    lateSuccess.status = ProjectFileOperationStatus::Succeeded;
    api.project_.pendingSaveAsCallback(lateSuccess);
    const auto finished = run(service, "jobs.get", object({{"jobId", jobId}}));
    REQUIRE(finished.ok);
    CHECK(finished.result["state"].toString() == "failed");
    CHECK(finished.result["error"]["code"].toString() == "conflict");
}
