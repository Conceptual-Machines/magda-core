// Socket lifecycle for the OSC control-surface listener (#1757), and the real
// datagrams that reach it.
//
// Lives in the JUCE target rather than magda_tests because juce::OSCReceiver
// constructs a MessageListener, which asserts without a MessageManager. That is
// not a detail worth working around: the thing under test here is a live
// receive thread delivering packets, and the environment it runs in should be
// the one the app actually has.

#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "magda/daw/audio/osc/OscService.hpp"
#include "magda/daw/core/Config.hpp"

using namespace magda;
using namespace magda::osc;

namespace {

/// Config is a process-wide singleton, so a test that changes it has to put it
/// back or it leaks into whatever runs next.
struct ScopedOscConfig {
    ScopedOscConfig(bool enabled, int port, const std::string& address = "127.0.0.1") {
        auto& config = Config::getInstance();
        wasEnabled_ = config.getOscEnabled();
        previousPort_ = config.getOscReceivePort();
        previousAddress_ = config.getOscBindAddress();
        config.setOscEnabled(enabled);
        config.setOscReceivePort(port);
        config.setOscBindAddress(address);
    }

    ~ScopedOscConfig() {
        auto& config = Config::getInstance();
        config.setOscEnabled(wasEnabled_);
        config.setOscReceivePort(previousPort_);
        config.setOscBindAddress(previousAddress_);
    }

    ScopedOscConfig(const ScopedOscConfig&) = delete;
    ScopedOscConfig& operator=(const ScopedOscConfig&) = delete;

  private:
    bool wasEnabled_ = false;
    int previousPort_ = 0;
    std::string previousAddress_;
};

/// Written by whichever thread drains and read by the test, so it carries its
/// own lock.
class ThreadSafeSink : public OscCommandSink {
  public:
    void apply(const OscCommand& command, float value) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        applied_.push_back({command, value});
    }

    std::vector<std::pair<OscCommand, float>> taken() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return applied_;
    }

  private:
    std::mutex mutex_;
    std::vector<std::pair<OscCommand, float>> applied_;
};

struct Harness {
    Harness() {
        auto ownedSink = std::make_unique<ThreadSafeSink>();
        sink = ownedSink.get();
        auto ownedRouter = std::make_unique<OscRouter>(std::move(ownedSink));
        router = ownedRouter.get();
        // Hold the drains so the test decides when they run, on its own thread.
        router->setDrainScheduler([]() {});
        // Detached from the binding registry: these tests are about the socket,
        // and the registry is a process-wide singleton other tests share.
        service = std::make_unique<OscService>(std::move(ownedRouter),
                                               OscService::RegistryAttachment::Detach);
    }

    /// UDP on loopback is effectively immediate; the deadline exists so a
    /// failure reports rather than hangs.
    bool waitForAccepted(std::uint64_t count) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (router->acceptedMessageCount() >= count)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    ThreadSafeSink* sink = nullptr;
    OscRouter* router = nullptr;
    std::unique_ptr<OscService> service;
};

/// A port nothing is listening on, found by binding and immediately releasing
/// one. Racy in principle, free in practice, and the alternative is hardcoding
/// a number that collides with whatever else the machine is running.
int findFreePort() {
    juce::DatagramSocket probe(false);
    if (!probe.bindToPort(0, "127.0.0.1"))
        return 0;
    return probe.getBoundPort();
}

}  // namespace

class OscServiceTest final : public juce::UnitTest {
  public:
    OscServiceTest() : juce::UnitTest("OSC Service Tests", "magda") {}

    void runTest() override {
        testDisabledOpensNothing();
        testEnableAndDisable();
        testUnchangedConfigKeepsSocket();
        testPortChangeRebinds();
        testUnbindableAddressLeavesNothingListening();
        testDatagramReachesSink();
        testBundlesAreUnpacked();
        testForeignAddressIsIgnored();
        testSenderBecomesAPeer();
        testUnansweredSenderIsNotASurface();
        testFirstAcceptedPacketCarriesItsPeer();
        testClosingForgetsThePeers();
        testCloseUnderTraffic();
    }

