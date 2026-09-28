#include "remote_jobs.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace magda::remote {
namespace {

juce::var makeObject() {
    return {new juce::DynamicObject()};
}

void set(const juce::var& object, const char* name, const juce::var& value) {
    object.getDynamicObject()->setProperty(name, value);
}

juce::var nullableRevision(const std::optional<Revision>& revision) {
    return revision ? juce::var(static_cast<juce::int64>(*revision)) : juce::var();
}

juce::var nullableTime(const std::optional<juce::int64>& time) {
    return time ? juce::var(*time) : juce::var();
}

}  // namespace

const char* toString(RemoteJobState state) {
    switch (state) {
        case RemoteJobState::Accepted:
            return "accepted";
        case RemoteJobState::Running:
            return "running";
        case RemoteJobState::Completed:
            return "completed";
        case RemoteJobState::Cancelled:
            return "cancelled";
        case RemoteJobState::Failed:
            return "failed";
        case RemoteJobState::Unsupported:
            return "unsupported";
    }
    return "failed";
}

const char* toString(JobRevisionPolicy policy) {
    return policy == JobRevisionPolicy::StableUntilCompletion ? "stable_until_completion"
                                                              : "check_at_start_only";
}

bool isTerminal(RemoteJobState state) {
    return state == RemoteJobState::Completed || state == RemoteJobState::Cancelled ||
           state == RemoteJobState::Failed || state == RemoteJobState::Unsupported;
}

juce::var toJson(const RemoteJobArtifact& artifact) {
    auto value = makeObject();
    set(value, "id", artifact.id);
    set(value, "kind", artifact.kind);
    set(value, "mediaType", artifact.mediaType);
    return value;
}

juce::var toJson(const RemoteJobDto& job) {
    auto value = makeObject();
    set(value, "id", job.id);
    set(value, "kind", job.kind);
    set(value, "state", juce::String(toString(job.state)));
    set(value, "progress", job.progress);
    set(value, "cancellable", job.cancellable && !isTerminal(job.state));
    set(value, "cancelRequested", job.cancelRequested);
    set(value, "projectBound", job.projectBound);
    set(value, "revisionPolicy", juce::String(toString(job.revisionPolicy)));
    set(value, "acceptedRevision", static_cast<juce::int64>(job.acceptedRevision));
    set(value, "completionRevision", nullableRevision(job.completionRevision));
    set(value, "createdAtMs", job.createdAtMs);
    set(value, "startedAtMs", nullableTime(job.startedAtMs));
    set(value, "finishedAtMs", nullableTime(job.finishedAtMs));
    set(value, "result", job.result);
    set(value, "error", job.error ? toJson(*job.error) : juce::var());
    juce::Array<juce::var> artifacts;
    for (const auto& artifact : job.artifacts)
        artifacts.add(toJson(artifact));
    set(value, "artifacts", artifacts);
    return value;
}

struct RemoteJobManager::Entry {
    RemoteJobDto dto;
    juce::String ownerClientId;
    Scope requiredScope = Scope::Read;
    CancelCallback cancel;
    std::chrono::steady_clock::time_point terminalAt;
};

namespace {

RemoteJobDto cloneDto(const RemoteJobDto& source) {
    auto copy = source;
    copy.result = source.result.clone();
    if (copy.error)
        copy.error->details = source.error->details.clone();
    return copy;
}

}  // namespace

RemoteJobManager::RemoteJobManager() : RemoteJobManager(Options{}) {}

RemoteJobManager::RemoteJobManager(Options options) : options_(options) {}

RemoteJobManager::~RemoteJobManager() {
    shutdown();
}

juce::int64 RemoteJobManager::nowMs() {
    return juce::Time::currentTimeMillis();
}

juce::String RemoteJobManager::makeId() {
    std::random_device entropy;
    juce::String id("job_");
    for (int i = 0; i < 4; ++i)
        id += juce::String::toHexString(static_cast<int>(entropy())).paddedLeft('0', 8);
    return id;
}

