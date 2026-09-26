#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"
#include "magda/daw/api/remote_engine_jobs.hpp"
#include "magda/daw/api/remote_file_handles.hpp"
#include "magda/daw/api/remote_service.hpp"

namespace {

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields = {}) {
    auto value = juce::var(new juce::DynamicObject());
    for (const auto& [name, field] : fields)
        value.getDynamicObject()->setProperty(name, field);
    return value;
}

RequestContext context(const juce::String& clientId = "engine-client") {
    RequestContext result;
    result.clientId = clientId;
    result.clientName = "engine-test";
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

juce::var renderInput(const juce::String& handle) {
    return object({
        {"destinationHandle", handle},
        {"range", object({{"unit", "beats"}, {"start", 0.0}, {"end", 4.0}})},
        {"format", "wav"},
        {"sampleRate", 48000},
        {"bitDepth", 24},
        {"dither", "tpdf"},
        {"normalise", false},
        {"normaliseToDb", 0.0},
        {"includeMasterEffects", true},
        {"includeTrackEffects", true},
        {"realTime", false},
        {"tailSeconds", 0.0},
        {"overwritePolicy", "fail"},
    });
}

juce::var captureInput(const juce::String& handle) {
    return object({{"destinationHandle", handle},
                   {"format", "wav"},
                   {"bitDepth", 24},
                   {"overwritePolicy", "fail"}});
}

juce::File destination(const juce::String& suffix) {
    auto file = juce::File::createTempFile(suffix);
    file.deleteFile();
    return file;
}

class MockEngineJobs final : public EngineJobSource {
  public:
    EngineJobStartStatus renderRange(OfflineRenderRequest request,
                                     std::shared_ptr<std::atomic_bool> cancelled,
                                     ProgressCallback onProgress,
                                     CompletionCallback onComplete) override {
        ++renderCalls;
        lastRender = std::move(request);
        renderCancelled = std::move(cancelled);
        if (renderStart != EngineJobStartStatus::Started)
            return renderStart;
        lastRender.destination.replaceWithText("rendered audio");
        if (onProgress)
            onProgress(0.5);
        if (deferRender)
            pendingRender = std::move(onComplete);
        else if (onComplete)
            onComplete(renderResult);
        return renderStart;
    }

    EngineJobStartStatus startMasterCapture(const juce::String& jobId,
                                            const juce::String& ownerClientId,
                                            const MasterCaptureRequest& request) override {
        ++captureStartCalls;
        if (captureStart != EngineJobStartStatus::Started)
            return captureStart;
        captureRequest = request;
        capture = {.supported = true,
                   .active = true,
                   .failed = false,
                   .jobId = jobId,
                   .ownerClientId = ownerClientId,
                   .format = request.format};
        return captureStart;
    }

    MasterCaptureResult stopMasterCapture(const juce::String& jobId) override {
        ++captureStopCalls;
        if (!capture.active || capture.jobId != jobId)
            return {false, "not active"};
        if (captureStopResult.success)
            captureRequest.destination.replaceWithText("captured audio");
        capture.active = false;
        return captureStopResult;
    }

    void cancelMasterCapture(const juce::String& jobId) override {
        if (capture.jobId == jobId) {
            ++captureCancelCalls;
            captureRequest.destination.deleteFile();
            capture.active = false;
        }
    }

    MasterCaptureSnapshot masterCaptureStatus() const override {
        return capture;
    }

    void shutdown() override {
        shutdownCalled = true;
    }

    EngineJobStartStatus renderStart = EngineJobStartStatus::Started;
    EngineJobResult renderResult{EngineJobResultStatus::Succeeded};
    bool deferRender = false;
    CompletionCallback pendingRender;
    OfflineRenderRequest lastRender;
    std::shared_ptr<std::atomic_bool> renderCancelled;
    int renderCalls = 0;

    EngineJobStartStatus captureStart = EngineJobStartStatus::Started;
    MasterCaptureResult captureStopResult{true, {}};
    MasterCaptureRequest captureRequest;
    MasterCaptureSnapshot capture{.supported = true};
    int captureStartCalls = 0;
    int captureStopCalls = 0;
    int captureCancelCalls = 0;
    bool shutdownCalled = false;
};

struct Fixture {
    Fixture() {
        service.setEngineJobSource(engineJobs);
    }

    juce::String approve(const juce::File& file, bool overwrite = false,
                         const juce::String& clientId = "engine-client") {
        return service.fileHandles().approve(
            clientId, "engine-test", RemoteFileCapability::AudioDestination, file, overwrite);
    }

    MockMagdaApi api;
    RemoteApiService service{api};
    std::shared_ptr<MockEngineJobs> engineJobs = std::make_shared<MockEngineJobs>();
};

}  // namespace

