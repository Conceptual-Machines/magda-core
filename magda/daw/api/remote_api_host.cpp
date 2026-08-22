#include "remote_api_host.hpp"

#include <juce_events/juce_events.h>

#include <random>

#include "AppPaths.hpp"
#include "Config.hpp"
#include "remote_audit.hpp"
#include "remote_clients.hpp"
#include "remote_mcp_server.hpp"
#include "remote_model_bridge.hpp"
#include "remote_service.hpp"
#include "remote_subscriptions.hpp"
#include "remote_websocket_server.hpp"

#if JUCE_WINDOWS
    #include <windows.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>

    #include <cerrno>
    #include <csignal>
#endif

namespace magda {
namespace remote {

namespace {

/**
 * Discovery records are per process, not per installation.
 *
 * MAGDA does not set `moreThanOneInstanceAllowed()` to false, so two instances
 * are a supported thing to run. Sharing one file between them means the second
 * to start hides the first, and the first to stop deletes the record of the one
 * still running. With a fixed port it is worse, because cpp-httplib sets
 * SO_REUSEPORT and both listeners bind: a client then holds one instance's token
 * and may be routed to the other, which does not accept it.
 *
 * Naming the record after the process keeps each instance's file its own, and
 * the pid inside is what makes cleanup safe — an instance deletes only its own,
 * and a record whose process no longer exists is debris anyone may collect.
 */
constexpr const char* kTokenFilePrefix = "remote-api-";
constexpr const char* kTokenFileSuffix = ".json";
constexpr const char* kTokenFilePattern = "remote-api-*.json";

juce::int64 currentProcessId() {
#if JUCE_WINDOWS
    return static_cast<juce::int64>(GetCurrentProcessId());
#else
    return static_cast<juce::int64>(getpid());
#endif
}

bool isProcessAlive(juce::int64 processId) {
#if JUCE_WINDOWS
    auto* handle =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (handle == nullptr)
        return false;
    DWORD exitCode = 0;
    const auto running = GetExitCodeProcess(handle, &exitCode) != 0 && exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return running;
#else
    // Signal 0 runs the existence and permission checks without delivering
    // anything. EPERM means the process is there and belongs to someone else,
    // which still counts as alive.
    return kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM;
#endif
}

/**
 * @brief Delete records left by instances that are no longer running.
 *
 * A crashed run cannot clean up after itself, and with per-process records
 * nobody else was going to. Ours is left alone here — `start()` handles that —
 * and so is every record whose process still answers, because that one belongs
 * to a live instance.
 *
 * A pid can in principle be recycled onto an unrelated process, which would keep
 * one dead record alive a while longer. The cost of that is a file, and the next
 * sweep after the recycled process exits takes it.
 */
void sweepAbandonedRecords(const juce::File& directory, juce::int64 ownProcessId) {
    for (const auto& record :
         directory.findChildFiles(juce::File::findFiles, false, kTokenFilePattern)) {
        const auto owner = record.getFileNameWithoutExtension()
                               .fromLastOccurrenceOf("-", false, false)
                               .getLargeIntValue();
        if (owner <= 0 || owner == ownProcessId)
            continue;
        if (!isProcessAlive(owner))
            record.deleteFile();
    }
}

/**
 * @brief 256 bits of entropy, hex encoded.
 *
 * `std::random_device` rather than `juce::Random`, because this is a credential:
 * JUCE's generator is a seeded PRNG whose output is predictable to anyone who
 * can guess the seed, while `random_device` is backed by the OS entropy source
 * on every platform MAGDA ships to.
 */
juce::String generateToken() {
    std::random_device entropy;
    juce::String token;
    for (int i = 0; i < 8; ++i)
        token += juce::String::toHexString(static_cast<int>(entropy())).paddedLeft('0', 8);
    return token;
}

/**
 * @brief Publish the token where a local client can find it, and only it.
 *
 * Created empty and restricted *before* anything secret goes in, so the token
 * is never briefly world-readable. On Windows the file inherits the ACL of the
 * per-user app data directory, which is already owner-only.
 */
/**
 * @brief Publish whatever is listening, with one credential per transport.
 *
 * A transport's port, URL and token appear together or not at all: a client
 * reads one group and needs all three, and a partial entry is a client that
 * connects to a port with no credential or presents a credential to nothing.
 *
 * `token`/`url`/`port` are the WebSocket's and keep their original names, so a
 * script written before the split still finds what it was reading.
 */
bool writeTokenFile(const juce::File& file, const juce::String& wsToken, int port,
                    const juce::String& mcpToken, int mcpPort) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    if (!file.create().wasOk())
        return false;

#if !JUCE_WINDOWS
    if (chmod(file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR) != 0) {
        // Refuse rather than leave a readable credential behind.
        file.deleteFile();
        return false;
    }
#endif

