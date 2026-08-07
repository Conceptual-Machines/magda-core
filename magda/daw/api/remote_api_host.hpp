#pragma once

#include <memory>

#include "remote_api.hpp"

namespace magda {

class AudioEngine;
class MagdaApi;

namespace remote {

class ModelChangeBridge;
class RemoteApiService;
class RemoteWebSocketServer;
class SubscriptionHub;

/**
 * @brief Owns the remote API for the lifetime of a running MAGDA (#1856).
 *
 * The dispatcher, the model bridge that feeds it local edits, and the WebSocket
 * transport are three objects with one lifetime and a required construction
 * order, so something has to own them together. This is that something, and it
 * is the only place in the app that knows the remote API exists at all.
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
 *     {"port":51734,"token":"…","url":"ws://127.0.0.1:51734/rpc","pid":4021}
 *
 * A client reads that file to learn where to connect and what to present. This
 * is the same shape Jupyter uses for its local server, and it means a client on
 * the machine needs no configuration while a process elsewhere gets nothing.
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

    /// The port actually bound, or 0 when not running.
    int boundPort() const;

    /// Where the token was published, whether or not it currently exists.
    juce::File tokenFile() const;

    /// The dispatcher, for adapters that share it — #1858's MCP adapter is the
    /// next one, and it must route through the same instance rather than build
    /// its own.
    RemoteApiService& service();

    /// The subscription hub, for the same reason: MCP resource updates and
    /// WebSocket subscriptions have to be fed by one projection of the model,
    /// not two.
    SubscriptionHub& subscriptions();

  private:
    // Declaration order is destruction order reversed, and it is load-bearing:
    // the server's connections deregister from the hub, the hub listens to the
    // service's change source, and the bridge writes into the service. Each has
    // to outlive the thing that talks to it.
    std::unique_ptr<RemoteApiService> service_;
    std::unique_ptr<ModelChangeBridge> bridge_;
    std::unique_ptr<SubscriptionHub> subscriptions_;
    std::unique_ptr<RemoteWebSocketServer> server_;
    juce::String token_;
};

}  // namespace remote
}  // namespace magda
