#pragma once

#include <juce_osc/juce_osc.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../../core/controllers/Binding.hpp"
#include "osc/OscAddress.hpp"
#include "osc/OscPacket.hpp"
#include "osc/OscPeers.hpp"

namespace magda::osc {

// ============================================================================
// OscCommandSink
// ============================================================================

/**
 * @brief Where a drained command is applied.
 *
 * The seam between "an OSC message arrived and survived coalescing" and "the
 * DAW changed", so routing is testable without an engine, project, or
 * socket. `apply` is always called on the message thread.
 */
class OscCommandSink {
  public:
    virtual ~OscCommandSink() = default;

    /**
     * @param command  What was addressed.
     * @param value    Its latest value, in the convention `argKindFor(kind)`
     *                 names: a normalized level, a BPM, a beat position, 0/1
     *                 for a set toggle, `kOscToggleRequest` for one to flip, or
     *                 1 for a trigger that fired.
     */
    virtual void apply(const OscCommand& command, float value) = 0;
};

// ============================================================================
// OscBindingSink
// ============================================================================

/**
 * @brief Where a bound OSC value is applied.
 *
 * Separate from `OscCommandSink` because a binding carries its own mode,
 * range and curve and resolves through the alias/resolver machinery, none of
 * which the fixed namespace has. Both are called on the message thread.
 */
class OscBindingSink {
  public:
    virtual ~OscBindingSink() = default;

    /// @param value The surface's value, normalized and clamped to [0,1],
    ///              before the binding's own mode / curve / range are applied.
    virtual void apply(const Binding& binding, float value) = 0;
};

// ============================================================================
// OscLearnCapture
// ============================================================================

/**
 * @brief The control a learn gesture caught, ready to become a binding source.
 *
 * The OSC counterpart of `LearnCapture`. Produced on the receive thread and
 * delivered on the message thread, same as the MIDI one.
 */
struct OscLearnCapture {
    juce::String address;
    int argIndex = 0;    ///< Which argument carried the value.
    float value = 0.0f;  ///< What it was at the moment of capture.
};

// ============================================================================
// OscBindingRoutes
// ============================================================================

/**
 * @brief The OSC bindings in force, with a pending value for each.
 *
 * Built on the message thread whenever the binding registry changes, then
 * published whole; the receive thread takes a `shared_ptr` copy (a refcount
 * bump, no lock, no allocation) and reads through it.
 *
 * Pending values live in here rather than in the router because a slot index
 * only means anything relative to the list it was resolved against -- index
 * and value must be interpreted against the same snapshot, or a re-bind
 * could land one control's value on another's parameter. A value written
 * into a snapshot that's since been replaced is simply dropped with it,
 * which for a fader mid-gesture is one lost update at the moment bindings
 * were edited.
 */
struct OscBindingRoutes {
    struct Entry {
        juce::String address;
        int argIndex = 0;
        Binding binding;
    };

    /// Sorted by address, so a lookup is a binary search and the bindings
    /// sharing one address are a contiguous run.
    std::vector<Entry> entries;

    /// Latest unapplied value per entry, who sent it, and which entries have
    /// one.
    std::unique_ptr<std::atomic<float>[]> values;
    std::unique_ptr<std::atomic<OscPeerId>[]> peers;
    std::unique_ptr<std::atomic<std::uint64_t>[]> dirty;
    std::size_t dirtyWords = 0;

    /// Build from the bindings in force. Anything that is not a valid OSC
    /// source is skipped rather than rejected -- the registry holds both kinds.
    static std::shared_ptr<OscBindingRoutes> build(const std::vector<Binding>& bindings);