TEST_CASE("Engine file operations publish closed scoped contracts",
          "[remote-api][contract][2846]") {
    const auto& registry = OperationRegistry::instance();
    for (const auto* name : {"engine.renderRange", "engine.masterCapture.start",
                             "engine.masterCapture.stop", "engine.masterCapture.status"}) {
        const auto* operation = registry.find(name);
        REQUIRE(operation != nullptr);
        CHECK(operation->outputSchema.getDynamicObject() != nullptr);
    }
    CHECK(registry.find("engine.renderRange")->requiredScope == Scope::Edit);
    CHECK(registry.find("engine.masterCapture.start")->requiredScope == Scope::Edit);
    CHECK(registry.find("engine.masterCapture.stop")->requiredScope == Scope::Edit);
    CHECK(registry.find("engine.masterCapture.status")->requiredScope == Scope::Read);
    CHECK_FALSE(validateOperationInput(*registry.find("engine.renderRange"), renderInput("file_ok"))
                    .has_value());
    auto unsafe = renderInput("file_ok");
    unsafe.getDynamicObject()->setProperty("path", "/tmp/output.wav");
    CHECK(validateOperationInput(*registry.find("engine.renderRange"), unsafe).has_value());
}

TEST_CASE("Render-range jobs use approved audio destinations and publish no paths",
          "[remote][engine-jobs][2846]") {
    Fixture fixture;
    const auto output = destination(".wav");
    const auto handle = fixture.approve(output);

    const auto response = run(fixture.service, "engine.renderRange", renderInput(handle));
    REQUIRE(response.ok);
    CHECK(response.result["state"].toString() == "completed");
    CHECK(static_cast<double>(response.result["progress"]) == 1.0);
    CHECK(fixture.engineJobs->renderCalls == 1);
    CHECK(fixture.engineJobs->lastRender.blockSize == 512);
    CHECK(fixture.engineJobs->lastRender.range.start.value == 0.0);
    CHECK(fixture.engineJobs->lastRender.range.end.value == 4.0);
    CHECK(output.existsAsFile());
    CHECK(output.loadFileAsString() == "rendered audio");
    const auto wire = juce::JSON::toString(response.toEnvelope(), true);
    CHECK_FALSE(wire.contains(output.getParentDirectory().getFullPathName()));
    output.deleteFile();

    const auto wrongOwner = run(fixture.service, "engine.renderRange", renderInput(handle),
                                context("different-connection"));
    CHECK_FALSE(wrongOwner.ok);
    CHECK(wrongOwner.error.code == ErrorCode::NotFound);
}

TEST_CASE("Cancelled renders cannot leave a completed artifact", "[remote][engine-jobs][2846]") {
    Fixture fixture;
    fixture.engineJobs->deferRender = true;
    const auto output = destination(".wav");
    const auto handle = fixture.approve(output);
    const auto accepted = run(fixture.service, "engine.renderRange", renderInput(handle));
    REQUIRE(accepted.ok);
    REQUIRE(accepted.result["state"].toString() == "running");
    const auto jobId = accepted.result["id"].toString();

    const auto cancelled = run(fixture.service, "jobs.cancel", object({{"jobId", jobId}}));
    REQUIRE(cancelled.ok);
    CHECK(cancelled.result["state"].toString() == "cancelled");
    REQUIRE(fixture.engineJobs->renderCancelled != nullptr);
    CHECK(fixture.engineJobs->renderCancelled->load());

    REQUIRE(fixture.engineJobs->pendingRender != nullptr);
    fixture.engineJobs->pendingRender({EngineJobResultStatus::Succeeded});
    const auto terminal = run(fixture.service, "jobs.get", object({{"jobId", jobId}}));
    REQUIRE(terminal.ok);
    CHECK(terminal.result["state"].toString() == "cancelled");
    CHECK(terminal.result["artifacts"].size() == 0);
    CHECK_FALSE(output.existsAsFile());
}

