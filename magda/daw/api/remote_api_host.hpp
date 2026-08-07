#pragma once

#include <memory>

#include "remote_api.hpp"

namespace magda {

class AudioEngine;
class MagdaApi;

namespace remote {

class ModelChangeBridge;
class RemoteApiService;
class RemoteMcpServer;
class RemoteWebSocketServer;
class SubscriptionHub;

/**
 * @brief Owns the remote API for the lifetime of a running MAGDA (#1856, #1858).
 *
 * The dispatcher, the model bridge that feeds it local edits, and the two
 * transports are objects with one lifetime and a required construction order, so
 * something has to own them together. This is that something, and it is the only
 * place in the app that knows the remote API exists at all.
 *
 * Both transports share one dispatcher and one subscription hub. That is the
 * point of the layering rather than an optimisation: two dispatchers would be
 * two revision counters and two undo groupings over one project, and two hubs
 * would be two projections of the same model drifting apart.
 *
 * Construct and destroy on the message thread: `ModelChangeBridge` attaches to
 * the model singletons, which notify there.
 *
 * ## The token
 *
 * Generated per run, never persisted to config. A config file gets copied
 * between machines, committed by accident, and pasted into bug reports; a
 * credential in one is a credential that leaks eventually. Instead the token
 * lives in a file beside the other app data, written with owner-only
 * permissions and deleted on shutdown, carrying the port alongside it:
 *
 *     remote-api-<pid>.json
 *     {"port":51734,"token":"…","url":"ws://127.0.0.1:51734/rpc",
 *      "mcpPort":51735,"mcpUrl":"http://127.0.0.1:51735/mcp","pid":4021}
 *
 * A client reads that file to learn where to connect and what to present. This
 * is the same shape Jupyter uses for its local server, and it means a client on
 * the machine needs no configuration while a process elsewhere gets nothing.
 *
 * Both transports are named because a client picks one: an MCP host wants
 * `mcpUrl`, a script that needs pushed state wants `url`. The token is the same
 * for both — it authenticates the user at the keyboard, not a protocol.
 *
 * The record is named after the process because MAGDA allows more than one
 * instance. One shared file would mean the second instance to start hides the
 * first, and the first to stop deletes the record of the one still running —
 * and with a fixed port, where SO_REUSEPORT lets both listeners bind, a client
 * could hold one instance's token and be routed to the other. A client that
 * finds several records is looking at several running instances and has to
 * choose; each is independently valid for the port it names.
 *
 * A record whose process is gone is debris from a crash, and any instance's
 * `start()` will collect it. An instance only ever deletes its own live record.
 *
 * ## Disabled means disabled
 *
 * `start()` returns false and opens no socket when the feature is off in
 * config, when no token could be generated, or when the port is taken. There is
 * no partial state where MAGDA is listening but unauthenticated.
 */
class RemoteApiHost {
  public:
    /**
     * @brief Construct the remote API over a facade, and optionally an engine.
     *
     * `engine` is what the `meters` subscription reads (#1857) and the only
     * thing it is used for. Passing nothing is a supported configuration, not a
     * degraded one: the whole API works, and `meters` delivers empty samples
     * rather than failing — a host with no audio engine is not something a
     * remote client can detect or act on.
     */
    explicit RemoteApiHost(MagdaApi& api, AudioEngine* engine = nullptr);
    ~RemoteApiHost();

    RemoteApiHost(const RemoteApiHost&) = delete;
    RemoteApiHost& operator=(const RemoteApiHost&) = delete;

    /**
     * @brief Read config, generate a token, publish it, and start listening.
     *
     * A no-op returning false when `Config::getRemoteApiEnabled()` is off, so
     * the caller can always call this and let configuration decide.
     */
    bool start();

    /// Stop listening and remove the token file. Idempotent; the destructor
    /// calls it.
    void stop();

    bool isRunning() const;

    /// The WebSocket port actually bound, or 0 when not running.
    int boundPort() const;

    /// The MCP port actually bound, or 0 when it is not listening. Zero with a
    /// live WebSocket is a supported state, not a broken one — see `start()`.
    int mcpPort() const;

    /// Where the token was published, whether or not it currently exists.
    juce::File tokenFile() const;

    /// The dispatcher, shared by both transports: it must be one instance, or
    /// revisions, undo grouping, and idempotency would each exist twice over one
    /// project.
    RemoteApiService& service();

    /// The subscription hub, for the same reason: MCP resource updates and
    /// WebSocket subscriptions have to be fed by one projection of the model,
    /// not two.
    SubscriptionHub& subscriptions();

  private:
    // Declaration order is destruction order reversed, and it is load-bearing:
    // a transport's connections deregister from the hub, the hub listens to the
    // service's change source, and the bridge writes into the service. Each has
    // to outlive the thing that talks to it.
    std::unique_ptr<RemoteApiService> service_;
    std::unique_ptr<ModelChangeBridge> bridge_;
    std::unique_ptr<SubscriptionHub> subscriptions_;
    std::unique_ptr<RemoteWebSocketServer> server_;
    std::unique_ptr<RemoteMcpServer> mcpServer_;
    juce::String token_;
};

/**
 * @brief The host the running application owns, or nullptr.
 *
 * The settings UI has to be able to turn the remote API on and off while MAGDA
 * is running — a toggle that only took effect after a restart would be a worse
 * answer than no toggle. The dialog is constructed from a static entry point
 * with no context to thread a pointer through, so the one instance the app owns
 * registers itself here.
 *
 * Message thread only, and last-constructed-wins. The application creates
 * exactly one; tests create several in sequence, and each destructor clears this
 * only if it is still the registered one, so an earlier host outliving a later
 * one cannot leave a dangling pointer behind.
 *
 * Returns nullptr in the headless CLI, in tests, and before the app has finished
 * starting — callers must check.
 */
RemoteApiHost* activeHost();

}  // namespace remote
}  // namespace magda