    /// The half-open range of entries matching `address`, or an empty range.
    std::pair<std::size_t, std::size_t> findRange(juce::StringRef address) const;
};

// ============================================================================
// Argument extraction
// ============================================================================

/**
 * @brief What a message means for the command its address named.
 *
 * Returns nullopt when the message should be ignored: the release half of a
 * momentary button, or a value-carrying address sent without a number to
 * carry (a bare `/magda/track/1/volume` says nothing about where the fader
 * is). Accepts both int and float arguments, since surfaces disagree about
 * which to send for a button.
 */
std::optional<float> oscValueFor(const OscCommand& command, const OscMessageView& message);

// ============================================================================
// OscRouter
// ============================================================================

/**
 * @brief Turns a stream of OSC messages into message-thread parameter writes.
 *
 * ## Every message carries who sent it
 *
 * MAGDA reads its own datagrams (#2096), so an `OscPeerId` travels with a
 * message from the socket to the feedback taps -- what lets the answering
 * half suppress an echo to the surface that produced a value without
 * suppressing it to every other surface. The peer table lives here rather
 * than in `OscService` because the router already knows what a message
 * *was*, and the feedback projector holds only a router reference.
 *
 * `kNoOscPeer` means "not from a socket" (a test, or code driving the router
 * directly): it names no surface, so feedback taps report it and nothing
 * answers -- correct, since a value from nowhere is not an echo of anyone.
 *
 * ## Two kinds of traffic, two paths
 *
 * A tablet mixer sends continuously while a finger is down -- a fader stream
 * at 100 Hz per control is normal, not adversarial. Posting every message to
 * the message thread in order would put the DAW's UI thread behind a network
 * firehose, with the user's latest fader position queued behind stale ones.
 *
 * So a **continuous value** (a level, a tempo, a beat position) is
 * published, not posted: every fixed-namespace address owns a slot
 * (`oscSlotIndex`), a store overwrites whatever hadn't been applied yet, and
 * the message thread drains the whole table at once. Only the latest value
 * per address lands, at whatever rate the message thread can absorb.
 *
 * A **discrete command** (a trigger or toggle) can't be treated that way: it
 * is an edge, and latest-value-wins is the wrong algebra for edges (two bare
 * mute messages should cancel, not collapse into "stays muted"; a
 * `[stop, position, play]` bundle resolved by slot order rather than arrival
 * order would land stopped). So triggers and toggles go into a small bounded
 * ring in arrival order instead -- affordable because they come from fingers
 * and buttons, not a fader stream.
 *
 * The drain applies coalesced values first, then ordered commands -- so for
 * the cue above, the locate lands and then the transport acts on it, rather
 * than rolling from the old position. Two continuous addresses are still not
 * ordered relative to each other (a drain walks slots), which is the
 * standard control-surface tradeoff once edges are off this path.
 *
 * ## Threading
 *
 * `handleMessage` runs on the OSC receive thread. Ordinary routing takes no
 * lock and allocates nothing except the one `callAsync` that carries a drain
 * to the message thread: parsing is a scan over address bytes, publishing is
 * a store plus an atomic bit. An armed learn session briefly locks while
 * claiming its one capture, so cancellation can happen safely from another
 * thread. At most one drain is outstanding at a time, so drain allocation is
 * bounded by drain rate, not message rate -- a faster surface can't make the
 * receive thread allocate more, only make MAGDA coarser, never the network
 * thread slower.
 *
 * Nothing here touches the audio thread; parameter writes reach it through
 * the same host-write path the MIDI control surfaces use.
 */
class OscRouter {
  public:
    explicit OscRouter(std::unique_ptr<OscCommandSink> sink);
    ~OscRouter();

    OscRouter(const OscRouter&) = delete;
    OscRouter& operator=(const OscRouter&) = delete;

    /**
     * @brief Whether a call to `handleMessage` should act or only answer.
     *
     * `Preflight` decides acceptance and changes nothing (no value
     * published, no drain scheduled, no learn session consumed) -- the
     * receive loop needs to know a packet is worth anything *before*
     * publishing it, since a peer not yet admitted has no id and publishing
     * under `kNoOscPeer` would lose that new surface's first echo bit. Paid
     * once per surface, not once per datagram.
     */
    enum class Dispatch : std::uint8_t { Preflight, Apply };

    /**
     * @brief Receive-thread entry point.
     *
     * **One caller at a time**: the ordered ring is single-producer and this
     * is the only writer to it. True today because one service owns one
     * socket and one receive loop, and rebinding stops the old thread before
     * the new one starts -- an invariant of this method, not an accident of
     * the caller; a second concurrent caller would corrupt the ring silently.
     *
     * @param message A view over the datagram it was parsed from, valid only
     *                for the duration of this call.
     * @param peer    Who sent it, from `peers()`. `kNoOscPeer` for a caller
     *                that is not a socket.
     *
     * @return true when the message was accepted. False means either its
     *         address was unknown or a reserved fixed-namespace address
     *         carried no usable value. Reserved addresses are never offered
     *         to learn or bindings regardless of this return value.
     */
    bool handleMessage(const OscMessageView& message, OscPeerId peer = kNoOscPeer,
                       Dispatch dispatch = Dispatch::Apply);

    /**
     * @brief The same, for a caller holding one of JUCE's messages.
     *
     * Tests, and anything that built a message rather than receiving one.
     * The receive path parses into a view directly, since building an
     * `OSCMessage` is what allocates.
     */
    bool handleMessage(const juce::OSCMessage& message, OscPeerId peer = kNoOscPeer,
                       Dispatch dispatch = Dispatch::Apply);

