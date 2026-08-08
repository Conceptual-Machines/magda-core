#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "remote_mcp.hpp"

namespace magda {
namespace remote {

class RemoteApiService;
class RemoteAuditLog;
class RemoteClientRegistry;
class SubscriptionHub;

/**
 * @brief MCP over Streamable HTTP, on top of `RemoteApiService` (#1858).
 *
 * A transport and nothing more, the same way `RemoteWebSocketServer` is. It
 * frames HTTP, decides who may connect, and hands every request to
 * `McpEndpoint`, which hands it to the dispatcher. It knows no operation names,
 * owns no schemas, and never touches `TrackManager`, `ClipManager`, or any other
 * model object.
 *
 * ## Its own listener
 *
 * A second `httplib::Server` on a second port rather than routes bolted onto the
 * WebSocket's. Three reasons, in order of how much they cost to get wrong: an
 * SSE stream occupies a connection thread for as long as it is open, so its
 * budget has to be sized separately from the WebSocket's connection cap; a
 * protocol bug in one transport should not take the other's listener down; and
 * the WebSocket server is shipped code that would otherwise need restructuring
 * to hand out its listener. The two share what actually matters — the
 * dispatcher, the subscription hub, the bearer token, and the discovery record.
 *
 * ## Dual era
 *
 * Revision `2026-07-28` made MCP stateless. There is no `initialize`, no
 * `Mcp-Session-Id`, no GET stream; a client declares its version, identity, and
 * capabilities on every request, and opens a notification stream by POSTing
 * `subscriptions/listen` and reading the SSE response.
 *
 * Hosts have not all moved, so this serves both shapes on one endpoint and picks
 * per request:
 *
 * 1. `params._meta` carries a modern protocol version — serve it statelessly.
 * 2. The method is `initialize` — mint a session and serve the legacy revision
 *    it negotiates.
 * 3. An `Mcp-Session-Id` header names a live session — serve that session's
 *    negotiated revision.
 * 4. None of the above — `400` with `-32602` naming the `_meta` a modern request
 *    needs. That reply is itself the signal a dual-era client uses: a
 *    recognisable modern JSON-RPC error means "this server is modern, retry
 *    properly" rather than "fall back to the deprecated transport".
 *
 * A modern request carrying `Mcp-Session-Id` is served statelessly and the
 * header is ignored, which is what the revision requires — no session is minted
 * and none is echoed.
 *
 * ## Streams
 *
 * Both eras end up at the same place: a bounded outbox drained by the connection
 * thread that owns the SSE response, fed by one `SubscriptionHub` client.
 *
 * A subscriber that stops reading fills its outbox and has further events
 * dropped rather than buffered — the hub's own policy, and the reason its sink
 * returns a bool. Because an MCP resource update carries only a URI and never a
 * payload, a dropped event costs nothing but latency: the next one the client
 * does take tells it to re-read, and re-reading was always how it was going to
 * get the state. There is no delta to lose and nothing to resync.
 *
 * Closing the stream is the cancellation. `Last-Event-ID` resumption is not
 * offered and event ids are not emitted, because a stream of "re-read this URI"
 * has nothing to replay.
 *
 * ## Who is connecting
 *
 * The client names itself in `clientInfo.name` — in `params._meta` for a modern
 * request, or in the `initialize` params for a legacy one, where it is recorded
 * on the session and reused for every later request on it. Normalised, that name
 * is the key its permissions are stored under (#1860); a request that carries
 * none is `unknown` and read-only.
 *
 * What "connected" means here is narrower than it is over WebSocket, and it has
 * to be: modern MCP is stateless, so a client that POSTs one tool call has no
 * connection to list or to disconnect. What the settings UI shows is therefore
 * the two things that genuinely persist — legacy sessions and open notification
 * streams. A stateless caller still appears in the client list, with its grant,
 * because that list is keyed by name rather than by connection.
 *
 * ## Threading
 *
 * The message thread is never used for I/O. cpp-httplib runs each connection on
 * a pool thread; a request handler blocks that thread on a condition variable
 * until `RemoteApiService::dispatch` completes on the message thread, then
 * writes the response from the pool thread it was already on. No
 * `MessageManagerLock` is taken and no socket write happens on the message
 * thread.
 *
 * Blocking a pool thread is why the pool is sized here rather than left to
 * cpp-httplib's default: every open SSE stream holds one for its lifetime, so
 * the pool is `maxStreams + maxConcurrentRequests` and `maxStreams` is capped
 * below it. Without that, enough subscribers would starve ordinary requests of
 * threads and the endpoint would stop answering while looking healthy.
 *
 * ## Lifetime
 *
 * `stop()` closes the listener, ends every stream, drops every session, and
 * joins the listener thread. It is safe to call twice, and the destructor calls
 * it. Ending a stream is prompt — the outbox is closed and the provider wakes —
 * rather than bounded by a read timeout the way the WebSocket's readers are.
 */
class RemoteMcpServer {
  public:
    struct Options {
        /// Loopback by default, and the default is the point: the MCP spec says
        /// a local server SHOULD bind only to 127.0.0.1, and the only credential
        /// here is a bearer token over cleartext HTTP.
        juce::String address{"127.0.0.1"};

