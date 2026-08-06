#include "remote_websocket_server.hpp"

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

// The frame cap has to be a compile-time constant, because it governs how much
// cpp-httplib will read into memory *before* a handler ever sees the message.
// Enforcing only in the handler would mean happily buffering 16 MB of a frame we
// intend to reject. `Options::maxFrameBytes` can lower this per server; it
// cannot raise it.
#define CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH (256 * 1024)

// How long a reader may sit in read() before it comes up for air, and therefore
// how long shutdown can take. cpp-httplib exposes no way to interrupt a blocked
// reader — there is no socket handle on the request or the socket, and
// set_socket_options only reaches the listening socket — so this timeout is the
// cancellation quantum. It is short because the server pings every second (see
// kPingIntervalSeconds), which keeps frames arriving from any client that is
// reading, so the timeout only expires on a client that has genuinely stopped.
#define CPPHTTPLIB_WEBSOCKET_READ_TIMEOUT_SECOND 3

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "remote_service.hpp"

namespace magda {
namespace remote {

namespace {

/// The path a client upgrades on. Everything else 404s.
constexpr const char* kEndpoint = "/rpc";

/**
 * How often the server pings an idle client.
 *
 * This is what makes the short read timeout safe. A client sitting in `read()`
 * answers each Ping with a Pong, which cpp-httplib consumes inside the same
 * read, so frames keep arriving and the timeout never fires on a live
 * connection. It is the reason a client must stay in `read()` between requests:
 * one that neither sends nor reads produces no traffic and is indistinguishable
 * from a dead one.
 */
constexpr time_t kPingIntervalSeconds = 1;

// JSON-RPC 2.0 reserves -32768..-32000; -32099..-32000 is the implementation-
// defined slice, which is where MAGDA's own failure modes land.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
constexpr int kNotFound = -32001;
constexpr int kConflict = -32002;
constexpr int kTimeout = -32003;
constexpr int kCancelled = -32004;
constexpr int kTooManyRequests = -32005;

/**
 * MAGDA's error taxonomy onto JSON-RPC's.
 *
 * The four standard codes are used where they genuinely mean the same thing, so
 * a generic JSON-RPC client reports something sensible without knowing MAGDA.
 * The rest have no standard equivalent and take implementation-defined codes;
 * `error.data.code` always carries the MAGDA name regardless, so a client that
 * does know MAGDA never has to reason about the mapping.
 */
int jsonRpcCodeFor(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidRequest:
            return kInvalidRequest;
        case ErrorCode::UnknownOperation:
            return kMethodNotFound;
        case ErrorCode::ValidationFailed:
            return kInvalidParams;
        case ErrorCode::NotFound:
            return kNotFound;
        case ErrorCode::Conflict:
            return kConflict;
        case ErrorCode::Timeout:
            return kTimeout;
        case ErrorCode::Cancelled:
            return kCancelled;
        case ErrorCode::InternalError:
            break;
    }
    return kInternalError;
}

juce::var makeObject() {
    return juce::var(new juce::DynamicObject());
}

void setProperty(juce::var& object, const char* name, const juce::var& value) {
    if (auto* dynamicObject = object.getDynamicObject())
        dynamicObject->setProperty(juce::Identifier(name), value);
}

/// A JSON-RPC failure. `id` is null when the request was unparseable enough
/// that we never learned it, which the spec explicitly allows.
std::string errorReply(const juce::var& id, int code, const juce::String& message,
                       const juce::var& data = {}) {
    auto error = makeObject();
    setProperty(error, "code", code);
    setProperty(error, "message", message);
    if (!data.isVoid())
        setProperty(error, "data", data);

    auto reply = makeObject();
    setProperty(reply, "jsonrpc", "2.0");
    setProperty(reply, "id", id);
    setProperty(reply, "error", error);
    return juce::JSON::toString(reply, true).toStdString();
}

/**
 * A dispatcher response as a JSON-RPC reply.
 *
 * `result` is whatever the operation produced, untouched. The revision and API
 * version go in a `meta` sibling — the mirror of how a request carries its own
 * meta beside `params`, and the only arrangement that works for every
 * operation: `tracks.list` answers with a bare array, which has nowhere to hang
 * a revision, so folding meta into the result would mean wrapping some
 * operations' output and not others'. A client would then have to know which
 * shape each operation returns before it could read either one.
 *
 * On failure the revision goes into `error.data` instead, where JSON-RPC keeps
 * application detail, and where it matters most: a client that lost an
 * optimistic-concurrency race learns the real revision in the message that
 * rejects its write.
 */
std::string replyFor(const juce::var& id, const Response& response) {
    if (!response.ok) {
        auto data = toJson(response.error);
        setProperty(data, "revision", static_cast<juce::int64>(response.revision));
        return errorReply(id, jsonRpcCodeFor(response.error.code), response.error.message, data);
    }

    auto meta = makeObject();
    setProperty(meta, "revision", static_cast<juce::int64>(response.revision));
    setProperty(meta, "apiVersion", juce::String(API_VERSION.data()));

    auto reply = makeObject();
    setProperty(reply, "jsonrpc", "2.0");
    setProperty(reply, "id", id);
    setProperty(reply, "result", response.result);
    setProperty(reply, "meta", meta);
    return juce::JSON::toString(reply, true).toStdString();
}

bool isNumber(const juce::var& value) {
    return value.isInt() || value.isInt64() || value.isDouble();
}

/**
 * @brief A JSON-RPC id as an idempotency key, or empty for an id we will not take.
 *
 * The type has to survive into the key. JSON-RPC treats the number 1 and the
 * string "1" as different identifiers, so collapsing both to `1` lets a client's
 * second write silently receive the first one's cached response instead of
 * executing.
 */
juce::String idempotencyKeyFor(const juce::var& id) {
    if (id.isString())
        return "s:" + id.toString();
    if (isNumber(id))
        return "n:" + id.toString();
    return {};
}

/// Compare in time independent of how much of the token matched, so a client
/// cannot learn the token one byte at a time from response latency.
bool secureEquals(const juce::String& a, const juce::String& b) {
    const auto* lhs = a.toRawUTF8();
    const auto* rhs = b.toRawUTF8();
    const auto lhsLength = std::strlen(lhs);
    const auto rhsLength = std::strlen(rhs);

    unsigned char difference = lhsLength == rhsLength ? 0 : 1;
    for (std::size_t i = 0, count = std::max(lhsLength, rhsLength); i < count; ++i) {
        const auto left = i < lhsLength ? lhs[i] : '\0';
        const auto right = i < rhsLength ? rhs[i] : '\0';
        difference |= static_cast<unsigned char>(left ^ right);
    }
    return difference == 0;
}

}  // namespace

// ===========================================================================
// Connection
// ===========================================================================

/**
 * One client's shared state, owned by a `shared_ptr` so a dispatch completion
 * that outlives the socket has something valid to land on.
 *
 * `socket` belongs to the reading thread. That thread alone may call `read()`
 * or `close()` on it, because `close()` does not merely write a Close frame —
 * it then reads, waiting for the peer's echo, and `SocketStream` buffers
 * internally, so a second reader corrupts the first one's state. Writes are
 * safe from anywhere: every write path in cpp-httplib takes the socket's own
 * write mutex, which is why the writer thread may send while the reader blocks.
 *
 * A completion arriving after the reader has gone finds `closed` set and drops
 * its payload rather than reaching for a stack frame that has returned.
 */
struct Connection {
    Connection(httplib::ws::WebSocket& webSocket, int identifier, int maxQueued, double burst)
        : socket(webSocket),
          id(identifier),
          maxQueuedReplies(maxQueued),
          tokens(burst),
          lastRefill(std::chrono::steady_clock::now()) {}