    /// Who has been heard from. Written by the receive thread, read by the
    /// feedback projector and the settings UI.
    OscPeers& peers() {
        return peers_;
    }
    const OscPeers& peers() const {
        return peers_;
    }

    /**
     * @brief Apply everything pending right now, on the calling thread.
     *
     * The message thread reaches this through the scheduled drain. Public
     * because tests drive it directly, and a service shutting down can
     * flush rather than strand the last fader position.
     */
    void drainPending();

    /** Replace the sink. Message thread only, and not while a drain is running. */
    void setSink(std::unique_ptr<OscCommandSink> sink);

    /** Where bound values are applied. Message thread only, as above. */
    void setBindingSink(std::unique_ptr<OscBindingSink> sink);

    /**
     * @brief Publish the OSC bindings now in force.
     *
     * Message thread only. Called whenever the binding registry changes;
     * the receive thread picks up the new set on its next message with no
     * synchronisation needed.
     *
     * Bindings are consulted only for addresses the fixed namespace
     * declines, so `/magda/...` can't be shadowed by one -- those addresses
     * are a documented contract, and a binding quietly taking one over would
     * break a stock template with no way to see why.
     */
    void updateBindings(const std::vector<Binding>& bindings);

    // ========================================================================
    // Learn
    // ========================================================================

    using LearnCallback = std::function<void(const OscLearnCapture&)>;

    /**
     * @brief Capture the next control a surface moves.
     *
     * Cancels any session in progress. The next message with a readable
     * number fires `onCaptured` on the message thread and is consumed
     * rather than routed, so wiggling a fader to learn it doesn't also move
     * whatever it's currently bound to.
     *
     * Addresses in the fixed namespace are never captured -- they already
     * do something documented, and a surface whose controls sit on
     * `/magda/...` is already mapped.
     *
     * May be called from any thread.
     */
    void beginLearnSession(LearnCallback onCaptured);

    /** Cancel without firing the callback. Safe when nothing is active. */
    void cancelLearnSession();

    bool isLearning() const;

    /**
     * @brief Watch the fixed-namespace values this router applies (#2091).
     *
     * Called from `drainPending` with the slot and value that landed, so
     * feedback can tell its own echo from a change the user made in MAGDA.
     * On the message thread, inside the drain, once per applied address.
     *
     * A toggle sent with no argument is not reported (it asks for a flip,
     * not a state, so the surface has to be told what it produced). Delta
     * addresses are not reported for the same reason they are never echoed:
     * there is no state behind them.
     *
     * Set before any message is handled, cleared by passing `{}`. The peer
     * reported is whichever surface's value the drain actually applied --
     * where two surfaces wrote the same address between drains, only the
     * last is reported, matching the slot's own latest-value-wins.
     */
    using FeedbackTap = std::function<void(OscPeerId peer, int slot, float value)>;
    void setFeedbackTap(FeedbackTap tap);

    /**
     * @brief The same watch, for bound addresses (#2091).
     *
     * Separate because a binding is addressed by the string the surface
     * sends, not a slot, and the value reported is the surface's own
     * position before the binding's mode/curve/range are applied -- the
     * quantity feedback needs, since that's what it would send back.
     *
     * Called from `drainPending`, on the message thread, once per applied
     * binding value.
     */
    using BindingFeedbackTap =
        std::function<void(OscPeerId peer, const juce::String& address, float value)>;
    void setBindingFeedbackTap(BindingFeedbackTap tap);

    /**
     * @brief How a pending drain reaches the message thread.
     *
     * Defaults to posting one, or running inline when already on the
     * message thread or when there is no message loop at all (headless,
     * where deferring would mean never applying).
     *
     * A test can replace it to hold the drain instead of running it -- the
     * only way to observe what coalescing kept, since applied inline every
     * value lands as it arrives and the table never holds more than one at
     * a time. Set before any message is handled.
     */
    void setDrainScheduler(std::function<void()> scheduler);

    /// Messages accepted since construction, for the settings UI and tests --
    /// "is anything actually arriving?" is the first question a silent
    /// surface raises.
    std::uint64_t acceptedMessageCount() const;

    /// Discrete commands dropped because the ordered ring was full. Non-zero
    /// means a sender outran the message thread with buttons, which no
    /// finger-driven surface can do -- reads as a flood, not normal use.
    std::uint64_t droppedCommandCount() const;

