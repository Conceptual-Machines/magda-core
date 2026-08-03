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
    if (operationName == "tracks.create" || operationName == "tracks.delete")
        return {Topic::Tracks, Topic::Clips, Topic::Devices};
    if (operationName.startsWith("tracks."))
        return {Topic::Tracks};
    if (operationName.startsWith("clips."))
        return {Topic::Clips};
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
    : api_(api), liveToken_(std::make_shared<std::atomic<bool>>(true)) {}

RemoteApiService::~RemoteApiService() {
    shutdown();
}

void RemoteApiService::dispatch(const juce::String& operationName, const juce::var& input,
                                const RequestContext& context, Callback onComplete) {
    jassert(onComplete != nullptr);
    const auto revision = currentRevision();

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

    std::shared_ptr<std::atomic<bool>> token;
    {
        const std::lock_guard<std::mutex> lock(tokenMutex_);
        token = liveToken_;
    }

    auto job = [this, operation, input, context, token,
                onComplete = std::move(onComplete)]() mutable {
        // The token, not `this`, is what makes this safe: after shutdown or a
        // project swap it reads false and the service is never touched.
        if (!token->load(std::memory_order_acquire)) {
            onComplete(Response::failure(ErrorCode::Cancelled, "request cancelled before execution",
                                         INITIAL_REVISION));
            return;
        }
        onComplete(execute(*operation, input, context));
    };

    // Already on the message thread, or there is no message thread to hop to.
    // The latter is the headless case — a test runner or a console host with no
    // event loop — where posting would queue work that nothing ever dispatches.
    // Running inline there is the correct behaviour, not a test accommodation:
    // the caller *is* the only thread that touches the model.
    if (juce::MessageManager::existsAndIsCurrentThread() ||
        juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        job();
    else
        juce::MessageManager::callAsync(std::move(job));
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

    // Re-checked after the hop, not only before it: a request can sit in the
    // queue behind slower work and expire while waiting.
    if (deadlinePassed(context))
        return Response::failure(ErrorCode::Timeout, "deadline passed before execution", revision);

    if (context.expectedRevision && *context.expectedRevision != revision) {
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

    const bool isWrite = operation.access == OperationAccess::Write;
    HandlerResult result;
    if (isWrite) {
        const ScopedUndoStep step(api_.undo(), operation.summary);
        result = operation.handler(api_, input, context);
    } else {
        result = operation.handler(api_, input, context);
    }

    if (result.failed())
        return Response::failure(*result.error, revision);

    // Only a committed write moves the revision. A read, or a write that failed
    // validation inside the handler, leaves it where it was — otherwise every
    // rejected request would invalidate every other client's expectedRevision.
    if (isWrite) {
        revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
        for (const auto topic : topicsFor(operation.name))
            changes_.markChanged(topic, revision);
    }

    auto response = Response::success(result.value, revision);
    if (isWrite) {
        if (const auto key = idempotencyKey(context); key.isNotEmpty())
            cacheResponse(key, response);
    }
    return response;
}

Revision RemoteApiService::currentRevision() const {
    return revision_.load(std::memory_order_acquire);
}

void RemoteApiService::shutdown() {
    if (shutdown_.exchange(true, std::memory_order_acq_rel))
        return;

    const std::lock_guard<std::mutex> lock(tokenMutex_);
    if (liveToken_)
        liveToken_->store(false, std::memory_order_release);
    changes_.discardPending();
}

bool RemoteApiService::isShutdown() const {
    return shutdown_.load(std::memory_order_acquire);
}

void RemoteApiService::projectReplaced() {
    {
        const std::lock_guard<std::mutex> lock(tokenMutex_);
        if (liveToken_)
            liveToken_->store(false, std::memory_order_release);
        // A fresh token so requests arriving after the swap are not cancelled
        // by the one that retired the old project's queued work.
        liveToken_ = std::make_shared<std::atomic<bool>>(true);
    }

    // Every outstanding expectedRevision refers to the old project, so move the
    // counter to guarantee those writes are rejected as stale rather than
    // silently applied to different state.
    revision_.fetch_add(1, std::memory_order_acq_rel);
    changes_.discardPending();

    {
        const std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.clear();
    }
}

void RemoteApiService::noteModelChanged(Topic topic) {
    if (shutdown_.load(std::memory_order_acquire))
        return;
    const auto revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
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
