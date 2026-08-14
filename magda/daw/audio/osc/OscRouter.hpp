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

namespace magda::osc {

// ============================================================================
// OscCommandSink
// ============================================================================

/**
 * @brief Where a drained command is applied.
 *
 * The seam between "an OSC message arrived and survived coalescing" and "the
 * DAW changed", so the routing above it is testable without an engine, a
 * project, or a socket. `apply` is always called on the message thread.
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
 * range and curve, and resolves through the alias/resolver machinery — none of
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
 * published whole. The receive thread takes a `shared_ptr` copy — a reference
 * count bump, no lock and no allocation — and reads through it.
 *
 * The pending values live *in here* rather than in the router, and that is the
 * point. A slot index only means anything relative to the list it was resolved
 * against, so an index and its value have to be interpreted against the same
 * snapshot or a re-bind could land one control's value on another's parameter.
 * Keeping both together makes that impossible to express: a value written into
 * a snapshot that has since been replaced is simply dropped along with it,
 * which for a fader mid-gesture is one lost update at the moment the user
 * edited their bindings.
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

    /// Latest unapplied value per entry, and which entries have one.
    std::unique_ptr<std::atomic<float>[]> values;
    std::unique_ptr<std::atomic<std::uint64_t>[]> dirty;
    std::size_t dirtyWords = 0;

    /// Build from the bindings in force. Anything that is not a valid OSC
    /// source is skipped rather than rejected — the registry holds both kinds.
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
 * momentary button, or a value-carrying address sent without a number to carry
 * (a bare `/magda/track/1/volume` says nothing about where the fader is).
 *
 * Int and float arguments are both accepted because surfaces disagree about
 * which to send for a button, and a template that works in TouchOSC should not
 * fail in Open Stage Control over the type tag of a 1.
 */
std::optional<float> oscValueFor(const OscCommand& command, const juce::OSCMessage& message);

// ============================================================================
// OscRouter
// ============================================================================

/**
 * @brief Turns a stream of OSC messages into message-thread parameter writes.
 *
 * ## Two kinds of traffic, two paths
 *
 * A tablet mixer sends continuously while a finger is down — a fader stream at
 * 100 Hz per control is the normal case, not the adversarial one, and eight
 * strips moving at once is a chord change. Posting each message to the message
 * thread would put the DAW's UI thread on the far end of a network firehose,
 * and posting each one *in order* would mean the user's last fader position
 * waiting behind several hundred stale ones.
 *
 * So a **continuous value** — a level, a tempo, a beat position — is not
 * posted, it is published. Every address in the fixed namespace owns a slot
 * (`oscSlotIndex`), a store into that slot overwrites whatever had not been
 * applied yet, and the message thread drains the whole table at once. What
 * lands is the most recent value per address, at whatever rate the message
 * thread can absorb, and the cost of a faster surface is finer resolution
 * rather than a longer queue.
 *
 * A **discrete command** — a trigger or a toggle — cannot be treated that way,
 * because it is an edge rather than a value and latest-value-wins is the wrong
 * algebra for edges. Two bare mute messages are two flips and should cancel;
 * collapsing them into one slot leaves the track muted. Worse, `play` and
 * `stop` are two addresses over one state, so resolving them by slot order
 * rather than arrival order makes a bundle of `[stop, position, play]` — an
 * ordinary show-control cue, delivered atomically into a single drain — land
 * stopped. So triggers and toggles go into a small bounded ring in arrival
 * order instead, which is affordable precisely because they come from fingers
 * and buttons rather than from a fader stream.
 *
 * The drain applies the coalesced values first and the ordered commands after.
 * That is the useful order for the cue above: the locate lands, then the
 * transport acts on it, rather than rolling from the old position and jumping.
 *
 * What remains is that two *continuous* addresses are not applied in arrival
 * order relative to each other — a drain walks slots. Within one address order
 * is preserved trivially, because only the latest value exists. That is the
 * standard control-surface tradeoff, and with edges moved off this path there
 * is no longer a pair of addresses whose relative order changes the outcome.
 *
 * ## Threading
 *
 * `handleMessage` runs on the OSC receive thread. Ordinary routing takes no
 * lock and, apart from the one `callAsync` that carries a drain to the message
 * thread, it allocates nothing: parsing is a scan over the address bytes, and
 * publishing is a store plus an atomic bit. An armed learn session briefly
 * locks while claiming its one capture so cancellation can safely happen from
 * another thread. Because at most one drain is outstanding at a time, the
 * drain allocation is bounded by the drain rate rather than by the message
 * rate — a surface sending faster cannot make the receive thread allocate
 * more. A slow or blocked message thread makes MAGDA coarser, never the network
 * thread slower.
 *
 * Nothing here touches the audio thread; parameter writes reach it through the
 * same host-write path the MIDI control surfaces use.
 */
class OscRouter {
  public:
    explicit OscRouter(std::unique_ptr<OscCommandSink> sink);
    ~OscRouter();