  private:
    void testDisabledOpensNothing() {
        beginTest("With OSC off, no socket is opened");

        const ScopedOscConfig config(false, 0);
        Harness h;

        expect(!h.service->applyConfig(), "a disabled service must not report a listener");
        expect(!h.service->isListening(), "a disabled service must hold no socket");
        expect(h.service->boundPort() == 0);
    }

    void testEnableAndDisable() {
        beginTest("Enabling binds a socket and disabling releases it");

        const ScopedOscConfig config(true, 0);
        Harness h;

        expect(h.service->applyConfig(), "an enabled service must bind");
        expect(h.service->isListening());
        // Port 0 asked the OS for a free one; the service reports what it got.
        expect(h.service->boundPort() > 0, "the bound port must be discoverable");
        expect(h.service->boundAddress() == "127.0.0.1");

        Config::getInstance().setOscEnabled(false);
        expect(!h.service->applyConfig(), "disabling must close the listener");
        expect(!h.service->isListening());
        expect(h.service->boundPort() == 0);
    }

    void testUnchangedConfigKeepsSocket() {
        beginTest("Reapplying unchanged config keeps the established socket");

        // Every config save reaches every listener. Rebinding on each one would
        // drop a surface mid-gesture because an unrelated setting changed — and
        // with an ephemeral port configured, comparing the *bound* port against
        // the configured 0 would find a mismatch every single time.
        const ScopedOscConfig config(true, 0);
        Harness h;

        expect(h.service->applyConfig());
        const int port = h.service->boundPort();

        expect(h.service->applyConfig());
        expect(h.service->boundPort() == port, "the socket must not have been rebound");
    }

    void testPortChangeRebinds() {
        beginTest("Changing the configured port rebinds");

        const int first = findFreePort();
        const int second = findFreePort();
        expect(first > 0 && second > 0 && first != second, "the test needs two free ports");

        const ScopedOscConfig config(true, first);
        Harness h;

        expect(h.service->applyConfig());
        expect(h.service->boundPort() == first, "the configured port must be the bound one");

        Config::getInstance().setOscReceivePort(second);
        expect(h.service->applyConfig());
        expect(h.service->boundPort() == second, "a port change must take effect immediately");
    }

    void testUnbindableAddressLeavesNothingListening() {
        beginTest("An address the OS will not bind leaves no listener");

        // The bind address is the whole of OSC's access control, so failing to
        // bind must fail closed rather than fall back to every interface.
        const ScopedOscConfig config(true, 0, "203.0.113.1");  // TEST-NET-3
        Harness h;

        expect(!h.service->applyConfig(), "an unbindable address must not report success");
        expect(!h.service->isListening(), "and must not leave a socket open anywhere else");
    }

    void testDatagramReachesSink() {
        beginTest("A datagram from a surface reaches the sink");

        const ScopedOscConfig config(true, 0);
        Harness h;
        expect(h.service->applyConfig());

        juce::OSCSender sender;
        expect(sender.connect("127.0.0.1", h.service->boundPort()));
        expect(sender.send(juce::OSCMessage("/magda/track/2/volume", 0.75f)));

        expect(h.waitForAccepted(1), "the receive thread must accept the message");
        h.router->drainPending();

        const auto applied = h.sink->taken();
        expect(applied.size() == 1);
        if (applied.size() == 1) {
            expect(applied[0].first.kind == OscCommandKind::TrackVolume);
            expect(applied[0].first.index == 2);
            expect(std::abs(applied[0].second - 0.75f) < 1.0e-6f);
        }
    }