    httplib::ws::WebSocket& socket;
    const int id;
    const int maxQueuedReplies;

    mutable std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> outbox;
    bool closed = false;
    int inFlight = 0;

    double tokens;
    std::chrono::steady_clock::time_point lastRefill;

    /**
     * @brief Queue one reply, or drop it if the client is not keeping up.
     *
     * A client that sends without ever reading fills the socket's send buffer,
     * parks the writer thread inside `send()`, and would otherwise let the
     * outbox grow without limit. Beyond the cap the payload is dropped: the
     * alternative is unbounded memory held on behalf of a client that is not
     * listening to any of it.
     */
    bool enqueue(std::string payload) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (closed || static_cast<int>(outbox.size()) >= maxQueuedReplies)
                return false;
            outbox.push_back(std::move(payload));
        }
        ready.notify_one();
        return true;
    }

    /**
     * @brief Queue a reply for an admitted request, releasing its slot if it
     *        cannot be queued.
     *
     * Called from the dispatch completion on the message thread, which does no
     * I/O here — appending to a deque is all of it. The slot itself is not
     * released until the bytes are actually written, so a client that stops
     * reading stops being admitted rather than accumulating work.
     */
    void complete(std::string payload) {
        if (!enqueue(std::move(payload)))
            release();
    }

    /// One admitted request has finished with the socket, by being written or
    /// by being dropped.
    void release() {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (inFlight > 0)
                --inFlight;
        }
        ready.notify_one();
    }

    void markClosed() {
        std::deque<std::string> abandoned;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            closed = true;
            abandoned.swap(outbox);
            inFlight -= static_cast<int>(abandoned.size());
            if (inFlight < 0)
                inFlight = 0;
        }
        ready.notify_all();
    }

    /**
     * @brief Take one request slot, or say why not.
     *
     * Every inbound frame comes through here, including one too malformed to
     * have a method: parsing costs work and every reply costs memory, so a
     * client that floods garbage must run out of allowance exactly like one
     * that floods valid requests.
     *
     * The bucket refills at `ratePerSecond` and holds at most one burst, so a
     * client may spend its allowance at once and then settles to the rate.
     * Returns an empty string when the request may proceed.
     */
    juce::String admit(int maxInFlight, double ratePerSecond, double burst) {
        const std::lock_guard<std::mutex> lock(mutex);

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double>(now - lastRefill).count();
        lastRefill = now;
        tokens = std::min(burst, tokens + elapsed * ratePerSecond);

        if (inFlight >= maxInFlight)
            return "too many requests in flight";
        if (tokens < 1.0)
            return "rate limit exceeded";

        tokens -= 1.0;
        ++inFlight;
        return {};
    }
};

