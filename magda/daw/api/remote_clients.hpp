#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "remote_scopes.hpp"

namespace magda {
namespace remote {

/// The two transport names, spelled once. They key the disconnect handlers, so
/// a mismatch between what a server registers and what a connection reports is
/// a disconnect button that silently does nothing.
inline constexpr const char* TRANSPORT_WEBSOCKET = "websocket";
inline constexpr const char* TRANSPORT_MCP = "mcp";

/// What a client was granted, and when it was last seen. Persisted.
struct ClientGrant {
    /// Normalised — see `normaliseClientName`. The key, not a display field.
    juce::String name;
    ScopeSet scopes = defaultClientScopes();
    /// Wall-clock milliseconds, for the settings table. Zero when unknown,
    /// which is what a grant restored from an older config looks like.
    juce::int64 firstSeenMs = 0;
    juce::int64 lastSeenMs = 0;

    bool operator==(const ClientGrant&) const = default;
};

/// One live connection. Not persisted: this is gone when MAGDA stops.
struct ConnectedClient {
    /// The transport's own handle for this connection, unique for the run —
    /// `ws:3:7`, `mcp:sess-…`. What `disconnect()` takes, and deliberately not
    /// the client name: a client may hold several connections and the user may
    /// want to drop one.
    juce::String connectionId;
    /// Normalised declared name. Several connections can share one.
    juce::String name;
    /// `websocket` or `mcp`.
    juce::String transport;
    juce::int64 connectedAtMs = 0;
    /// The grant as it stood when this connection was admitted. Shown in the
    /// settings table; never used to authorise, because a grant revoked since
    /// then has to take effect on the next request rather than the next
    /// connection.
    ScopeSet scopesAtConnect;

    bool operator==(const ConnectedClient&) const = default;
};

/**
 * @brief Who has connected, and what the user let them do (#1860).
 *
 * One instance per running MAGDA, owned by `RemoteApiHost` and shared by both
 * transports — the same reason the dispatcher is shared. Two registries would be
 * two answers to "may this client edit", and the settings UI would be editing
 * one of them.
 *
 * ## Grants outlive connections
 *
 * A grant is keyed by the client's normalised name and persists across restarts,
 * because the alternative is asking the user to re-grant on every launch, which
 * teaches them to click yes without reading. A connection is per-run state that
 * exists only to be listed and disconnected.
 *
 * Looking a grant up is therefore what happens on *every request*, not at
 * connect time. That is the whole mechanism behind "revoking a client takes
 * effect without restart": there is no per-connection copy of the permission to
 * go stale.
 *
 * ## Threading
 *
 * Every method is safe from any thread and takes one mutex. Requests arrive on
 * transport threads and the settings UI edits from the message thread, so this
 * is not optional.
 *
 * `onChanged` is invoked outside the lock, on whichever thread caused the
 * change. It exists so the host can persist to config and the UI can repaint;
 * a listener that needs the message thread must hop for itself.
 */
class RemoteClientRegistry {
  public:
    /// Closes one live connection. Registered by each transport for its own
    /// connections, and called with a `connectionId` that transport issued.
    using DisconnectHandler = std::function<bool(const juce::String& connectionId)>;

    RemoteClientRegistry();
    ~RemoteClientRegistry();

    RemoteClientRegistry(const RemoteClientRegistry&) = delete;
    RemoteClientRegistry& operator=(const RemoteClientRegistry&) = delete;

    // -----------------------------------------------------------------------
    // Grants
    // -----------------------------------------------------------------------

    /**
     * @brief What `clientName` may do, registering it read-only on first sight.
     *
     * The hot path: called once per request by both transports. Registering here
     * rather than at connect time is what puts a client in the settings list the
     * moment it asks for anything, including a client that connected before the
     * user opened the dialog.
     *
     * `clientName` is normalised for the caller, so a transport may pass through
     * whatever the client declared.
     */
    ScopeSet scopesFor(const juce::String& clientName);

    /// The same lookup without creating an entry, for a caller that is only
    /// displaying. Nothing when the name has never been seen.
    std::optional<ScopeSet> peekScopes(const juce::String& clientName) const;

    /// Grant exactly this set. `read` is forced on: a client that is connected
    /// at all can read, and a row in the settings table that grants nothing
    /// would be indistinguishable from one that was never granted.
    void setScopes(const juce::String& clientName, ScopeSet scopes);

    /// Drop the stored grant. The client reverts to read-only the next time it
    /// asks for anything; it does not disconnect.
    void forget(const juce::String& clientName);

    /// Every grant, by name.
    std::vector<ClientGrant> grants() const;

    // -----------------------------------------------------------------------
    // Live connections
    // -----------------------------------------------------------------------

    void noteConnected(ConnectedClient client);
    void noteDisconnected(const juce::String& connectionId);

    /// Open connections, in connection order.
    std::vector<ConnectedClient> connections() const;
    int connectionCount() const;

    /**
     * @brief Register how to close a transport's connections.
     *
     * Keyed by transport name so `disconnect()` can route a connection id to the
     * server that owns it. Passing an empty handler deregisters, which every
     * transport must do before it is destroyed.
     */
    void setDisconnectHandler(const juce::String& transport, DisconnectHandler handler);

    /// Close one connection. False when the id is not live, or when the
    /// transport that owns it has already gone away.
    bool disconnect(const juce::String& connectionId);

    /// Close every connection this client holds. Returns how many were closed.
    int disconnectClient(const juce::String& clientName);

    // -----------------------------------------------------------------------
    // Persistence
    // -----------------------------------------------------------------------

    /// Grants only — connections are per-run and are not written anywhere.
    juce::var grantsToJson() const;

    /// Replaces every grant. Unknown scope names are dropped rather than
    /// rejected, so a config from a newer build loads with the scopes this one
    /// understands instead of failing shut.
    void loadGrantsFromJson(const juce::var& value);

    /// Called after any grant change, outside the lock. One at a time.
    void setChangeHandler(std::function<void()> onChanged);

  private:
    void notifyChanged();

    mutable std::mutex mutex_;
    /// Ordered so the settings table and the config file are both stable
    /// between runs rather than reordered by hash seeding.
    std::map<std::string, ClientGrant> grants_;
    std::vector<ConnectedClient> connections_;
    std::map<std::string, DisconnectHandler> disconnectHandlers_;

    std::mutex changeHandlerMutex_;
    std::function<void()> onChanged_;
};

}  // namespace remote
}  // namespace magda
