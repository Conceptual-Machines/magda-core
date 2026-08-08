#pragma once

#include <juce_osc/juce_osc.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

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
 * ## Why this coalesces
 *
 * A tablet mixer sends continuously while a finger is down — a fader stream at
 * 100 Hz per control is the normal case, not the adversarial one, and eight
 * strips moving at once is a chord change. Posting each message to the message
 * thread would put the DAW's UI thread on the far end of a network firehose,
 * and posting each one *in order* would mean the user's last fader position
 * waiting behind several hundred stale ones.
 *
 * So the receive thread does not post messages; it publishes values. Every
 * address in the fixed namespace owns a slot (`oscSlotIndex`), a store into
 * that slot overwrites whatever had not been applied yet, and the message
 * thread drains the whole table at once. What lands is the most recent value
 * per address, at whatever rate the message thread can absorb, and the cost of
 * a faster surface is finer resolution rather than a longer queue.
 *
 * The consequence worth knowing is that values are not applied in arrival
 * order across different addresses — a drain walks slots. Within one address
 * order is preserved trivially, because only the latest value exists. This is
 * the standard control-surface tradeoff and it is why the namespace carries
 * levels and states rather than anything sequenced.
 *
 * ## Threading
 *
 * `handleMessage` runs on the OSC receive thread. It allocates nothing, takes
 * no lock, and touches only atomics — a slow or blocked message thread makes
 * MAGDA coarser, never the network thread slower. At most one drain is ever
 * outstanding, whatever the message rate.
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
     * @return true when the address was in the fixed namespace and the message
     *         was accepted. False means the message was not ours — an unknown
     *         address, or a known one carrying nothing usable — which is
     *         information for a caller that wants to offer it to bindings, not
     *         an error.
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

  private:
    /// 64 slots per word, rounded up.
    static constexpr int kDirtyWords = (kOscSlotCount + 63) / 64;

    void submit(const OscCommand& command, float value);
    void scheduleDrain();
    void postDrain();

    std::unique_ptr<OscCommandSink> sink_;
    std::function<void()> scheduler_;

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

    std::atomic<std::uint64_t> accepted_{0};

    /// Cleared before the object dies so a drain still sitting in the message
    /// queue completes without touching it.
    std::shared_ptr<std::atomic<bool>> alive_{std::make_shared<std::atomic<bool>>(true)};
};

}  // namespace magda::osc