    OscRouter(const OscRouter&) = delete;
    OscRouter& operator=(const OscRouter&) = delete;

    /**
     * @brief Receive-thread entry point.
     *
     * **One caller at a time.** The ordered ring below is single-producer, and
     * this is the only thing that writes to it. That holds today because a
     * service owns one `OSCReceiver` and therefore one receive thread, and
     * because rebinding stops the old thread before the new one starts — but
     * it is an invariant of this method rather than an accident of the caller,
     * and a second concurrent caller would corrupt the ring silently.
     *
     * @return true when the message was accepted. False means either that its
     *         address was unknown or that a reserved fixed-namespace address
     *         carried no usable value. Reserved addresses are never offered to
     *         learn or bindings, regardless of this return value.
     */
    bool handleMessage(const juce::OSCMessage& message);

    /**
     * @brief Apply everything pending right now, on the calling thread.
     *
     * The message thread reaches this through the scheduled drain. It is public
     * because tests drive it directly, and because a caller shutting the
     * service down can flush rather than strand the last fader position.
     */
    void drainPending();

    /** Replace the sink. Message thread only, and not while a drain is running. */
    void setSink(std::unique_ptr<OscCommandSink> sink);

    /** Where bound values are applied. Message thread only, as above. */
    void setBindingSink(std::unique_ptr<OscBindingSink> sink);

    /**
     * @brief Publish the OSC bindings now in force.
     *
     * Message thread only. Called whenever the binding registry changes; the
     * receive thread picks the new set up on its next message without
     * synchronising with anything.
     *
     * Bindings are consulted only for addresses the fixed namespace declines,
     * so `/magda/…` cannot be shadowed by one. That is deliberate: those
     * addresses are a documented contract, and a binding that quietly took one
     * over would make a stock template stop working with no way to see why.
     */
    void updateBindings(const std::vector<Binding>& bindings);

    // ========================================================================
    // Learn
    // ========================================================================

    using LearnCallback = std::function<void(const OscLearnCapture&)>;

    /**
     * @brief Capture the next control a surface moves.
     *
     * Any session in progress is cancelled first. The next message carrying a
     * readable number fires `onCaptured` on the message thread, and is consumed
     * rather than routed — wiggling a fader to learn it should not also move
     * whatever it is currently bound to.
     *
     * Addresses in the fixed namespace are not captured. They already do
     * something, that something is a documented contract, and binding one would
     * mean the same gesture both moving a track fader and driving a parameter.
     * A surface whose controls sit on `/magda/…` is already mapped.
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
     * Called from `drainPending` with the slot and the value that landed, so
     * feedback can tell its own echo from a change the user made in MAGDA. On
     * the message thread, inside the drain, once per applied address.
     *
     * A toggle sent with no argument is not reported: it asks for a flip rather
     * than naming a state, so the surface does not know what it produced and has
     * to be told. Delta addresses are not reported for the same reason they are
     * never echoed — there is no state behind them.
     *
     * Set before any message is handled, and cleared by passing `{}`.
     */
    using FeedbackTap = std::function<void(int slot, float value)>;
    void setFeedbackTap(FeedbackTap tap);

