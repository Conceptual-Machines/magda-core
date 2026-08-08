#include "remote_clients.hpp"

#include <algorithm>
#include <utility>

namespace magda::remote {
namespace {

juce::int64 nowMs() {
    return juce::Time::getCurrentTime().toMilliseconds();
}

}  // namespace

RemoteClientRegistry::RemoteClientRegistry() = default;
RemoteClientRegistry::~RemoteClientRegistry() = default;

// ===========================================================================
// Grants
// ===========================================================================

ScopeSet RemoteClientRegistry::scopesFor(const juce::String& clientName) {
    const auto key = normaliseClientName(clientName).toStdString();

    bool created = false;
    ScopeSet scopes;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto found = grants_.find(key);
        if (found == grants_.end()) {
            ClientGrant grant;
            grant.name = juce::String(key);
            grant.scopes = defaultClientScopes();
            grant.firstSeenMs = nowMs();
            grant.lastSeenMs = grant.firstSeenMs;
            found = grants_.emplace(key, std::move(grant)).first;
            created = true;
        } else {
            found->second.lastSeenMs = nowMs();
        }
        scopes = found->second.scopes;
    }

    // Only a new client is worth waking the UI and the config writer for.
    // `lastSeenMs` moves on every request, and persisting that would rewrite the
    // config file at request rate for no benefit anyone can see.
    if (created)
        notifyChanged();

    return scopes;
}

std::optional<ScopeSet> RemoteClientRegistry::peekScopes(const juce::String& clientName) const {
    const auto key = normaliseClientName(clientName).toStdString();
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = grants_.find(key);
    if (found == grants_.end())
        return std::nullopt;
    return found->second.scopes;
}

void RemoteClientRegistry::setScopes(const juce::String& clientName, ScopeSet scopes) {
    const auto key = normaliseClientName(clientName).toStdString();
    // Forced rather than validated. A caller asking for a grant without `read`
    // means "this client may write but not look", which no operation set
    // expresses and which would leave a settings row showing an empty grant that
    // still admitted the client.
    scopes.add(Scope::Read);

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto& grant = grants_[key];
        if (grant.name.isEmpty()) {
            grant.name = juce::String(key);
            grant.firstSeenMs = nowMs();
        }
        if (grant.scopes == scopes)
            return;  // Nothing changed; do not churn the config file.
        grant.scopes = scopes;
    }

    notifyChanged();
}

void RemoteClientRegistry::forget(const juce::String& clientName) {
    const auto key = normaliseClientName(clientName).toStdString();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (grants_.erase(key) == 0)
            return;
    }
    notifyChanged();
}

std::vector<ClientGrant> RemoteClientRegistry::grants() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ClientGrant> result;
    result.reserve(grants_.size());
    for (const auto& [key, grant] : grants_)
        result.push_back(grant);
    return result;
}

// ===========================================================================
// Live connections
// ===========================================================================

void RemoteClientRegistry::noteConnected(ConnectedClient client) {
    client.name = normaliseClientName(client.name);
    if (client.connectedAtMs == 0)
        client.connectedAtMs = nowMs();

    const std::lock_guard<std::mutex> lock(mutex_);
    // Replace rather than append on a repeated id. A transport that reuses one
    // would otherwise leave a phantom row that no disconnect could clear,
    // because `noteDisconnected` removes a single entry.
    const auto found = std::find_if(connections_.begin(), connections_.end(),
                                    [&](const ConnectedClient& existing) {
                                        return existing.connectionId == client.connectionId;
                                    });
    if (found != connections_.end())
        *found = std::move(client);
    else
        connections_.push_back(std::move(client));
}

void RemoteClientRegistry::noteDisconnected(const juce::String& connectionId) {
    const std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(),
                                      [&](const ConnectedClient& client) {
                                          return client.connectionId == connectionId;
                                      }),
                       connections_.end());
}

std::vector<ConnectedClient> RemoteClientRegistry::connections() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return connections_;
}

