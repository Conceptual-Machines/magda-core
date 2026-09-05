#include "remote_mcp_server.hpp"

// httplib pulls in <windows.h>, which defines min/max as macros and drags in
// half the Win32 surface. Both break JUCE headers compiled after it.
#if JUCE_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

// cpp-httplib's limits are compile definitions on the httplib target in the root
// CMakeLists.txt. They cannot be #defined here: this is a header-only library,
// so a definition in one translation unit changes only the inline functions that
// unit sees, and two units in an executable disagreeing is an ODR violation the
// linker resolves however it likes.
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include "remote_audit.hpp"
#include "remote_clients.hpp"
#include "remote_http_auth.hpp"
#include "remote_service.hpp"
#include "remote_subscriptions.hpp"

namespace magda {
namespace remote {

namespace {

constexpr const char* kEndpoint = "/mcp";

constexpr const char* kProtocolVersionHeader = "MCP-Protocol-Version";
constexpr const char* kMethodHeader = "Mcp-Method";
constexpr const char* kNameHeader = "Mcp-Name";
constexpr const char* kSessionHeader = "Mcp-Session-Id";

/// The sentinel a client wraps a header value in when it is not plain ASCII.
constexpr const char* kBase64Prefix = "=?base64?";
constexpr const char* kBase64Suffix = "?=";

juce::var makeObject() {
    return {new juce::DynamicObject()};
}

void setProperty(juce::var& object, const char* name, const juce::var& value) {
    if (auto* dynamicObject = object.getDynamicObject())
        dynamicObject->setProperty(juce::Identifier(name), value);
}

/**
 * @brief A header value as the client meant it.
 *
 * `Mcp-Name` carries a tool name or a resource URI, and HTTP field values are
 * restricted to visible ASCII. A client whose value falls outside that wraps it
 * in `=?base64?…?=`, and the specification requires the server to decode before
 * comparing — otherwise a legitimate request with a non-ASCII name would always
 * look like a header/body mismatch.
 */
juce::String decodeHeaderValue(const juce::String& raw) {
    if (!raw.startsWith(kBase64Prefix) || !raw.endsWith(kBase64Suffix))
        return raw;

    const auto encoded = raw.substring(static_cast<int>(std::strlen(kBase64Prefix)),
                                       raw.length() - static_cast<int>(std::strlen(kBase64Suffix)));

    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, encoded))
        return raw;
    return decoded.toString();
}

std::string jsonRpcEnvelope(const juce::var& id, const char* member, const juce::var& value) {
    auto reply = makeObject();
    setProperty(reply, "jsonrpc", "2.0");
    // A JSON-RPC error may carry a null id when the request was unreadable
    // enough that we never learned one; a result never can.
    setProperty(reply, "id", id);
    setProperty(reply, member, value);
    return juce::JSON::toString(reply, true).toStdString();
}

std::string jsonRpcResult(const juce::var& id, const juce::var& result) {
    return jsonRpcEnvelope(id, "result", result);
}

std::string jsonRpcError(const juce::var& id, const McpError& error) {
    return jsonRpcEnvelope(id, "error", error.toJson());
}

/// One SSE event carrying a JSON-RPC message. No `id:` field is emitted: this
/// revision does not offer `Last-Event-ID` resumption, and a stream whose every
/// message says "re-read this URI" has nothing to replay anyway.
std::string sseData(const juce::var& message) {
    return "data: " + juce::JSON::toString(message, true).toStdString() + "\n\n";
}

/// A comment line. Per the SSE grammar a client must ignore it, which makes it
/// the standard way to keep an idle connection from being closed by an
/// intermediary or a client idle timeout.
std::string sseKeepAlive() {
    return ":\n\n";
}

void writeJson(httplib::Response& response, int status, const std::string& body) {
    response.status = status;
    response.set_content(body, "application/json");
}

/**
 * @brief A session identifier a client cannot guess.
 *
 * Only the legacy era has these, and only it needs them, but while it exists a
 * session id is a bearer credential for everything that session may do. 128 bits
 * from the OS entropy source, hex encoded — visible ASCII, as the specification
 * requires — rather than `juce::Random`, which is a seeded PRNG.
 */
juce::String generateSessionId() {
    std::random_device entropy;
    juce::String id;
    for (int i = 0; i < 4; ++i)
        id += juce::String::toHexString(static_cast<int>(entropy())).paddedLeft('0', 8);
    return id;
}

}  // namespace

// ===========================================================================
// EventStream
// ===========================================================================

/**
 * One client's notification stream: a bounded outbox and the thread that drains
 * it.
 *
 * Shared by both eras. A modern `subscriptions/listen` response stream and a
 * legacy `GET` session stream differ only in how they were opened and whether
 * their messages carry a subscription id — the delivery, the backpressure, and
 * the shutdown are one mechanism.
 *
 * The filter lives here rather than being captured by the hub sink because the
 * legacy era can change it mid-stream: `resources/subscribe` arrives on a
 * different connection than the one holding the stream open, so a copy captured
 * at registration would go stale the moment a client subscribed to a second
 * resource.
 */
struct EventStream {
    EventStream(int outboxCapacity, juce::var streamSubscriptionId, juce::String streamHandle,
                juce::String streamClientName)
        : capacity(outboxCapacity),
          subscriptionId(std::move(streamSubscriptionId)),
          handle(std::move(streamHandle)),
          clientName(std::move(streamClientName)) {}

    enum class Take { Frame, KeepAlive, Closed };

    const int capacity;
    const juce::var subscriptionId;
    /// `mcp:stream:<n>` — what the settings UI disconnects, and what this
    /// stream is called in the client registry and the audit log.
    const juce::String handle;
    /// Normalised declared name of whoever opened it.
    const juce::String clientName;

    mutable std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> outbox;
    McpEndpoint::ListenFilter filter;
    bool closed = false;

    /**
     * The hub's handle on this stream, or 0 when it is not subscribed.
     *
     * Atomic because the legacy era reads it from a different connection than
     * the one that set it: `resources/subscribe` arrives on its own request
     * while the thread serving the `GET` stream may still be registering. The
     * outcome is correct either way — the later of the two reads a filter the
     * earlier already committed under a lock — but the read itself has to be
     * defined.
     */
    std::atomic<SubscriptionHub::ClientId> subscriber{0};

    bool push(std::string frame) {
        {
            const std::scoped_lock lock(mutex);
            if (closed || static_cast<int>(outbox.size()) >= capacity)
                return false;
            outbox.push_back(std::move(frame));
        }
        ready.notify_one();
        return true;
    }

    /**
     * @brief Turn one topic movement into the notifications this client wants.
     *
     * Returns false only when the client could not take them, which is the
     * subscription hub's signal that it has stopped reading. Having nothing to
     * send is emphatically not that: a client watching `magda://transport` sees
     * every `tracks` flush arrive here, and reporting those as drops would have
     * the hub disconnect an entirely healthy subscriber.
     */
    bool publish(Topic topic, const McpEndpoint& endpoint) {
        std::vector<std::string> frames;
        {
            const std::scoped_lock lock(mutex);
            if (closed)
                return false;
            const auto uris = endpoint.urisAffectedBy(topic, filter);
            if (uris.empty())
                return true;
            // All or nothing. A URI dropped for want of room would be a resource
            // the client never learns is stale, while its siblings from the same
            // flush arrive — so the whole flush is refused and the hub is told,
            // which is what makes the drop recoverable rather than silent.
            if (static_cast<int>(outbox.size() + uris.size()) > capacity)
                return false;
            for (const auto& uri : uris)
                frames.push_back(sseData(McpEndpoint::resourceUpdated(uri, subscriptionId)));
            for (auto& frame : frames)
                outbox.push_back(std::move(frame));
        }
        ready.notify_one();
        return true;
    }

