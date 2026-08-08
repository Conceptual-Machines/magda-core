#pragma once

#include <memory>
#include <mutex>

#include "remote_api.hpp"

namespace magda {

class AudioEngine;
class MagdaApi;

namespace remote {

class ModelChangeBridge;
class RemoteApiService;
class RemoteAuditLog;
class RemoteClientRegistry;
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

    /**
     * @brief Close the listeners and withdraw the credential, permanently.
     *
     * Shuts the dispatcher and the subscription hub down as well, which is
     * one-way: `RemoteApiService::shutdown()` retires its execution state and
     * never comes back. This is the destruction path, and `start()` must not be
     * called afterwards — it would bind listeners around a dispatcher that
     * answers every request with `Cancelled`.
     *
     * Idempotent; the destructor calls it. To turn the feature off and leave it
     * restartable, use `stopListening()`.
     */
    void stop();

    /**
     * @brief Close the listeners and withdraw the credential, reversibly.
     *
     * What the settings toggle needs: no socket and no published token, but the
     * dispatcher, the model bridge, and the subscription hub all still alive, so
     * a later `start()` produces a working server rather than one whose every
     * operation is already cancelled.
     *
     * Idempotent, and safe to call when nothing is listening.
     */
    void stopListening();

    /**
     * @brief Throw the current credential away and issue a new one (#1860).
     *
     * Every connected client is disconnected, because that is what rotation
     * means: a token that still admitted the sessions it was replacing would
     * not have been revoked. Clients reconnect by re-reading the discovery
     * record, which this rewrites before returning — so a well-behaved one
     * recovers on its own and a stale copy of the old token never works again.
     *
     * A no-op returning false when nothing is listening: there is no credential
     * to rotate, and minting one without a listener would publish a record for
     * a port nobody is answering on.
     *
     * Message thread only, like `start()`.
     */
    bool rotateToken();

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

    /**
     * @brief Who may do what, and who is connected right now (#1860).
     *
     * Shared by both transports and edited by the settings dialog. Outlives the
     * listeners deliberately: a grant is the user's decision about a client, and
     * switching the feature off and on again must not silently forget it.
     */
    RemoteClientRegistry& clients();

    /// What remote clients have done this run. Bounded and in memory only.
    RemoteAuditLog& audit();

  private:
    /**
     * @brief Mirror the registry's grants into the config file.
     *
     * Wired as the registry's change handler, so a grant made in the settings
     * dialog and one created by a client connecting for the first time are
     * persisted by the same path — which means this is called from transport
     * threads as well as the message thread.
     */
    void persistGrants();

    /**
     * @brief The pending config write, shared by every thread that asks for one.
     *
     * A slot rather than a value captured per call, because the writes have to
     * be ordered and there is more than one writer. A transport thread can
     * snapshot the grants, the user can then revoke a scope and have that saved,
     * and the transport's older snapshot would otherwise land last and restore
     * what was just revoked — in the file the next launch reads.
     *
     * Whoever finds `posted` false owns the hop; everyone after simply replaces
     * `latest`, which the pending task re-reads when it runs. So the newest
     * state always wins and a burst of changes costs one write.
     *
     * Held by `shared_ptr` so a queued write can outlive this host without
     * reaching into it.
     */
    struct GrantWriter {
        std::mutex mutex;
        juce::var latest;
        bool posted = false;
    };
    std::shared_ptr<GrantWriter> grantWriter_ = std::make_shared<GrantWriter>();
    // Declaration order is destruction order reversed, and it is load-bearing:
    // a transport's connections deregister from the hub, the hub listens to the
    // service's change source, and the bridge writes into the service. Each has
    // to outlive the thing that talks to it.
    //
    // The audit log and the client registry come first because both transports
    // hold raw pointers to them and the dispatcher holds one to the log — so
    // they have to be the last two standing.
    std::unique_ptr<RemoteAuditLog> audit_;
    std::unique_ptr<RemoteClientRegistry> clients_;
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
