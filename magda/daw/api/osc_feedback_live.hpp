#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "../audio/controllers/ControllerFeedback.hpp"
#include "../audio/controllers/ControllerParamReader.hpp"
#include "../audio/osc/OscFeedback.hpp"
#include "../audio/osc/OscPeers.hpp"
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
 * @brief Projects MAGDA's state onto the OSC fixed namespace (#2091, #2096).
 *
 * The answering half of #1757. Everything below is a projection: it reads the
 * model, hands the values to `OscFeedback`, and lets the diff there decide what
 * is actually worth a datagram. Nothing here decides *when* to send, only what
 * is currently true.
 *
 * ## One surface per peer
 *
 * MAGDA reads its own datagrams (#2096), so it knows which host a value came
 * from. That is what makes this a fan-out rather than a single destination:
 * each peer gets its own `OscFeedback` — its own diff, its own echo bits, its
 * own sender aimed at that host and the configured feedback port.
 *
 * The alternative, one table with one destination, is wrong in a way that only
 * shows up with two surfaces: a fader moved on surface A would be suppressed as
 * an echo to surface B as well, so B would sit showing a value nobody holds.
 *
 * What is *not* per surface is the expensive part. Resolving a binding's target
 * — the alias machinery, the parameter read — happens once per tick and the
 * answer is handed to every surface. Per surface there is an array compare.
 *
 * A surface appearing is exactly observable now, so the snapshot that catches
 * it up is too: a new peer gets one, and nobody else does.
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
 * timer runs there, and `OscRouter`'s drain — which is where the feedback taps
 * come from — is there too, so none of this needs synchronisation. The one
 * thing that crosses a thread boundary is `OscPeers`, which carries its own.
 *
 * The tick is an owned object rather than a base class, the same shape
 * `SubscriptionHub` gives its sampler. `juce::Timer` holds a
 * `SharedResourcePointer<TimerThread>`, so *inheriting* one starts JUCE's timer
 * thread the moment a projector exists — including in a process that has no
 * message loop to drive it. Owning it means a MAGDA with OSC off costs nothing
 * and a headless one costs nothing at all.
 */
class OscFeedbackProjector : private ConfigListener, private BindingRegistryListener {
  public:
    /**
     * @brief Where a surface's messages go once it has been heard from.
     *
     * Called once per peer, with the host it is talking from and the configured
     * feedback port. Defaulted, and the default is an `OscSenderSink` — which is
     * the whole of the application's case. A test supplies one to observe the
     * projection without opening a socket, and gets one sink per surface, which
     * is what makes the per-peer rules observable at all.
     */
    using SinkFactory =
        std::function<std::unique_ptr<osc::OscMessageSink>(const juce::String& host, int port)>;

    /**
     * @param api      The facade. Must outlive this.
     * @param changes  The shared change source (#1857). Must outlive this.
     * @param router   The router whose peers these surfaces are and whose
     *                 applied values are their own echo. Must outlive this.
     * @param reader   Where a bound target's current value comes from. Null is
     *                 legal and means the fixed namespace only, which is the
     *                 headless case: bindings resolve against an engine.
     * @param factory  See `SinkFactory`.
     */
    OscFeedbackProjector(MagdaApi& api, remote::ChangeSource& changes, osc::OscRouter& router,
                         std::unique_ptr<ControllerParamReader> reader, SinkFactory factory = {});
    ~OscFeedbackProjector() override;

    OscFeedbackProjector(const OscFeedbackProjector&) = delete;
    OscFeedbackProjector& operator=(const OscFeedbackProjector&) = delete;

    /// Whether the tick is running. False with OSC off, which is also when this
    /// costs nothing.
    bool isTicking() const {
        return tick_ != nullptr;
    }

    /// Matches `ChangeSource`'s own flush cadence, which is the rate at which
    /// the published half can produce anything new.
    static constexpr int kTickIntervalMs = 33;

    /**
     * @brief Start or stop the tick, and re-aim the surfaces if the port moved.
     *
     * There is no feedback enable to read, and that is the point of #2096: a
     * peer exists only because `OscService` bound a socket, which happens only
     * when OSC is on, so "is there anyone to answer" is already the answer to
     * "should MAGDA answer". What is left is the port, and a change to it is
     * every surface being answered somewhere else — so they are rebuilt, and
     * snapshotted, rather than re-pointed.
     *
     * @return true when the tick is running afterwards.
     */
    bool applyConfig();

    /// How many surfaces are being answered.
    int surfaceCount() const {
        return static_cast<int>(surfaces_.size());
    }

    /// Feedback messages sent to the surface at `peer`, or 0 when it is not one.
    std::uint64_t sentTo(osc::OscPeerId peer) const;

    /// Send everything every surface could care about on the next tick. A
    /// surface that has just appeared gets this on its own; this is the whole
    /// fleet, for a caller that has invalidated what they were all told.
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

  private:
    class ControllerSink;
    class Tick;

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

    /// One peer, and everything that is true of it alone.
    struct Surface {
        osc::OscPeerId peer = osc::kNoOscPeer;
        juce::String host;
        std::uint64_t resumptions = 0;
        std::unique_ptr<osc::OscFeedback> feedback;
        std::vector<BoundAddress> bound;
    };

    /// Config changed: the feedback port may have. Reapplying is idempotent, so
    /// an unrelated save leaves the surfaces alone.
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

    /**
     * @brief Bring the surfaces into line with the peers the router has heard.
     *
     * Cheap when nothing changed — one atomic read of the peer generation — so
     * it runs at the top of every tick, and again from a feedback tap that
     * names a peer with no surface yet. That second call is not belt and braces:
     * the first drain of a first gesture lands before the tick that would have
     * built the surface, and without it the echo bit for that gesture is lost.
     */
    void syncSurfaces();

    Surface* surfaceFor(osc::OscPeerId peer);

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

    /// One address across every surface. The projection reads the model once and
    /// the diff is what is per-surface.
    void publishAll(const osc::OscCommand& command, float value);
    void retireAll(const osc::OscCommand& command);

    /// Feed one surface's copy of a bound address, given the position its target
    /// currently implies.
    void publishBinding(Surface& surface, const juce::String& address, float position);

    /// The entry for `address` on `surface`, created if this is the first time
    /// it has been seen. Linear: bindings number in the tens, and an OSC address
    /// is a short string, so a map would cost more in allocation than the scan
    /// does in comparisons.
    static BoundAddress& boundEntryFor(Surface& surface, const juce::String& address);

    MagdaApi& api_;
    remote::ChangeSource& changes_;
    osc::OscRouter& router_;

    std::unique_ptr<ControllerParamReader> reader_;
    SinkFactory factory_;
    std::unique_ptr<Tick> tick_;

    std::vector<Surface> surfaces_;
    std::uint64_t peersGeneration_ = 0;
    bool surfacesStale_ = true;

    /// What the surfaces are aimed at. Read from `Config` at construction so a
    /// projector driven directly — a headless host, a test — is aimed the same
    /// way one that has had `applyConfig` called is.
    int feedbackPort_ = 0;

    int changeToken_ = 0;

    /// Set by the change source, consumed by the tick.
    bool mixerDirty_ = true;
    bool macrosDirty_ = true;
    bool bindingsDirty_ = true;

    /// The highest 1-based mixer position ever published, so shrinking the
    /// project retires what it left behind.
    int highestPosition_ = 0;

    /// One pass's worth of resolved binding positions, reused so the pass does
    /// not allocate per tick.
    struct BoundValue {
        juce::String address;
        float position = 0.0f;
    };
    std::vector<BoundValue> boundValues_;

    /// Cleared before the object dies, so a `ControllerFeedbackSink` still
    /// installed on the router singleton stops reaching in here rather than
    /// reaching into a destroyed projector.
    std::shared_ptr<std::atomic<bool>> alive_{std::make_shared<std::atomic<bool>>(true)};
};

}  // namespace magda