    /**
     * @brief Block until there is something to write, or it is time not to.
     *
     * `KeepAlive` is the timeout expiring with an empty outbox, which is a
     * comment line rather than nothing: it is how a stream that has been quiet
     * for a while proves the connection is still there.
     */
    Take take(std::chrono::milliseconds timeout, std::string& frame) {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait_for(lock, timeout, [this] { return closed || !outbox.empty(); });
        if (!outbox.empty()) {
            frame = std::move(outbox.front());
            outbox.pop_front();
            return Take::Frame;
        }
        if (closed)
            return Take::Closed;
        return Take::KeepAlive;
    }

    void close(std::string terminalFrame = {}) {
        {
            const std::scoped_lock lock(mutex);
            if (closed)
                return;
            outbox.clear();
            if (!terminalFrame.empty())
                outbox.push_back(std::move(terminalFrame));
            closed = true;
        }
        ready.notify_all();
    }

    bool isClosed() const {
        const std::scoped_lock lock(mutex);
        return closed;
    }

    void setFilter(McpEndpoint::ListenFilter updated) {
        const std::scoped_lock lock(mutex);
        filter = std::move(updated);
    }

    McpEndpoint::ListenFilter currentFilter() const {
        const std::scoped_lock lock(mutex);
        return filter;
    }
};

// ===========================================================================
// Session
// ===========================================================================

/**
 * A legacy client's session.
 *
 * Exists only for `2025-11-25` and earlier. It holds the negotiated version, the
 * resources the client subscribed to, and the stream those updates go out on if
 * the client has opened one.
 *
 * Subscriptions are recorded whether or not a stream is attached, but they only
 * reach the hub while one is: an update has nowhere to go otherwise, and
 * registering a hub client with no drain would have the hub see a subscriber
 * that refuses every event and eventually disconnect it. A client that
 * subscribes and then opens its GET stream gets exactly what it asked for; one
 * that never opens a stream costs nothing.
 */
struct Session {
    juce::String id;
    juce::String protocolVersion;
    /// Normalised `clientInfo.name` from the `initialize` that created this
    /// session. Recorded once and reused, because a legacy client sends
    /// `clientInfo` only in the handshake — re-reading it per request would
    /// leave every later one anonymous.
    juce::String clientName;
    std::chrono::steady_clock::time_point lastSeen;
    /// URIs the client has subscribed to, whether or not a stream is attached.
    std::vector<juce::String> subscribedUris;
    /// The GET stream, when one is open.
    std::shared_ptr<EventStream> stream;
};

// ===========================================================================
// Impl
// ===========================================================================

struct RemoteMcpServer::Impl {
    struct Waiter;

    Impl(RemoteApiService& apiService, Options serverOptions, SubscriptionHub* hub)
        : options(std::move(serverOptions)),
          subscriptions(hub),
          endpoint(apiService,
                   McpEndpoint::Options{"MAGDA", options.serverVersion, options.defaultDeadlineMs},
                   hub),
          // Full, not empty. A bucket that starts at zero refuses the very first
          // request and only becomes usable after enough wall-clock has passed —
          // which for a client that connects and immediately asks something is
          // indistinguishable from the server being broken.
          tokens(static_cast<double>(options.maxConcurrentRequests)) {}

    const Options options;
    SubscriptionHub* const subscriptions;
    McpEndpoint endpoint;

    httplib::Server server;
    std::thread listener;
    std::atomic<bool> running{false};
    std::atomic<int> port{0};

    std::atomic<int> inFlight{0};
    /// Names streams uniquely for this server's lifetime, so the settings UI's
    /// disconnect cannot address a stream that has already been replaced.
    std::atomic<juce::uint64> nextStreamId{1};

    mutable std::mutex streamMutex;
    std::vector<std::shared_ptr<EventStream>> streams;

    mutable std::mutex sessionMutex;
    std::unordered_map<std::string, Session> sessions;

    mutable std::mutex waiterMutex;
    std::vector<std::weak_ptr<Waiter>> waiters;

    /**
     * A single token bucket rather than one per client.
     *
     * The listener is loopback-only, so every request arrives from 127.0.0.1 and
     * a per-address bucket would be a global one wearing a disguise. This is
     * therefore a bound on the endpoint as a whole, which is what it can
     * honestly be: nothing here can tell two local processes apart, since they
     * present the same token by design.
     */
    mutable std::mutex rateMutex;
    double tokens;
    std::chrono::steady_clock::time_point lastRefill = std::chrono::steady_clock::now();

    // -----------------------------------------------------------------------
    // Admission
    // -----------------------------------------------------------------------

    bool admit() {
        const std::scoped_lock lock(rateMutex);
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double>(now - lastRefill).count();
        lastRefill = now;
        const auto burst = static_cast<double>(options.maxConcurrentRequests);
        tokens = std::min(burst, tokens + elapsed * options.maxRequestsPerSecond);
        if (tokens < 1.0)
            return false;
        tokens -= 1.0;
        return true;
    }

    /**
     * @brief Everything that can refuse a client before a handler acts.
     *
     * Returns true when the request may proceed; otherwise `response` already
     * carries the refusal.
     *
     * Validating `Origin` is a MUST in the MCP transport specification, and the
     * reason is DNS rebinding: without it a page the user merely visited could
     * reach this listener from the browser they are already running.
     *
     * Deliberately called from each route rather than installed as
     * cpp-httplib's pre-routing handler, which is the obvious place for it.
     * Pre-routing runs before the request body is read, so refusing there
     * leaves an unread POST body in the socket; when the client asked for
     * `Connection: close` — as a one-shot client does — cpp-httplib skips its
     * drain and closes anyway, and closing a socket with unread data sends RST
     * rather than FIN. The refusal is already in the client's receive buffer at
     * that point, and RST discards it: the client sees a reset connection
     * instead of the 401 or 403 it was sent, on a race it loses often enough to
     * matter. Refusing from the route runs after cpp-httplib has consumed the
     * body, so the close is clean and the status always arrives.
     */
    bool authorise(const httplib::Request& request, httplib::Response& response) {
        if (!isAuthorised(juce::String(request.get_header_value("Authorization")),
                          options.bearerToken)) {
            // The reason, never the credential.
            auditRefusal("invalid or missing bearer token");
            response.status = httplib::StatusCode::Unauthorized_401;
            return false;
        }

        if (!isOriginAllowed(request.has_header("Origin"),
                             juce::String(request.get_header_value("Origin")),
                             options.allowedOrigins)) {
            auditRefusal("origin not allowed");
            // The spec allows a JSON-RPC error body with no id here, and one is
            // more use to a developer than a bare 403.
            writeJson(response, httplib::StatusCode::Forbidden_403,
                      jsonRpcError({}, McpError{MCP_INVALID_REQUEST, "Origin not allowed"}));
            return false;
        }

        return true;
    }