int RemoteClientRegistry::connectionCount() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(connections_.size());
}

void RemoteClientRegistry::setDisconnectHandler(const juce::String& transport,
                                                DisconnectHandler handler) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (handler)
        disconnectHandlers_[transport.toStdString()] = std::move(handler);
    else
        disconnectHandlers_.erase(transport.toStdString());
}

bool RemoteClientRegistry::disconnect(const juce::String& connectionId) {
    DisconnectHandler handler;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto found = std::find_if(
            connections_.begin(), connections_.end(),
            [&](const ConnectedClient& client) { return client.connectionId == connectionId; });
        if (found == connections_.end())
            return false;

        const auto owner = disconnectHandlers_.find(found->transport.toStdString());
        if (owner == disconnectHandlers_.end())
            return false;
        handler = owner->second;
    }

    // Outside the lock: closing a connection ends with the transport calling
    // `noteDisconnected`, which takes this same non-recursive mutex. Whether
    // that happens on this thread or the connection's own is the transport's
    // business, and holding the lock across it would deadlock on the first
    // transport that answered synchronously.
    return handler(connectionId);
}

int RemoteClientRegistry::disconnectClient(const juce::String& clientName) {
    const auto name = normaliseClientName(clientName);

    // Snapshot the ids under the lock, close outside it, for the reason above.
    std::vector<juce::String> ids;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& client : connections_) {
            if (client.name == name)
                ids.push_back(client.connectionId);
        }
    }

    int closed = 0;
    for (const auto& id : ids) {
        if (disconnect(id))
            ++closed;
    }
    return closed;
}

// ===========================================================================
// Persistence
// ===========================================================================

juce::var RemoteClientRegistry::grantsToJson() const {
    juce::Array<juce::var> array;
    for (const auto& grant : grants()) {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("name", grant.name);
        entry->setProperty("scopes", scopesToJson(grant.scopes));
        if (grant.firstSeenMs != 0)
            entry->setProperty("firstSeenMs", grant.firstSeenMs);
        if (grant.lastSeenMs != 0)
            entry->setProperty("lastSeenMs", grant.lastSeenMs);
        array.add(juce::var(entry));
    }
    return array;
}

void RemoteClientRegistry::loadGrantsFromJson(const juce::var& value) {
    std::map<std::string, ClientGrant> loaded;
    if (const auto* array = value.getArray()) {
        for (const auto& entry : *array) {
            const auto name = normaliseClientName(entry["name"].toString());
            // An entry whose name was absent or unusable normalises to
            // `unknown` — a real bucket, so this is a merge rather than a skip.
            // Merging keeps the last grant to name it rather than silently
            // dropping one of two rows that collided.
            auto& grant = loaded[name.toStdString()];
            grant.name = name;
            grant.scopes = scopesFromJson(entry["scopes"]);
            grant.scopes.add(Scope::Read);
            grant.firstSeenMs = static_cast<juce::int64>(entry["firstSeenMs"]);
            grant.lastSeenMs = static_cast<juce::int64>(entry["lastSeenMs"]);
        }
    }

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        grants_ = std::move(loaded);
    }
    // Deliberately silent. This runs during startup, from the same code that
    // would answer the notification by writing the config back out — which would
    // rewrite the file with what was just read from it.
}

void RemoteClientRegistry::setChangeHandler(std::function<void()> onChanged) {
    const std::lock_guard<std::mutex> lock(changeHandlerMutex_);
    onChanged_ = std::move(onChanged);
}

void RemoteClientRegistry::notifyChanged() {
    std::function<void()> handler;
    {
        const std::lock_guard<std::mutex> lock(changeHandlerMutex_);
        handler = onChanged_;
    }
    // Copied and called outside both locks: a handler is free to read grants
    // back, which takes `mutex_`, and free to replace itself, which takes
    // `changeHandlerMutex_`.
    if (handler)
        handler();
}

}  // namespace magda::remote
