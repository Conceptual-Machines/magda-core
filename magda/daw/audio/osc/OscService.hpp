#pragma once

#include <juce_osc/juce_osc.h>

#include <memory>

#include "../../core/Config.hpp"
#include "../../core/controllers/BindingRegistry.hpp"
#include "osc/OscRouter.hpp"

namespace magda::osc {

/**
 * @brief The UDP listener behind MAGDA's OSC control surfaces (#1757).
 *
 * Owns the socket and the receive thread, and hands everything that arrives to
 * an `OscRouter`. Configuration decides whether it exists at all: with OSC off
 * — the default — `applyConfig` binds nothing, and there is no state in which
 * MAGDA holds a port open that the user did not ask for.
 *
 * ## The socket is bound here, not by JUCE
 *
 * `OSCReceiver::connect(port)` would be one line, but it binds every interface
 * unconditionally. OSC has no authentication of any kind — the protocol has no
 * notion of one — so which interfaces the socket answers on is the entire
 * access control story, and it has to be the user's decision. Binding our own
 * `DatagramSocket` and handing it over is what makes `oscBindAddress`
 * meaningful: `127.0.0.1` for a bridge on this machine, `0.0.0.0` for the
 * tablet on the sofa.
 *
 * The socket outlives the receiver deliberately — `connectToSocket` keeps a
 * non-owning pointer, so the receive thread must be stopped before the socket
 * it is reading from goes away.
 *
 * ## Reacting to settings
 *
 * A `ConfigListener`, so switching OSC on, off, or onto another port takes
 * effect immediately rather than at the next launch — a toggle that needed a
 * restart would be a worse answer than no toggle. Reapplying is cheap and
 * idempotent: an unrelated config save leaves an established listener alone
 * rather than dropping every connected surface.
 */
class OscService : private ConfigListener,
                   private BindingRegistryListener,
                   private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback> {
  public:
    /**
     * @brief Whether this service tracks the global binding registry.
     *
     * The application wants `Attach`: bindings live in one registry shared with
     * the MIDI surfaces, and the router has to follow it. A test usually wants
     * `Detach` and to drive `OscRouter::updateBindings` itself, so it is not
     * reading and writing a process-wide singleton to say something about one
     * address.
     */
    enum class RegistryAttachment : std::uint8_t { Attach, Detach };

    explicit OscService(std::unique_ptr<OscRouter> router,
                        RegistryAttachment attach = RegistryAttachment::Attach);
    ~OscService() override;

    OscService(const OscService&) = delete;
    OscService& operator=(const OscService&) = delete;

    /**
     * @brief Bring the listener into line with configuration.
     *
     * Opens the socket when OSC is enabled and nothing is listening, closes it
     * when it is disabled, and rebinds when the port or address changed. Safe
     * to call repeatedly; the common case is that nothing has changed and
     * nothing happens.
     *
     * @return true when a socket is bound afterwards.
     */
    bool applyConfig();

    /** Close the socket and stop the receive thread. Idempotent. */
    void stop();

    bool isListening() const {
        return receiver_ != nullptr;
    }

    /// The bound port, or 0 when not listening.
    int boundPort() const;

    /// The address actually bound, or empty when not listening.
    juce::String boundAddress() const {
        return boundAddress_;
    }

    OscRouter& router() {
        return *router_;
    }

    /**
     * @brief Re-read the OSC bindings from the registry and hand them to the
     *        router.
     *
     * Message thread only. Called on every registry change, and once when the
     * service starts so a project's saved bindings are live before the first
     * message arrives.
     */
    void refreshBindings();

  private:
    void configChanged() override;
    void bindingRegistryChanged(BindingScope scope) override;

    // Realtime callbacks: these run on the receive thread, not the message
    // thread. Everything they touch is in OscRouter and lock-free by design.
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void oscBundleReceived(const juce::OSCBundle& bundle) override;

    /**
     * @brief Deliver a bundle's contents, flattening any nesting.
     *
     * Bundles arrive whole — JUCE hands realtime listeners the bundle rather
     * than its messages — and surfaces send them routinely, so ignoring them
     * would mean a template that works in one editor going silent in another.
     *
     * The time tag is not honoured: everything is applied on arrival.
     * Scheduling a bundle for a future beat is a real OSC feature and a
     * separate piece of work; treating a timestamped bundle as immediate is the
     * conservative reading, and matches what the coalescing layer above could
     * represent anyway.
     */
    void deliverBundle(const juce::OSCBundle& bundle, int depth);

    bool open(int port, const juce::String& address);
    void close();

    std::unique_ptr<OscRouter> router_;

    /// Declared before the receiver so it is destroyed after it: the receive
    /// thread reads through a non-owning pointer to this socket.
    std::unique_ptr<juce::DatagramSocket> socket_;
    std::unique_ptr<juce::OSCReceiver> receiver_;

    /// What configuration asked for, which is not what was bound when it was 0.
    int requestedPort_ = 0;
    int boundPort_ = 0;
    juce::String boundAddress_;
    bool attachToRegistry_ = true;
};

}  // namespace magda::osc
