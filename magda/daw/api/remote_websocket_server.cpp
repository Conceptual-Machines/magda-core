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
 * The MAGDA envelope and the JSON-RPC envelope say overlapping things, so
 * rather than nest one inside the other the operation's `result` becomes the
 * JSON-RPC `result` with `revision` and `apiVersion` folded in beside it. On
 * failure the revision goes into `error.data`, where it matters most: a client
 * that lost an optimistic-concurrency race learns what the revision actually is
 * in the same message that rejects its write.
 */
std::string replyFor(const juce::var& id, const Response& response) {
    if (!response.ok) {
        auto data = toJson(response.error);
        setProperty(data, "revision", static_cast<juce::int64>(response.revision));
        return errorReply(id, jsonRpcCodeFor(response.error.code), response.error.message, data);
    }

    auto result = response.result;
    if (result.getDynamicObject() == nullptr) {
        // Operations that answer with a bare value still need somewhere to hang
        // the revision, so they get wrapped rather than losing it.
        auto wrapped = makeObject();
        setProperty(wrapped, "value", result);
        result = wrapped;
    } else {
        result = result.clone();
    }
    setProperty(result, "revision", static_cast<juce::int64>(response.revision));
    setProperty(result, "apiVersion", juce::String(API_VERSION.data()));

    auto reply = makeObject();
    setProperty(reply, "jsonrpc", "2.0");
    setProperty(reply, "id", id);
    setProperty(reply, "result", result);
    return juce::JSON::toString(reply, true).toStdString();
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
 * `socket` is a reference to a `ws::WebSocket` living on the reading thread's
 * stack, so it is only ever touched by the reading thread and by the writer
 * thread that the reading thread joins before returning. A completion arriving
 * afterwards finds `closed` set and drops its payload rather than reaching for
 * a socket that no longer exists.
 */
struct Connection {
    Connection(httplib::ws::WebSocket& webSocket, int identifier, double burst)
        : socket(webSocket),
          id(identifier),
          tokens(burst),
          lastRefill(std::chrono::steady_clock::now()) {}

    httplib::ws::WebSocket& socket;
    const int id;

    mutable std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> outbox;
    bool closed = false;
    int inFlight = 0;

    double tokens;
    std::chrono::steady_clock::time_point lastRefill;

    void send(std::string payload) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (closed)
                return;
            outbox.push_back(std::move(payload));
        }
        ready.notify_one();
    }

    /// Called from the dispatch completion on the message thread. Nothing here
    /// touches the socket — appending to a deque is all the message thread does.
    void complete(std::string payload) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            --inFlight;
            if (closed)
                return;
            outbox.push_back(std::move(payload));
        }
        ready.notify_one();
    }

    void markClosed() {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            closed = true;
        }
        ready.notify_all();
    }

    /**
     * @brief Take one request slot, or say why not.
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

    int connectionCount() const {
        const std::lock_guard<std::mutex> lock(liveMutex);
        return static_cast<int>(live.size());
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

        if (connectionCount() >= options.maxConnections) {
            response.status = httplib::StatusCode::ServiceUnavailable_503;
            return httplib::Server::HandlerResponse::Handled;
        }

        return httplib::Server::HandlerResponse::Unhandled;
    }

    /// Parse one frame and dispatch it, or answer with the reason it failed.
    void handleMessage(const std::shared_ptr<Connection>& connection, const std::string& message) {
        if (message.size() > options.maxFrameBytes) {
            connection->send(errorReply({}, kInvalidRequest, "request too large"));
            connection->socket.close(httplib::ws::CloseStatus::MessageTooBig, "request too large");
            return;
        }

        juce::var parsed;
        if (juce::JSON::parse(juce::String(message), parsed).failed() ||
            parsed.getDynamicObject() == nullptr) {
            connection->send(errorReply({}, kParseError, "malformed JSON"));
            return;
        }

        const auto id = parsed["id"];
        const auto method = parsed["method"].toString();

        if (parsed["jsonrpc"].toString() != "2.0") {
            connection->send(errorReply(id, kInvalidRequest, "jsonrpc must be \"2.0\""));
            return;
        }
        if (method.isEmpty()) {
            connection->send(errorReply(id, kInvalidRequest, "method is required"));
            return;
        }
        // Every operation returns a revision the client needs in order to make
        // its next write, so a notification would be a request whose answer the
        // client cannot do without.
        if (id.isVoid() || id.isUndefined()) {
            connection->send(errorReply({}, kInvalidRequest, "notifications are not supported"));
            return;
        }

        auto params = parsed["params"];
        if (params.isVoid() || params.isUndefined())
            params = makeObject();
        if (params.getDynamicObject() == nullptr) {
            connection->send(errorReply(id, kInvalidParams, "params must be an object"));
            return;
        }

        if (const auto refusal =
                connection->admit(options.maxInFlightPerConnection, options.maxRequestsPerSecond,
                                  options.maxInFlightPerConnection);
            refusal.isNotEmpty()) {
            connection->send(errorReply(id, kTooManyRequests, refusal));
            return;
        }

        RequestContext context;
        context.clientId = "ws:" + juce::String(connection->id);
        // Scoped by connection so two clients reusing id 1 do not collide in the
        // dispatcher's idempotency cache.
        context.requestId = context.clientId + ":" + id.toString();

        // Operation schemas are declared additionalProperties:false, so anything
        // that is not operation input has to arrive beside params rather than
        // inside it.
        const auto meta = parsed["meta"];
        auto deadlineMs = options.defaultDeadlineMs;
        if (meta.getDynamicObject() != nullptr) {
            if (const auto expected = meta["expectedRevision"]; !expected.isVoid())
                context.expectedRevision =
                    static_cast<Revision>(static_cast<juce::int64>(expected));
            if (const auto requested = meta["deadlineMs"]; !requested.isVoid())
                deadlineMs = std::min(deadlineMs, static_cast<int>(requested));
        }
        if (deadlineMs > 0)
            context.deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(deadlineMs);

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
        auto connection = std::make_shared<Connection>(socket, nextConnectionId.fetch_add(1),
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
                if (!connection->socket.send(payload))
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

    /// Close every live client so their reading threads leave `read()`. Stopping
    /// the acceptor alone would leave connected clients running indefinitely.
    void closeLiveConnections() {
        const std::lock_guard<std::mutex> lock(liveMutex);
        for (const auto& connection : live) {
            connection->markClosed();
            connection->socket.close(httplib::ws::CloseStatus::GoingAway, "server shutting down");
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

    impl_->server.stop();
    impl_->closeLiveConnections();

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
