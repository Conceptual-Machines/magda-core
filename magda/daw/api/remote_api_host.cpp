#include "remote_api_host.hpp"

#include <random>

#include "AppPaths.hpp"
#include "Config.hpp"
#include "remote_model_bridge.hpp"
#include "remote_service.hpp"
#include "remote_websocket_server.hpp"

#if !JUCE_WINDOWS
    #include <sys/stat.h>
#endif

namespace magda {
namespace remote {

namespace {

constexpr const char* kTokenFileName = "remote-api.json";

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
    auto& config = Config::getInstance();
    if (!config.getRemoteApiEnabled())
        return false;

    if (server_ != nullptr && server_->isRunning())
        return true;

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
    return paths::dataDir().getChildFile(kTokenFileName);
}

RemoteApiService& RemoteApiHost::service() {
    return *service_;
}

}  // namespace remote
}  // namespace magda