    auto* payload = new juce::DynamicObject();
    // Each group is present only when its listener actually came up. A URL for a
    // port nothing is answering on is worse than its absence, which a client can
    // at least act on — and since #2142 either transport can legitimately be the
    // one that is off, so this is the normal case rather than a failure.
    if (port > 0 && wsToken.isNotEmpty()) {
        payload->setProperty("port", port);
        payload->setProperty("token", wsToken);
        payload->setProperty("url", "ws://127.0.0.1:" + juce::String(port) + "/rpc");
    }
    // Named separately because a client picks one: an MCP host wants `mcpUrl`, a
    // script that needs pushed state wants `url`. The tokens are separate too,
    // so rotating one transport cannot invalidate the other's sessions.
    if (mcpPort > 0 && mcpToken.isNotEmpty()) {
        payload->setProperty("mcpPort", mcpPort);
        payload->setProperty("mcpToken", mcpToken);
        payload->setProperty("mcpUrl", "http://127.0.0.1:" + juce::String(mcpPort) +
                                           RemoteMcpServer::endpointPath());
    }
    // Whose listener this is. A reader can tell two live instances apart, and
    // cleanup can tell an abandoned record from someone else's live one.
    payload->setProperty("pid", currentProcessId());

    // Write into the file we just restricted, rather than replaceWithText: that
    // writes a temp file and moves it over the target, so the new inode arrives
    // with default permissions and the chmod above is silently undone.
    juce::FileOutputStream stream(file);
    if (!stream.openedOk() ||
        !stream.writeText(juce::JSON::toString(juce::var(payload), false), false, false, nullptr)) {
        file.deleteFile();
        return false;
    }
    stream.flush();
    return true;
}

/// See `activeHost()`. Message thread only, so a plain pointer rather than an
/// atomic — an atomic here would advertise a thread-safety this cannot have,
/// since the object it points at is not safe to use from another thread anyway.
RemoteApiHost* activeHostInstance = nullptr;

}  // namespace

RemoteApiHost* activeHost() {
    return activeHostInstance;
}

RemoteApiHost::RemoteApiHost(MagdaApi& api, AudioEngine* engine)
    : audit_(std::make_shared<RemoteAuditLog>()),
      clients_(std::make_unique<RemoteClientRegistry>()),
      service_(std::make_unique<RemoteApiService>(api)),
      bridge_(std::make_unique<ModelChangeBridge>(*service_)),
      subscriptions_(std::make_unique<SubscriptionHub>(api, *service_)) {
    if (engine != nullptr)
        subscriptions_->setMeterSource(makeLiveMeterSource(*engine));

    service_->setAuditLog(audit_);

    // Grants are restored before the change handler is installed, so loading
    // them does not immediately write them back out again.
    clients_->loadGrantsFromJson(Config::getInstance().getRemoteApiClients());
    clients_->setChangeHandler([this] { persistGrants(); });

    activeHostInstance = this;
}

RemoteApiHost::~RemoteApiHost() {
    stop();

    // Before the members go: the handler captures `this` and the registry is
    // free to notify from a transport thread, so leaving it installed while the
    // object unwinds is a call into a half-destroyed host. `stop()` has already
    // joined the transports, so nothing can be mid-notification here.
    if (clients_ != nullptr)
        clients_->setChangeHandler(nullptr);
    if (service_ != nullptr)
        service_->setAuditLog(nullptr);

    // Only if we are still the registered one. Tests construct hosts in
    // sequence, and an earlier one destructing after a later one was registered
    // must not clear the later one's registration.
    if (activeHostInstance == this)
        activeHostInstance = nullptr;
}

void RemoteApiHost::persistGrants() {
    auto writer = grantWriter_;
    {
        const std::lock_guard<std::mutex> lock(writer->mutex);
        // Snapshotted *under* this lock, not before it. Reading first and
        // storing second leaves the same reordering one level down: a thread
        // can read A, be preempted while another reads B and stores it, then
        // resume and overwrite `latest` with the older A — and, seeing a write
        // already posted, return. The task then persists A and the revocation
        // that produced B is undone.
        //
        // Serialising the read with the store is what makes `latest` actually
        // the latest. It costs holding the registry's lock inside this one,
        // which is safe in this order and only this order: the registry always
        // notifies after releasing its own lock, so nothing ever takes these two
        // the other way round.
        writer->latest = clients_->grantsToJson();
        // Already a write on its way. It reads `latest` when it runs, so it will
        // carry this change too — and posting a second task would be how the
        // older of two snapshots ends up applied last.
        //
        // That reordering is the reason this is a slot rather than a captured
        // value: a transport thread can snapshot, the user can then revoke a
        // scope in the settings dialog and have it saved inline, and the
        // transport's older snapshot would land afterwards and put the revoked
        // grant back — silently, and permanently, because config is what the
        // next launch reads.
        if (writer->posted)
            return;
        writer->posted = true;
    }

    auto apply = [writer] {
        // Held across the whole write, not just the read of `latest`.
        //
        // On the message-thread path there is only ever one of these in flight,
        // but the inline path below has no such guarantee: clearing `posted` and
        // then writing unlocked lets a second transport thread walk in, see
        // `posted` false, and run its own apply while this one is still inside
        // `Config::save()`. Two threads then write a singleton that has no
        // locking of its own — which is exactly the situation the hop exists to
        // avoid, reintroduced on the path that cannot hop.
        //
        // Taken before `mutex` and never the other way round. `persistGrants`
        // only ever takes `mutex`, so a recorder is never blocked behind a file
        // write; it just leaves a newer `latest` for whoever is inside here to
        // pick up.
        const std::lock_guard<std::mutex> applying(writer->applyMutex);

        juce::var latest;
        {
            const std::lock_guard<std::mutex> lock(writer->mutex);
            latest = writer->latest;
            writer->posted = false;
        }
        auto& config = Config::getInstance();
        config.setRemoteApiClients(latest);
        config.save();
    };

    // `Config` is a singleton with no internal locking, and `save()` reads every
    // member of it while the settings pages write them. This is reached from a
    // transport thread whenever a client MAGDA has not seen registers itself, so
    // writing from here would race the UI over the whole config file — for a
    // change that is not urgent by any measure. Hopping also gives the
    // coalescing above one thread to serialise on.
    //
    // Inline when there is no message loop to post to, which is the headless
    // case: a test or a console host, where nothing else is writing config.
    if (juce::MessageManager::existsAndIsCurrentThread() ||
        juce::MessageManager::getInstanceWithoutCreating() == nullptr) {
        apply();
        return;
    }

    // The task captures only the shared slot, so it is safe for it to outlive
    // this host. If the loop is already quitting it is dropped, and the grant
    // is not persisted — the same outcome as the process being killed a moment
    // earlier, and not something worth a synchronous write on shutdown.
    if (!juce::MessageManager::callAsync(apply)) {
        const std::lock_guard<std::mutex> lock(writer->mutex);
        writer->posted = false;
    }
}

bool RemoteApiHost::start() {
    // Other instances' records are theirs to remove; only the ones whose process
    // has gone are ours to sweep.
    sweepAbandonedRecords(paths::dataDir(), currentProcessId());

    // Independently, and neither result gates the other: a machine where the
    // MCP port is taken should still get its WebSocket.
    startTransport(Transport::WebSocket);
    startTransport(Transport::Mcp);

    if (isRunning())
        return true;

    // Nothing came up, and a record exists only while something is answering on
    // it. Our own can still be here from a crashed run under this pid — the
    // sweep above only collects records whose process is gone, and this process
    // is very much alive. Left alone it advertises a port nothing is listening
    // on, and if the OS has reissued that port it points a client at a stranger.
    // `publishRecord` deletes the file when both servers are down.
    publishRecord();
    return false;
}

bool RemoteApiHost::startTransport(Transport transport) {
    if (isRunning(transport))
        return true;

    auto& config = Config::getInstance();
    const auto wanted = transport == Transport::WebSocket ? config.getRemoteApiWebSocketEnabled()
                                                          : config.getRemoteApiMcpEnabled();
    if (!wanted)
        return false;

    auto token = generateToken();
    if (token.isEmpty()) {
        DBG("RemoteApiHost: could not generate a token; not starting");
        return false;
    }

    // The token is a credential that will appear in headers this process logs
    // around. Registering it means any line that quotes one is masked before it
    // reaches the audit log or the app log.
    registerRemoteSecret(token);

    juce::StringArray allowedOrigins;
    for (const auto& origin : config.getRemoteApiAllowedOrigins())
        allowedOrigins.add(juce::String::fromUTF8(origin.c_str()));

    if (transport == Transport::WebSocket) {
        RemoteWebSocketServer::Options options;
        options.bearerToken = token;
        options.port = config.getRemoteApiPort();
        options.clients = clients_.get();
        options.audit = audit_;
        for (const auto& origin : allowedOrigins)
            options.allowedOrigins.push_back(origin);

        server_ = std::make_unique<RemoteWebSocketServer>(*service_, options, subscriptions_.get());
        if (!server_->start()) {
            server_.reset();
            forgetRemoteSecret(token);
            return false;
        }
        wsToken_ = token;
    } else {
        RemoteMcpServer::Options options;
        options.bearerToken = token;
        options.port = config.getRemoteApiMcpPort();
        options.clients = clients_.get();
        options.audit = audit_;
        for (const auto& origin : allowedOrigins)
            options.allowedOrigins.push_back(origin);

        mcpServer_ = std::make_unique<RemoteMcpServer>(*service_, options, subscriptions_.get());
        if (!mcpServer_->start()) {
            DBG("RemoteApiHost: MCP endpoint did not start");
            mcpServer_.reset();
            forgetRemoteSecret(token);
            return false;
        }
        mcpToken_ = token;
    }

    // A running listener whose token nobody can read is useless and still a
    // listener, so a failure to publish takes down the one just started. The
    // other transport keeps its entry, which `publishRecord` rewrote from live
    // state before failing.
    if (!publishRecord()) {
        DBG("RemoteApiHost: could not write " + redactedFileName(tokenFile()));
        stopTransport(transport);
        return false;
    }

    if (transport == Transport::WebSocket) {
        juce::Logger::writeToLog("Remote API listening on ws://127.0.0.1:" +
                                 juce::String(server_->boundPort()) + "/rpc");
    } else {
        juce::Logger::writeToLog(
            "MCP endpoint listening on http://127.0.0.1:" + juce::String(mcpServer_->boundPort()) +
            RemoteMcpServer::endpointPath());
    }
    return true;
}

void RemoteApiHost::stopTransport(Transport transport) {
    if (transport == Transport::WebSocket) {
        if (server_ == nullptr)
            return;
        server_->stop();
        server_.reset();
        forgetRemoteSecret(wsToken_);
        wsToken_ = {};
    } else {
        if (mcpServer_ == nullptr)
            return;
        mcpServer_->stop();
        mcpServer_.reset();
        forgetRemoteSecret(mcpToken_);
        mcpToken_ = {};
    }

    // Rewritten rather than deleted: the other transport may still be up, and
    // its client should not lose the record it re-reads on reconnect.
    // `publishRecord` deletes the file itself once nothing is listening.
    publishRecord();
    forgetConnections(transport);
}

bool RemoteApiHost::publishRecord() {
    const auto file = tokenFile();

    // Nothing listening means no record. One that outlived its listeners hands a
    // client a port a crashed run left behind, and if the OS has reissued it,
    // points them at a stranger.
    if (server_ == nullptr && mcpServer_ == nullptr) {
        file.deleteFile();
        return true;
    }

    return writeTokenFile(file, wsToken_, boundPort(), mcpToken_, mcpPort());
}

void RemoteApiHost::forgetConnections(Transport transport) {
    if (clients_ == nullptr)
        return;

    // Connections were tracked by the server that has just gone. Anything still
    // listed refers to a socket that no longer exists — a settings table showing
    // phantom clients, with a disconnect button that would answer false. Grants
    // are untouched: those are the user's decisions, not per-run state.
    const juce::String name =
        transport == Transport::WebSocket ? TRANSPORT_WEBSOCKET : TRANSPORT_MCP;
    for (const auto& client : clients_->connections())
        if (client.transport == name)
            clients_->noteDisconnected(client.connectionId);
}

void RemoteApiHost::stopListening() {
    // MCP before the WebSocket, and both before the hub: an open notification
    // stream is a registered hub client, and the hub must not be shut down
    // underneath one.
    stopTransport(Transport::Mcp);
    stopTransport(Transport::WebSocket);
}

bool RemoteApiHost::rotateToken(Transport transport) {
    if (!isRunning(transport))
        return false;

    // A restart of that one listener rather than a swap of the credential in
    // place. Both servers read `bearerToken` from an immutable Options copy
    // taken at construction, and making it mutable would mean a live comparison
    // against a value changing under the comparing thread — for the one
    // operation where being half-applied is worst. Rebuilding is what guarantees
    // every existing connection *on this transport* is gone: they were admitted
    // by a token that no longer exists. The other transport is never touched.
    stopTransport(transport);
    return startTransport(transport);
}

void RemoteApiHost::stop() {
    stopListening();

    // Only on the way out. Both of these are one-way — the service retires its
    // execution state and never restores it — so doing them when the user
    // merely switched the feature off would leave a host that can never be
    // started again: the listeners would bind, and every operation through them
    // would answer `Cancelled`.
    //
    // The hub goes first: it is what detaches the change listener the service
    // owns, and it must not outlive the source it is attached to.
    if (subscriptions_ != nullptr)
        subscriptions_->shutdown();
    if (service_ != nullptr)
        service_->shutdown();
}

bool RemoteApiHost::isRunning() const {
    return isRunning(Transport::WebSocket) || isRunning(Transport::Mcp);
}

bool RemoteApiHost::isRunning(Transport transport) const {
    if (transport == Transport::WebSocket)
        return server_ != nullptr && server_->isRunning();
    return mcpServer_ != nullptr && mcpServer_->isRunning();
}

int RemoteApiHost::boundPort() const {
    return server_ != nullptr ? server_->boundPort() : 0;
}

int RemoteApiHost::mcpPort() const {
    return mcpServer_ != nullptr ? mcpServer_->boundPort() : 0;
}

juce::File RemoteApiHost::tokenFile() const {
    return paths::dataDir().getChildFile(juce::String(kTokenFilePrefix) +
                                         juce::String(currentProcessId()) + kTokenFileSuffix);
}

RemoteApiService& RemoteApiHost::service() {
    return *service_;
}

SubscriptionHub& RemoteApiHost::subscriptions() {
    return *subscriptions_;
}

RemoteClientRegistry& RemoteApiHost::clients() {
    return *clients_;
}

RemoteAuditLog& RemoteApiHost::audit() {
    return *audit_;
}

}  // namespace remote
}  // namespace magda
