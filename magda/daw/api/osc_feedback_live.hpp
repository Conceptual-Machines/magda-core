#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <vector>

#include "../audio/controllers/ControllerFeedback.hpp"
#include "../audio/controllers/ControllerParamReader.hpp"
#include "../audio/osc/OscFeedback.hpp"
#include "../core/Config.hpp"
#include "../core/TypeIds.hpp"
#include "../core/controllers/Binding.hpp"
#include "../core/controllers/BindingRegistry.hpp"
#include "remote_changes.hpp"

namespace magda {

class MagdaApi;

namespace osc {
class OscRouter;
}

/**
 * @brief Projects MAGDA's state onto the OSC fixed namespace (#2091).
 *
 * The answering half of #1757. Everything below is a projection: it reads the
 * model, hands the values to `OscFeedback`, and lets the diff there decide what
 * is actually worth a datagram. Nothing here decides *when* to send, only what
 * is currently true.
 *
 * ## Two halves, published and sampled
 *
 * The split is `SubscriptionHub`'s, for the same reason it has one.
 *
 * **Published.** Track and master volume, pan, mute, solo and sends, and the
 * focused device's macros. Every one of those notifies a model listener however
 * it moved — `TrackManager::setTrackVolume` calls `notifyTrackPropertyChanged`
 * whether a fader, an OSC message or automation moved it — so `ChangeSource`
 * says when to re-read them, and a project in which nothing moves costs nothing
 * per tick. That is what the shared coalescing layer buys here, and it is why
 * this is built on it rather than beside it.
 *
 * **Sampled.** Play, record, loop, tempo and the playhead. None of these reach
 * `ChangeSource`: `ModelChangeBridge` listens to the model singletons, and
 * `Topic::Transport` is marked only when a *remote API request* named
 * `transport.*` ran, so pressing Play in MAGDA is invisible to it. The playhead
 * has to be sampled regardless, since it moves without notifying anyone, so the
 * five scalars ride the same tick. Five reads against a facade is what that tick
 * already costs.
 *
 * ## Bound addresses
 *
 * A binding's address gets the position its target's value corresponds to, which
 * is the parameter read back through the binding's own range and curve — the
 * inverses in `BindingTransform`. Two things drive it: this class's own tick,
 * which catches the user dragging the on-screen control, and
 * `ControllerFeedbackSink`, which catches a write that arrived from another
 * control surface.
 *
 * ## Threading
 *
 * Message thread throughout. The `ChangeSource` listener is called there, the
 * timer runs there, and `OscRouter`'s drain — which is where `noteReceived`
 * comes from — is there too, so `OscFeedback` needs no synchronisation at all.
 *
 * The tick is an owned object rather than a base class, the same shape
 * `SubscriptionHub` gives its sampler. `juce::Timer` holds a
 * `SharedResourcePointer<TimerThread>`, so *inheriting* one starts JUCE's timer
 * thread the moment a projector exists — including in a process that has no
 * message loop to drive it. Owning it means a projector with no destination
 * costs nothing and a headless one costs nothing at all.
 */
class OscFeedbackProjector : private ConfigListener, private BindingRegistryListener {
  public:
    /**
     * @param api       The facade. Must outlive this.
     * @param changes   The shared change source (#1857). Must outlive this.
     * @param router    The router whose applied values are this projector's own
     *                  echo, and whose message count says when a surface has
     *                  started talking. Must outlive this.
     * @param reader    Where a bound target's current value comes from. Null is
     *                  legal and means the fixed namespace only, which is the
     *                  headless case: bindings resolve against an engine.
     * @param sink      Where finished messages go. Null — the application's
     *                  case — builds an `OscSenderSink` and makes
     *                  `setDestination` mean what it says. Supplying one instead
     *                  is how a test observes the projection without opening a
     *                  socket, and it makes the projector permanently "sending",
     *                  since there is no destination to configure.
     */
    OscFeedbackProjector(MagdaApi& api, remote::ChangeSource& changes, osc::OscRouter& router,
                         std::unique_ptr<ControllerParamReader> reader,
                         std::unique_ptr<osc::OscMessageSink> sink = nullptr);
    ~OscFeedbackProjector() override;

    /// Whether the tick is running. False with no destination configured, which
    /// is also when this costs nothing.
    bool isTicking() const {
        return tick_ != nullptr;
    }

    OscFeedbackProjector(const OscFeedbackProjector&) = delete;
    OscFeedbackProjector& operator=(const OscFeedbackProjector&) = delete;

    /// Matches `ChangeSource`'s own flush cadence, which is the rate at which
    /// the published half can produce anything new.
    static constexpr int kTickIntervalMs = 33;

    /// Inbound silence long enough that traffic resuming is more likely a
    /// surface that has just connected than a pause in a gesture. The best
    /// proxy available for "a new peer appeared", since JUCE does not report
    /// who sent a datagram.
    static constexpr int kResyncSilenceMs = 5000;

