#include "osc/OscService.hpp"

namespace magda::osc {

namespace {

/// How deep a nested bundle is followed before it is treated as hostile.
/// Bundles legitimately nest one or two levels; a stream that nests further is
/// either broken or trying to make the receive thread recurse for free.
constexpr int kMaxBundleDepth = 8;

}  // namespace

OscService::OscService(std::unique_ptr<OscRouter> router) : router_(std::move(router)) {
    jassert(router_ != nullptr);
}

OscService::~OscService() {
    Config::getInstance().removeListener(this);
    close();
}

// ============================================================================
// Configuration
// ============================================================================

bool OscService::applyConfig() {
    auto& config = Config::getInstance();

    // Registering here rather than in the constructor means a service that is
    // never started never observes config at all.
    config.removeListener(this);
    config.addListener(this);

    if (!config.getOscEnabled()) {
        close();
        return false;
    }

    const int port = config.getOscReceivePort();
    const auto address = juce::String(config.getOscBindAddress());

    // Already where it should be: leave the socket alone. Rebinding on every
    // config save would drop whatever a surface was in the middle of sending.
    //
    // Compared against what was *asked* for, not what was bound. A configured
    // port of 0 means "any free port", and the two differ by definition — so
    // comparing the bound port would find a mismatch every time and rebind on
    // every unrelated config save.
    if (isListening() && port == requestedPort_ && address == boundAddress_)
        return true;

    close();
    return open(port, address);
}

void OscService::configChanged() {
    applyConfig();
}

// ============================================================================
// Socket lifecycle
// ============================================================================

bool OscService::open(int port, const juce::String& address) {
    jassert(receiver_ == nullptr);

    auto socket = std::make_unique<juce::DatagramSocket>(/*enableBroadcasting*/ false);
    const bool bound =
        address.isEmpty() ? socket->bindToPort(port) : socket->bindToPort(port, address);
    if (!bound) {
        // Another process holds the port, or the address names no interface on
        // this machine. Either way there is no half-open state to clean up.
        DBG("OscService: could not bind " << address << ":" << port);
        return false;
    }

    auto receiver = std::make_unique<juce::OSCReceiver>("MAGDA OSC");
    receiver->addListener(this);
    if (!receiver->connectToSocket(*socket)) {
        DBG("OscService: could not start the receive thread on " << address << ":" << port);
        receiver->removeListener(this);
        return false;
    }

    socket_ = std::move(socket);
    receiver_ = std::move(receiver);
    requestedPort_ = port;
    boundPort_ = socket_->getBoundPort();
    boundAddress_ = address;
    return true;
}

void OscService::close() {
    if (receiver_ != nullptr) {
        // Stop the thread before the socket it reads through disappears.
        receiver_->disconnect();
        receiver_->removeListener(this);
        receiver_.reset();
    }
    socket_.reset();
    requestedPort_ = 0;
    boundPort_ = 0;
    boundAddress_.clear();
}

void OscService::stop() {
    close();
}

int OscService::boundPort() const {
    return isListening() ? boundPort_ : 0;
}

// ============================================================================
// Receive thread
// ============================================================================

void OscService::oscMessageReceived(const juce::OSCMessage& message) {
    router_->handleMessage(message);
}

void OscService::oscBundleReceived(const juce::OSCBundle& bundle) {
    deliverBundle(bundle, 0);
}

void OscService::deliverBundle(const juce::OSCBundle& bundle, int depth) {
    if (depth >= kMaxBundleDepth)
        return;

    for (const auto& element : bundle) {
        if (element.isMessage())
            router_->handleMessage(element.getMessage());
        else if (element.isBundle())
            deliverBundle(element.getBundle(), depth + 1);
    }
}

}  // namespace magda::osc