void RemoteJobManager::setChangeCallback(ChangeCallback callback) {
    const std::scoped_lock lock(mutex_);
    changed_ = std::move(callback);
}

void RemoteJobManager::notifyChanged() {
    ChangeCallback callback;
    {
        const std::scoped_lock lock(mutex_);
        callback = changed_;
    }
    if (callback)
        callback();
}

void RemoteJobManager::pruneLocked() {
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(entries_, [&](const Entry& entry) {
        return isTerminal(entry.dto.state) && options_.terminalRetention.count() >= 0 &&
               now - entry.terminalAt >= options_.terminalRetention;
    });

    std::size_t terminalCount = 0;
    for (const auto& entry : entries_)
        terminalCount += isTerminal(entry.dto.state) ? 1u : 0u;
    while (terminalCount > options_.maxRetainedTerminalJobs) {
        const auto oldest = std::ranges::find_if(
            entries_, [](const Entry& entry) { return isTerminal(entry.dto.state); });
        if (oldest == entries_.end())
            break;
        entries_.erase(oldest);
        --terminalCount;
    }
}

juce::String RemoteJobManager::accept(RemoteJobSpec spec, CancelCallback cancel) {
    if (spec.kind.isEmpty() || spec.kind.length() > 128 || spec.ownerClientId.isEmpty())
        return {};

    Entry entry;
    entry.dto.id = makeId();
    entry.dto.kind = std::move(spec.kind);
    entry.dto.cancellable = spec.cancellable;
    entry.dto.projectBound = spec.projectBound;
    entry.dto.revisionPolicy = spec.revisionPolicy;
    entry.dto.acceptedRevision = spec.acceptedRevision;
    entry.dto.createdAtMs = nowMs();
    entry.ownerClientId = std::move(spec.ownerClientId);
    entry.requiredScope = spec.requiredScope;
    entry.cancel = std::move(cancel);

    juce::String id;
    {
        const std::scoped_lock lock(mutex_);
        if (shutdown_)
            return {};
        pruneLocked();
        // A collision is fantastically unlikely, but an opaque id must still
        // never address two records if the entropy provider misbehaves.
        while (std::ranges::any_of(
            entries_, [&](const Entry& existing) { return existing.dto.id == entry.dto.id; }))
            entry.dto.id = makeId();
        id = entry.dto.id;
        entries_.push_back(std::move(entry));
    }
    notifyChanged();
    return id;
}

bool RemoteJobManager::markRunning(const juce::String& id) {
    bool changed = false;
    {
        const std::scoped_lock lock(mutex_);
        const auto found =
            std::ranges::find(entries_, id, [](const Entry& entry) { return entry.dto.id; });
        if (found == entries_.end() || found->dto.state != RemoteJobState::Accepted)
            return false;
        found->dto.state = RemoteJobState::Running;
        found->dto.startedAtMs = nowMs();
        changed = true;
    }
    if (changed)
        notifyChanged();
    return changed;
}

bool RemoteJobManager::reportProgress(const juce::String& id, double progress) {
    if (!std::isfinite(progress) || progress < 0.0 || progress > 1.0)
        return false;
    bool changed = false;
    {
        const std::scoped_lock lock(mutex_);
        const auto found =
            std::ranges::find(entries_, id, [](const Entry& entry) { return entry.dto.id; });
        if (found == entries_.end() || isTerminal(found->dto.state) ||
            progress < found->dto.progress)
            return false;
        changed = progress != found->dto.progress;
        found->dto.progress = progress;
    }
    if (changed)
        notifyChanged();
    return true;
}