// ===========================================================================
// Impl
// ===========================================================================

struct RemoteWebSocketServer::Impl {
    Impl(RemoteApiService& apiService, Options serverOptions)
        : service(apiService), options(std::move(serverOptions)) {}

    RemoteApiService& service;
    const Options options;

    httplib::Server server;
    std::thread listener;
    std::atomic<bool> running{false};
    std::atomic<int> port{0};
    std::atomic<int> nextConnectionId{1};

    mutable std::mutex liveMutex;
    std::vector<std::shared_ptr<Connection>> live;

    /**
     * Connections admitted but not necessarily registered yet.
     *
     * The cap cannot be enforced by counting `live`: authorise() runs before the
     * handshake and registration happens after it, so concurrent upgrades would
     * all look at an empty roster and all be let in. This is claimed atomically
     * at admission and released when the connection's thread returns, which
     * closes that window.
     */
    std::atomic<int> admitted{0};

    int connectionCount() const {
        const std::lock_guard<std::mutex> lock(liveMutex);
        return static_cast<int>(live.size());
    }

    /// True for a request that is actually trying to become a WebSocket on our
    /// endpoint, so a plain GET cannot consume a connection slot it will never
    /// give back.
    bool isUpgradeToEndpoint(const httplib::Request& request) const {
        return request.path == kEndpoint &&
               juce::String(request.get_header_value("Upgrade")).equalsIgnoreCase("websocket") &&
               !request.get_header_value("Sec-WebSocket-Key").empty();
    }

