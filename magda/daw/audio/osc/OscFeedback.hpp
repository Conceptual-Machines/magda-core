#pragma once

#include <juce_osc/juce_osc.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "osc/OscAddress.hpp"

namespace magda::osc {

// ============================================================================
// OscMessageSink
// ============================================================================

/**
 * @brief Where a feedback message goes once it has an address and a value.
 *
 * The seam between "MAGDA's state changed and the surface should hear about it"
 * and "a datagram left the machine", so everything above it — the diffing, the
 * echo rule, the budget — is testable without opening a socket. Called on the
 * message thread.
 *
 * @return false when the message could not be sent, which for UDP means the
 *         socket is not connected rather than that the surface is not there.
 */
class OscMessageSink {
  public:
    virtual ~OscMessageSink() = default;
    virtual bool send(const juce::String& address, float value) = 0;
};

// ============================================================================
// OscSenderSink
// ============================================================================

/**
 * @brief The sink backed by a real `juce::OSCSender`.
 *
 * One socket for the lifetime of the sink, aimed at one destination. There is
 * no per-peer routing because there is no way to learn a peer:
 * `juce::OSCReceiver` hands a realtime listener an `OSCMessage` and drops the
 * datagram's source address inside its own receive loop, and JUCE's OSC parser
 * is private to the module, so reading the sender would mean reimplementing it.
 * Feedback therefore goes where configuration says and nowhere else.
 */
class OscSenderSink : public OscMessageSink {
  public:
    OscSenderSink() = default;
    ~OscSenderSink() override;

    OscSenderSink(const OscSenderSink&) = delete;
    OscSenderSink& operator=(const OscSenderSink&) = delete;

    /**
     * @brief Aim at a destination, replacing any current one.
     *
     * An empty host disconnects, which is how feedback is turned off: there is
     * no separate enable, because a destination is the whole of what feedback
     * needs to exist.
     *
     * @return true when a socket is connected afterwards.
     */
    bool connect(const juce::String& host, int port);
    void disconnect();

    bool isConnected() const {
        return connected_;
    }

    juce::String destinationHost() const {
        return host_;
    }

    int destinationPort() const {
        return port_;
    }

    bool send(const juce::String& address, float value) override;

  private:
    juce::OSCSender sender_;
    juce::String host_;
    int port_ = 0;
    bool connected_ = false;
};

// ============================================================================
// OscFeedback
// ============================================================================

/**
 * @brief Echoes MAGDA's state back onto the fixed namespace (#2091).
 *
 * A surface that can drive MAGDA but never hears back shows whatever its
 * template defaulted to: a motorised fader stays where the user left it, and
 * moving a fader in the app leaves the surface lying about the value. This is
 * the half that answers.
 *
 * ## A projection, diffed
 *
 * Callers publish the current value of an address; nothing is sent unless it
 * differs from the value that address was last told. That one rule covers three
 * of the requirements at once. "On change" is what the diff means. The rate
 * limit falls out of the flush cadence, because an address can be sent at most
 * once per flush however often it was published in between. And latest value
 * wins by construction, because a slot holds one value.
 *
 * The address space is fixed and bounded at compile time (`kOscSlotCount`), so
 * every address owns a slot and the whole of this is arrays and bitmaps rather
 * than a map keyed by string.
 *
 * ## The echo rule
 *
 * A value that has just arrived from the surface is a value the surface already
 * shows, so sending it back is redundant, and for a control that reports its own
 * motorised position it is a loop. `noteReceived` records the value and sets a
 * bit; a flush suppresses a slot whose projected value matches what was received
 * *and* whose bit is set, then clears every bit.
 *
 * The bit is the part that makes this correct rather than merely quiet. Matching
 * on value alone looks equivalent and is not: a surface sets a fader to 0.62,
 * the user drags MAGDA's fader to 0.9 and then back to 0.62, and a value-only
 * rule would suppress the send that brings the surface back from 0.9. Bounding
 * the suppression to the flush the arrival belongs to is what stops that.
 *
 * A suppressed slot is still recorded as sent, so the next value that differs
 * from it goes out normally. And because the bit is cleared every flush, the
 * flush after a gesture ends sends the value once — a confirmation rather than
 * an echo, and where the surface finds out what MAGDA rounded it to.
 *
 * ## Snapshots
 *
 * `requestSnapshot` sends every slot that has a value, ignoring both the diff
 * and the echo rule, so a surface that has just joined is correct rather than
 * blank. Bounded by what the project really has, not by `kMaxTrackNumber`:
 * `retire` is how a caller says a position no longer addresses anything, and a
 * retired slot is neither sent nor remembered.
 *
 * ## The budget
 *
 * A flush sends at most `kMaxMessagesPerFlush`. Only a snapshot of a large
 * project reaches that, and a snapshot is also the one thing that has to finish,
 * so what does not fit stays dirty and goes out on the next flush rather than
 * being dropped. A sixty-track snapshot is three ticks, not one long one.
 *
 * ## Threading
 *
 * Message thread only, all of it. `noteReceived` is called from
 * `OscRouter::drainPending`, which is already there, and `flush` from the
 * projection above it.
 */
class OscFeedback {
  public:
    explicit OscFeedback(std::unique_ptr<OscMessageSink> sink);
    ~OscFeedback();

