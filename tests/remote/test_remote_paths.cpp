#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/project/ProjectManager.hpp"

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

RequestContext context() {
    RequestContext result;
    result.clientId = "connection-a";
    result.clientName = "path-test";
    result.transport = "test";
    result.scopes = allScopes();
    return result;
}

Response run(RemoteApiService& service, const char* operation, juce::var input) {
    Response response;
    int completions = 0;
    service.dispatch(operation, input, context(), [&](Response completed) {
        response = std::move(completed);
        ++completions;
    });
    REQUIRE(completions == 1);
    return response;
}

juce::File canonical(const juce::File& file) {
    return juce::File(std::filesystem::weakly_canonical(
                          std::filesystem::path(file.getFullPathName().toStdString()))
                          .string());
}

juce::var openInput(const juce::String& path, const char* dirtyPolicy = "fail") {
    return object({{"path", path},
                   {"dirtyPolicy", dirtyPolicy},
                   {"autosavePolicy", "fail"},
                   {"missingMediaPolicy", "fail"},
                   {"unavailableDevicePolicy", "fail"}});
}

juce::var saveAsInput(const juce::String& path, const char* overwritePolicy = "fail") {
    return object({{"path", path}, {"overwritePolicy", overwritePolicy}, {"mediaPolicy", "copy"}});
}

}  // namespace

TEST_CASE("Project file operations publish path-based closed schemas",
          "[remote-api][contract][project-file][2886]") {
    const auto& registry = OperationRegistry::instance();
    CHECK(registry.find("fileHandles.list") == nullptr);
    CHECK(registry.find("fileHandles.revoke") == nullptr);

    const auto* open = registry.find("project.open");
    const auto* saveAs = registry.find("project.saveAs");
    REQUIRE(open != nullptr);
    REQUIRE(saveAs != nullptr);
    CHECK(open->access == OperationAccess::Control);
    CHECK(open->requiredScope == Scope::Edit);
    CHECK(saveAs->access == OperationAccess::Control);
    CHECK(saveAs->requiredScope == Scope::Edit);

    CHECK_FALSE(validateOperationInput(*open, openInput("/tmp/project.mgd")).has_value());
    CHECK(validateOperationInput(*open, object({{"sourceHandle", "file_old"},
                                                {"dirtyPolicy", "fail"},
                                                {"autosavePolicy", "fail"},
                                                {"missingMediaPolicy", "fail"},
                                                {"unavailableDevicePolicy", "fail"}}))
              .has_value());
    CHECK_FALSE(validateOperationInput(*saveAs, saveAsInput("/tmp/project.mgd")).has_value());
    CHECK(validateOperationInput(*saveAs, object({{"destinationHandle", "file_old"},
                                                  {"overwritePolicy", "fail"},
                                                  {"mediaPolicy", "copy"}}))
              .has_value());
}

TEST_CASE("Project paths must be absolute before JUCE files are built",
          "[remote][service][project-file][2886]") {
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto open = run(service, "project.open", openInput("relative/project.mgd"));
    CHECK_FALSE(open.ok);
    CHECK(open.error.code == ErrorCode::ValidationFailed);
    CHECK(api.project_.openAsyncCalls == 0);

    const auto save = run(service, "project.saveAs", saveAsInput("relative/project.mgd"));
    CHECK_FALSE(save.ok);
    CHECK(save.error.code == ErrorCode::ValidationFailed);
    CHECK(api.project_.saveAsAsyncCalls == 0);
}

TEST_CASE("Project open canonicalises its absolute source path",
          "[remote][service][project-file][2886]") {
    TempPath source(".mgd");
    REQUIRE(source.file.replaceWithText("project"));
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto separator = juce::File::getSeparatorString();
    const auto spelling = source.file.getParentDirectory().getFullPathName() + separator +
                          "unused" + separator + ".." + separator + source.file.getFileName();
    const auto response = run(service, "project.open", openInput(spelling));
    REQUIRE(response.ok);
    CHECK(response.result["kind"].toString() == "project.open");
    CHECK(response.result["state"].toString() == "completed");
    CHECK(api.project_.openAsyncCalls == 1);
    CHECK(api.project_.lastOpenSource == canonical(source.file));
    CHECK(response.result["result"]["project"]["path"].toString() ==
          canonical(source.file).getFullPathName());

    api.project_.dirty = true;
    const auto dirty = run(service, "project.open", openInput(source.file.getFullPathName()));
    CHECK_FALSE(dirty.ok);
    CHECK(dirty.error.code == ErrorCode::Conflict);
    CHECK(api.project_.openAsyncCalls == 1);
}

#if !JUCE_WINDOWS
TEST_CASE("Project open resolves symbolic links", "[remote][service][project-file][2886]") {
    TempPath source(".mgd");
    TempPath link(".mgd");
    link.file.deleteFile();
    REQUIRE(source.file.replaceWithText("project"));
    std::error_code ec;
    std::filesystem::create_symlink(source.file.getFullPathName().toStdString(),
                                    link.file.getFullPathName().toStdString(), ec);
    REQUIRE_FALSE(ec);

    MockMagdaApi api;
    RemoteApiService service(api);
    const auto response = run(service, "project.open", openInput(link.file.getFullPathName()));
    REQUIRE(response.ok);
    CHECK(api.project_.lastOpenSource == canonical(source.file));
    link.file.deleteFile();
}
#endif

TEST_CASE("Project file job cancellation is terminal and reaches the loader",
          "[remote][service][project-file][2886]") {
    TempPath source(".mgd");
    REQUIRE(source.file.replaceWithText("project"));
    MockMagdaApi api;
    api.project_.deferOpenAsync = true;
    RemoteApiService service(api);

    const auto accepted = run(service, "project.open", openInput(source.file.getFullPathName()));
    REQUIRE(accepted.ok);
    CHECK(accepted.result["state"].toString() == "running");
    const auto jobId = accepted.result["id"].toString();
    REQUIRE(api.project_.lastOpenOptions.cancelled != nullptr);

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

TEST_CASE("Project save-as returns its canonical wrapped target and enforces overwrite policy",
          "[remote][service][project-file][2886]") {
    TempPath requested(".mgd");
    requested.file.deleteFile();
    const auto target = canonical(ProjectManager::saveTargetFor(requested.file));
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto saved =
        run(service, "project.saveAs", saveAsInput(requested.file.getFullPathName()));
    REQUIRE(saved.ok);
    CHECK(saved.result["state"].toString() == "completed");
    CHECK(api.project_.lastSaveAsDestination == target);
    CHECK(saved.result["result"]["project"]["path"].toString() == target.getFullPathName());

    REQUIRE(target.getParentDirectory().createDirectory());
    REQUIRE(target.replaceWithText("existing"));
    const auto refused =
        run(service, "project.saveAs", saveAsInput(requested.file.getFullPathName(), "fail"));
    CHECK_FALSE(refused.ok);
    CHECK(refused.error.code == ErrorCode::Conflict);

    const auto replaced =
        run(service, "project.saveAs", saveAsInput(requested.file.getFullPathName(), "replace"));
    REQUIRE(replaced.ok);
    CHECK(replaced.result["state"].toString() == "completed");
    target.getParentDirectory().deleteRecursively();
}

TEST_CASE("Project save-as fails when the project changes before completion",
          "[remote][service][project-file][2886]") {
    TempPath destination(".mgd");
    destination.file.deleteFile();
    MockMagdaApi api;
    api.project_.deferSaveAsAsync = true;
    RemoteApiService service(api);

    const auto accepted =
        run(service, "project.saveAs", saveAsInput(destination.file.getFullPathName()));
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
