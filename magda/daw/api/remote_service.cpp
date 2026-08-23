#include "remote_service.hpp"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <chrono>

#include "magda_api.hpp"
#include "undo_api.hpp"

namespace magda::remote {
namespace {

/**
 * Which subscription topics a committed operation invalidates.
 *
 * Deliberately over-broad where a mutation cascades: deleting a track takes its
 * clips and devices with it, so a subscriber watching only `clips` still has to
 * hear about it. An extra notification costs one coalesced flush; a missing one
 * leaves a client silently stale, which is the failure mode #1857 has to resync
 * out of.
 */
std::vector<Topic> topicsFor(const juce::String& operationName) {
    if (operationName.startsWith("project."))
        return {Topic::Project};
    // Session slots are keyed by track and derived from clips, so a track
    // disappearing takes its column of the grid with it.
    if (operationName == "tracks.create" || operationName == "tracks.delete")
        return {Topic::Tracks, Topic::Clips, Topic::Devices, Topic::Session};
    if (operationName.startsWith("tracks."))
        return {Topic::Tracks};
    // `session.get` projects its slots out of the clips, so creating or deleting
    // one changes the session grid whether or not the request said "session".
    // Over-broad on the clip operations that cannot affect it — adding a note —
    // which costs nothing: an unchanged projection is coalesced away before any
    // event is built.
    if (operationName.startsWith("clips."))
        return {Topic::Clips, Topic::Session};
    if (operationName.startsWith("devices.") || operationName.startsWith("racks."))
        return {Topic::Devices};
    if (operationName.startsWith("selection."))
        return {Topic::Selection};
    if (operationName.startsWith("transport."))
        return {Topic::Transport};
    if (operationName.startsWith("session."))
        return {Topic::Session};
    if (operationName.startsWith("automation."))
        return {Topic::Automation};
    return {Topic::Project};
}

bool deadlinePassed(const RequestContext& context) {
    return context.deadline && std::chrono::steady_clock::now() > *context.deadline;
}

/// Publishes the executing thread for the duration of a handler and restores
/// the previous value, so nesting cannot leave a stale id behind.
class ScopedExecutingThread {
  public:
    explicit ScopedExecutingThread(std::atomic<std::thread::id>& slot)
        : slot_(slot), previous_(slot.exchange(std::this_thread::get_id())) {}
    ~ScopedExecutingThread() {
        slot_.store(previous_);
    }

    ScopedExecutingThread(const ScopedExecutingThread&) = delete;
    ScopedExecutingThread& operator=(const ScopedExecutingThread&) = delete;

  private:
    std::atomic<std::thread::id>& slot_;
    std::thread::id previous_;
};

/// Balances beginCompound/endCompound so one mutating request is one undo step
/// even on the early-return paths inside a handler.
class ScopedUndoStep {
  public:
    ScopedUndoStep(UndoApi& undo, const juce::String& description) : undo_(undo) {
        undo_.beginCompound(description);
    }
    ~ScopedUndoStep() {
        undo_.endCompound();
    }

    ScopedUndoStep(const ScopedUndoStep&) = delete;
    ScopedUndoStep& operator=(const ScopedUndoStep&) = delete;

