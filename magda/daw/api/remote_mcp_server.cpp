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
    return juce::var(new juce::DynamicObject());
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
    EventStream(int outboxCapacity, juce::var streamSubscriptionId)
        : capacity(outboxCapacity), subscriptionId(std::move(streamSubscriptionId)) {}

    enum class Take { Frame, KeepAlive, Closed };

    const int capacity;
    const juce::var subscriptionId;

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
            const std::lock_guard<std::mutex> lock(mutex);
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
            const std::lock_guard<std::mutex> lock(mutex);
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
        if (closed)
            return Take::Closed;
        if (outbox.empty())
            return Take::KeepAlive;
        frame = std::move(outbox.front());
        outbox.pop_front();
        return Take::Frame;
    }

    void close() {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            closed = true;
            outbox.clear();
        }
        ready.notify_all();
    }

    bool isClosed() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return closed;
    }

    void setFilter(McpEndpoint::ListenFilter updated) {
        const std::lock_guard<std::mutex> lock(mutex);
        filter = std::move(updated);
    }

    McpEndpoint::ListenFilter currentFilter() const {
        const std::lock_guard<std::mutex> lock(mutex);
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

    mutable std::mutex streamMutex;
    std::vector<std::shared_ptr<EventStream>> streams;

    mutable std::mutex sessionMutex;
    std::unordered_map<std::string, Session> sessions;

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
        const std::lock_guard<std::mutex> lock(rateMutex);
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
     * @brief Everything that can refuse a client before a handler runs.
     *
     * Validating `Origin` is a MUST in the MCP transport specification, and the
     * reason is DNS rebinding: without it a page the user merely visited could
     * reach this listener from the browser they are already running.
     */
    httplib::Server::HandlerResponse authorise(const httplib::Request& request,
                                               httplib::Response& response) {
        if (!isAuthorised(juce::String(request.get_header_value("Authorization")),
                          options.bearerToken)) {
            response.status = httplib::StatusCode::Unauthorized_401;
            return httplib::Server::HandlerResponse::Handled;
        }

        if (!isOriginAllowed(request.has_header("Origin"),
                             juce::String(request.get_header_value("Origin")),
                             options.allowedOrigins)) {
            // The spec allows a JSON-RPC error body with no id here, and one is
            // more use to a developer than a bare 403.
            writeJson(response, httplib::StatusCode::Forbidden_403,
                      jsonRpcError({}, McpError{MCP_INVALID_REQUEST, "Origin not allowed"}));
            return httplib::Server::HandlerResponse::Handled;
        }

        return httplib::Server::HandlerResponse::Unhandled;
    }

    // -----------------------------------------------------------------------
    // Sessions
    // -----------------------------------------------------------------------

    /// Drop sessions nobody has touched. A client is told to DELETE when it is
    /// done; one that crashes never does, and a session holds a subscription.
    void collectIdleSessionsLocked() {
        const auto now = std::chrono::steady_clock::now();
        const auto limit = std::chrono::seconds(options.sessionIdleSeconds);
        for (auto it = sessions.begin(); it != sessions.end();) {
            const auto attached = it->second.stream != nullptr && !it->second.stream->isClosed();
            if (!attached && now - it->second.lastSeen > limit)
                it = sessions.erase(it);
            else
                ++it;
        }
    }

    std::optional<juce::String> createSession(const juce::String& version) {
        const std::lock_guard<std::mutex> lock(sessionMutex);
        collectIdleSessionsLocked();
        if (static_cast<int>(sessions.size()) >= options.maxSessions)
            return std::nullopt;

        Session session;
        session.id = generateSessionId();
        session.protocolVersion = version;
        session.lastSeen = std::chrono::steady_clock::now();
        const auto id = session.id;
        sessions.emplace(id.toStdString(), std::move(session));
        return id;
    }

    /// The negotiated version for a live session, touching its idle clock.
    std::optional<juce::String> touchSession(const juce::String& id) {
        const std::lock_guard<std::mutex> lock(sessionMutex);
        const auto it = sessions.find(id.toStdString());
        if (it == sessions.end())
            return std::nullopt;
        it->second.lastSeen = std::chrono::steady_clock::now();
        return it->second.protocolVersion;
    }

    bool deleteSession(const juce::String& id) {
        std::shared_ptr<EventStream> stream;
        {
            const std::lock_guard<std::mutex> lock(sessionMutex);
            const auto it = sessions.find(id.toStdString());
            if (it == sessions.end())
                return false;
            stream = it->second.stream;
            sessions.erase(it);
        }
        // Outside the lock: closing wakes the provider thread, which may be in
        // the middle of taking a frame.
        if (stream != nullptr)
            releaseStream(stream);
        return true;
    }

    // -----------------------------------------------------------------------
    // Streams
    // -----------------------------------------------------------------------

    std::shared_ptr<EventStream> claimStream(const juce::var& subscriptionId) {
        const std::lock_guard<std::mutex> lock(streamMutex);
        if (static_cast<int>(streams.size()) >= options.maxStreams)
            return nullptr;
        // Sized against the connection's own reading. Each entry is one short
        // "re-read this URI" line, so the cap is about how far behind a client
        // may fall rather than about memory.
        auto stream = std::make_shared<EventStream>(64, subscriptionId);
        streams.push_back(stream);
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
                [weak](const juce::String&) {
                    // Marking is all a foreign thread may do; the provider
                    // notices on its next wait and unwinds the connection.
                    if (auto live = weak.lock())
                        live->close();
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
        stream->close();

        const std::lock_guard<std::mutex> lock(streamMutex);
        streams.erase(std::remove(streams.begin(), streams.end(), stream), streams.end());
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
        /// True when this request created the session, which is the one response
        /// that has to carry `Mcp-Session-Id`.
        bool mintedSession = false;
    };

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
                return std::nullopt;
            }
            // A legacy version named in `_meta` is a client mixing the two
            // shapes. The version is real, so it is not a version error — it
            // still needs a handshake or a session, so fall through to those.
        }

        if (method == "initialize") {
            resolution.era = McpEra::Legacy;
            resolution.version = negotiateLegacyVersion(params);
            const auto minted = createSession(resolution.version);
            if (!minted) {
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
            resolution.version = *known;
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
        endpoint.handle(call, [waiter](McpReply reply) {
            {
                const std::lock_guard<std::mutex> lock(waiter->mutex);
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
                writeJson(response, httplib::StatusCode::OK_200,
                          jsonRpcError(id, McpError{MCP_INTERNAL_ERROR,
                                                    "The request was not answered in time"}));
                return;
            }
        }

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

        const auto filter = endpoint.parseListenFilter(call.params);
        auto stream = claimStream(id);
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
            const std::lock_guard<std::mutex> lock(sessionMutex);
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
        if (!admit()) {
            writeJson(response, httplib::StatusCode::TooManyRequests_429,
                      jsonRpcError({}, McpError{MCP_INVALID_REQUEST, "Rate limit exceeded"}));
            return;
        }

        if (inFlight.fetch_add(1) >= options.maxConcurrentRequests) {
            inFlight.fetch_sub(1);
            writeJson(
                response, httplib::StatusCode::ServiceUnavailable_503,
                jsonRpcError({}, McpError{MCP_INVALID_REQUEST, "Too many requests in flight"}));
            return;
        }
        struct InFlightGuard {
            std::atomic<int>& counter;
            ~InFlightGuard() {
                counter.fetch_sub(1);
            }
        } guard{inFlight};

        if (request.body.size() > options.maxBodyBytes) {
            writeJson(response, httplib::StatusCode::PayloadTooLarge_413,
                      jsonRpcError({}, McpError{MCP_INVALID_REQUEST, "Request body too large"}));
            return;
        }

        juce::var parsed;
        if (juce::JSON::parse(
                juce::String::fromUTF8(request.body.data(), static_cast<int>(request.body.size())),
                parsed)
                .failed() ||
            parsed.getDynamicObject() == nullptr) {
            writeJson(response, httplib::StatusCode::BadRequest_400,
                      jsonRpcError({}, McpError{MCP_PARSE_ERROR, "Malformed JSON"}));
            return;
        }

        const auto id = parsed["id"];
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
        // The transport's own name for the caller. Never `clientInfo`, which is
        // self-reported and which the specification says not to act on.
        call.clientId = resolution.sessionId.isNotEmpty() ? "mcp:" + resolution.sessionId
                                                          : juce::String("mcp:stateless");
        call.idempotencyScope = resolution.sessionId;

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
        if (!touchSession(sessionId)) {
            response.status = httplib::StatusCode::NotFound_404;
            return;
        }

        auto stream = claimStream({});
        if (stream == nullptr) {
            response.status = httplib::StatusCode::ServiceUnavailable_503;
            return;
        }

        {
            const std::lock_guard<std::mutex> lock(sessionMutex);
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
    stop();
}

const char* RemoteMcpServer::endpointPath() {
    return kEndpoint;
}

const McpEndpoint& RemoteMcpServer::endpoint() const {
    return impl_->endpoint;
}

int RemoteMcpServer::streamCount() const {
    const std::lock_guard<std::mutex> lock(impl_->streamMutex);
    return static_cast<int>(impl_->streams.size());
}

int RemoteMcpServer::sessionCount() const {
    const std::lock_guard<std::mutex> lock(impl_->sessionMutex);
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

    impl_->server.set_pre_routing_handler(
        [this](const httplib::Request& request, httplib::Response& response) {
            return impl_->authorise(request, response);
        });

    impl_->server.set_payload_max_length(impl_->options.maxBodyBytes);

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
                           impl_->handlePost(request, response);
                       });
    impl_->server.Get(kEndpoint,
                      [this](const httplib::Request& request, httplib::Response& response) {
                          impl_->handleGet(request, response);
                      });
    impl_->server.Delete(kEndpoint,
                         [this](const httplib::Request& request, httplib::Response& response) {
                             impl_->handleDelete(request, response);
                         });

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

    // Streams first. Each is a pool thread parked in its content provider, and
    // closing the outbox is what wakes it — `server.stop()` alone would leave
    // them waiting on a condition variable nobody was going to notify.
    std::vector<std::shared_ptr<EventStream>> live;
    {
        const std::lock_guard<std::mutex> lock(impl_->streamMutex);
        live = impl_->streams;
    }
    for (const auto& stream : live)
        impl_->releaseStream(stream);

    {
        const std::lock_guard<std::mutex> lock(impl_->sessionMutex);
        impl_->sessions.clear();
    }

    impl_->server.stop();

    if (impl_->listener.joinable())
        impl_->listener.join();

    impl_->port.store(0);
}

}  // namespace remote
}  // namespace magda
