#pragma once

#include <juce_core/juce_core.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "remote_api.hpp"

namespace magda::remote {

/** State shared by every long-running remote operation. */
enum class RemoteJobState { Accepted, Running, Completed, Cancelled, Failed, Unsupported };

/**
 * Whether a project-bound job may finish after the project has changed.
 *
 * `StableUntilCompletion` is the safe default for lifecycle and render work:
 * completion is rejected if the project revision differs from the one captured
 * when the job was accepted. `CheckAtStartOnly` is for work that deliberately
 * operates on an immutable snapshot prepared at acceptance time.
 */
enum class JobRevisionPolicy { StableUntilCompletion, CheckAtStartOnly };

struct RemoteJobSpec {
    juce::String kind;
    juce::String ownerClientId;
    Scope requiredScope = Scope::Read;
    Revision acceptedRevision = INITIAL_REVISION;
    JobRevisionPolicy revisionPolicy = JobRevisionPolicy::StableUntilCompletion;
    bool projectBound = true;
    bool cancellable = true;
};

struct RemoteJobArtifact {
    juce::String id;
    juce::String kind;
    juce::String mediaType;

    bool operator==(const RemoteJobArtifact&) const = default;
};

struct RemoteJobDto {
    juce::String id;
    juce::String kind;
    RemoteJobState state = RemoteJobState::Accepted;
    double progress = 0.0;
    bool cancellable = true;
    bool cancelRequested = false;
    bool projectBound = true;
    JobRevisionPolicy revisionPolicy = JobRevisionPolicy::StableUntilCompletion;
    Revision acceptedRevision = INITIAL_REVISION;
    std::optional<Revision> completionRevision;
    juce::int64 createdAtMs = 0;
    std::optional<juce::int64> startedAtMs;
    std::optional<juce::int64> finishedAtMs;
    juce::var result;
    std::optional<Error> error;
    std::vector<RemoteJobArtifact> artifacts;
};

const char* toString(RemoteJobState state);
const char* toString(JobRevisionPolicy policy);
bool isTerminal(RemoteJobState state);
juce::var toJson(const RemoteJobArtifact& artifact);
juce::var toJson(const RemoteJobDto& job);

/**
 * @brief Bounded, owner-scoped state machine for remote asynchronous work.
 *
 * The manager deliberately does not choose a worker thread. Project loading,
 * offline rendering, and callback capture have different execution constraints;
 * their implementations accept a job here and then drive its state from the
 * appropriate engine. This class owns the part that must not vary between
 * them: opaque ids, legal transitions, progress, cancellation, revision policy,
 * safe results, retention, ownership, and change notification.
 */
class RemoteJobManager {
  public:
    using CancelCallback = std::function<void()>;
    using ChangeCallback = std::function<void()>;

    struct Options {
        std::size_t maxRetainedTerminalJobs = 128;
        std::chrono::milliseconds terminalRetention = std::chrono::minutes(10);
        std::size_t maxResultBytes = 64 * 1024;
    };

    RemoteJobManager();
    explicit RemoteJobManager(Options options);
    ~RemoteJobManager();

    RemoteJobManager(const RemoteJobManager&) = delete;
    RemoteJobManager& operator=(const RemoteJobManager&) = delete;

    /// Accept a job and return its unguessable public id. Empty on invalid spec.
    juce::String accept(RemoteJobSpec spec, CancelCallback cancel = {});
    bool markRunning(const juce::String& id);
    bool reportProgress(const juce::String& id, double progress);

    /**
     * Finish successfully. For `StableUntilCompletion`, a changed revision
     * turns the job into `failed/conflict` instead; the producer must call this
     * before publishing a project transition or final artifact.
     */
    bool complete(const juce::String& id, juce::var result, Revision currentRevision,
                  std::vector<RemoteJobArtifact> artifacts = {});
    bool fail(const juce::String& id, Error error, Revision currentRevision);
    bool unsupported(const juce::String& id, const juce::String& message, Revision currentRevision);

    std::optional<RemoteJobDto> get(const juce::String& id, const juce::String& ownerClientId,
                                    ScopeSet scopes, Error& error);
    std::vector<RemoteJobDto> list(const juce::String& ownerClientId, ScopeSet scopes);
    bool cancel(const juce::String& id, const juce::String& ownerClientId, ScopeSet scopes,
                Error& error);

    /// Cancel and forget everything owned by a transport identity that left.
    void ownerDisconnected(const juce::String& ownerClientId);
    /// Cancel every active job tied to the outgoing project.
    void projectReplaced();
    /// Cancel all active jobs, forget all records, and reject new work.
    void shutdown();

    void setChangeCallback(ChangeCallback callback);

  private:
    struct Entry;

    static juce::int64 nowMs();
    static juce::String makeId();
    void pruneLocked();
    void notifyChanged();
    bool finish(const juce::String& id, RemoteJobState state, juce::var result,
                std::optional<Error> error, Revision currentRevision,
                std::vector<RemoteJobArtifact> artifacts);

    const Options options_;
    std::mutex mutex_;
    std::vector<Entry> entries_;
    ChangeCallback changed_;
    bool shutdown_ = false;
};

}  // namespace magda::remote
