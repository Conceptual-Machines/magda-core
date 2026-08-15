#include "osc/OscService.hpp"

#include "osc/OscPacket.hpp"

namespace magda::osc {

namespace {

/// The largest a UDP payload can be. Allocated once, when the thread starts.
constexpr int kDatagramBufferSize = 65536;

/// How long a poll waits before checking whether it has been asked to stop.
constexpr int kPollIntervalMs = 100;

/// A packet shorter than one four-byte word cannot be an address.
constexpr int kMinDatagramSize = 4;

}  // namespace

// ============================================================================
// ReceiveLoop
// ============================================================================

/**
 * @brief The thread that reads the socket, and the reason #2096 exists.
 *
 * `juce::OSCReceiver` ran a loop shaped exactly like this one and called
 * `socket->read (buffer, size, false)` — the overload with no sender. The
 * overload used here reports it, which is the whole difference between "MAGDA
 * answers a host the user typed in" and "MAGDA answers whoever is talking".
 *
 * Everything below the read is allocation-free: `parseOscPacket` builds views
 * over this buffer, and `OscRouter::handleMessage` publishes into arrays. The
 * one allocation left is JUCE's — `DatagramSocket::read` builds a `juce::String`
 * for the sender's IP, and `juce::String` has no small-string optimisation — so
 * this path costs one allocation per datagram against the several that came
 * with building an `OSCMessage` per message.
 */
class OscService::ReceiveLoop final : public juce::Thread, private OscPacketReceiver {
  public:
    ReceiveLoop(juce::DatagramSocket& socket, OscRouter& router)
        : juce::Thread("MAGDA OSC"), socket_(socket), router_(router) {}

    ~ReceiveLoop() override {
        // The socket is shut down by the owner before this runs, which is what
        // unblocks a poll that is mid-wait.
        stopThread(2000);
    }

    void run() override {
        buffer_.malloc(kDatagramBufferSize);

        while (!threadShouldExit()) {
            const int ready = socket_.waitUntilReady(true, kPollIntervalMs);
            if (ready < 0 || threadShouldExit())
                return;
            if (ready == 0)
                continue;

            juce::String senderHost;
            // Read and deliberately unused. A surface sends from an ephemeral
            // port and listens on a fixed one, so this number is not where a
            // reply goes and not part of who a peer is — see `OscPeers`. It is
            // here because the overload that reports the host reports both.
            int senderPort = 0;
            const int bytes =
                socket_.read(buffer_.getData(), kDatagramBufferSize, false, senderHost, senderPort);
            if (bytes < kMinDatagramSize)
                continue;

            // Interned before the parse rather than after, so a datagram that
            // turns out to be malformed still counts as the surface being
            // there. "Something is arriving and none of it parses" is a
            // different problem from silence, and the settings UI should be
            // able to tell them apart.
            peer_ = router_.peers().intern(senderHost, juce::Time::currentTimeMillis());

            // Being heard from is not being answered. Feedback opens a sender
            // and streams a whole project at a peer, so enrolling one on the
            // strength of four bytes of anything would make MAGDA a UDP
            // reflector: the source address is spoofable and the default bind
            // is every interface. A peer becomes answerable only once the
            // router has accepted something from it.
            accepted_ = false;
            parseOscPacket(buffer_.getData(), static_cast<std::size_t>(bytes), *this);
            if (accepted_)
                router_.peers().markAnswerable(peer_);
        }
    }

  private:
    void oscMessage(const OscMessageView& message) override {
        // Not short-circuited: every message in a bundle still has to be routed.
        if (router_.handleMessage(message, peer_))
            accepted_ = true;
    }

    juce::DatagramSocket& socket_;
    OscRouter& router_;

    /// Set before each packet is parsed and read only from `oscMessage`, which
    /// the parse calls synchronously on this thread.
    OscPeerId peer_ = kNoOscPeer;
    /// Whether this packet contained anything the router took.
    bool accepted_ = false;
    juce::HeapBlock<char> buffer_;
};

// ============================================================================
// OscService
// ============================================================================

OscService::OscService(std::unique_ptr<OscRouter> router, RegistryAttachment attach)
    : router_(std::move(router)), attachToRegistry_(attach == RegistryAttachment::Attach) {
    jassert(router_ != nullptr);
    if (attachToRegistry_) {
        BindingRegistry::getInstance().addListener(this);
        refreshBindings();
    }
}

OscService::~OscService() {
    Config::getInstance().removeListener(this);
    if (attachToRegistry_)
        BindingRegistry::getInstance().removeListener(this);
    close();
}

void OscService::refreshBindings() {
    auto& registry = BindingRegistry::getInstance();
    auto bindings = registry.bindings(BindingScope::Global);
    auto project = registry.bindings(BindingScope::Project);
    bindings.insert(bindings.end(), project.begin(), project.end());
    router_->updateBindings(bindings);
}

void OscService::bindingRegistryChanged(BindingScope /*scope*/) {
    // Both scopes feed one route table, so which one moved does not matter —
    // and rebuilding the whole thing is what keeps a removed binding from
    // outliving its row.
    refreshBindings();
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
    jassert(receive_ == nullptr);

    auto socket = std::make_unique<juce::DatagramSocket>(/*enableBroadcasting*/ false);
    const bool bound =
        address.isEmpty() ? socket->bindToPort(port) : socket->bindToPort(port, address);
    if (!bound) {
        // Another process holds the port, or the address names no interface on
        // this machine. Either way there is no half-open state to clean up.
        DBG("OscService: could not bind " << address << ":" << port);
        return false;
    }

    auto receive = std::make_unique<ReceiveLoop>(*socket, *router_);
    if (!receive->startThread()) {
        DBG("OscService: could not start the receive thread on " << address << ":" << port);
        return false;
    }

    socket_ = std::move(socket);
    receive_ = std::move(receive);
    requestedPort_ = port;
    boundPort_ = socket_->getBoundPort();
    boundAddress_ = address;
    return true;
}

void OscService::close() {
    const bool wasListening = receive_ != nullptr;

    if (receive_ != nullptr) {
        // Ask first, then shut the socket down: a poll sitting in
        // `waitUntilReady` wakes on the shutdown rather than on the timeout, so
        // this is immediate rather than up to one poll interval.
        receive_->signalThreadShouldExit();
        socket_->shutdown();
        receive_.reset();  // joins
    }
    socket_.reset();

    // The peers behind a socket that has closed are not peers of the one that
    // replaces it: a rebind is a new listener, and whoever was talking to the
    // old one has to be heard again before it is answered.
    //
    // Only when something was actually closed. `close` runs on the way to every
    // rebind and on every unrelated config save, and clearing an already-empty
    // table would still bump the generation, which is the signal the feedback
    // projector rebuilds its surfaces on.
    if (wasListening)
        router_->peers().clear();

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

}  // namespace magda::osc
