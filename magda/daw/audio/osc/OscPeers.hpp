#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace magda::osc {

/// A peer's identity as it travels through the router: a small integer rather
/// than a string, so a slot table, a ring entry and a binding row can each
/// carry one cheaply.
///
/// **Unique for the life of the process, and never a slot index.** The table
/// below is bounded and reuses storage, but a value already published into the
/// router's queues names the peer that published it and has to keep naming it
/// until it drains. Handing an evicted host's number to whoever took its slot
/// would let a held binding value from the old peer be recorded as the echo of
/// the new one, so the new surface's first update would be suppressed while it
/// still showed its pre-drain snapshot. An id that is never reused makes a
/// stale one match nothing, which is exactly what should happen to it.
using OscPeerId = int;

/// No peer: a message that did not come off a socket. Tests submit these, and
/// so does anything driving the router directly.
inline constexpr OscPeerId kNoOscPeer = -1;

/**
 * @brief Who is talking to MAGDA over OSC (#2096).
 *
 * The thing #2091 could not have. `juce::OSCReceiver` consumed the datagram's
 * source address inside its own receive loop, so feedback had to be aimed at a
 * host the user typed in. MAGDA now owns the read loop, and every datagram
 * arrives with the address it came from — which is what makes "answer whoever
 * is talking" expressible at all.
 *
 * ## Identity is the host, not the host and port
 *
 * A surface sends from an ephemeral port and listens on a fixed one. That
 * asymmetry is why `oscFeedbackPort` survives: the port to reply on cannot be
 * inferred from the port a message came from. It also means the sending port is
 * not part of who a peer *is* — keying on it would mint a new peer, and a new
 * snapshot, every time a surface's socket was recycled.
 *
 * The cost is that two surfaces on one machine are one peer. That is the same
 * limitation stated from the other end: there is one reply port, so there is
 * one reply.
 *
 * ## Bounded, and least-recently-heard evicted
 *
 * Eight, which is more surfaces than a mixer has fingers. The bound is what
 * keeps an unauthenticated UDP port from turning a spoofed source address into
 * unbounded growth, and eviction by last-heard is what keeps a real surface
 * from being pushed out by one.
 *
 * ## Threading
 *
 * `intern` is the receive thread; everything else is the message thread. A
 * `CriticalSection` rather than something lock-free, because the contended case
 * does not exist: the receive thread holds it for a scan of at most eight short
 * strings, and the message thread takes it once per tick.
 *
 * `generation` is the cheap question — it changes only when the set of peers
 * *worth answering* changes, so the projector can notice a surface appearing
 * without comparing strings on every tick. It is atomic so that question needs
 * no lock at all.
 *
 * ## Being heard from is not being answered
 *
 * A peer exists as soon as a datagram arrives, because "something is arriving
 * and none of it parses" is a different problem from silence and the settings
 * UI has to be able to tell them apart. But a peer is only *answerable* once it
 * has sent something MAGDA understood.
 *
 * That distinction is load-bearing rather than tidy. The bind address defaults
 * to every interface, UDP source addresses are trivially spoofed, and a
 * snapshot is hundreds of messages: without it, four bytes of garbage naming
 * someone else as the sender would enrol a peer, and the next tick would open a
 * sender to that host and stream a full project at it. Cycling more spoofed
 * sources than the table holds would sustain that indefinitely and evict the
 * real surface on the way past. Answering only peers that have said one
 * well-formed thing closes it, and the eviction order below is what keeps the
 * flood from pushing the real tablet out.
 */
class OscPeers {
  public:
    static constexpr int kMaxPeers = 8;

    struct Peer {
        OscPeerId id = kNoOscPeer;
        juce::String host;
        juce::int64 firstSeenMs = 0;
        juce::int64 lastSeenMs = 0;
        /// Datagrams, not accepted messages: this counts what arrived from the
        /// peer, including what the router went on to reject.
        std::uint64_t datagrams = 0;
        /// At least one message from this peer was understood. Only these are
        /// answered — see the class comment.
        bool answerable = false;
    };

    /// What a datagram's sender turned out to be. `answerable` is the peer's
    /// state *before* this datagram, which is what lets the caller skip `admit`
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
     * **An unvalidated host never displaces a peer that is being answered.**
     * With every slot holding a real surface there is nowhere to put it, and
     * `kNoOscPeer` is returned: the datagram is still routed, it simply has no
     * peer until it has proved itself through `admit`. Evicting here instead
     * would mean one spoofed packet could drop a live surface, and the surface
     * coming back would re-take the slot and be re-snapshotted — the same churn
     * the answerable bit exists to stop, just one level up.
     *
     * The cost is that the settings list stops counting new hosts once eight
     * surfaces are connected. Losing a diagnostic line at that bound is a much
     * better trade than losing a surface.
     */
    Arrival intern(juce::StringRef host, juce::int64 nowMs);

    /**
     * @brief Admit `host` as a peer worth answering.
     *
     * Receive thread, and only once the router has accepted something from this
     * datagram. Marks an existing peer answerable, or takes a slot for a host
     * `intern` had no room for — evicting the least recently heard peer if that
     * is what it costs, because a surface that has proved itself outranks one
     * that has been quiet longer.
     *
     * Idempotent, and only a transition moves the generation: a fader mid
     * gesture must not make the projector rebuild its fleet on every packet.
     */
    OscPeerId admit(juce::StringRef host, juce::int64 nowMs);

    /// The peers currently known, most recently heard from first.
    std::vector<Peer> snapshot() const;

    /// The host `id` names, or empty when it names nothing.
    juce::String hostFor(OscPeerId id) const;

    int count() const;

    /// Bumped when a peer becomes answerable, when an answerable peer is
    /// displaced, and by `clear`. Deliberately *not* bumped by traffic from a
    /// host that has said nothing usable: a spoofed flood would otherwise make
    /// the projector rebuild its fleet every tick without ever creating a
    /// surface.
    std::uint64_t generation() const {
        return generation_.load(std::memory_order_acquire);
    }

    /// Forget everyone. What the socket closing means: the peers behind a
    /// binding that no longer exists are not peers of the one that replaces it.
    void clear();

  private:
    struct Entry {
        /// `kNoOscPeer` when the slot is free. The id belongs to the host, not
        /// to the slot, so it changes every time the slot is taken over.
        OscPeerId id = kNoOscPeer;
        bool answerable = false;
        juce::String host;
        juce::int64 firstSeenMs = 0;
        juce::int64 lastSeenMs = 0;
        std::uint64_t datagrams = 0;

        bool used() const {
            return id != kNoOscPeer;
        }
    };

    /// A free slot, else the least recently heard entry that is not being
    /// answered, else -1. Caller holds the lock.
    int slotForUnvalidatedHost() const;

    std::array<Entry, kMaxPeers> entries_;
    /// Never reset, not even by `clear` — see `OscPeerId`.
    OscPeerId nextId_ = 0;
    std::atomic<std::uint64_t> generation_{0};
    mutable juce::CriticalSection lock_;
};

}  // namespace magda::osc