TEST_CASE("Render-range validates authority, range, overwrite, and failure cleanup",
          "[remote][engine-jobs][2846]") {
    Fixture fixture;
    const auto output = destination(".wav");
    const auto handle = fixture.approve(output);

    auto invalidRange = renderInput(handle);
    invalidRange["range"].getDynamicObject()->setProperty("end", 0.0);
    const auto invalid = run(fixture.service, "engine.renderRange", invalidRange);
    CHECK_FALSE(invalid.ok);
    CHECK(invalid.error.code == ErrorCode::ValidationFailed);
    CHECK(fixture.engineJobs->renderCalls == 0);

    const auto wrongCapability = fixture.service.fileHandles().approve(
        "engine-client", "engine-test", RemoteFileCapability::ProjectDestination, output);
    const auto wrongHandle =
        run(fixture.service, "engine.renderRange", renderInput(wrongCapability));
    CHECK_FALSE(wrongHandle.ok);
    CHECK(fixture.engineJobs->renderCalls == 0);

    auto readOnly = context();
    readOnly.scopes = ScopeSet{Scope::Read};
    const auto denied = run(fixture.service, "engine.renderRange", renderInput(handle), readOnly);
    CHECK_FALSE(denied.ok);
    CHECK(denied.error.code == ErrorCode::PermissionDenied);
    CHECK(fixture.engineJobs->renderCalls == 0);

    REQUIRE(output.replaceWithText("keep me"));
    const auto unapprovedHandle = fixture.approve(output, false);
    auto replace = renderInput(unapprovedHandle);
    replace.getDynamicObject()->setProperty("overwritePolicy", "replace");
    const auto unapproved = run(fixture.service, "engine.renderRange", replace);
    CHECK_FALSE(unapproved.ok);
    CHECK(unapproved.error.code == ErrorCode::PermissionDenied);
    CHECK(output.loadFileAsString() == "keep me");
    output.deleteFile();

    fixture.engineJobs->renderResult = {EngineJobResultStatus::Failed};
    const auto failedOutput = destination(".wav");
    const auto failedHandle = fixture.approve(failedOutput);
    const auto failed = run(fixture.service, "engine.renderRange", renderInput(failedHandle));
    REQUIRE(failed.ok);
    CHECK(failed.result["state"].toString() == "failed");
    CHECK(failed.result["error"]["code"].toString() == "internal_error");
    CHECK(failed.result["artifacts"].size() == 0);
    CHECK_FALSE(failedOutput.existsAsFile());

    Fixture lateDestination;
    lateDestination.engineJobs->deferRender = true;
    const auto lateOutput = destination(".wav");
    const auto lateHandle = lateDestination.approve(lateOutput);
    const auto accepted =
        run(lateDestination.service, "engine.renderRange", renderInput(lateHandle));
    REQUIRE(accepted.ok);
    REQUIRE(lateOutput.replaceWithText("created while rendering"));
    REQUIRE(lateDestination.engineJobs->pendingRender != nullptr);
    lateDestination.engineJobs->pendingRender({EngineJobResultStatus::Succeeded});
    const auto lateJob = run(lateDestination.service, "jobs.get",
                             object({{"jobId", accepted.result["id"].toString()}}));
    REQUIRE(lateJob.ok);
    CHECK(lateJob.result["state"].toString() == "failed");
    CHECK(lateJob.result["error"]["code"].toString() == "conflict");
    CHECK(lateJob.result["artifacts"].size() == 0);
    CHECK(lateOutput.loadFileAsString() == "created while rendering");
    lateOutput.deleteFile();
}

TEST_CASE("A changed project invalidates a late render and removes its artifact",
          "[remote][engine-jobs][2846]") {
    Fixture fixture;
    fixture.engineJobs->deferRender = true;
    const auto output = destination(".wav");
    const auto handle = fixture.approve(output);
    const auto accepted = run(fixture.service, "engine.renderRange", renderInput(handle));
    REQUIRE(accepted.ok);
    const auto jobId = accepted.result["id"].toString();

    fixture.service.noteModelChanged(Topic::Tracks);
    REQUIRE(fixture.engineJobs->pendingRender != nullptr);
    fixture.engineJobs->pendingRender({EngineJobResultStatus::Succeeded});

    const auto terminal = run(fixture.service, "jobs.get", object({{"jobId", jobId}}));
    REQUIRE(terminal.ok);
    CHECK(terminal.result["state"].toString() == "failed");
    CHECK(terminal.result["error"]["code"].toString() == "conflict");
    CHECK(terminal.result["artifacts"].size() == 0);
    CHECK_FALSE(output.existsAsFile());
}

