#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "remote_api.hpp"
#include "remote_changes.hpp"

namespace magda {

class MagdaApi;

namespace remote {

/**
 * @brief The outcome of one dispatched operation.
 *
 * `revision` is the project revision *after* the operation: unchanged for a
 * read or a failure, incremented for a committed write. A client can therefore
 * use the response to seed the `expectedRevision` of its next write without a
 * separate round trip.
 */
struct Response {
    bool ok = false;
    juce::var result;
    Error error;
    Revision revision = INITIAL_REVISION;

    static Response success(juce::var result, Revision revision);
    static Response failure(Error error, Revision revision);
    static Response failure(ErrorCode code, const juce::String& message, Revision revision);

    /// Wire form: the #1854 envelope plus the revision.
    juce::var toEnvelope() const;
};

/**
 * @brief Executes remote operations safely against `MagdaApi`.
 *
 * The single point where a transport's thread becomes a DAW mutation. Every
 * adapter — WebSocket (#1856), MCP (#1858), and any later one — routes through
 * this rather than reaching into the facade itself, so validation, revisions,
 * undo grouping, idempotency, and error mapping exist once.
 *
 * ## Threading
 *
 * `dispatch` may be called from any thread. The work splits three ways:
 *
 * - **Calling thread**: operation lookup and JSON-schema validation. Both are
 *   pure functions of the request, so rejecting a malformed request never
 *   reaches the message thread at all — a client sending garbage cannot cost
 *   the DAW anything.
 * - **Message thread**: the handler, and copying results into DTOs. This is
 *   forced, not chosen: `MagdaApi` reads live model state, which is
 *   message-thread-only.
 * - **Calling thread again**: serializing the DTO to bytes, in the adapter.
 *
 * Handlers therefore run serially. That is a real throughput ceiling and is
 * inherent to the engine rather than to this design — a worker pool would only
 * queue on the same thread. Deadlines and the bounded idempotency cache are the
 * mitigation; per-client rate and concurrency limits are #1860.
 *
 * Nothing here touches the audio thread, and no `MessageManagerLock` is taken:
 * the hop is always an async post, never a blocking wait.
 *
 * ## Lifetime
 *
 * Queued work captures a shared cancellation token rather than `this`. After
 * `shutdown()` — which the destructor calls — any lambda still in the message
 * queue observes the cancelled token and completes with `Cancelled` without
 * dereferencing the service. `projectReplaced()` does the same for work queued
 * against a project that no longer exists.
 */
class RemoteApiService {
  public:
    using Callback = std::function<void(Response)>;

    explicit RemoteApiService(MagdaApi& api);
    ~RemoteApiService();

    RemoteApiService(const RemoteApiService&) = delete;
    RemoteApiService& operator=(const RemoteApiService&) = delete;

    /**
     * @brief Validate on the calling thread, then execute on the message thread.
     *
     * `onComplete` is invoked exactly once, on the message thread for work that
     * was queued, or synchronously on the calling thread when the request is
     * rejected before the hop.
     */
    void dispatch(const juce::String& operationName, const juce::var& input,
                  const RequestContext& context, Callback onComplete);

    /**
     * @brief Synchronous form, for callers already on the message thread.
     *
     * Asserts if called from anywhere else — the point of this class is that
     * the hop is not optional.
     */
    Response dispatchSync(const juce::String& operationName, const juce::var& input,
                          const RequestContext& context);

    Revision currentRevision() const;

    /// Cancel queued work and stop accepting new requests.
    void shutdown();
    bool isShutdown() const;

    /**
     * @brief Invalidate work queued against the outgoing project.
     *
     * Bumps the revision so every in-flight `expectedRevision` becomes stale,
     * drops pending change notifications, and cancels queued handlers. Queued
     * requests complete with `Cancelled` rather than running against state that
     * has been replaced underneath them.
     */
    void projectReplaced();

    /**
     * @brief Record a committed change that did not originate from this service.
     *
     * The path for edits made in the MAGDA UI or by any other local actor.
     * Bumps the revision and marks the topic, so a remote client's
     * `expectedRevision` correctly goes stale when the user edits underneath it.
     */
    void noteModelChanged(Topic topic);

    /**
     * @brief The same, for a change that invalidates several topics at once.
     *
     * One revision for the whole set rather than one per topic. Creating a
     * session clip changes both `clips` and the `session` grid derived from it,
     * and that is one edit — advancing the counter twice for it would be the
     * same double-bump the re-entrancy check above exists to prevent.
     */
    void noteModelChanged(std::initializer_list<Topic> topics);