        /// 0 asks the OS for a free port, which is what the tests use and what
        /// the discovery record exists to communicate.
        int port = 0;

        /// Required. `start()` fails rather than open an unauthenticated
        /// listener.
        juce::String bearerToken;

        /// Browser origins permitted to POST. Empty means no browser may; a
        /// native client sends no `Origin` and is unaffected. Validating this is
        /// a MUST in the MCP transport spec — it is what stops a web page from
        /// reaching a local server through DNS rebinding.
        std::vector<juce::String> allowedOrigins;

        /// Bodies above this are refused. MCP requests are small.
        std::size_t maxBodyBytes = std::size_t{256} * 1024;

        /// Requests executing at once, across all clients. The dispatcher
        /// serializes handlers anyway; this bounds how many threads can be
        /// waiting on it.
        int maxConcurrentRequests = 8;

        /// Concurrent notification streams, modern and legacy together. Each
        /// holds a pool thread for its lifetime.
        int maxStreams = 4;

        /// Live legacy sessions. A modern client creates none.
        int maxSessions = 8;

        /// A legacy session with no request and no stream for this long is
        /// collected. Clients are told to DELETE when they are done, but a
        /// client that crashes never does.
        int sessionIdleSeconds = 300;

        /// Token-bucket rate limit, per remote address.
        double maxRequestsPerSecond = 50.0;

        /// How long a request may wait before the dispatcher abandons it.
        int defaultDeadlineMs = 10'000;

        /// How long a quiet stream waits before emitting an SSE comment. Keeps
        /// intermediaries and client idle timeouts from closing a subscription
        /// that simply has nothing to say yet.
        int keepAliveIntervalMs = 15'000;

        /// Reported in `server/discover` and `initialize`.
        juce::String serverVersion{"1.0"};

        /**
         * Where per-client grants are looked up (#1860).
         *
         * Null means no permission model, which refuses everything rather than
         * allowing it: `RequestContext::scopes` defaults to empty. Must outlive
         * this server.
         */
        RemoteClientRegistry* clients = nullptr;

        /// Where connections and refusals are recorded. Empty disables auditing
        /// for this transport. Shared ownership, for the same reason the
        /// WebSocket transport holds it that way.
        std::shared_ptr<RemoteAuditLog> audit;
    };

    /// `subscriptions` is optional: without one the server does not advertise
    /// the `resources.subscribe` capability, and a client that asks to subscribe
    /// anyway is told so rather than silently receiving nothing.
    RemoteMcpServer(RemoteApiService& service, Options options,
                    SubscriptionHub* subscriptions = nullptr);
    ~RemoteMcpServer();

    RemoteMcpServer(const RemoteMcpServer&) = delete;
    RemoteMcpServer& operator=(const RemoteMcpServer&) = delete;

    /**
     * @brief Bind and begin accepting.
     *
     * Returns false without opening anything if the token is empty or the port
     * is unavailable. Binding is synchronous, so `boundPort()` is usable the
     * moment this returns true.
     */
    bool start();

    /// Stop accepting, end every stream and session, join the listener.
    /// Idempotent.
    void stop();

    bool isRunning() const;

    /// The port actually bound, which differs from `Options::port` when 0 was
    /// requested. Zero when not running.
    int boundPort() const;

    /// The path clients POST to. Constant; exposed so callers building the
    /// discovery record do not hardcode it a second time.
    static const char* endpointPath();

    /// Open notification streams. For tests and diagnostics.
    int streamCount() const;

    /// Live legacy sessions. For tests and diagnostics.
    int sessionCount() const;

    /// The protocol layer, for tests and for anything that needs the tool or
    /// resource catalogue without going over a socket.
    const McpEndpoint& endpoint() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace remote
}  // namespace magda