bool RemoteJobManager::finish(const juce::String& id, RemoteJobState state, juce::var result,
                              std::optional<Error> error, Revision currentRevision,
                              std::vector<RemoteJobArtifact> artifacts) {
    bool changed = false;
    {
        const std::scoped_lock lock(mutex_);
        const auto found =
            std::ranges::find(entries_, id, [](const Entry& entry) { return entry.dto.id; });
        if (found == entries_.end() || isTerminal(found->dto.state))
            return false;

        if (state == RemoteJobState::Completed &&
            found->dto.revisionPolicy == JobRevisionPolicy::StableUntilCompletion &&
            currentRevision != found->dto.acceptedRevision) {
            state = RemoteJobState::Failed;
            result = juce::var();
            artifacts.clear();
            error = Error{
                ErrorCode::Conflict, "project revision changed while the job was running", {}};
        } else if (!result.isVoid() && result.getDynamicObject() == nullptr) {
            state = RemoteJobState::Failed;
            result = juce::var();
            artifacts.clear();
            error = Error{ErrorCode::InternalError, "job result must be a safe object", {}};
        } else if (!result.isVoid() &&
                   static_cast<std::size_t>(
                       juce::JSON::toString(result, false).getNumBytesAsUTF8()) >
                       options_.maxResultBytes) {
            state = RemoteJobState::Failed;
            result = juce::var();
            artifacts.clear();
            error = Error{ErrorCode::InternalError, "job result exceeded the safe size limit", {}};
        }

        if (state == RemoteJobState::Completed) {
            if (artifacts.size() > 64 ||
                std::ranges::any_of(artifacts, [](const RemoteJobArtifact& artifact) {
                    return artifact.kind.isEmpty() || artifact.kind.length() > 128 ||
                           artifact.mediaType.length() > 128;
                })) {
                state = RemoteJobState::Failed;
                result = juce::var();
                artifacts.clear();
                error = Error{ErrorCode::InternalError, "job artifacts violated safe limits", {}};
            } else {
                // Producers describe an artifact; the public handle is always
                // minted here so a path or engine id can never cross by mistake.
                for (auto& artifact : artifacts)
                    artifact.id = "artifact_" + makeId().substring(4);
            }
        }

        found->dto.state = state;
        found->dto.progress = state == RemoteJobState::Completed ? 1.0 : found->dto.progress;
        found->dto.cancellable = false;
        found->dto.completionRevision = currentRevision;
        found->dto.finishedAtMs = nowMs();
        found->dto.result = result.clone();
        found->dto.error = std::move(error);
        if (found->dto.error)
            found->dto.error->details = found->dto.error->details.clone();
        found->dto.artifacts = std::move(artifacts);
        found->cancel = {};
        found->terminalAt = std::chrono::steady_clock::now();
        pruneLocked();
        changed = true;
    }
    if (changed)
        notifyChanged();
    return changed;
}

bool RemoteJobManager::complete(const juce::String& id, juce::var result, Revision currentRevision,
                                std::vector<RemoteJobArtifact> artifacts) {
    return finish(id, RemoteJobState::Completed, std::move(result), std::nullopt, currentRevision,
                  std::move(artifacts));
}

bool RemoteJobManager::fail(const juce::String& id, Error error, Revision currentRevision) {
    return finish(id, RemoteJobState::Failed, {}, std::move(error), currentRevision, {});
}

bool RemoteJobManager::unsupported(const juce::String& id, const juce::String& message,
                                   Revision currentRevision) {
    return finish(id, RemoteJobState::Unsupported, {},
                  Error{ErrorCode::ValidationFailed, message, {}}, currentRevision, {});
}

std::optional<RemoteJobDto> RemoteJobManager::get(const juce::String& id,
                                                  const juce::String& ownerClientId,
                                                  ScopeSet scopes, Error& error) {
    const std::scoped_lock lock(mutex_);
    pruneLocked();
    const auto found =
        std::ranges::find(entries_, id, [](const Entry& entry) { return entry.dto.id; });
    if (found == entries_.end() || found->ownerClientId != ownerClientId) {
        error = Error{ErrorCode::NotFound, "job not found", {}};
        return std::nullopt;
    }
    if (!scopes.has(found->requiredScope)) {
        error = Error{ErrorCode::PermissionDenied,
                      "job requires " + scopeName(found->requiredScope) + " scope",
                      {}};
        return std::nullopt;
    }
    return cloneDto(found->dto);
}

