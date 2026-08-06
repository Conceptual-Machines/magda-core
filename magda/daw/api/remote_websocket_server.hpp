#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "remote_api.hpp"

namespace magda {
namespace remote {

class RemoteApiService;

/**
 * @brief JSON-RPC 2.0 over WebSocket, on top of `RemoteApiService` (#1856).
 *
 * A transport and nothing more. It parses frames, decides who is allowed to
 * connect, and hands every request to `RemoteApiService::dispatch`. It knows no
 * operation names, owns no schemas, and never touches `TrackManager`,
 * `ClipManager`, or any other model object — a second implementation of DAW
 * operations is exactly what the dispatcher exists to prevent.
 *
 * ## Wire format
 *
 * Requests are ordinary JSON-RPC 2.0:
 *
 *     {"jsonrpc":"2.0","id":7,"method":"tracks.create","params":{...}}
 *
 * `method` is an operation name from the registry and `params` is its input,
 * passed through untouched. Notifications (no `id`) are rejected: every
 * operation produces a revision the client needs, so there is no useful
 * fire-and-forget.
 *
 * Anything the dispatcher needs that is not operation input rides in a
 * top-level `meta` sibling of `params`:
 *
 *     {"jsonrpc":"2.0","id":7,"method":"tracks.rename","params":{...},
 *      "meta":{"expectedRevision":42,"deadlineMs":5000}}
 *
 * It cannot live inside `params`, because operation schemas are declared with
 * `additionalProperties:false` and would reject it as an unknown field. A
 * sibling member is also what JSON-RPC's own extensibility rules expect, and
 * clients that do not send one simply get the defaults.
 *
 * A reply mirrors that shape: `result` is the operation's output verbatim, and
 * `meta` beside it carries `revision` and `apiVersion`. Meta cannot live inside
 * the result, because not every operation returns an object — `tracks.list`
 * answers with a bare array — and wrapping only those would make the reply
 * shape depend on which operation was called.
 *
 * A failure becomes a JSON-RPC `error` whose `data` keeps the MAGDA error code
 * and the revision, so a client that lost an optimistic-concurrency race can
 * resync without a second call.
 *
 * ## Threading
 *
 * The message thread is never used for I/O. The listener runs on its own
 * thread, cpp-httplib gives each connection another, and each connection owns a
 * writer thread that drains an outbox. Reading, parsing, framing, and socket
 * writes therefore all happen away from the message thread; the only hop into
 * it is the one `dispatch` already makes for the handler itself.
 *
 * That hop is why the writer thread exists. A dispatch completion fires on the
 * message thread, and writing the response from there would put a socket write
 * on the UI's thread. Instead the completion appends bytes to the connection's
 * outbox — a mutex and a deque, no I/O — and returns; the writer thread wakes
 * and sends. The reading thread is meanwhile blocked in `read()` and could not
 * have written the reply itself without serializing every client to one
 * in-flight request.
 *
 * ## Lifetime
 *
 * A connection can die with requests still queued against it. Completions
 * capture a `shared_ptr` to per-connection state rather than a raw pointer, and
 * a closed connection's outbox is drained and discarded — the same shape
 * `RemoteApiService` uses for work queued against a retired project.
 *
 * `stop()` closes the listener, marks every connection closed, and joins every
 * thread it started. It is safe to call twice, and the destructor calls it.
 *
 * Shutdown is bounded rather than immediate. cpp-httplib exposes no way to
 * interrupt a reader blocked in `read()` — there is no socket handle on the
 * request or the socket, and `set_socket_options` reaches only the listening
 * socket — and its `close()` reads as well as writes, so calling it from the
 * stopping thread would put two threads in one buffered stream. Each reader
 * therefore notices on its own, within one read timeout of a few seconds.
 *
 * ## What a client has to do
 *
 * **Stay in `read()` between requests.** The server pings every second, and a
 * client sitting in a read answers automatically, which is what keeps the short
 * read timeout from expiring on a live connection. A client that neither sends
 * nor reads produces no traffic at all and will be dropped, because nothing
 * distinguishes it from one that has gone away. This costs a real client
 * nothing — waiting for replies and, once #1857 lands, for pushed changes, is
 * where a client spends its time anyway.
 */
class RemoteWebSocketServer {
  public:
    struct Options {
        /// Loopback by default, and the default is the point: this is a local
        /// control surface, not a service. Binding anywhere else exposes an
        /// unencrypted socket whose only credential is a bearer token.
        juce::String address{"127.0.0.1"};

        /// 0 asks the OS for a free port, which is what the tests use.
        int port = 0;

        /// Required. `start()` fails rather than open an unauthenticated
        /// listener, so there is no configuration that accidentally allows
        /// anonymous access.
        juce::String bearerToken;

        /// Browser origins permitted to upgrade. Empty means no browser may
        /// connect; a native client sends no `Origin` at all and is unaffected.
        /// Absent from a request is not the same as unrecognised — the first is
        /// a non-browser client, the second is a page we did not authorise.
        std::vector<juce::String> allowedOrigins;

        /// Connections are a thread each, so this is a real resource bound
        /// rather than a policy knob. Further connections are refused at the
        /// handshake with 503.
        int maxConnections = 8;

        /// Frames above this are refused and the connection is closed with
        /// `MessageTooBig`. Requests are small; anything large is a mistake or
        /// an attack.
        std::size_t maxFrameBytes = std::size_t{256} * 1024;

        /// Requests accepted per connection before the dispatcher has answered
        /// the earlier ones. Handlers are serialized by the service, so this
        /// bounds how much one client can queue against everyone else. A slot is
        /// held until the reply's bytes are written, not until the handler
        /// returns, so a client that stops reading stops being admitted.
        int maxInFlightPerConnection = 8;

        /// Replies allowed to sit unwritten before further ones are dropped.
        /// The backstop for a client that sends without ever reading: its socket
        /// buffer fills, the writer parks inside send(), and without this the
        /// queue would grow for as long as it kept talking.
        int maxQueuedRepliesPerConnection = 32;

        /// Token-bucket rate limit per connection. Bursts up to
        /// `maxInFlightPerConnection`, then holds this rate.
        double maxRequestsPerSecond = 50.0;

        /// How long a request may wait before the dispatcher abandons it. A
        /// client may ask for less via `meta.deadlineMs`, never for more.
        int defaultDeadlineMs = 10'000;
    };

    RemoteWebSocketServer(RemoteApiService& service, Options options);
    ~RemoteWebSocketServer();

    RemoteWebSocketServer(const RemoteWebSocketServer&) = delete;
    RemoteWebSocketServer& operator=(const RemoteWebSocketServer&) = delete;

    /**
     * @brief Bind and begin accepting.
     *
     * Returns false without opening anything if the token is empty or the port
     * is unavailable. Binding happens synchronously, so `boundPort()` is usable
     * the moment this returns true — a caller does not have to poll for it.
     */
    bool start();

    /// Stop accepting, close live connections, join every thread. Idempotent.
    void stop();

    bool isRunning() const;

    /// The port actually bound, which differs from `Options::port` when 0 was
    /// requested. Zero when not running.
    int boundPort() const;

    /// Connections currently open. For tests and diagnostics.
    int connectionCount() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace remote
}  // namespace magda