    /**
     * @brief Everything that can refuse a client, before the protocol switches.
     *
     * cpp-httplib consults this ahead of the 101 precisely so authentication can
     * answer with an HTTP status instead. A refusal here leaves the client with
     * a failed HTTP request and no socket, which is what "fail closed" has to
     * mean — rejecting after the upgrade would hand out a connection first and
     * take it away afterwards.
     */
    httplib::Server::HandlerResponse authorise(const httplib::Request& request,
                                               httplib::Response& response) {
        if (!secureEquals(juce::String(request.get_header_value("Authorization")),
                          "Bearer " + options.bearerToken)) {
            response.status = httplib::StatusCode::Unauthorized_401;
            return httplib::Server::HandlerResponse::Handled;
        }

        // A native client sends no Origin at all; only a browser does, and a
        // browser we did not authorise is refused. Treating "absent" as
        // "unrecognised" would lock out every non-browser client.
        if (request.has_header("Origin")) {
            const juce::String origin(request.get_header_value("Origin"));
            const auto& permitted = options.allowedOrigins;
            if (std::find(permitted.begin(), permitted.end(), origin) == permitted.end()) {
                response.status = httplib::StatusCode::Forbidden_403;
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        if (isUpgradeToEndpoint(request)) {
            // Claim the slot here rather than testing a count: between a test and
            // the registration that follows the handshake, every concurrent
            // upgrade would see the same spare capacity.
            if (admitted.fetch_add(1) >= options.maxConnections) {
                admitted.fetch_sub(1);
                response.status = httplib::StatusCode::ServiceUnavailable_503;
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        return httplib::Server::HandlerResponse::Unhandled;
    }

    /// Reply to a frame that never became a request, and give back its slot.
    static void refuse(const std::shared_ptr<Connection>& connection, const juce::var& id, int code,
                       const juce::String& message) {
        connection->enqueue(errorReply(id, code, message));
        connection->release();
    }

    /// Parse one frame and dispatch it, or answer with the reason it failed.
    void handleMessage(const std::shared_ptr<Connection>& connection, const std::string& message) {
        // Admission comes first, before the frame is even parsed. Rejecting
        // later would leave the cheapest thing a client can send — a megabyte of
        // garbage — as the one thing no limit applied to.
        if (const auto refusal =
                connection->admit(options.maxInFlightPerConnection, options.maxRequestsPerSecond,
                                  options.maxInFlightPerConnection);
            refusal.isNotEmpty()) {
            connection->enqueue(errorReply({}, kTooManyRequests, refusal));
            return;
        }

        if (message.size() > options.maxFrameBytes) {
            refuse(connection, {}, kInvalidRequest, "request too large");
            connection->socket.close(httplib::ws::CloseStatus::MessageTooBig, "request too large");
            return;
        }

        juce::var parsed;
        if (juce::JSON::parse(juce::String(message), parsed).failed() ||
            parsed.getDynamicObject() == nullptr) {
            refuse(connection, {}, kParseError, "malformed JSON");
            return;
        }

        const auto id = parsed["id"];
        const auto method = parsed["method"].toString();

        if (parsed["jsonrpc"].toString() != "2.0") {
            refuse(connection, id, kInvalidRequest, "jsonrpc must be \"2.0\"");
            return;
        }
        if (method.isEmpty()) {
            refuse(connection, id, kInvalidRequest, "method is required");
            return;
        }
        // Every operation returns a revision the client needs in order to make
        // its next write, so a notification would be a request whose answer the
        // client cannot do without.
        if (id.isVoid() || id.isUndefined()) {
            refuse(connection, {}, kInvalidRequest, "notifications are not supported");
            return;
        }

        // JSON-RPC allows a string or a number. Anything else has no defined
        // meaning, and accepting it would put a shape in the idempotency key
        // that cannot be told apart from another.
        const auto idKey = idempotencyKeyFor(id);
        if (idKey.isEmpty()) {
            refuse(connection, {}, kInvalidRequest, "id must be a string or a number");
            return;
        }

        auto params = parsed["params"];
        if (params.isVoid() || params.isUndefined())
            params = makeObject();
        if (params.getDynamicObject() == nullptr) {
            refuse(connection, id, kInvalidParams, "params must be an object");
            return;
        }

        RequestContext context;
        context.clientId = "ws:" + juce::String(connection->id);
        // Scoped by connection so two clients reusing id 1 do not collide in the
        // dispatcher's idempotency cache, and typed so that one client's numeric
        // 1 and string "1" — different requests under JSON-RPC — cannot either.
        context.requestId = context.clientId + ":" + idKey;

        // Operation schemas are declared additionalProperties:false, so anything
        // that is not operation input has to arrive beside params rather than
        // inside it.
        const auto meta = parsed["meta"];
        auto deadlineMs = options.defaultDeadlineMs;
        if (meta.getDynamicObject() != nullptr) {
            if (const auto expected = meta["expectedRevision"]; !expected.isVoid()) {
                if (!isNumber(expected)) {
                    refuse(connection, id, kInvalidRequest,
                           "meta.expectedRevision must be a number");
                    return;
                }
                context.expectedRevision =
                    static_cast<Revision>(static_cast<juce::int64>(expected));
            }

            // A client may ask for less time than the server allows, never for
            // more — and never for none. Taking the minimum without checking the
            // sign lets -1 win it, after which a non-positive deadline is read as
            // "no deadline" and the request outlives every bound there is.
            if (const auto requested = meta["deadlineMs"]; !requested.isVoid()) {
                if (!isNumber(requested) || static_cast<int>(requested) <= 0) {
                    refuse(connection, id, kInvalidRequest,
                           "meta.deadlineMs must be a positive number of milliseconds");
                    return;
                }
                deadlineMs = std::min(deadlineMs, static_cast<int>(requested));
            }
        }
        context.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(deadlineMs);

        service.dispatch(method, params, context, [connection, id](Response response) {
            connection->complete(replyFor(id, response));
        });
    }

    /**
     * @brief One client, for as long as it stays connected.
     *
     * Runs on a thread cpp-httplib owns. The writer thread it starts is joined
     * before this returns, because `socket` lives on this stack frame.
     */
    void runConnection(httplib::ws::WebSocket& socket) {
        // Balances the reservation authorise() took before the handshake.
        const struct ReservationGuard {
            std::atomic<int>& count;
            ~ReservationGuard() {
                count.fetch_sub(1);
            }
        } reservation{admitted};

        auto connection = std::make_shared<Connection>(socket, nextConnectionId.fetch_add(1),
                                                       options.maxQueuedRepliesPerConnection,
                                                       options.maxInFlightPerConnection);
        {
            const std::lock_guard<std::mutex> lock(liveMutex);
            live.push_back(connection);
        }

        std::thread writer([connection] {
            while (true) {
                std::string payload;
                {
                    std::unique_lock<std::mutex> lock(connection->mutex);
                    connection->ready.wait(
                        lock, [&] { return connection->closed || !connection->outbox.empty(); });
                    if (connection->closed)
                        break;
                    payload = std::move(connection->outbox.front());
                    connection->outbox.pop_front();
                }
                const auto sent = connection->socket.send(payload);
                // The slot belongs to the request until its bytes are gone, so a
                // client that stops reading stops being admitted instead of
                // queueing more work behind a stalled write.
                connection->release();
                if (!sent)
                    break;
            }
        });

        std::string message;
        while (socket.is_open() && running.load()) {
            if (socket.read(message) == httplib::ws::ReadResult::Fail)
                break;
            handleMessage(connection, message);
        }

        connection->markClosed();
        writer.join();

        {
            const std::lock_guard<std::mutex> lock(liveMutex);
            live.erase(std::remove(live.begin(), live.end(), connection), live.end());
        }
    }
};

// ===========================================================================
// RemoteWebSocketServer
// ===========================================================================

RemoteWebSocketServer::RemoteWebSocketServer(RemoteApiService& service, Options options)
    : impl_(std::make_unique<Impl>(service, std::move(options))) {}

RemoteWebSocketServer::~RemoteWebSocketServer() {
    stop();
}

bool RemoteWebSocketServer::start() {
    if (impl_->running.load())
        return true;

    // An empty token would mean an open listener. There is no configuration
    // that should produce one, so this fails rather than degrades.
    if (impl_->options.bearerToken.isEmpty()) {
        DBG("RemoteWebSocketServer: refusing to start without a bearer token");
        return false;
    }

    impl_->server.set_pre_routing_handler(
        [this](const httplib::Request& request, httplib::Response& response) {
            return impl_->authorise(request, response);
        });

    impl_->server.set_payload_max_length(impl_->options.maxFrameBytes);

    // Keeps traffic flowing on an idle connection so the read timeout stays a
    // shutdown quantum rather than a disconnect. Deliberately no
    // set_websocket_max_missed_pongs: that arms cpp-httplib's heartbeat thread
    // to call close() itself, and close() reads — the exact cross-thread read
    // this design exists to avoid. A dead peer is caught by the read timeout.
    impl_->server.set_websocket_ping_interval(kPingIntervalSeconds);

    impl_->server.WebSocket(kEndpoint, [this](const httplib::Request&, httplib::ws::WebSocket& ws) {
        impl_->runConnection(ws);
    });

    // Two different calls, and they are easy to confuse: bind_to_any_port's
    // second parameter is socket flags, not a port, so passing a configured port
    // there would bind somewhere else entirely.
    const auto address = impl_->options.address.toStdString();
    auto port = impl_->options.port;
    if (port > 0) {
        if (!impl_->server.bind_to_port(address, port))
            port = 0;
    } else {
        port = impl_->server.bind_to_any_port(address);
    }

    if (port <= 0) {
        DBG("RemoteWebSocketServer: failed to bind " + impl_->options.address + ":" +
            juce::String(impl_->options.port));
        return false;
    }

    impl_->port.store(port);
    impl_->running.store(true);
    impl_->listener = std::thread([this] { impl_->server.listen_after_bind(); });

    DBG("RemoteWebSocketServer: listening on ws://" + impl_->options.address + ":" +
        juce::String(port) + kEndpoint);
    return true;
}

void RemoteWebSocketServer::stop() {
    if (!impl_->running.exchange(false))
        return;

    // Only mark the connections; do not touch their sockets. `close()` writes a
    // Close frame and then reads, waiting for the peer's echo, so calling it
    // here would put this thread into the same buffered stream the connection's
    // own thread is already blocked reading. Each reader instead notices within
    // one read timeout and closes its own socket, which is the one thread
    // allowed to.
    {
        const std::lock_guard<std::mutex> lock(impl_->liveMutex);
        for (const auto& connection : impl_->live)
            connection->markClosed();
    }

    impl_->server.stop();

    if (impl_->listener.joinable())
        impl_->listener.join();

    impl_->port.store(0);
}

bool RemoteWebSocketServer::isRunning() const {
    return impl_->running.load();
}

int RemoteWebSocketServer::boundPort() const {
    return impl_->port.load();
}

int RemoteWebSocketServer::connectionCount() const {
    return impl_->connectionCount();
}

}  // namespace remote
}  // namespace magda