std::vector<RemoteJobDto> RemoteJobManager::list(const juce::String& ownerClientId,
                                                 ScopeSet scopes) {
    const std::scoped_lock lock(mutex_);
    pruneLocked();
    std::vector<RemoteJobDto> result;
    for (const auto& entry : entries_)
        if (entry.ownerClientId == ownerClientId && scopes.has(entry.requiredScope))
            result.push_back(cloneDto(entry.dto));
    return result;
}

bool RemoteJobManager::cancel(const juce::String& id, const juce::String& ownerClientId,
                              ScopeSet scopes, Error& error) {
    CancelCallback callback;
    {
        const std::scoped_lock lock(mutex_);
        pruneLocked();
        const auto found =
            std::ranges::find(entries_, id, [](const Entry& entry) { return entry.dto.id; });
        if (found == entries_.end() || found->ownerClientId != ownerClientId) {
            error = Error{ErrorCode::NotFound, "job not found", {}};
            return false;
        }
        if (!scopes.has(found->requiredScope)) {
            error = Error{ErrorCode::PermissionDenied,
                          "job requires " + scopeName(found->requiredScope) + " scope",
                          {}};
            return false;
        }
        if (isTerminal(found->dto.state)) {
            error = Error{ErrorCode::Conflict, "job is already terminal", {}};
            return false;
        }
        if (!found->dto.cancellable) {
            error = Error{ErrorCode::Conflict, "job does not support cancellation", {}};
            return false;
        }

        found->dto.cancelRequested = true;
        found->dto.cancellable = false;
        found->dto.state = RemoteJobState::Cancelled;
        found->dto.finishedAtMs = nowMs();
        found->dto.error = Error{ErrorCode::Cancelled, "job cancelled", {}};
        found->terminalAt = std::chrono::steady_clock::now();
        callback = std::move(found->cancel);
        pruneLocked();
    }
    if (callback)
        callback();
    notifyChanged();
    return true;
}

void RemoteJobManager::ownerDisconnected(const juce::String& ownerClientId) {
    std::vector<CancelCallback> callbacks;
    bool changed = false;
    {
        const std::scoped_lock lock(mutex_);
        for (auto& entry : entries_) {
            if (entry.ownerClientId == ownerClientId && !isTerminal(entry.dto.state) &&
                entry.cancel)
                callbacks.push_back(std::move(entry.cancel));
        }
        changed = std::erase_if(entries_, [&](const Entry& entry) {
                      return entry.ownerClientId == ownerClientId;
                  }) > 0;
    }
    for (const auto& callback : callbacks)
        callback();
    if (changed)
        notifyChanged();
}

void RemoteJobManager::projectReplaced() {
    std::vector<CancelCallback> callbacks;
    bool changed = false;
    {
        const std::scoped_lock lock(mutex_);
        for (auto& entry : entries_) {
            if (entry.dto.projectBound && !isTerminal(entry.dto.state) && entry.cancel)
                callbacks.push_back(std::move(entry.cancel));
        }
        changed =
            std::erase_if(entries_, [](const Entry& entry) { return entry.dto.projectBound; }) > 0;
    }
    for (const auto& callback : callbacks)
        callback();
    if (changed)
        notifyChanged();
}

void RemoteJobManager::shutdown() {
    std::vector<CancelCallback> callbacks;
    bool changed = false;
    {
        const std::scoped_lock lock(mutex_);
        if (shutdown_)
            return;
        shutdown_ = true;
        for (auto& entry : entries_)
            if (!isTerminal(entry.dto.state) && entry.cancel)
                callbacks.push_back(std::move(entry.cancel));
        changed = !entries_.empty();
        entries_.clear();
    }
    for (const auto& callback : callbacks)
        callback();
    if (changed)
        notifyChanged();
}

}  // namespace magda::remote