    /**
     * @brief Point feedback at a destination and start, or stop it.
     *
     * An empty host stops the timer and disconnects, so a MAGDA with feedback
     * off does no per-tick work at all. A change of destination is a new surface
     * by definition, so it takes a full snapshot.
     *
     * @return true when a destination is connected afterwards.
     */
    bool setDestination(const juce::String& host, int port);

    /// Bring the destination into line with `Config`. Safe to call repeatedly.
    bool applyConfig();

    bool isSending() const;

    /// Send everything the surface could care about on the next tick.
    void requestSnapshot();

    /**
     * @brief Project what is dirty and send what changed, on the calling thread.
     *
     * What the timer does, once. Public for the same reason
     * `OscRouter::drainPending` and `SubscriptionHub::sampleNow` are: a host
     * with no MessageManager has no timer, which is the headless test case, and
     * driving it directly is the only way to observe what one tick decided.
     */
    void tick();

    /// A feedback sink for `ControllerRouter`, so a parameter written by any
    /// control surface reaches the OSC bindings that address it. Ownership goes
    /// to the router; it holds a pointer back here and must not outlive this.
    std::unique_ptr<ControllerFeedbackSink> makeControllerFeedbackSink();

    osc::OscFeedback& feedback() {
        return *feedback_;
    }

  private:
    class ControllerSink;
    class Tick;

    /// Config changed: the destination may have. Reapplying is idempotent, so
    /// an unrelated save leaves a connected surface alone.
    void configChanged() override;

    /**
     * @brief The bindings in force changed, so what each address shows may have.
     *
     * `OscService` already listens for this to rebuild the inbound routes;
     * without the same listener here, a binding added or learned after the last
     * tick would drive its target while the address it drives it from stayed
     * blank until some unrelated edit happened to dirty the pass. OSC learn is
     * the sharpest case, because its capture is consumed rather than applied, so
     * nothing moves to notify anyone.
     *
     * The remembered values go with it: an address whose binding was re-pointed
     * is showing the old target's position, and there is nothing to diff a new
     * target against.
     */
    void bindingRegistryChanged(BindingScope scope) override;

    void onChanges(const std::vector<remote::ChangeSource::Change>& changes);

    /// Track and master strips: volume, pan, mute, solo, sends.
    void projectMixer();
    /// The focused device's sixteen macros, or nothing when nothing is focused.
    void projectMacros();
    /// Play, record, loop, tempo, position.
    void projectTransport();
    /// Every OSC binding's address, at the position its target implies.
    void projectBindings();

    /// Publish one strip's block at a 1-based mixer position.
    void projectStrip(int position, TrackId trackId);

    /// Forget the positions between `firstUnused` and the highest position this
    /// projector has ever published, so a deleted track stops being part of a
    /// snapshot.
    void retirePositionsFrom(int firstUnused);

    /// Feed a binding's address, given the value its target currently holds.
    void publishBinding(const Binding& binding, float targetValue);

    MagdaApi& api_;
    remote::ChangeSource& changes_;
    osc::OscRouter& router_;

    std::unique_ptr<ControllerParamReader> reader_;
    std::unique_ptr<osc::OscFeedback> feedback_;
    osc::OscSenderSink* sender_ = nullptr;  ///< Owned by `feedback_`.
    std::unique_ptr<Tick> tick_;

    int changeToken_ = 0;

    /// Set by the change source, consumed by the tick.
    bool mixerDirty_ = true;
    bool macrosDirty_ = true;
    bool bindingsDirty_ = true;

    /// The highest 1-based mixer position ever published, so shrinking the
    /// project retires what it left behind.
    int highestPosition_ = 0;

    /// Bound addresses are not slots, so they carry their own copy of what the
    /// slot table holds: what was last sent, and what arrived from the surface.
    /// `echoed` is the same bit for the same reason — see `OscFeedback`.
    struct BoundAddress {
        juce::String address;
        float lastSent = 0.0f;
        float lastReceived = 0.0f;
        bool hasSent = false;
        bool echoed = false;
    };
    std::vector<BoundAddress> bound_;

    /// The entry for `address`, created if this is the first time it has been
    /// seen. Linear: bindings number in the tens, and an OSC address is a short
    /// string, so a map would cost more in allocation than the scan does in
    /// comparisons.
    BoundAddress& boundEntryFor(const juce::String& address);

    std::uint64_t lastAcceptedCount_ = 0;
    juce::int64 lastInboundMs_ = 0;

    /// Cleared before the object dies, so a `ControllerFeedbackSink` still
    /// installed on the router singleton stops reaching in here rather than
    /// reaching into a destroyed projector.
    std::shared_ptr<std::atomic<bool>> alive_{std::make_shared<std::atomic<bool>>(true)};
};

}  // namespace magda