    void testBundlesAreUnpacked() {
        beginTest("Bundled messages are unpacked");

        // Surfaces bundle by default, so a reader that only handled bare
        // messages would go silent against many of them.
        const ScopedOscConfig config(true, 0);
        Harness h;
        expect(h.service->applyConfig());

        juce::OSCBundle inner;
        inner.addElement(juce::OSCMessage("/magda/track/3/pan", 0.25f));

        juce::OSCBundle bundle;
        bundle.addElement(juce::OSCMessage("/magda/track/1/volume", 0.5f));
        bundle.addElement(inner);  // nesting is legal and has to survive too

        juce::OSCSender sender;
        expect(sender.connect("127.0.0.1", h.service->boundPort()));
        expect(sender.send(bundle));

        expect(h.waitForAccepted(2), "both the message and the nested one must arrive");
        h.router->drainPending();
        expect(h.sink->taken().size() == 2);
    }

    void testForeignAddressIsIgnored() {
        beginTest("A datagram that is not ours changes nothing");

        const ScopedOscConfig config(true, 0);
        Harness h;
        expect(h.service->applyConfig());

        juce::OSCSender sender;
        expect(sender.connect("127.0.0.1", h.service->boundPort()));
        expect(sender.send(juce::OSCMessage("/someone/else/fader", 0.5f)));
        // Then one we do understand, so there is a point by which the first
        // must already have been processed and declined.
        expect(sender.send(juce::OSCMessage("/magda/master/volume", 0.5f)));

        expect(h.waitForAccepted(1));
        h.router->drainPending();

        const auto applied = h.sink->taken();
        expect(applied.size() == 1, "only the address we own may be applied");
        if (applied.size() == 1)
            expect(applied[0].first.kind == OscCommandKind::MasterVolume);
    }

    void testSenderBecomesAPeer() {
        beginTest("A surface that sends becomes a peer with the host it sent from");

        // The whole of #2096 in one assertion: JUCE's receive loop read this
        // address into a local and dropped it, so feedback had to be aimed at a
        // host the user typed in.
        const ScopedOscConfig config(true, 0);
        Harness h;
        expect(h.service->applyConfig());
        expect(h.router->peers().count() == 0, "nothing has talked to it yet");

        juce::OSCSender sender;
        expect(sender.connect("127.0.0.1", h.service->boundPort()));
        expect(sender.send(juce::OSCMessage("/magda/track/1/volume", 0.5f)));
        expect(h.waitForAccepted(1));

        const auto peers = h.router->peers().snapshot();
        expect(peers.size() == 1, "one sender is one peer");
        if (peers.size() == 1) {
            expect(peers[0].host == "127.0.0.1", "the peer is the host it sent from");
            expect(peers[0].datagrams >= 1);
            expect(h.router->peers().hostFor(peers[0].id) == "127.0.0.1");
        }

        // A second datagram from the same socket is the same surface, not a new
        // one: identity is the host, so an ephemeral port cannot mint peers.
        expect(sender.send(juce::OSCMessage("/magda/track/1/volume", 0.6f)));
        expect(h.waitForAccepted(2));
        expect(h.router->peers().count() == 1, "the same host must stay one peer");
    }

    void testUnansweredSenderIsNotASurface() {
        beginTest("A sender MAGDA does not understand is heard but never answered");

        // Feedback opens a sender and streams a whole project at a peer, and a
        // UDP source address is spoofable with the default all-interfaces bind.
        // So a datagram has to be understood before its sender is answerable,
        // or four bytes of anything turns MAGDA into a reflector.
        const ScopedOscConfig config(true, 0);
        Harness h;
        expect(h.service->applyConfig());

        juce::DatagramSocket sender(false);
        expect(sender.bindToPort(0, "127.0.0.1"));

        // Not OSC at all, then valid OSC on an address MAGDA does not own.
        const char junk[] = {'x', 'y', 'z', '\0'};
        expect(sender.write("127.0.0.1", h.service->boundPort(), junk, sizeof(junk)) > 0);

        juce::OSCSender foreign;
        expect(foreign.connect("127.0.0.1", h.service->boundPort()));
        expect(foreign.send(juce::OSCMessage("/someone/else/fader", 0.5f)));

        // Then one we do understand from a *different* socket, so there is a
        // point by which the first two must already have been processed.
        juce::OSCSender ours;
        expect(ours.connect("127.0.0.1", h.service->boundPort()));
        expect(ours.send(juce::OSCMessage("/magda/master/volume", 0.5f)));
        expect(h.waitForAccepted(1));

        // All three came from loopback, so they are one peer by design — and
        // that peer is answerable only because the third was understood.
        const auto peers = h.router->peers().snapshot();
        expect(peers.size() == 1);
        if (peers.size() == 1) {
            expect(peers[0].datagrams >= 3, "every datagram is counted, parsed or not");
            expect(peers[0].answerable, "the understood one makes it answerable");
        }
    }