    /**
     * @brief The same watch, for bound addresses (#2091).
     *
     * Separate because a binding is not a slot: it is addressed by the string
     * the surface sends, and the value reported is the surface's own position
     * before the binding's mode, curve and range are applied — which is the
     * quantity feedback has to compare against, since that is what it would
     * send back.
     *
     * Called from `drainPending`, on the message thread, once per applied
     * binding value.
     */
    using BindingFeedbackTap = std::function<void(const juce::String& address, float value)>;
    void setBindingFeedbackTap(BindingFeedbackTap tap);

    /**
     * @brief How a pending drain reaches the message thread.
     *
     * Defaults to posting one, or running it inline when the caller is already
     * on the message thread and when there is no message loop at all — the
     * headless case, where deferring would mean never applying.
     *
     * A test replaces it to hold the drain instead of running it, which is the
     * only way to observe what coalescing kept: applied inline, every value
     * lands as it arrives and the table never holds more than one at a time.
     * Set before any message is handled.
     */
    void setDrainScheduler(std::function<void()> scheduler);

    /// Messages accepted since construction, for the settings UI and tests —
    /// "is anything actually arriving?" is the first question a surface that
    /// will not talk to MAGDA raises.
    std::uint64_t acceptedMessageCount() const;

    /// Discrete commands dropped because the ordered ring was full. Non-zero
    /// means a sender outran the message thread with buttons, which no surface
    /// driven by fingers can do — so it reads as a flood rather than as use.
    std::uint64_t droppedCommandCount() const;

    /// Depth of the ordered ring, and the most discrete commands one drain
    /// applies before yielding. Public because it is a characteristic of the
    /// class rather than an implementation detail: it bounds both how much a
    /// burst can carry and how long the message thread stays inside a drain.
    ///
    /// Sized above the whole discrete address space — every track's mute and
    /// solo, plus the transport's four — so a surface can state-dump every
    /// button it owns in one bundle and lose none of it. That is the realistic
    /// worst case; past it, a sender is outrunning the message thread with
    /// buttons, which fingers cannot do. Bounded so that costs a fixed amount
    /// of memory and some dropped presses rather than unbounded growth.
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
    };

    void submit(const OscCommand& command, float value);
    void scheduleDrain();
    void postDrain();
    void requestFollowUpDrain();
    bool pushOrdered(const OscCommand& command, float value);
    void drainOrdered();

    /// nullopt when the address is outside the fixed namespace; otherwise the
    /// bool says whether its payload was accepted. A recognized address with an
    /// unusable payload is still reserved and must not fall through to learn or
    /// bindings.
    std::optional<bool> handleFixedNamespace(const juce::OSCMessage& message);
    bool handleBindings(const juce::OSCMessage& message);
    bool handleLearn(const juce::OSCMessage& message);
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

    /// Swapped whole on the message thread, read through a shared_ptr copy on
    /// the receive thread. Null until bindings are first published.
    std::shared_ptr<OscBindingRoutes> bindingRoutes_;

    /// Latest unapplied value per address. Written by the receive thread, read
    /// by the drain; a torn read is impossible and a stale one is corrected by
    /// the next store, which re-dirties the slot.
    std::array<std::atomic<float>, kOscSlotCount> values_{};

    /// Which slots have something to apply. A bitmap rather than a queue so the
    /// receive thread's publish is one `fetch_or` and repeated writes to one
    /// address cost nothing extra.
    std::array<std::atomic<std::uint64_t>, kDirtyWords> dirty_{};

    /// Whether a drain is already posted. Cleared at the *start* of the drain,
    /// not the end: a value published while the drain is walking the table must
    /// be able to schedule the next one, or it would sit in its slot until the
    /// surface happened to move again.
    std::atomic<bool> drainScheduled_{false};

    /// Triggers and toggles, in the order they arrived. Single-producer: one
    /// `OSCReceiver` owns one receive thread, and `handleMessage` is documented
    /// as that thread's entry point. Single-consumer: the drain.
    std::array<OrderedCommand, kOrderedCapacity> ordered_{};
    std::atomic<std::uint32_t> orderedWrite_{0};
    std::atomic<std::uint32_t> orderedRead_{0};

    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> dropped_{0};

    /// Cleared before the object dies so a drain still sitting in the message
    /// queue completes without touching it.
    std::shared_ptr<std::atomic<bool>> alive_{std::make_shared<std::atomic<bool>>(true)};
};

}  // namespace magda::osc
