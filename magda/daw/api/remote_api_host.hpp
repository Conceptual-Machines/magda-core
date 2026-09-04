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
 * Owns the dispatcher, the model bridge that feeds it local edits, and the two
 * transports, which share one required construction order. Both transports
 * share one dispatcher and one subscription hub, not as an optimization: two
 * dispatchers would be two revision counters and two undo groupings over one
 * project, and two hubs would be two projections of the model drifting apart.
 *
 * Construct and destroy on the message thread: `ModelChangeBridge` attaches to
 * the model singletons, which notify there.
 *
 * The auth token is generated per run, never persisted to config, and lives in
 * an owner-only, per-process discovery file deleted on shutdown (see
 * docs/architecture/remote-api-host.md for the file format and why). `start()`
 * returns false and opens no socket when the feature is off, no token could be
 * generated, or the port is taken -- there is no partial state where MAGDA is
 * listening but unauthenticated.
 */
/// Which listener. Both are carried by one `RemoteApiHost`, but they are
/// started, stopped, and credentialled independently (#2142).
enum class Transport { WebSocket, Mcp };

class RemoteApiHost {
  public:
    /**
     * @brief Construct the remote API over a facade, and optionally an engine.
     *
     * `engine` is what the `meters` subscription reads (#1857) and the only
     * thing it is used for. Passing nothing is supported, not degraded: the
     * whole API works and `meters` delivers empty samples instead of failing.
     */
    explicit RemoteApiHost(MagdaApi& api, AudioEngine* engine = nullptr);
    ~RemoteApiHost();

    RemoteApiHost(const RemoteApiHost&) = delete;
    RemoteApiHost& operator=(const RemoteApiHost&) = delete;

    /**
     * @brief Start whichever transports config enables, and publish the record.
     *
     * Each transport is independent: a disabled one is skipped, and a failed
     * bind does not take the other down. Returns true when at least one
     * listener came up.
     */
    bool start();

    /**
     * @brief Open one transport, leaving the other exactly as it is (#2142).
     *
     * Mints that transport's credential, binds its port, and rewrites the
     * discovery record to add it without touching the other transport's
     * entry. Returns false when config has it off, it is already running, or
     * the port could not be bound. Message thread only, like `start()`.
     */
    bool startTransport(Transport transport);

    /**
     * @brief Close one transport, leaving the other listening.
     *
     * Withdraws its credential and discovery-record entry, and drops its
     * connections. Idempotent. Removes the record entirely once neither
     * transport is up.
     */
    void stopTransport(Transport transport);

    /**
     * @brief Close the listeners and withdraw the credential, permanently.
     *
     * Also shuts the dispatcher and subscription hub down, one-way
     * (`RemoteApiService::shutdown()` never comes back) -- this is the
     * destruction path, and `start()` must not be called afterwards. Use
     * `stopListening()` to turn the feature off and leave it restartable.
     */
    void stop();

    /**
     * @brief Close the listeners and withdraw the credential, reversibly.
     *
     * What the settings toggle needs: no socket, no published token, but the
     * dispatcher, model bridge, and subscription hub stay alive so a later
     * `start()` works again. Idempotent.
     */
    void stopListening();

    /**
     * @brief Throw the current credential away and issue a new one (#1860).
     *
     * Every connected client is disconnected, since that is what rotation
     * means. Clients reconnect by re-reading the discovery record, rewritten
     * before this returns. A no-op returning false when this transport is
     * not listening. Each transport holds a separate token (#2142), so
     * re-credentialling one doesn't drop the other's clients -- that buys
     * granularity, not isolation, since both still live in the same
     * owner-only file. Message thread only, like `start()`.
     */
    bool rotateToken(Transport transport);

    /// Whether anything is listening.
    bool isRunning() const;

    /// Whether that particular transport is listening.
    bool isRunning(Transport transport) const;

    /// The WebSocket port actually bound, or 0 when not running.
    int boundPort() const;

    /// The MCP port actually bound, or 0 when not listening. Zero with a live
    /// WebSocket is a supported state, not a broken one -- see `start()`.
    int mcpPort() const;

    /// Where the token was published, whether or not it currently exists.
    juce::File tokenFile() const;

    /// The dispatcher, shared by both transports so revisions, undo grouping,
    /// and idempotency each exist once per project, not twice.
    RemoteApiService& service();

    /// The subscription hub, for the same reason: one projection of the
    /// model feeding both MCP resource updates and WebSocket subscriptions.
    SubscriptionHub& subscriptions();

    /**
     * @brief Who may do what, and who is connected right now (#1860).
     *
     * Shared by both transports and edited by the settings dialog. Outlives
     * the listeners deliberately: switching the feature off and on must not
     * silently forget a grant.
     */
    RemoteClientRegistry& clients();

    /// What remote clients have done this run. Bounded and in memory only.
    RemoteAuditLog& audit();

  private:
    /**
     * @brief Mirror the registry's grants into the config file.
     *
     * Wired as the registry's change handler, so a grant from the settings
     * dialog and one from a client's first connection are persisted by the
     * same path -- called from transport threads as well as the message
     * thread.
     */
    void persistGrants();

    /**
     * @brief The pending config write, shared by every thread that asks for one.
     *
     * A slot rather than a value captured per call, because writes must be
     * ordered across more than one writer. See
     * docs/architecture/remote-api-host.md for the two-mutex protocol.
     * Held by `shared_ptr` so a queued write can outlive this host.
     */
    struct GrantWriter {
        /// Guards `latest` and `posted`. Held briefly, never across I/O.
        std::mutex mutex;
        /// Serializes the config write itself. Always taken before `mutex`,
        /// never after.
        std::mutex applyMutex;
        juce::var latest;
        bool posted = false;
    };
    std::shared_ptr<GrantWriter> grantWriter_ = std::make_shared<GrantWriter>();

    // Declaration order is destruction order reversed, and it is load-bearing.
    // See docs/architecture/remote-api-host.md ("Member declaration order").
    std::shared_ptr<RemoteAuditLog> audit_;
    std::unique_ptr<RemoteClientRegistry> clients_;
    std::unique_ptr<RemoteApiService> service_;
    std::unique_ptr<ModelChangeBridge> bridge_;
    std::unique_ptr<SubscriptionHub> subscriptions_;
    std::unique_ptr<RemoteWebSocketServer> server_;
    std::unique_ptr<RemoteMcpServer> mcpServer_;

    /// One credential per transport. Separate so either can be rotated alone;
    /// see `rotateToken`.
    juce::String wsToken_;
    juce::String mcpToken_;

    /**
     * @brief Rewrite the discovery record from whatever is listening right now.
     *
     * Called after every start, stop, and rotation: the record is a
     * projection of live state, not of config, so a port in it that nothing
     * answers on is exactly the failure mode it exists to prevent. Deletes
     * the file when neither transport is up. Returns false when the record
     * could not be written, which callers treat as fatal to the listeners
     * they just brought up.
     */
    bool publishRecord();

    /// Drop the connections one transport was carrying.
    void forgetConnections(Transport transport);
};

/**
 * @brief The host the running application owns, or nullptr.
 *
 * The settings UI needs to toggle the remote API at runtime, so the one
 * instance the app owns registers itself here rather than being threaded
 * through the dialog's static entry point.
 *
 * Message thread only, last-constructed-wins: the application creates
 * exactly one, tests create several in sequence, and each destructor clears
 * this only if it is still the registered one. Returns nullptr in the
 * headless CLI, in tests, and before the app has finished starting.
 */
RemoteApiHost* activeHost();

}  // namespace remote
}  // namespace magda
