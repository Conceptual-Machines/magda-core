#include "remote_api_host.hpp"

#include <random>

#include "AppPaths.hpp"
#include "Config.hpp"
#include "remote_model_bridge.hpp"
#include "remote_service.hpp"
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
bool writeTokenFile(const juce::File& file, const juce::String& token, int port) {
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

}  // namespace

RemoteApiHost::RemoteApiHost(MagdaApi& api)
    : service_(std::make_unique<RemoteApiService>(api)),
      bridge_(std::make_unique<ModelChangeBridge>(*service_)) {}

RemoteApiHost::~RemoteApiHost() {
    stop();
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

    RemoteWebSocketServer::Options options;
    options.bearerToken = token_;
    options.port = config.getRemoteApiPort();
    for (const auto& origin : config.getRemoteApiAllowedOrigins())
        options.allowedOrigins.push_back(juce::String::fromUTF8(origin.c_str()));

    server_ = std::make_unique<RemoteWebSocketServer>(*service_, options);
    if (!server_->start()) {
        server_.reset();
        token_ = {};
        return false;
    }

    // A running listener whose token nobody can read is useless and still a
    // listener, so a failure to publish takes the server down with it.
    if (!writeTokenFile(tokenFile(), token_, server_->boundPort())) {
        DBG("RemoteApiHost: could not write " + tokenFile().getFullPathName());
        server_->stop();
        server_.reset();
        token_ = {};
        return false;
    }

    juce::Logger::writeToLog(
        "Remote API listening on ws://127.0.0.1:" + juce::String(server_->boundPort()) + "/rpc");
    return true;
}

void RemoteApiHost::stop() {
    if (server_ != nullptr) {
        server_->stop();
        server_.reset();
    }
    // The token dies with the run that generated it; leaving the file behind
    // would advertise a port that is no longer listening and a credential that
    // no longer works.
    tokenFile().deleteFile();
    token_ = {};

    if (service_ != nullptr)
        service_->shutdown();
}

bool RemoteApiHost::isRunning() const {
    return server_ != nullptr && server_->isRunning();
}

int RemoteApiHost::boundPort() const {
    return server_ != nullptr ? server_->boundPort() : 0;
}

juce::File RemoteApiHost::tokenFile() const {
    return paths::dataDir().getChildFile(juce::String(kTokenFilePrefix) +
                                         juce::String(currentProcessId()) + kTokenFileSuffix);
}

RemoteApiService& RemoteApiHost::service() {
    return *service_;
}

}  // namespace remote
}  // namespace magda