    /**
     * @brief Record continuous movement on a topic without bumping the revision.
     *
     * For values that change at automation or gesture rate — a drag preview, a
     * parameter following an LFO, a macro sweeping during playback. Subscribers
     * still get a coalesced notification, but the revision stays put.
     *
     * The distinction matters: these fire throughout playback, so bumping the
     * revision for them would leave `expectedRevision` permanently stale
     * whenever the transport is rolling and make optimistic concurrency
     * unusable. Revisions track committed edits; this tracks motion.
     */
    void noteModelActivity(Topic topic);

    /**
     * @brief True when the calling thread is inside a handler right now.
     *
     * The model bridge uses this to tell a listener callback fired *by* a
     * remote mutation apart from a genuine concurrent edit. Without it a live
     * `tracks.create` would bump the revision once from the model notification
     * and again when the handler returns, and a multi-field write would advance
     * it several times for a single request.
     */
    bool isExecutingOnThisThread() const;

    ChangeSource& changes();
    const ChangeSource& changes() const;

    /// Idempotency cache capacity, in completed write responses.
    void setIdempotencyCacheCapacity(std::size_t capacity);

    /**
     * @brief Forget every cached write response.
     *
     * Idempotency keys are scoped by the transport's own name for a client —
     * `ws:1`, and so on — which is only unique for as long as that transport
     * lives. Replacing the listeners restarts those counters, so a new
     * connection becomes `ws:1` again and its first request can collide with a
     * key left by an entirely different client. The cached response would then
     * be returned for a write that never executed.
     *
     * Anything that replaces a listener must call this. It is not the same as
     * `projectReplaced()`, which also bumps the revision and cancels queued
     * work: here the project is untouched and only the identities are void.
     */
    void clearIdempotencyCache();

  private:
    struct CachedResponse {
        juce::String key;
        Response response;
    };

    /**
     * @brief Shared gate between queued work and the service's lifetime.
     *
     * A queued job holds a `shared_ptr` to this, never a bare `this`. It takes
     * `mutex` and re-reads `service`: non-null means the service is alive and
     * stays alive for the duration, because `shutdown()` clears the pointer
     * under the same lock and therefore cannot complete while a job is running.
     * A raw `this` guarded by a separate flag would leave a window between
     * observing the flag and dereferencing the pointer.
     *
     * The same lock serializes execution. On a host with a message thread that
     * is nearly free — handlers already run only there. On a headless host,
     * where `dispatch` runs inline on the caller's thread, it is what preserves
     * the one-handler-at-a-time guarantee that the model requires.
     */
    struct ExecutionState {
        std::mutex mutex;
        RemoteApiService* service = nullptr;
        /// The revision at the moment this state was retired. Work that arrives
        /// afterwards reports it rather than zero, so a cancellation never moves
        /// a client's cursor backwards.
        Revision revisionAtRetirement = INITIAL_REVISION;
    };

    Response execute(const OperationDescriptor& operation, const juce::var& input,
                     const RequestContext& context);

    std::shared_ptr<ExecutionState> currentState() const;
    void retireState();

    static juce::String idempotencyKey(const RequestContext& context);
    std::optional<Response> cachedResponse(const juce::String& key) const;
    void cacheResponse(const juce::String& key, const Response& response);

    MagdaApi& api_;
    ChangeSource changes_;

    std::atomic<Revision> revision_{INITIAL_REVISION};
    std::atomic<bool> shutdown_{false};

    /// Retired on shutdown and replaced on project swap, so work queued against
    /// the outgoing project cannot run against the incoming one.
    std::shared_ptr<ExecutionState> state_;
    mutable std::mutex stateMutex_;

    /**
     * The thread currently inside a handler, or a default id when none is.
     *
     * Model listeners fire synchronously from inside a handler's own mutation,
     * so the bridge would otherwise bump the revision again for a change the
     * dispatcher is about to publish. Comparing against the executing thread
     * suppresses exactly those re-entrant callbacks and leaves genuine
     * concurrent UI edits alone.
     */
    std::atomic<std::thread::id> executingThread_{};

    mutable std::mutex cacheMutex_;
    std::vector<CachedResponse> cache_;
    std::size_t cacheCapacity_ = 256;
};

}  // namespace remote
}  // namespace magda