    void testFirstAcceptedPacketCarriesItsPeer() {
        beginTest("The first accepted packet is applied under the peer it creates");

        // An unadmitted host has no id, so publishing the packet and admitting
        // afterwards would put the whole of it into the router under
        // `kNoOscPeer` -- and the first gesture a new surface makes would be
        // echoed straight back at it. The receive loop reads such a packet
        // twice: once to decide, then once to apply under the id it will keep.
        const ScopedOscConfig config(true, 0);
        Harness h;
        expect(h.service->applyConfig());

        // The tap is what feedback hangs off, so it is what has to see the id.
        std::mutex tapped;
        std::vector<OscPeerId> peersSeen;
        h.router->setFeedbackTap([&tapped, &peersSeen](OscPeerId peer, int, float) {
            const std::lock_guard<std::mutex> lock(tapped);
            peersSeen.push_back(peer);
        });

        juce::OSCSender sender;
        expect(sender.connect("127.0.0.1", h.service->boundPort()));
        expect(sender.send(juce::OSCMessage("/magda/track/1/volume", 0.25f)));
        expect(h.waitForAccepted(1));
        h.router->drainPending();

        const auto peers = h.router->peers().snapshot();
        expect(peers.size() == 1);

        const std::lock_guard<std::mutex> lock(tapped);
        expect(peersSeen.size() == 1, "the value reached the tap exactly once");
        if (peersSeen.size() == 1 && peers.size() == 1) {
            expect(peersSeen[0] != kNoOscPeer, "and not as coming from nobody");
            expect(peersSeen[0] == peers[0].id, "but as coming from the peer it created");
        }

        h.router->setFeedbackTap({});
    }

    void testClosingForgetsThePeers() {
        beginTest("Closing the socket forgets who was talking to it");

        // A rebind is a new listener. Whoever was talking to the old one has to
        // be heard again before MAGDA starts answering them.
        const ScopedOscConfig config(true, 0);
        Harness h;
        expect(h.service->applyConfig());

        juce::OSCSender sender;
        expect(sender.connect("127.0.0.1", h.service->boundPort()));
        expect(sender.send(juce::OSCMessage("/magda/master/volume", 0.5f)));
        expect(h.waitForAccepted(1));
        expect(h.router->peers().count() == 1);

        h.service->stop();
        expect(h.router->peers().count() == 0, "a closed socket has no peers");
    }

    void testCloseUnderTraffic() {
        beginTest("Closing while a surface is still sending is not a crash");

        // The receive thread has to stop before the socket it reads through is
        // released. This is the window where getting that ordering wrong shows.
        const ScopedOscConfig config(true, 0);
        Harness h;
        expect(h.service->applyConfig());

        juce::OSCSender sender;
        expect(sender.connect("127.0.0.1", h.service->boundPort()));
        for (int i = 0; i < 200; ++i)
            sender.send(juce::OSCMessage("/magda/track/1/volume", 0.005f * static_cast<float>(i)));

        h.service->stop();
        expect(!h.service->isListening());
    }
};

static OscServiceTest oscServiceTest;