    /**
     * @brief Depth of the ordered ring, and the most discrete commands one
     * drain applies before yielding.
     *
     * Public because it's a characteristic of the class, not an
     * implementation detail: it bounds how much a burst can carry and how
     * long the message thread stays inside a drain. Sized above the whole
     * discrete address space (every track's mute and solo, plus the
     * transport's four) so a surface can state-dump every button it owns in
     * one bundle and lose none of it -- past that, a sender is outrunning
     * the message thread with buttons, which fingers cannot do. Bounded so
     * the cost is fixed memory and some dropped presses, not unbounded growth.
     */
    static constexpr std::uint32_t kOrderedCapacity = 512;
    static_assert(kOrderedCapacity > (kMaxTrackNumber * 2) + 4,
                  "the ring must hold one press of every discrete address at once");

  private:
    /// 64 slots per word, rounded up.
    static constexpr int kDirtyWords = (kOscSlotCount + 63) / 64;

    /// Applied in arrival order, ahead of nothing and behind the value table.
    struct OrderedCommand {
        OscCommand command;
        float value = 0.0f;
        OscPeerId peer = kNoOscPeer;
    };

    void submit(const OscCommand& command, float value, OscPeerId peer);
    void scheduleDrain();
    void postDrain();
    void requestFollowUpDrain();
    bool pushOrdered(const OscCommand& command, float value, OscPeerId peer);
    void drainOrdered();

    /// nullopt when the address is outside the fixed namespace; otherwise
    /// the bool says whether its payload was accepted. A recognized address
    /// with an unusable payload is still reserved and must not fall through
    /// to learn or bindings.
    std::optional<bool> handleFixedNamespace(const OscMessageView& message, OscPeerId peer,
                                             Dispatch dispatch);
    bool handleBindings(const OscMessageView& message, OscPeerId peer, Dispatch dispatch);
    bool handleLearn(const OscMessageView& message, Dispatch dispatch);
    void drainBindings();

    struct LearnState {
        LearnCallback callback;
    };

    std::unique_ptr<LearnState> learn_;
    /// Fast-path flag: ordinary routing avoids taking `learnLock_` when no
    /// learn session is armed.
    std::atomic<bool> learning_{false};
    /// Guards the lifetime and callback held by `learn_`.
    juce::CriticalSection learnLock_;

    std::unique_ptr<OscCommandSink> sink_;
    std::unique_ptr<OscBindingSink> bindingSink_;
    std::function<void()> scheduler_;
    FeedbackTap feedbackTap_;
    BindingFeedbackTap bindingFeedbackTap_;

    /// Swapped whole on the message thread, read through a shared_ptr copy
    /// on the receive thread. Null until bindings are first published.
    std::shared_ptr<OscBindingRoutes> bindingRoutes_;

    /// Latest unapplied value per address. Written by the receive thread,
    /// read by the drain; a torn read is impossible and a stale one is
    /// corrected by the next store, which re-dirties the slot.
    std::array<std::atomic<float>, kOscSlotCount> values_{};

    /// Who wrote the value in the slot beside it. Never read without the
    /// dirty bit, which is only set after both stores, so there is no
    /// "unset" state to initialise this to. A full id rather than a byte,
    /// since ids are unique for the session so one left here after its peer
    /// goes away resolves to nobody rather than to whoever took its place.
    std::array<std::atomic<OscPeerId>, kOscSlotCount> valuePeers_{};

    /// Which slots have something to apply. A bitmap rather than a queue so
    /// the receive thread's publish is one `fetch_or` and repeated writes to
    /// one address cost nothing extra.
    std::array<std::atomic<std::uint64_t>, kDirtyWords> dirty_{};

    /// Whether a drain is already posted. Cleared at the *start* of the
    /// drain, not the end: a value published while the drain is walking the
    /// table must still be able to schedule the next one, or it would sit
    /// in its slot until the surface happened to move again.
    std::atomic<bool> drainScheduled_{false};

    /// Triggers and toggles, in arrival order. Single-producer (one socket
    /// owns one receive loop; `handleMessage` is documented as its entry
    /// point), single-consumer (the drain).
    std::array<OrderedCommand, kOrderedCapacity> ordered_{};
    std::atomic<std::uint32_t> orderedWrite_{0};
    std::atomic<std::uint32_t> orderedRead_{0};

    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> dropped_{0};

    OscPeers peers_;

    /// Cleared before the object dies so a drain still sitting in the
    /// message queue completes without touching it.
    std::shared_ptr<std::atomic<bool>> alive_{std::make_shared<std::atomic<bool>>(true)};
};

}  // namespace magda::osc
