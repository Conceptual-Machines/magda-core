#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace magda::osc {

/// A peer's identity as it travels through the router: a number rather than
/// a string, so a slot table, a ring entry and a binding row can each carry
/// one cheaply.
///
/// **Unique for the life of the process, and never a slot index.** The
/// table below is bounded and reuses storage, but a value already published
/// into the router's queues names the peer that published it until it
/// drains. Handing an evicted host's number to whoever took its slot would
/// let a held binding value from the old peer be recorded as the new peer's
/// own echo, so the new surface's first update would be wrongly suppressed.
/// An id that is never reused makes a stale one match nothing.
///
/// Unsigned and 64 bits wide because "never reused" has to survive an
/// adversary as well as a session: ids are minted only by `admit`, so a
/// flood of spoofed junk can't advance the counter, and even a flood of
/// well-formed OSC from spoofed hosts at a million admissions a second
/// would take half a million years to exhaust it.
using OscPeerId = std::uint64_t;

/// No peer: a message that did not come off a socket, or one from a host
/// that has not said anything MAGDA understood yet. Reserved rather than
/// allocated, which is why ids start at 1.
inline constexpr OscPeerId kNoOscPeer = 0;

/**
 * @brief Who is talking to MAGDA over OSC (#2096).
 *
 * `juce::OSCReceiver` used to consume the datagram's source address inside
 * its own receive loop, so feedback could only aim at a host the user typed
 * in. MAGDA now owns the read loop, so every datagram arrives with the
 * address it came from, making "answer whoever is talking" possible (#2091).
 *
 * ## Identity is the host, not the host and port
 *
 * A surface sends from an ephemeral port and listens on a fixed one
 * (`oscFeedbackPort`), since the reply port can't be inferred from the port
 * a message arrived on. Keying on the sending port would mint a new peer
 * every time a surface's socket recycled. The cost: two surfaces on one
 * machine are one peer, since there's one reply port and so one reply.
 *
 * ## Bounded, and least-recently-heard evicted
 *
 * Eight, more surfaces than a mixer has fingers. The bound keeps an
 * unauthenticated UDP port from turning a spoofed source address into
 * unbounded growth; eviction by last-heard keeps a real surface from being
 * pushed out by one.
 *
 * ## Threading
 *
 * `intern` runs on the receive thread; everything else on the message
 * thread. A `CriticalSection` rather than lock-free, since there's no
 * contended case: the receive thread holds it for a scan of at most eight
 * short strings, and the message thread takes it once per tick.
 *
 * `generation` changes only when the set of peers *worth answering*
 * changes, so the projector can notice a surface appearing without
 * comparing strings every tick, and is atomic so that check needs no lock.
 *
 * ## Being heard from is not being answered
 *
 * A peer exists as soon as a datagram arrives, but is only *answerable*
 * once it has sent something MAGDA understood -- the settings UI needs to
 * tell "garbage arriving" apart from silence.
 *
 * That distinction is a security boundary, not tidiness: the bind address
 * defaults to every interface, UDP source addresses are trivially spoofed,
 * and a snapshot is hundreds of messages. Without it, four spoofed bytes
 * naming someone else as sender would enrol a peer and the next tick would
 * stream a full project at that host, and cycling more spoofed sources than
 * the table holds would sustain that indefinitely while evicting the real
 * surface. Answering only peers that have said one well-formed thing closes
 * that hole.
 */
class OscPeers {
  public:
    static constexpr int kMaxPeers = 8;

    struct Peer {
        OscPeerId id = kNoOscPeer;
        juce::String host;
        juce::int64 firstSeenMs = 0;
        juce::int64 lastSeenMs = 0;
        /// Times this answerable peer spoke after 5+ seconds of silence.
        /// Lets the projector tell this peer's restart apart from a
        /// generation change caused by some other peer.
        std::uint64_t resumptions = 0;
        /// Datagrams, not accepted messages: counts what arrived from the
        /// peer, including what the router went on to reject.
        std::uint64_t datagrams = 0;
        /// At least one message from this peer was understood. Only these
        /// are answered -- see the class comment.
        bool answerable = false;
    };

    /// What a datagram's sender turned out to be. `answerable` is the
    /// peer's state *before* this datagram, letting the caller skip `admit`
    /// on the hot path.
    struct Arrival {
        OscPeerId id = kNoOscPeer;
        bool answerable = false;
    };

    /**
     * @brief The id for `host` on a datagram that has not been parsed yet.
     *
     * Receive thread. Allocates only when a host is seen for the first time.
     *
     * Always returns `kNoOscPeer`: an unvalidated host is counted in the
     * table so the settings list can show it, but isn't given a number,
     * since a number is what answers a surface and what a spoofed flood
     * would otherwise consume without limit.
     *
     * **An unvalidated host never displaces a peer that is being answered.**
     * With every slot holding a real surface the arrival is dropped
     * entirely -- evicting here would mean one spoofed packet could drop a
     * live surface, which would then re-take its slot and be
     * re-snapshotted, the same churn the answerable bit exists to stop.
     *
     * The cost: the settings list stops counting new hosts once eight
     * surfaces are connected. Losing that diagnostic line is a much better
     * trade than losing a surface.
     */
    Arrival intern(juce::StringRef host, juce::int64 nowMs);

    /**
     * @brief Admit `host` as a peer worth answering.
     *
     * Receive thread, once the router has accepted something from this
     * datagram. Marks an existing peer answerable, or takes a slot for a
     * host `intern` had no room for -- evicting the least recently heard
     * peer if needed, since a surface that has proved itself outranks one
     * that's been quiet longer.
     *
     * Idempotent, and only a transition moves the generation: a fader
     * mid-gesture must not make the projector rebuild its fleet on every
     * packet.
     */
    OscPeerId admit(juce::StringRef host, juce::int64 nowMs);

    /// The peers currently known, most recently heard from first.
    std::vector<Peer> snapshot() const;

    /// The host `id` names, or empty when it names nothing.
    juce::String hostFor(OscPeerId id) const;

    int count() const;

    /// Bumped when a peer becomes answerable, resumes after five seconds,
    /// is displaced, or by `clear`. Deliberately not bumped by ordinary
    /// traffic from a host that's said nothing usable, or a spoofed flood
    /// would make the projector rebuild its fleet every tick for nothing.
    std::uint64_t generation() const {
        return generation_.load(std::memory_order_acquire);
    }

    /// Forget everyone. What the socket closing means: the peers behind a
    /// binding that no longer exists are not peers of the one that replaces it.
    void clear();

  private:
    struct Entry {
        bool used = false;
        /// `kNoOscPeer` until the host is admitted -- being admitted and
        /// having an id are the same thing (see OscPeerId).
        OscPeerId id = kNoOscPeer;
        juce::String host;
        juce::int64 firstSeenMs = 0;
        juce::int64 lastSeenMs = 0;
        std::uint64_t resumptions = 0;
        std::uint64_t datagrams = 0;

        bool answerable() const {
            return id != kNoOscPeer;
        }
    };

    /// A free slot, else the least recently heard entry that is not being
    /// answered, else -1. Caller holds the lock.
    int slotForUnvalidatedHost() const;

    std::array<Entry, kMaxPeers> entries_;
    /// Never reset, not even by `clear` -- see `OscPeerId`. Advanced only
    /// by `admit`.
    OscPeerId nextId_ = 1;
    std::atomic<std::uint64_t> generation_{0};
    mutable juce::CriticalSection lock_;
};

}  // namespace magda::osc
