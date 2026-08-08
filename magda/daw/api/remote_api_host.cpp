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
bool writeTokenFile(const juce::File& file, const juce::String& token, int port, int mcpPort) {
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
    payload->setProperty("port", port);
    payload->setProperty("token", token);
    payload->setProperty("url", "ws://127.0.0.1:" + juce::String(port) + "/rpc");
    // Named separately because a client picks one: an MCP host wants `mcpUrl`, a
    // script that needs pushed state wants `url`. Present only when the MCP
    // listener actually came up — a URL for a port nothing is answering on is
    // worse than its absence, which a client can at least act on.
    if (mcpPort > 0) {
        payload->setProperty("mcpPort", mcpPort);
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
    if (server_ != nullptr && server_->isRunning())
        return true;

    // Our own record predates this listener, and a record without a listener
    // behind it is worse than none: it hands a client a port and a credential
    // that a crashed previous run left behind, and if the OS has reissued that
    // port it points them at a stranger. Clearing it before every early return —
    // disabled in config, no token, a port already taken — keeps the rule that a
    // record exists only while something is answering on it. The already-running
    // check comes first so a second call cannot delete a record still in use.
    tokenFile().deleteFile();

    // Other instances' records are theirs to remove; only the ones whose process
    // has gone are ours to sweep.
    sweepAbandonedRecords(paths::dataDir(), currentProcessId());

    auto& config = Config::getInstance();
    if (!config.getRemoteApiEnabled())
        return false;

    token_ = generateToken();
    if (token_.isEmpty()) {
        DBG("RemoteApiHost: could not generate a token; not starting");
        return false;
    }

    // The token is a credential that will appear in headers this process logs
    // around. Registering it means any line that quotes one is masked before it
    // reaches the audit log or the app log.
    registerRemoteSecret(token_);

    RemoteWebSocketServer::Options options;
    options.bearerToken = token_;
    options.port = config.getRemoteApiPort();
    options.clients = clients_.get();
    options.audit = audit_;
    for (const auto& origin : config.getRemoteApiAllowedOrigins())
        options.allowedOrigins.push_back(juce::String::fromUTF8(origin.c_str()));

    server_ = std::make_unique<RemoteWebSocketServer>(*service_, options, subscriptions_.get());
    if (!server_->start()) {
        server_.reset();
        forgetRemoteSecret(token_);
        token_ = {};
        return false;
    }

    // The MCP endpoint, on its own port and behind the same token and the same
    // enable flag. Its failure is not fatal to the WebSocket: they are separate
    // listeners precisely so one cannot take the other down, and a MAGDA with a
    // working control socket and no MCP endpoint is a degraded state a user can
    // still work in. The discovery record then simply omits `mcpUrl`.
    RemoteMcpServer::Options mcpOptions;
    mcpOptions.bearerToken = token_;
    mcpOptions.port = config.getRemoteApiMcpPort();
    mcpOptions.allowedOrigins = options.allowedOrigins;
    mcpOptions.clients = clients_.get();
    mcpOptions.audit = audit_;

    mcpServer_ = std::make_unique<RemoteMcpServer>(*service_, mcpOptions, subscriptions_.get());
    if (!mcpServer_->start()) {
        DBG("RemoteApiHost: MCP endpoint did not start; the WebSocket API is unaffected");
        mcpServer_.reset();
    }

    // A running listener whose token nobody can read is useless and still a
    // listener, so a failure to publish takes the servers down with it.
    if (!writeTokenFile(tokenFile(), token_, server_->boundPort(), mcpPort())) {
        // The name, not the path. This line goes to the app log, which users
        // paste into bug reports, and the directory is their home directory.
        DBG("RemoteApiHost: could not write " + redactedFileName(tokenFile()));
        if (mcpServer_ != nullptr) {
            mcpServer_->stop();
            mcpServer_.reset();
        }
        server_->stop();
        server_.reset();
        forgetRemoteSecret(token_);
        token_ = {};
        return false;
    }

    juce::Logger::writeToLog(
        "Remote API listening on ws://127.0.0.1:" + juce::String(server_->boundPort()) + "/rpc");
    if (mcpServer_ != nullptr) {
        juce::Logger::writeToLog(
            "MCP endpoint listening on http://127.0.0.1:" + juce::String(mcpServer_->boundPort()) +
            RemoteMcpServer::endpointPath());
    }
    return true;
}

void RemoteApiHost::stopListening() {
    // Before the hub, like the WebSocket server: an open notification stream is
    // a registered hub client, and the hub must not be shut down underneath one.
    if (mcpServer_ != nullptr) {
        mcpServer_->stop();
        mcpServer_.reset();
    }
    if (server_ != nullptr) {
        server_->stop();
        server_.reset();
    }
    // The token dies with the listener that used it; leaving the file behind
    // would advertise a port that is no longer listening and a credential that
    // no longer works.
    tokenFile().deleteFile();
    forgetRemoteSecret(token_);
    token_ = {};

    // Connections were tracked by the servers that have just gone. Anything
    // still listed refers to a socket that no longer exists — a settings table
    // showing phantom clients, with a disconnect button that would answer
    // false. Grants are untouched: those are the user's decisions, not
    // per-run state.
    if (clients_ != nullptr) {
        for (const auto& client : clients_->connections())
            clients_->noteDisconnected(client.connectionId);
    }
}

bool RemoteApiHost::rotateToken() {
    if (!isRunning())
        return false;

    // A restart rather than a swap of the credential in place. Both servers
    // read `bearerToken` from an immutable Options copy taken at construction,
    // and making it mutable would mean a live comparison against a value
    // changing under the comparing thread — for the one operation where being
    // half-applied is worst. Rebuilding is what guarantees every existing
    // connection is gone: they were admitted by a token that no longer exists.
    stopListening();
    return start();
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
    return server_ != nullptr && server_->isRunning();
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