    OscFeedback(const OscFeedback&) = delete;
    OscFeedback& operator=(const OscFeedback&) = delete;

    /// Above a full snapshot of an ordinary project and well under one of the
    /// largest the namespace can address, which is the case it exists to split
    /// across flushes rather than to truncate.
    static constexpr int kMaxMessagesPerFlush = 192;

    /// How close two values have to be for one to count as the echo of the
    /// other. Loose enough to absorb a normalized value that has been through a
    /// parameter's real range and back, tight enough that no gesture a user can
    /// make lands inside it.
    static constexpr float kEchoEpsilon = 1.0e-4f;

    /**
     * @brief State the address at `slot` currently has.
     *
     * Cheap to call with an unchanged value, which is the common case: the
     * projection above re-reads whole blocks rather than tracking which member
     * of a track moved.
     */
    void publish(int slot, float value);

    void publish(const OscCommand& command, float value) {
        publish(oscSlotIndex(command), value);
    }

    /// Say that `slot` addresses nothing now — a mixer position past the last
    /// track. Nothing is sent, and the slot is forgotten, so a snapshot does not
    /// resurrect the value a deleted track had.
    void retire(int slot);

    void retire(const OscCommand& command) {
        retire(oscSlotIndex(command));
    }

    /// Record a value that arrived from the surface on `slot`. Called by the
    /// router's drain, on the message thread.
    void noteReceived(int slot, float value);

    /// Send everything with a value on the next flush, diff and echo rule
    /// ignored.
    void requestSnapshot();

    /**
     * @brief Send what is owed.
     *
     * @return how many messages were sent, which is 0 for the ordinary case of
     *         a project in which nothing moved.
     */
    int flush();

    /// Forget every value without sending anything. For a project swap, where
    /// what was last sent describes state that no longer exists — and where the
    /// caller wants the surface refreshed rather than diffed against a ghost.
    void reset();

    OscMessageSink& sink() {
        return *sink_;
    }

    /// Messages sent since construction. The settings UI shows it, because
    /// "is anything going out?" is the first question a silent surface raises.
    std::uint64_t sentMessageCount() const {
        return sent_;
    }

    /// Slot sends postponed by the budget. Non-zero means a snapshot of a large
    /// project spanned several flushes, which is the design rather than a fault;
    /// a number that keeps climbing while nothing is moving is not.
    std::uint64_t deferredMessageCount() const {
        return deferred_;
    }

  private:
    static constexpr int kWords = (kOscSlotCount + 63) / 64;

    static bool test(const std::array<std::uint64_t, kWords>& bits, int slot) {
        return (bits[static_cast<std::size_t>(slot / 64)] & (1ULL << (slot % 64))) != 0;
    }
    static void set(std::array<std::uint64_t, kWords>& bits, int slot) {
        bits[static_cast<std::size_t>(slot / 64)] |= 1ULL << (slot % 64);
    }
    static void clear(std::array<std::uint64_t, kWords>& bits, int slot) {
        bits[static_cast<std::size_t>(slot / 64)] &= ~(1ULL << (slot % 64));
    }

    /// True for the two seek addresses, which carry a distance rather than a
    /// position. There is no state behind them to echo, on the way in or out.
    static bool isSendable(const OscCommand& command);

    std::unique_ptr<OscMessageSink> sink_;

    std::array<float, kOscSlotCount> current_{};
    std::array<float, kOscSlotCount> lastSent_{};
    std::array<float, kOscSlotCount> lastReceived_{};

    /// A value has been published and not retired.
    std::array<std::uint64_t, kWords> valid_{};
    /// `lastSent_` means something.
    std::array<std::uint64_t, kWords> hasSent_{};
    /// Published and not yet sent.
    std::array<std::uint64_t, kWords> dirty_{};
    /// Received since the last flush.
    std::array<std::uint64_t, kWords> echoed_{};

    bool snapshotPending_ = false;
    std::uint64_t sent_ = 0;
    std::uint64_t deferred_ = 0;
};

}  // namespace magda::osc