    // -----------------------------------------------------------------------
    // Sessions
    // -----------------------------------------------------------------------

    /// Drop sessions nobody has touched. A client is told to DELETE when it is
    /// done; one that crashes never does, and a session holds a subscription.
    /// Collected ids are returned rather than dropped, so the caller can
    /// deregister them from the client registry outside the session lock.
    void collectIdleSessionsLocked(std::vector<juce::String>& collected) {
        const auto now = std::chrono::steady_clock::now();
        const auto limit = std::chrono::seconds(options.sessionIdleSeconds);
        for (auto it = sessions.begin(); it != sessions.end();) {
            const auto attached = it->second.stream != nullptr && !it->second.stream->isClosed();
            if (!attached && now - it->second.lastSeen > limit) {
                collected.push_back(it->second.id);
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }

    void collectIdleSessions() {
        std::vector<juce::String> collected;
        {
            const std::scoped_lock lock(sessionMutex);
            collectIdleSessionsLocked(collected);
        }
        noteSessionsClosed(collected);
    }

    std::optional<juce::String> createSession(const juce::String& version,
                                              const juce::String& clientName) {
        std::vector<juce::String> collected;
        juce::String id;
        {
            const std::scoped_lock lock(sessionMutex);
            collectIdleSessionsLocked(collected);
            if (static_cast<int>(sessions.size()) >= options.maxSessions) {
                // Still report the sessions that were just collected, or the
                // settings list keeps showing connections that have gone.
                noteSessionsClosed(collected);
                return std::nullopt;
            }

            Session session;
            session.id = generateSessionId();
            session.protocolVersion = version;
            session.clientName = clientName;
            session.lastSeen = std::chrono::steady_clock::now();
            id = session.id;
            sessions.emplace(id.toStdString(), std::move(session));
        }

        noteSessionsClosed(collected);
        noteSessionOpen(id, clientName);
        return id;
    }

    /// A live session's negotiated version and client, touching its idle clock.
    struct SessionState {
        juce::String protocolVersion;
        juce::String clientName;
    };

    std::optional<SessionState> touchSession(const juce::String& id) {
        const std::scoped_lock lock(sessionMutex);
        const auto it = sessions.find(id.toStdString());
        if (it == sessions.end())
            return std::nullopt;
        it->second.lastSeen = std::chrono::steady_clock::now();
        return SessionState{it->second.protocolVersion, it->second.clientName};
    }

    bool deleteSession(const juce::String& id) {
        std::shared_ptr<EventStream> stream;
        juce::String clientName;
        {
            const std::scoped_lock lock(sessionMutex);
            const auto it = sessions.find(id.toStdString());
            if (it == sessions.end())
                return false;
            stream = it->second.stream;
            clientName = it->second.clientName;
            sessions.erase(it);
        }
        // Outside the lock: closing wakes the provider thread, which may be in
        // the middle of taking a frame.
        if (stream != nullptr)
            releaseStream(stream);
        noteSessionClosed(id, clientName);
        return true;
    }

    // -----------------------------------------------------------------------
    // Client registry and audit
    // -----------------------------------------------------------------------

    /// `mcp:<sessionId>` — the same string `Call::clientId` carries, so the
    /// audit log, the idempotency cache, and the disconnect button all name a
    /// session the same way.
    static juce::String handleForSession(const juce::String& sessionId) {
        return "mcp:" + sessionId;
    }

    void noteSessionOpen(const juce::String& sessionId, const juce::String& clientName) {
        if (options.clients != nullptr) {
            ConnectedClient record;
            record.connectionId = handleForSession(sessionId);
            record.name = clientName;
            record.transport = TRANSPORT_MCP;
            record.scopesAtConnect = options.clients->scopesFor(clientName);
            options.clients->noteConnected(std::move(record));
        }
        audit(clientName, handleForSession(sessionId), AUDIT_CONNECTION_OPEN,
              AuditOutcome::Connected, "session");
    }

    void noteSessionClosed(const juce::String& sessionId, const juce::String& clientName) {
        if (options.clients != nullptr)
            options.clients->noteDisconnected(handleForSession(sessionId));
        audit(clientName, handleForSession(sessionId), AUDIT_CONNECTION_CLOSE,
              AuditOutcome::Disconnected, "session");
    }

    /// Idle collection has no client name to hand — the session is already
    /// gone — so these are reported without one.
    void noteSessionsClosed(const std::vector<juce::String>& sessionIds) {
        for (const auto& id : sessionIds)
            noteSessionClosed(id, {});
    }

    void audit(const juce::String& clientName, const juce::String& connectionId,
               const juce::String& operation, AuditOutcome outcome,
               const juce::String& detail = {}) const {
        if (options.audit == nullptr)
            return;
        // Every MCP audit happens on a pool thread inside a live request, so
        // this one needs no ownership dance — the shared_ptr in `options` is
        // what keeps the log alive, and nothing here outlives the server.
        AuditEntry entry;
        entry.client = clientName;
        entry.connectionId = connectionId;
        entry.transport = TRANSPORT_MCP;
        entry.operation = operation;
        entry.outcome = outcome;
        entry.detail = detail;
        options.audit->record(std::move(entry));
    }

    void auditRefusal(const juce::String& reason) const {
        audit({}, {}, AUDIT_CONNECTION_REJECTED, AuditOutcome::Rejected, reason);
    }

    /**
     * @brief Close one MCP connection by handle, for the settings UI.
     *
     * A session handle drops the session and its stream; a stream handle closes
     * the stream. Both are prompt — unlike the WebSocket, there is no reader
     * blocked in a socket to wait for.
     */
    bool disconnectByHandle(const juce::String& handle) {
        std::shared_ptr<EventStream> target;
        {
            const std::scoped_lock lock(streamMutex);
            const auto found = std::find_if(
                streams.begin(), streams.end(),
                [&](const std::shared_ptr<EventStream>& s) { return s->handle == handle; });
            if (found != streams.end())
                target = *found;
        }
        if (target != nullptr) {
            releaseStream(target);
            return true;
        }

        // Not a stream, so it is a session — whose id is the handle minus the
        // prefix `handleForSession` added.
        if (!handle.startsWith("mcp:"))
            return false;
        return deleteSession(handle.substring(4));
    }

    // -----------------------------------------------------------------------
    // Streams
    // -----------------------------------------------------------------------

    std::shared_ptr<EventStream> claimStream(const juce::var& subscriptionId,
                                             const juce::String& clientName) {
        std::shared_ptr<EventStream> stream;
        {
            const std::scoped_lock lock(streamMutex);
            if (static_cast<int>(streams.size()) >= options.maxStreams)
                return nullptr;
            // Sized against the connection's own reading. Each entry is one
            // short "re-read this URI" line, so the cap is about how far behind
            // a client may fall rather than about memory.
            stream = std::make_shared<EventStream>(
                64, subscriptionId, "mcp:stream:" + juce::String(nextStreamId.fetch_add(1)),
                clientName);
            streams.push_back(stream);
        }

        if (options.clients != nullptr) {
            ConnectedClient record;
            record.connectionId = stream->handle;
            record.name = clientName;
            record.transport = TRANSPORT_MCP;
            record.scopesAtConnect = options.clients->scopesFor(clientName);
            options.clients->noteConnected(std::move(record));
        }
        audit(clientName, stream->handle, AUDIT_CONNECTION_OPEN, AuditOutcome::Connected, "stream");
        return stream;
    }

    /**
     * @brief Register a stream with the subscription hub for the topics it needs.
     *
     * Called with the stream's filter already set, and after its acknowledgment
     * is queued, so the ordering the specification requires — acknowledgment
     * first, no notification before it — falls out of the outbox being FIFO
     * rather than needing a second mechanism to enforce it.
     */
    void subscribeStream(const std::shared_ptr<EventStream>& stream) {
        if (subscriptions == nullptr)
            return;

        const auto filter = stream->currentFilter();
        const auto topics = endpoint.topicsFor(filter);

        if (stream->subscriber == 0) {
            auto* protocol = &endpoint;
            std::weak_ptr<EventStream> weak = stream;
            stream->subscriber = subscriptions->addClient(
                [weak, protocol](const SubscriptionEvent& event) {
                    // The hub's own event types — snapshot, delta, sample — carry
                    // no meaning here. An MCP resource update is a URI and
                    // nothing else, so every kind of change collapses to the
                    // same "re-read this", and a client that fell behind
                    // recovers by doing exactly what it would have done anyway.
                    if (auto live = weak.lock())
                        return live->publish(event.topic, *protocol);
                    return false;
                },
                [weak, protocol](const juce::String&) {
                    // Closing the outbox is safe under the hub's lock; the
                    // provider drains this terminal result and its cleanup then
                    // deregisters the stream without re-entering the hub here.
                    if (auto live = weak.lock()) {
                        std::string terminal;
                        if (!live->subscriptionId.isVoid() && !live->subscriptionId.isUndefined()) {
                            terminal = sseData(protocol->listenClosedResult(live->subscriptionId));
                        }
                        live->close(std::move(terminal));
                    }
                });
        }

        if (topics.empty())
            return;

        juce::Array<juce::var> topicNames;
        for (const auto topic : topics)
            topicNames.add(juce::String(toString(topic)));

        auto params = makeObject();
        setProperty(params, "topics", topicNames);
        // No snapshot: an MCP client reads resources over HTTP, so a snapshot
        // payload it would have to ignore is bytes across a socket for nothing.
        // Passing false is the client asserting it has state, which here is
        // exactly true — it either just read the resource or is about to.
        setProperty(params, "snapshot", false);

        subscriptions->handle(stream->subscriber, "subscriptions.subscribe", params,
                              [](Response) {});
    }

    /// Replace what a stream watches. The legacy era needs this because
    /// `resources/subscribe` arrives after the stream is already open.
    void resubscribeStream(const std::shared_ptr<EventStream>& stream) {
        if (subscriptions == nullptr || stream->subscriber == 0)
            return;

        // Clear first, then re-add. Unsubscribing with no topics means every
        // topic, so this converges on the new set whether the change added or
        // removed a resource — no diff to compute and none to get wrong.
        subscriptions->handle(stream->subscriber, "subscriptions.unsubscribe", makeObject(),
                              [](Response) {});
        subscribeStream(stream);
    }

    void releaseStream(const std::shared_ptr<EventStream>& stream) {
        if (stream == nullptr)
            return;

        // Deregister before closing: removeClient takes the hub's lock, so it
        // returns only once a publish already inside the sink has finished.
        if (subscriptions != nullptr && stream->subscriber != 0) {
            subscriptions->removeClient(stream->subscriber);
            stream->subscriber = 0;
        }
        std::string terminal;
        if (!stream->subscriptionId.isVoid() && !stream->subscriptionId.isUndefined())
            terminal = sseData(endpoint.listenClosedResult(stream->subscriptionId));
        stream->close(std::move(terminal));

        {
            const std::scoped_lock lock(streamMutex);
            streams.erase(std::remove(streams.begin(), streams.end(), stream), streams.end());
        }

        if (options.clients != nullptr)
            options.clients->noteDisconnected(stream->handle);
        audit(stream->clientName, stream->handle, AUDIT_CONNECTION_CLOSE,
              AuditOutcome::Disconnected, "stream");
    }

    /// Attach the response's chunked body to a stream and drain it until the
    /// client goes away.
    void serveStream(httplib::Response& response, const std::shared_ptr<EventStream>& stream) {
        response.set_header("Cache-Control", "no-cache");
        // Tells nginx and friends not to buffer, which would hold notifications
        // until enough accumulated to be worth forwarding.
        response.set_header("X-Accel-Buffering", "no");

        const auto keepAlive = std::chrono::milliseconds(options.keepAliveIntervalMs);
        auto self = this;

        response.set_chunked_content_provider(
            "text/event-stream",
            [stream, keepAlive](std::size_t, httplib::DataSink& sink) {
                std::string frame;
                switch (stream->take(keepAlive, frame)) {
                    case EventStream::Take::Closed:
                        sink.done();
                        return false;
                    case EventStream::Take::KeepAlive:
                        frame = sseKeepAlive();
                        break;
                    case EventStream::Take::Frame:
                        break;
                }
                // A failed write is the client having gone away, which for this
                // transport *is* the cancellation: there is no separate
                // notification to wait for and nothing to unwind.
                return sink.write(frame.data(), frame.size());
            },
            [self, stream](bool) { self->releaseStream(stream); });
    }

    // -----------------------------------------------------------------------
    // Era selection
    // -----------------------------------------------------------------------

    McpError unsupportedVersion(const juce::String& requested) const {
        juce::Array<juce::var> supported;
        for (const auto& version : mcpProtocolVersions())
            supported.add(version);

        auto data = makeObject();
        setProperty(data, "supported", supported);
        setProperty(data, "requested", requested);
        return {MCP_UNSUPPORTED_PROTOCOL_VERSION, "Unsupported protocol version", data,
                httplib::StatusCode::BadRequest_400};
    }

    struct Resolution {
        McpEra era = McpEra::Modern;
        juce::String version;
        juce::String sessionId;
        /// Normalised declared name: from `_meta` for a modern request, from
        /// the session for a legacy one. Never empty — `normaliseClientName`
        /// answers `unknown` for a caller that said nothing.
        juce::String clientName;
        /// True when this request created the session, which is the one response
        /// that has to carry `Mcp-Session-Id`.
        bool mintedSession = false;
    };

    /**
     * @brief The name a modern request declares, from `_meta`.
     *
     * Optional in the specification and optional here: a request without it is
     * `unknown`, which is a client entry like any other rather than a refusal.
     * Refusing would break every conforming host that simply does not send it.
     */
    static juce::String modernClientName(const juce::var& params) {
        const auto meta = params["_meta"];
        if (meta.getDynamicObject() == nullptr)
            return normaliseClientName({});
        return normaliseClientName(meta[MCP_META_CLIENT_INFO]["name"].toString());
    }

    /**
     * @brief Decide which revision of the protocol this request is speaking.
     *
     * Modern is tried first and wins outright, including over an `Mcp-Session-Id`
     * the client should not have sent: `2026-07-28` requires that header to be
     * ignored rather than honoured, so a client that kept one from an earlier
     * connection is served statelessly instead of being quietly given session
     * semantics it no longer expects.
     */
    std::optional<McpError> resolveEra(const httplib::Request& request, const juce::String& method,
                                       const juce::var& params, Resolution& resolution) {
        const auto meta = params["_meta"];
        const auto declared = meta.getDynamicObject() != nullptr
                                  ? meta[MCP_META_PROTOCOL_VERSION].toString()
                                  : juce::String();

        if (declared.isNotEmpty()) {
            if (!isSupportedVersion(declared))
                return unsupportedVersion(declared);
            if (eraForVersion(declared) == McpEra::Modern) {
                resolution.era = McpEra::Modern;
                resolution.version = declared;
                resolution.clientName = modernClientName(params);
                return std::nullopt;
            }
            // A legacy version named in `_meta` is a client mixing the two
            // shapes. The version is real, so it is not a version error — it
            // still needs a handshake or a session, so fall through to those.
        }

        if (method == "initialize") {
            resolution.era = McpEra::Legacy;
            resolution.version = negotiateLegacyVersion(params);
            // The legacy handshake carries `clientInfo` at the top of `params`
            // rather than in `_meta`, and carries it exactly once — which is
            // why the session keeps it.
            resolution.clientName = normaliseClientName(params["clientInfo"]["name"].toString());
            const auto minted = createSession(resolution.version, resolution.clientName);
            if (!minted) {
                auditRefusal("too many open sessions");
                return McpError{MCP_INTERNAL_ERROR,
                                "too many open sessions",
                                {},
                                httplib::StatusCode::ServiceUnavailable_503};
            }
            resolution.sessionId = *minted;
            resolution.mintedSession = true;
            return std::nullopt;
        }

        const juce::String header(request.get_header_value(kSessionHeader));
        if (header.isNotEmpty()) {
            const auto known = touchSession(header);
            // 404 specifically: it is what tells a legacy client to start a new
            // session with a fresh `initialize` rather than to give up.
            if (!known) {
                return McpError{MCP_INVALID_REQUEST,
                                "Unknown or expired session",
                                {},
                                httplib::StatusCode::NotFound_404};
            }
            resolution.era = McpEra::Legacy;
            resolution.version = known->protocolVersion;
            resolution.clientName = known->clientName;
            resolution.sessionId = header;
            return std::nullopt;
        }

        // The dual-era fallback signal. A recognisable modern JSON-RPC error in
        // a 400 is how a client tells "this is a modern MCP endpoint, send the
        // right thing" apart from "there is no MCP endpoint here, drop to the
        // deprecated transport".
        return McpError{MCP_INVALID_PARAMS,
                        "A request must carry io.modelcontextprotocol/protocolVersion in "
                        "params._meta, or an Mcp-Session-Id from a prior initialize",
                        {},
                        httplib::StatusCode::BadRequest_400};
    }

    /**
     * @brief Check the headers that mirror the body, as `2026-07-28` requires.
     *
     * The point is not redundancy. Intermediaries are allowed to route and
     * rate-limit on these headers without parsing the body, so a server that
     * executed the body while a load balancer had decided on the header would be
     * two components acting on two different requests. Rejecting the
     * disagreement is what keeps that from being exploitable.
     */
    std::optional<McpError> validateHeaders(const httplib::Request& request,
                                            const juce::String& method, const juce::var& params,
                                            const juce::String& version) const {
        const auto mismatch = [](const juce::String& message) {
            return McpError{MCP_HEADER_MISMATCH, message, {}, httplib::StatusCode::BadRequest_400};
        };

        const juce::String headerVersion(request.get_header_value(kProtocolVersionHeader));
        if (headerVersion.isEmpty())
            return mismatch(juce::String(kProtocolVersionHeader) + " header is required");
        if (headerVersion != version) {
            return mismatch(juce::String(kProtocolVersionHeader) + " header '" + headerVersion +
                            "' does not match params._meta value '" + version + "'");
        }

        const juce::String headerMethod(request.get_header_value(kMethodHeader));
        if (headerMethod.isEmpty())
            return mismatch(juce::String(kMethodHeader) + " header is required");
        if (headerMethod != method) {
            return mismatch(juce::String(kMethodHeader) + " header '" + headerMethod +
                            "' does not match body method '" + method + "'");
        }

        // `Mcp-Name` mirrors whichever field names the thing being acted on, and
        // is required only for the methods that have one.
        const char* nameField = nullptr;
        if (method == "tools/call" || method == "prompts/get")
            nameField = "name";
        else if (method == "resources/read")
            nameField = "uri";

        if (nameField != nullptr) {
            if (!request.has_header(kNameHeader))
                return mismatch(juce::String(kNameHeader) + " header is required for " + method);
            const auto actual =
                decodeHeaderValue(juce::String(request.get_header_value(kNameHeader)));
            const auto expected = params[nameField].toString();
            if (actual != expected) {
                return mismatch(juce::String(kNameHeader) + " header '" + actual +
                                "' does not match body params." + nameField + " '" + expected +
                                "'");
            }
        }

        return std::nullopt;
    }

    /// The `_meta` fields a modern request must carry. `clientInfo` is optional
    /// and, being self-reported, is never read for anything but logging.
    static std::optional<McpError> validateModernMeta(const juce::var& params) {
        const auto meta = params["_meta"];
        if (meta[MCP_META_CLIENT_CAPABILITIES].getDynamicObject() == nullptr) {
            return McpError{MCP_INVALID_PARAMS,
                            juce::String(MCP_META_CLIENT_CAPABILITIES) +
                                " is required in params._meta",
                            {},
                            httplib::StatusCode::BadRequest_400};
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Request handling
    // -----------------------------------------------------------------------

    /// Shared between the pool thread that is waiting and the dispatch
    /// completion that may arrive after it has given up. Owned by both, so the
    /// completion never writes into a stack frame that has returned.
    struct Waiter {
        std::mutex mutex;
        std::condition_variable ready;
        bool done = false;
        McpReply reply;
    };

    bool registerWaiter(const std::shared_ptr<Waiter>& waiter) {
        const std::scoped_lock lock(waiterMutex);
        // Synchronises with cancelWaiters(): a handler that reaches this point
        // after stop() has already swept the list must not add an unwakeable
        // waiter behind that sweep.
        if (!running.load())
            return false;
        waiters.erase(std::remove_if(waiters.begin(), waiters.end(),
                                     [](const auto& weak) { return weak.expired(); }),
                      waiters.end());
        waiters.push_back(waiter);
        return true;
    }

    void unregisterWaiter(const std::shared_ptr<Waiter>& waiter) {
        const std::scoped_lock lock(waiterMutex);
        waiters.erase(std::remove_if(waiters.begin(), waiters.end(),
                                     [&](const auto& weak) {
                                         const auto live = weak.lock();
                                         return live == nullptr || live == waiter;
                                     }),
                      waiters.end());
    }

    void cancelWaiters() {
        std::vector<std::shared_ptr<Waiter>> live;
        {
            const std::scoped_lock lock(waiterMutex);
            for (const auto& weak : waiters)
                if (auto waiter = weak.lock())
                    live.push_back(std::move(waiter));
            waiters.clear();
        }
        for (const auto& waiter : live) {
            {
                const std::scoped_lock lock(waiter->mutex);
                if (waiter->done)
                    continue;
                waiter->reply =
                    McpReply::fail(MCP_INTERNAL_ERROR, "Request cancelled while server stopped");
                waiter->done = true;
            }
            waiter->ready.notify_one();
        }
    }

    /**
     * @brief Run one call and write its reply, on this pool thread.
     *
     * Blocking here is the whole design. `dispatch` executes on the message
     * thread and calls back from there; writing the response at that point would
     * put a socket write on the UI's thread. Instead this thread — which owns the
     * connection and has nothing else to do — waits, and writes when it wakes.
     */
    void executeAndReply(const McpEndpoint::Call& call, const juce::var& id,
                         httplib::Response& response) {
        auto waiter = std::make_shared<Waiter>();
        if (!registerWaiter(waiter)) {
            writeJson(response, httplib::StatusCode::ServiceUnavailable_503,
                      jsonRpcError(id, McpError{MCP_INTERNAL_ERROR,
                                                "Server is stopping",
                                                {},
                                                httplib::StatusCode::ServiceUnavailable_503}));
            return;
        }
        endpoint.handle(call, [waiter](McpReply reply) {
            {
                const std::scoped_lock lock(waiter->mutex);
                if (waiter->done)
                    return;
                waiter->reply = std::move(reply);
                waiter->done = true;
            }
            waiter->ready.notify_one();
        });

        {
            std::unique_lock<std::mutex> lock(waiter->mutex);
            // The dispatcher enforces its own deadline, so this is only the
            // backstop for a completion that never arrives at all — a message
            // thread that has stopped running. Without it that would hold a pool
            // thread for the life of the process.
            const auto limit =
                std::chrono::milliseconds(options.defaultDeadlineMs) + std::chrono::seconds(2);
            if (!waiter->ready.wait_for(lock, limit, [&] { return waiter->done; })) {
                unregisterWaiter(waiter);
                writeJson(response, httplib::StatusCode::OK_200,
                          jsonRpcError(id, McpError{MCP_INTERNAL_ERROR,
                                                    "The request was not answered in time"}));
                return;
            }
        }

        unregisterWaiter(waiter);

        const auto& reply = waiter->reply;
        if (reply.failed()) {
            writeJson(response, reply.error->httpStatus, jsonRpcError(id, *reply.error));
            return;
        }
        writeJson(response, httplib::StatusCode::OK_200, jsonRpcResult(id, reply.result));
    }

    void handleListen(const McpEndpoint::Call& call, const juce::var& id,
                      httplib::Response& response) {
        if (subscriptions == nullptr) {
            writeJson(response, httplib::StatusCode::OK_200,
                      jsonRpcError(id, McpError{MCP_INVALID_REQUEST,
                                                "This server does not support subscriptions"}));
            return;
        }

        // A stream pushes the same projections the read operations return, so
        // opening one cannot need less than reading does. Checked here because
        // this path never reaches the dispatcher.
        if (!call.scopes.has(Scope::Read)) {
            audit(call.clientName, call.clientId, call.method, AuditOutcome::Denied,
                  scopeName(Scope::Read));
            writeJson(response, httplib::StatusCode::OK_200,
                      jsonRpcError(id, McpError{MCP_PERMISSION_DENIED,
                                                "subscriptions/listen requires the 'read' "
                                                "permission, which this client has not been "
                                                "granted"}));
            return;
        }

        const auto filter = endpoint.parseListenFilter(call.params);
        auto stream = claimStream(id, call.clientName);
        if (stream == nullptr) {
            writeJson(response, httplib::StatusCode::ServiceUnavailable_503,
                      jsonRpcError(id, McpError{MCP_INTERNAL_ERROR,
                                                "Too many open notification streams",
                                                {},
                                                httplib::StatusCode::ServiceUnavailable_503}));
            return;
        }

        stream->setFilter(filter);
        // Queued before the hub can publish anything, which is how the
        // "acknowledgment first, and no notification before it" rule is kept:
        // the outbox is FIFO, so nothing can overtake a frame already in it.
        stream->push(sseData(endpoint.acknowledgment(filter, id)));
        subscribeStream(stream);
        serveStream(response, stream);
    }

    void handleLegacySubscribe(const McpEndpoint::Call& call, const juce::var& id,
                               httplib::Response& response, bool subscribe) {
        if (subscriptions == nullptr) {
            writeJson(response, httplib::StatusCode::OK_200,
                      jsonRpcError(id, McpError{MCP_INVALID_REQUEST,
                                                "This server does not support subscriptions"}));
            return;
        }

        const auto uri = call.params["uri"].toString();
        if (uri.isEmpty()) {
            writeJson(response, httplib::StatusCode::OK_200,
                      jsonRpcError(
                          id, McpError{MCP_INVALID_PARAMS, juce::String(call.method) +
                                                               " requires a string params.uri"}));
            return;
        }
        // Refusing a resource nothing can invalidate is the honest answer:
        // accepting it would leave the client waiting on its stream forever for
        // an event that has no source.
        if (subscribe && !endpoint.topicForResource(uri).has_value()) {
            writeJson(response, httplib::StatusCode::OK_200,
                      jsonRpcError(id, McpError{MCP_INVALID_PARAMS,
                                                "Resource cannot be subscribed to: " + uri}));
            return;
        }

        std::shared_ptr<EventStream> stream;
        {
            const std::scoped_lock lock(sessionMutex);
            const auto it = sessions.find(call.idempotencyScope.toStdString());
            if (it == sessions.end()) {
                writeJson(response, httplib::StatusCode::NotFound_404,
                          jsonRpcError(id, McpError{MCP_INVALID_REQUEST,
                                                    "Unknown or expired session",
                                                    {},
                                                    httplib::StatusCode::NotFound_404}));
                return;
            }

            auto& uris = it->second.subscribedUris;
            const auto existing = std::find(uris.begin(), uris.end(), uri);
            if (subscribe) {
                if (existing == uris.end())
                    uris.push_back(uri);
            } else if (existing != uris.end()) {
                uris.erase(existing);
            }

            stream = it->second.stream;
            if (stream != nullptr) {
                auto updated = stream->currentFilter();
                updated.resourceSubscriptions = uris;
                stream->setFilter(updated);
            }
        }

        // Outside the session lock: reconciling talks to the hub, which takes
        // its own.
        if (stream != nullptr)
            resubscribeStream(stream);

        writeJson(response, httplib::StatusCode::OK_200, jsonRpcResult(id, makeObject()));
    }

    void handlePost(const httplib::Request& request, httplib::Response& response) {
        collectIdleSessions();

        juce::var parsed;
        if (juce::JSON::parse(
                juce::String::fromUTF8(request.body.data(), static_cast<int>(request.body.size())),
                parsed)
                .failed() ||
            parsed.getDynamicObject() == nullptr) {
            writeJson(response, httplib::StatusCode::BadRequest_400,
                      jsonRpcError({}, McpError{MCP_PARSE_ERROR,
                                                "Malformed JSON",
                                                {},
                                                httplib::StatusCode::BadRequest_400}));
            return;
        }
        const auto id = parsed["id"];

        if (!running.load()) {
            writeJson(response, httplib::StatusCode::ServiceUnavailable_503,
                      jsonRpcError(id, McpError{MCP_INTERNAL_ERROR,
                                                "Server is stopping",
                                                {},
                                                httplib::StatusCode::ServiceUnavailable_503}));
            return;
        }

        // Admission failures still answer the request that was refused. The
        // body is already in memory at this point, so losing its id would make
        // the bridge forward an uncorrelated id:null error and strand the host.
        if (request.body.size() > options.maxBodyBytes) {
            writeJson(response, httplib::StatusCode::PayloadTooLarge_413,
                      jsonRpcError(id, McpError{MCP_INVALID_REQUEST,
                                                "Request body too large",
                                                {},
                                                httplib::StatusCode::PayloadTooLarge_413}));
            return;
        }
        if (!admit()) {
            auditRefusal("rate limit exceeded");
            writeJson(response, httplib::StatusCode::TooManyRequests_429,
                      jsonRpcError(id, McpError{MCP_INVALID_REQUEST,
                                                "Rate limit exceeded",
                                                {},
                                                httplib::StatusCode::TooManyRequests_429}));
            return;
        }

        if (inFlight.fetch_add(1) >= options.maxConcurrentRequests) {
            inFlight.fetch_sub(1);
            writeJson(response, httplib::StatusCode::ServiceUnavailable_503,
                      jsonRpcError(id, McpError{MCP_INVALID_REQUEST,
                                                "Too many requests in flight",
                                                {},
                                                httplib::StatusCode::ServiceUnavailable_503}));
            return;
        }
        struct InFlightGuard {
            std::atomic<int>& counter;
            ~InFlightGuard() {
                counter.fetch_sub(1);
            }
        } guard{inFlight};

        // MCP is JSON-RPC 2.0, and the envelope is checked before anything is
        // routed. Without this a request carrying no `jsonrpc` at all — or a
        // different version — still reaches a tool and mutates the project,
        // then receives a 2.0 reply it never asked for. The WebSocket transport
        // has always rejected these; this one had not.
        // isString() before comparing: JSON has one numeric type, and JUCE
        // renders the number 2.0 as the text "2.0", so a bare toString()
        // comparison accepts `"jsonrpc": 2.0` — which JSON-RPC does not.
        if (!parsed["jsonrpc"].isString() || parsed["jsonrpc"].toString() != "2.0") {
            writeJson(response, httplib::StatusCode::BadRequest_400,
                      jsonRpcError(id, McpError{MCP_INVALID_REQUEST, "jsonrpc must be \"2.0\""}));
            return;
        }

        const auto methodValue = parsed["method"];
        if (!methodValue.isString() || methodValue.toString().isEmpty()) {
            writeJson(response, httplib::StatusCode::BadRequest_400,
                      jsonRpcError(
                          id, McpError{MCP_INVALID_REQUEST, "method must be a non-empty string"}));
            return;
        }
        const auto method = methodValue.toString();

        auto params = parsed["params"];
        if (params.isVoid() || params.isUndefined())
            params = makeObject();
        if (params.getDynamicObject() == nullptr) {
            writeJson(response, httplib::StatusCode::BadRequest_400,
                      jsonRpcError(id, McpError{MCP_INVALID_PARAMS, "params must be an object"}));
            return;
        }

        // A notification has no id and therefore no answer. `202` with no body
        // is the transport's whole contract for one. Nothing this server needs
        // arrives this way — the legacy `notifications/initialized` is
        // acknowledged and discarded, because the session already exists by the
        // time it is sent — but refusing them would break conforming clients
        // that send it.
        if (id.isVoid() || id.isUndefined()) {
            response.status = httplib::StatusCode::Accepted_202;
            return;
        }

        Resolution resolution;
        if (const auto error = resolveEra(request, method, params, resolution)) {
            writeJson(response, error->httpStatus, jsonRpcError(id, *error));
            return;
        }

        if (resolution.era == McpEra::Modern) {
            if (const auto error = validateHeaders(request, method, params, resolution.version)) {
                writeJson(response, error->httpStatus, jsonRpcError(id, *error));
                return;
            }
            if (const auto error = validateModernMeta(params)) {
                writeJson(response, error->httpStatus, jsonRpcError(id, *error));
                return;
            }
        }

        // A minted session has to be announced on the response that created it,
        // and set before any body is written.
        if (resolution.mintedSession)
            response.set_header(kSessionHeader, resolution.sessionId.toStdString());

        McpEndpoint::Call call;
        call.method = method;
        call.params = params;
        call.era = resolution.era;
        call.protocolVersion = resolution.version;
        // The transport's own name for the caller — never `clientInfo`, which
        // the specification says not to authorise on, and which this does not:
        // admission was the bearer token, before any of this ran.
        call.clientId = resolution.sessionId.isNotEmpty() ? handleForSession(resolution.sessionId)
                                                          : juce::String("mcp:stateless");
        call.idempotencyScope = resolution.sessionId;
        // The client's own claim, kept separate from the id above, and used for
        // one thing: choosing which of the user's grants applies (#1860).
        // Resolved per request so revoking one takes effect on the next.
        call.clientName = resolution.clientName;
        call.scopes =
            options.clients != nullptr ? options.clients->scopesFor(call.clientName) : ScopeSet{};

        switch (routeFor(method, resolution.era)) {
            case McpRouting::Stream:
                handleListen(call, id, response);
                return;
            case McpRouting::Subscribe:
                handleLegacySubscribe(call, id, response, true);
                return;
            case McpRouting::Unsubscribe:
                handleLegacySubscribe(call, id, response, false);
                return;
            case McpRouting::Endpoint:
                break;
        }

        executeAndReply(call, id, response);
    }

    /**
     * @brief The legacy standalone notification stream.
     *
     * `405` without a session is the modern answer and the correct one: this
     * revision removed the GET endpoint, so a client that has not established a
     * session has not established that it is speaking a revision where GET means
     * anything.
     */
    void handleGet(const httplib::Request& request, httplib::Response& response) {
        const juce::String sessionId(request.get_header_value(kSessionHeader));
        if (sessionId.isEmpty() || subscriptions == nullptr) {
            response.status = httplib::StatusCode::MethodNotAllowed_405;
            return;
        }
        const auto session = touchSession(sessionId);
        if (!session) {
            response.status = httplib::StatusCode::NotFound_404;
            return;
        }

        // The same read gate the modern `subscriptions/listen` path applies. A
        // bare GET carries no body to answer with, so a client denied here sees
        // a 403 rather than a JSON-RPC error.
        const auto scopes = options.clients != nullptr
                                ? options.clients->scopesFor(session->clientName)
                                : ScopeSet{};
        if (!scopes.has(Scope::Read)) {
            audit(session->clientName, handleForSession(sessionId), "resources.stream",
                  AuditOutcome::Denied, scopeName(Scope::Read));
            response.status = httplib::StatusCode::Forbidden_403;
            return;
        }

        auto stream = claimStream({}, session->clientName);
        if (stream == nullptr) {
            response.status = httplib::StatusCode::ServiceUnavailable_503;
            return;
        }

        {
            const std::scoped_lock lock(sessionMutex);
            const auto it = sessions.find(sessionId.toStdString());
            if (it == sessions.end()) {
                releaseStream(stream);
                response.status = httplib::StatusCode::NotFound_404;
                return;
            }
            // One stream per session. The revision permits several, but a second
            // one would need the hub's events fanned across them without
            // duplication, and a compatibility path does not need to grow that:
            // a client is told plainly rather than quietly given a stream that
            // sees half the events.
            if (it->second.stream != nullptr && !it->second.stream->isClosed()) {
                releaseStream(stream);
                response.status = httplib::StatusCode::Conflict_409;
                return;
            }

            McpEndpoint::ListenFilter filter;
            filter.resourceSubscriptions = it->second.subscribedUris;
            stream->setFilter(filter);
            it->second.stream = stream;
        }

        subscribeStream(stream);
        serveStream(response, stream);
    }

    void handleDelete(const httplib::Request& request, httplib::Response& response) {
        const juce::String sessionId(request.get_header_value(kSessionHeader));
        if (sessionId.isEmpty()) {
            response.status = httplib::StatusCode::MethodNotAllowed_405;
            return;
        }
        response.status = deleteSession(sessionId) ? httplib::StatusCode::NoContent_204
                                                   : httplib::StatusCode::NotFound_404;
    }
};

// ===========================================================================
// RemoteMcpServer
// ===========================================================================

RemoteMcpServer::RemoteMcpServer(RemoteApiService& service, Options options,
                                 SubscriptionHub* subscriptions)
    : impl_(std::make_unique<Impl>(service, std::move(options), subscriptions)) {}

RemoteMcpServer::~RemoteMcpServer() {
    try {
        stop();
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("[RemoteMcpServer] ") + e.what());
    } catch (...) {
        juce::Logger::writeToLog("[RemoteMcpServer] unknown exception during teardown");
    }
}

const char* RemoteMcpServer::endpointPath() {
    return kEndpoint;
}

const McpEndpoint& RemoteMcpServer::endpoint() const {
    return impl_->endpoint;
}

int RemoteMcpServer::streamCount() const {
    const std::scoped_lock lock(impl_->streamMutex);
    return static_cast<int>(impl_->streams.size());
}

int RemoteMcpServer::sessionCount() const {
    const std::scoped_lock lock(impl_->sessionMutex);
    return static_cast<int>(impl_->sessions.size());
}

bool RemoteMcpServer::isRunning() const {
    return impl_->running.load();
}

int RemoteMcpServer::boundPort() const {
    return impl_->port.load();
}

bool RemoteMcpServer::start() {
    if (impl_->running.load())
        return true;

    // An empty token would mean an open listener. There is no configuration
    // that should produce one, so this fails rather than degrades.
    if (impl_->options.bearerToken.isEmpty()) {
        DBG("RemoteMcpServer: refusing to start without a bearer token");
        return false;
    }

    // Leave enough headroom for the route to parse the envelope and correlate
    // an ordinary oversized request with its JSON-RPC id. cpp-httplib remains a
    // hard backstop against arbitrarily large bodies.
    impl_->server.set_payload_max_length(impl_->options.maxBodyBytes + 64 * 1024);

    // Sized rather than defaulted. A request handler blocks its pool thread
    // while the message thread works, and every open SSE stream holds one for
    // its whole lifetime — so with cpp-httplib's default pool, enough
    // subscribers would leave nothing to answer requests with and the endpoint
    // would stall while still accepting connections.
    const auto poolSize = static_cast<std::size_t>(impl_->options.maxStreams +
                                                   impl_->options.maxConcurrentRequests + 2);
    impl_->server.new_task_queue = [poolSize] { return new httplib::ThreadPool(poolSize); };

    impl_->server.Post(kEndpoint,
                       [this](const httplib::Request& request, httplib::Response& response) {
                           if (impl_->authorise(request, response))
                               impl_->handlePost(request, response);
                       });
    impl_->server.Get(kEndpoint,
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (impl_->authorise(request, response))
                              impl_->handleGet(request, response);
                      });
    impl_->server.Delete(kEndpoint,
                         [this](const httplib::Request& request, httplib::Response& response) {
                             if (impl_->authorise(request, response))
                                 impl_->handleDelete(request, response);
                         });

    // Registered before the listener opens and cleared in stop(), so the
    // settings UI can never route a disconnect to a server that has gone away.
    // Two different calls, and they are easy to confuse: bind_to_any_port's
    // second parameter is socket flags, not a port.
    const auto address = impl_->options.address.toStdString();
    auto port = impl_->options.port;
    if (port > 0) {
        if (!impl_->server.bind_to_port(address, port))
            port = 0;
    } else {
        port = impl_->server.bind_to_any_port(address);
    }

    if (port <= 0) {
        DBG("RemoteMcpServer: failed to bind " + impl_->options.address + ":" +
            juce::String(impl_->options.port));
        return false;
    }

    if (impl_->options.clients != nullptr) {
        impl_->options.clients->setDisconnectHandler(
            TRANSPORT_MCP,
            [this](const juce::String& handle) { return impl_->disconnectByHandle(handle); });
    }

    impl_->port.store(port);
    impl_->running.store(true);
    impl_->listener = std::thread([this] { impl_->server.listen_after_bind(); });

    DBG("RemoteMcpServer: listening on http://" + impl_->options.address + ":" +
        juce::String(port) + kEndpoint);
    return true;
}

void RemoteMcpServer::stop() {
    if (!impl_->running.exchange(false))
        return;

    try {
        // First, so nothing can route a disconnect into a server that is tearing
        // its streams and sessions down.
        if (impl_->options.clients != nullptr)
            impl_->options.clients->setDisconnectHandler(TRANSPORT_MCP, nullptr);

        // Requests already dispatched may be waiting for a completion posted to
        // this same message thread. Wake their pool threads before joining them.
        impl_->cancelWaiters();

        // Streams first. Each is a pool thread parked in its content provider, and
        // closing the outbox is what wakes it — `server.stop()` alone would leave
        // them waiting on a condition variable nobody was going to notify.
        std::vector<std::shared_ptr<EventStream>> live;
        {
            const std::scoped_lock lock(impl_->streamMutex);
            live = impl_->streams;
        }
        // `releaseStream` deregisters each from the client registry, so the
        // settings list empties as the streams close rather than keeping rows for
        // connections that no longer exist.
        for (const auto& stream : live)
            impl_->releaseStream(stream);

        {
            std::vector<juce::String> closed;
            {
                const std::scoped_lock lock(impl_->sessionMutex);
                closed.reserve(impl_->sessions.size());
                for (const auto& [key, session] : impl_->sessions)
                    closed.push_back(session.id);
                impl_->sessions.clear();
            }
            impl_->noteSessionsClosed(closed);
        }

        impl_->server.stop();
    } catch (const std::exception& e) {
        juce::Logger::writeToLog(juce::String("[RemoteMcpServer::stop] ") + e.what());
    } catch (...) {
        juce::Logger::writeToLog("[RemoteMcpServer::stop] unknown exception");
    }

    // Must run even if teardown above threw: a still-joinable std::thread
    // calls std::terminate from its own destructor, unconditionally.
    if (impl_->listener.joinable())
        impl_->listener.join();

    impl_->port.store(0);
}

}  // namespace remote
}  // namespace magda