TEST_CASE("Master capture is an owned long-running job with explicit capability",
          "[remote][engine-jobs][2846]") {
    Fixture fixture;
    const auto output = destination(".wav");
    const auto handle = fixture.approve(output);
    const auto started = run(fixture.service, "engine.masterCapture.start", captureInput(handle));
    REQUIRE(started.ok);
    CHECK(started.result["state"].toString() == "running");
    const auto jobId = started.result["id"].toString();

    const auto status = run(fixture.service, "engine.masterCapture.status", object());
    REQUIRE(status.ok);
    CHECK(static_cast<bool>(status.result["supported"]));
    CHECK(static_cast<bool>(status.result["active"]));
    CHECK(status.result["jobId"].toString() == jobId);
    const auto otherStatus = run(fixture.service, "engine.masterCapture.status", object(),
                                 context("different-connection"));
    REQUIRE(otherStatus.ok);
    CHECK(otherStatus.result["jobId"].isVoid());

    const auto stopped =
        run(fixture.service, "engine.masterCapture.stop", object({{"jobId", jobId}}));
    REQUIRE(stopped.ok);
    CHECK(stopped.result["state"].toString() == "completed");
    CHECK(stopped.result["artifacts"].size() == 1);
    CHECK(output.existsAsFile());
    output.deleteFile();

    Fixture unsupported;
    unsupported.engineJobs->captureStart = EngineJobStartStatus::Unsupported;
    const auto unsupportedOutput = destination(".wav");
    const auto unsupportedHandle = unsupported.approve(unsupportedOutput);
    const auto rejected =
        run(unsupported.service, "engine.masterCapture.start", captureInput(unsupportedHandle));
    REQUIRE(rejected.ok);
    CHECK(rejected.result["state"].toString() == "unsupported");
    CHECK_FALSE(unsupportedOutput.existsAsFile());
}

TEST_CASE("Master capture cancellation, failure, and revision conflict publish no artifact",
          "[remote][engine-jobs][2846]") {
    SECTION("owner cancellation") {
        Fixture fixture;
        const auto output = destination(".wav");
        const auto handle = fixture.approve(output);
        const auto started =
            run(fixture.service, "engine.masterCapture.start", captureInput(handle));
        REQUIRE(started.ok);
        const auto jobId = started.result["id"].toString();

        const auto cancelled = run(fixture.service, "jobs.cancel", object({{"jobId", jobId}}));
        REQUIRE(cancelled.ok);
        CHECK(cancelled.result["state"].toString() == "cancelled");
        CHECK(cancelled.result["artifacts"].size() == 0);
        CHECK(fixture.engineJobs->captureCancelCalls == 1);
        CHECK_FALSE(output.existsAsFile());
    }

    SECTION("writer failure") {
        Fixture fixture;
        fixture.engineJobs->captureStopResult = {false, "plugin detail must stay private"};
        const auto output = destination(".wav");
        const auto handle = fixture.approve(output);
        const auto started =
            run(fixture.service, "engine.masterCapture.start", captureInput(handle));
        REQUIRE(started.ok);
        const auto stopped = run(fixture.service, "engine.masterCapture.stop",
                                 object({{"jobId", started.result["id"].toString()}}));
        REQUIRE(stopped.ok);
        CHECK(stopped.result["state"].toString() == "failed");
        CHECK(stopped.result["artifacts"].size() == 0);
        CHECK_FALSE(juce::JSON::toString(stopped.toEnvelope(), true).contains("plugin detail"));
        CHECK_FALSE(output.existsAsFile());
    }

    SECTION("project revision changes") {
        Fixture fixture;
        const auto output = destination(".wav");
        const auto handle = fixture.approve(output);
        const auto started =
            run(fixture.service, "engine.masterCapture.start", captureInput(handle));
        REQUIRE(started.ok);
        const auto jobId = started.result["id"].toString();
        fixture.service.noteModelChanged(Topic::Tracks);

        const auto stopped =
            run(fixture.service, "engine.masterCapture.stop", object({{"jobId", jobId}}));
        REQUIRE(stopped.ok);
        CHECK(stopped.result["state"].toString() == "failed");
        CHECK(stopped.result["error"]["code"].toString() == "conflict");
        CHECK(stopped.result["artifacts"].size() == 0);
        CHECK(fixture.engineJobs->captureCancelCalls == 1);
        CHECK_FALSE(output.existsAsFile());
    }
}