  private:
    UndoApi& undo_;
};

}  // namespace

// ===========================================================================
// Response
// ===========================================================================

Response Response::success(juce::var result, Revision revision) {
    Response response;
    response.ok = true;
    response.result = std::move(result);
    response.revision = revision;
    return response;
}

Response Response::failure(Error error, Revision revision) {
    Response response;
    response.ok = false;
    response.error = std::move(error);
    response.revision = revision;
    return response;
}

Response Response::failure(ErrorCode code, const juce::String& message, Revision revision) {
    return failure(Error{code, message, {}}, revision);
}

juce::var Response::toEnvelope() const {
    auto envelope = ok ? successEnvelope(result) : errorEnvelope(error);
    if (auto* object = envelope.getDynamicObject())
        object->setProperty("revision", static_cast<juce::int64>(revision));
    return envelope;
}

// ===========================================================================
// RemoteApiService
// ===========================================================================

RemoteApiService::RemoteApiService(MagdaApi& api)
    : api_(api), state_(std::make_shared<ExecutionState>()) {
    state_->service = this;
}

RemoteApiService::~RemoteApiService() {
    shutdown();
}

std::shared_ptr<RemoteApiService::ExecutionState> RemoteApiService::currentState() const {
    const std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

void RemoteApiService::retireState() {
    std::shared_ptr<ExecutionState> retiring;
    {
        const std::lock_guard<std::mutex> lock(stateMutex_);
        retiring = std::move(state_);
        state_.reset();
    }
    if (!retiring)
        return;
    // Taking the execution lock here is the whole point: it cannot be acquired
    // while a job is inside execute(), so once this returns no job is running
    // and none can start.
    const std::lock_guard<std::mutex> lock(retiring->mutex);
    retiring->revisionAtRetirement = currentRevision();
    retiring->service = nullptr;
}

void RemoteApiService::dispatch(const juce::String& operationName, const juce::var& input,
                                const RequestContext& context, Callback onComplete) {
    jassert(onComplete != nullptr);
    const auto revision = currentRevision();

    // Every exit from here on is audited, so the record cannot be missing the
    // requests that failed — which are the ones anybody actually reads it for.
    // Wrapping rather than calling `record` at each `return` also keeps the
    // exactly-once callback promise: there is one place that completes.
    //
    // The log is captured as an owning `shared_ptr` and `this` is not captured
    // at all. This wrapper travels into work queued on the message thread and
    // runs *outside* the execution lock — deliberately, so a callback may
    // re-dispatch — which means it can outlive not just this service but the
    // whole host. A borrowed pointer would be dangling by then; ownership is
    // what makes the write safe rather than merely likely to be.
    onComplete = [log = auditLog(), operationName, context,
                  inner = std::move(onComplete)](Response response) mutable {
        recordAudit(log, operationName, context, response);
        inner(std::move(response));
    };

    if (shutdown_.load(std::memory_order_acquire)) {
        onComplete(
            Response::failure(ErrorCode::Cancelled, "remote API service is shut down", revision));
        return;
    }

    const auto* operation = OperationRegistry::instance().find(operationName);
    if (operation == nullptr) {
        onComplete(Response::failure(ErrorCode::UnknownOperation,
                                     "unknown operation: " + operationName, revision));
        return;
    }

    // A subscription method reached the dispatcher, which means the transport
    // that received it cannot push. Saying so beats UnknownOperation: the
    // operation is real and the client's request is well formed, and the only
    // thing wrong is where it arrived.
    if (operation->transportScoped) {
        onComplete(Response::failure(ErrorCode::InvalidRequest,
                                     operationName +
                                         " is handled by the transport, and this transport "
                                         "does not support subscriptions",
                                     revision));
        return;
    }

    // Permission before validation, and before the idempotency cache (#1860).
    //
    // Before validation because a client that may not call something has no
    // business learning the shape of its input — a refusal that varied with
    // whether the payload was well formed would be an oracle for the schema.
    //
    // Before the cache because a cached response is a previous *success*, and
    // replaying one to a client whose grant has since been revoked would be the
    // single case where revocation did not take effect immediately.
    if (!context.scopes.has(operation->requiredScope)) {
        onComplete(Response::failure(ErrorCode::PermissionDenied,
                                     operationName + " requires the '" +
                                         scopeName(operation->requiredScope) +
                                         "' permission, which this client has not been granted",
                                     revision));
        return;
    }

    // Validation happens here, on the caller's thread, precisely because it is
    // a pure function of the request: a malformed or oversized payload is
    // rejected without ever occupying the message thread.
    if (auto error = validateOperationInput(*operation, input)) {
        onComplete(Response::failure(*error, revision));
        return;
    }

    // Replay a completed retry before queuing, so a client that lost the
    // response to a network fault does not re-apply the mutation.
    const auto key = idempotencyKey(context);
    if (operation->access == OperationAccess::Write && key.isNotEmpty()) {
        if (auto cached = cachedResponse(key)) {
            onComplete(*cached);
            return;
        }
    }

    if (deadlinePassed(context)) {
        onComplete(
            Response::failure(ErrorCode::Timeout, "deadline passed before dispatch", revision));
        return;
    }

    auto state = currentState();
    if (!state) {
        onComplete(
            Response::failure(ErrorCode::Cancelled, "remote API service is shut down", revision));
        return;
    }

    // Copied before the job takes ownership, so the callAsync-failure path
    // below still has a way to complete the request.
    auto fallback = onComplete;

    auto job = [state, operation, input, context, onComplete = std::move(onComplete)]() mutable {
        Response response;
        {
            // The shared state, never a bare `this`, is what makes this safe.
            // The lock is held across the handler, so the service cannot be
            // retired out from under it, and two handlers cannot run at once
            // even when the caller's thread executes them inline.
            const std::lock_guard<std::mutex> lock(state->mutex);
            if (state->service == nullptr) {
                // The revision the service had when it was retired, not zero:
                // a client's cursor must never be moved backwards by a failure.
                response =
                    Response::failure(ErrorCode::Cancelled, "request cancelled before execution",
                                      state->revisionAtRetirement);
            } else {
                response = state->service->execute(*operation, input, context);
            }
        }
        // Deliberately outside the lock. A callback is free to shut the service
        // down, replace the project, or dispatch the next request — all of
        // which take this same non-recursive mutex, and would deadlock if it
        // were still held.
        onComplete(std::move(response));
    };

    // Already on the message thread, or there is no message thread to hop to.
    // The latter is the headless case — a test runner or a console host with no
    // event loop — where posting would queue work that nothing ever dispatches.
    // Execution is serialized by the state lock either way, so running inline
    // does not weaken the one-handler-at-a-time guarantee.
    if (juce::MessageManager::existsAndIsCurrentThread() ||
        juce::MessageManager::getInstanceWithoutCreating() == nullptr) {
        job();
        return;
    }

    // callAsync returns false once the message loop is quitting, and the job is
    // destroyed unrun. Completing here keeps the exactly-once callback promise
    // that adapters rely on to release per-request state.
    if (!juce::MessageManager::callAsync(std::move(job))) {
        fallback(Response::failure(ErrorCode::Cancelled,
                                   "message loop is shutting down; request not dispatched",
                                   revision));
    }
}

Response RemoteApiService::dispatchSync(const juce::String& operationName, const juce::var& input,
                                        const RequestContext& context) {
    if (isMessageThreadAssertionEnabled()) {
        JUCE_ASSERT_MESSAGE_THREAD
    }

    Response response;
    bool completed = false;
    dispatch(operationName, input, context, [&](Response result) {
        response = std::move(result);
        completed = true;
    });
    // dispatch() runs inline when already on the message thread, and every
    // pre-hop rejection also completes inline, so this holds for every path
    // this function is allowed to be called on.
    jassert(completed);
    juce::ignoreUnused(completed);
    return response;
}

Response RemoteApiService::execute(const OperationDescriptor& operation, const juce::var& input,
                                   const RequestContext& context) {
    auto revision = currentRevision();
    const bool isWrite = operation.access == OperationAccess::Write;
    const auto key = idempotencyKey(context);

    // Re-checked here and not only before queuing. Two retries of the same
    // request can both miss the pre-queue lookup while the first is still in
    // flight; this runs under the serialized execution lock, so by now the
    // first has completed and cached its response. Without it the duplicate
    // would apply the mutation twice, or fail with a spurious conflict when the
    // client sent an expectedRevision.
    if (isWrite && key.isNotEmpty()) {
        if (auto cached = cachedResponse(key))
            return *cached;
    }

    // Re-checked after the hop, not only before it: a request can sit in the
    // queue behind slower work and expire while waiting.
    if (deadlinePassed(context))
        return Response::failure(ErrorCode::Timeout, "deadline passed before execution", revision);

    // Writes only. A read is safe at any revision, so a client that carries its
    // cursor on every request would otherwise get Conflict on `project.get`
    // after any intervening edit — precisely when it most needs to re-read.
    if (isWrite && context.expectedRevision && *context.expectedRevision != revision) {
        return Response::failure(
            ErrorCode::Conflict,
            "expected revision " +
                juce::String(static_cast<juce::int64>(*context.expectedRevision)) +
                ", project is at " + juce::String(static_cast<juce::int64>(revision)),
            revision);
    }

    if (operation.handler == nullptr)
        return Response::failure(ErrorCode::InternalError,
                                 "operation " + operation.name + " has no handler", revision);

    HandlerResult result;
    {
        // Model listeners fire synchronously from inside the handler's own
        // mutation. Publishing the executing thread here lets the bridge tell
        // those re-entrant callbacks apart from a genuine concurrent UI edit,
        // so one request produces one revision instead of one per notification.
        const ScopedExecutingThread marker(executingThread_);
        if (isWrite) {
            const ScopedUndoStep step(api_.undo(), operation.summary);
            result = operation.handler(api_, input, context);
        } else {
            result = operation.handler(api_, input, context);
        }
    }

    if (result.failed())
        return Response::failure(*result.error, revision);

    // Only a committed write moves the revision. A read, a write that failed
    // validation inside the handler, and a write the handler resolved to a
    // no-op all leave it where it was — otherwise a request that changed
    // nothing would invalidate every other client's expectedRevision.
    const bool committed = isWrite && result.mutated;
    if (committed) {
        revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
        for (const auto topic : topicsFor(operation.name))
            changes_.markChanged(topic, revision);
    }

    auto response = Response::success(result.value, revision);
    // Every successful write is cached, including one that resolved to a no-op.
    // `mutated` governs the revision and change publication, not idempotency:
    // clearing an already-empty lane succeeds without changing anything, and if
    // that request id were forgotten, a retry after points were added would
    // delete them instead of replaying the original response.
    if (isWrite && key.isNotEmpty())
        cacheResponse(key, response);
    return response;
}

bool RemoteApiService::isExecutingOnThisThread() const {
    return executingThread_.load(std::memory_order_acquire) == std::this_thread::get_id();
}

Revision RemoteApiService::currentRevision() const {
    return revision_.load(std::memory_order_acquire);
}

void RemoteApiService::shutdown() {
    if (shutdown_.exchange(true, std::memory_order_acq_rel))
        return;

    // Retires the shared state under the execution lock, so this returns only
    // once no handler is running and none can start. Queued jobs then observe a
    // null service and complete with Cancelled without touching it.
    retireState();
    changes_.discardPending();
}

bool RemoteApiService::isShutdown() const {
    return shutdown_.load(std::memory_order_acquire);
}

void RemoteApiService::projectReplaced() {
    if (shutdown_.load(std::memory_order_acquire))
        return;

    // Retire the outgoing state, then install a fresh one so requests arriving
    // after the swap are not cancelled by the retirement of the old project's
    // queued work.
    retireState();
    {
        const std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = std::make_shared<ExecutionState>();
        state_->service = this;
    }

    // Every outstanding expectedRevision refers to the old project, so move the
    // counter to guarantee those writes are rejected as stale rather than
    // silently applied to different state. Pending changes describe the
    // outgoing project, so they are dropped rather than delivered — but the
    // swap itself is published, at the new revision, so subscribers resync.
    //
    // Every discrete topic, not just `project`: a different project has
    // different tracks, clips, devices, selection, session grid, automation, and
    // transport state, and a subscriber watching only one of those would
    // otherwise hear nothing at all and keep showing the old project's contents
    // until something happened to change that topic in the new one. The two
    // continuous topics are excluded because nothing marks them — a meter
    // reading is sampled, not invalidated.
    const auto revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
    changes_.discardPending();
    for (std::size_t index = 0; index < TOPIC_COUNT; ++index) {
        const auto topic = static_cast<Topic>(index);
        if (!isContinuousTopic(topic))
            changes_.markChanged(topic, revision);
    }

    {
        const std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.clear();
    }
}

void RemoteApiService::noteModelChanged(Topic topic) {
    noteModelChanged({topic});
}

void RemoteApiService::noteModelChanged(std::initializer_list<Topic> topics) {
    if (shutdown_.load(std::memory_order_acquire))
        return;

    // Fired from inside a handler's own mutation. The dispatcher publishes one
    // revision for the whole request when the handler returns, so bumping again
    // here would advance it two or more times for one logical write. The topics
    // are still marked, so subscribers see the change either way.
    if (isExecutingOnThisThread()) {
        for (const auto topic : topics)
            changes_.markChanged(topic, currentRevision() + 1);
        return;
    }

    const auto revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
    for (const auto topic : topics)
        changes_.markChanged(topic, revision);
}

void RemoteApiService::noteModelActivity(Topic topic) {
    if (shutdown_.load(std::memory_order_acquire))
        return;
    changes_.markChanged(topic, currentRevision());
}

ChangeSource& RemoteApiService::changes() {
    return changes_;
}

const ChangeSource& RemoteApiService::changes() const {
    return changes_;
}

void RemoteApiService::setAuditLog(std::shared_ptr<RemoteAuditLog> log) {
    const std::lock_guard<std::mutex> lock(auditMutex_);
    audit_ = std::move(log);
}

std::shared_ptr<RemoteAuditLog> RemoteApiService::auditLog() const {
    const std::lock_guard<std::mutex> lock(auditMutex_);
    return audit_;
}

void RemoteApiService::recordAudit(const std::shared_ptr<RemoteAuditLog>& log,
                                   const juce::String& operationName, const RequestContext& context,
                                   const Response& response) {
    if (log == nullptr)
        return;

    // A request with no declared client is an in-process caller — the
    // subscription hub projecting a snapshot, or a test. Recording those would
    // bury the remote traffic the log exists to show under MAGDA talking to
    // itself.
    if (context.clientName.isEmpty())
        return;

    AuditEntry entry;
    entry.client = context.clientName;
    entry.connectionId = context.clientId;
    entry.transport = context.transport;
    entry.operation = operationName;
    entry.requestId = context.requestId;

    if (response.ok) {
        entry.outcome = AuditOutcome::Ok;
    } else if (response.error.code == ErrorCode::PermissionDenied) {
        entry.outcome = AuditOutcome::Denied;
        // The scope that would have allowed it, and nothing else. The client
        // gets a sentence; the log gets the one word the user would act on.
        if (const auto* operation = OperationRegistry::instance().find(operationName))
            entry.detail = scopeName(operation->requiredScope);
    } else {
        entry.outcome = AuditOutcome::Failed;
        // The code, never `response.error.message`: a validation failure's
        // message quotes the offending value, which is client input and may be
        // anything at all.
        entry.detail = toString(response.error.code);
    }

    log->record(std::move(entry));
}

void RemoteApiService::setIdempotencyCacheCapacity(std::size_t capacity) {
    const std::lock_guard<std::mutex> lock(cacheMutex_);
    cacheCapacity_ = capacity;
    if (cache_.size() > cacheCapacity_)
        cache_.erase(cache_.begin(),
                     cache_.begin() + static_cast<long>(cache_.size() - cacheCapacity_));
}

juce::String RemoteApiService::idempotencyKey(const RequestContext& context) {
    if (context.requestId.isEmpty())
        return {};
    // Scoped by client: two clients are free to number their own requests from
    // 1, and one must never replay the other's response.
    return context.clientId + "\n" + context.requestId;
}

std::optional<Response> RemoteApiService::cachedResponse(const juce::String& key) const {
    const std::lock_guard<std::mutex> lock(cacheMutex_);
    const auto found =
        std::find_if(cache_.begin(), cache_.end(),
                     [&key](const CachedResponse& entry) { return entry.key == key; });
    if (found == cache_.end())
        return std::nullopt;
    return found->response;
}

void RemoteApiService::cacheResponse(const juce::String& key, const Response& response) {
    const std::lock_guard<std::mutex> lock(cacheMutex_);
    const auto found =
        std::find_if(cache_.begin(), cache_.end(),
                     [&key](const CachedResponse& entry) { return entry.key == key; });
    if (found != cache_.end()) {
        found->response = response;
        return;
    }
    if (cacheCapacity_ == 0)
        return;
    // Bounded and FIFO: a long-lived session must not accumulate one entry per
    // mutation for the life of the process.
    if (cache_.size() >= cacheCapacity_)
        cache_.erase(cache_.begin());
    cache_.push_back({key, response});
}

}  // namespace magda::remote
