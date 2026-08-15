#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace magda::osc {

/// A peer's identity as it travels through the router: an index into `OscPeers`
/// rather than a string, so a slot table, a ring entry and a binding row can
/// each carry one in a byte.
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
 * `generation` is the cheap question — it changes only when the *set* of peers
 * changes, so the projector can notice a surface appearing without comparing
 * strings on every tick. It is atomic so that question needs no lock at all.
 */
class OscPeers {
  public:
    static constexpr int kMaxPeers = 8;
    static_assert(kMaxPeers <= 127, "a peer id has to fit the byte the router carries it in");

    struct Peer {
        OscPeerId id = kNoOscPeer;
        juce::String host;
        juce::int64 firstSeenMs = 0;
        juce::int64 lastSeenMs = 0;
        /// Datagrams, not accepted messages: this counts what arrived from the
        /// peer, including what the router went on to reject.
        std::uint64_t datagrams = 0;
    };

    /**
     * @brief The id for `host`, adding it if this is the first datagram from it.
     *
     * Receive thread. Allocates only when a host is seen for the first time,
     * which is once per surface per session.
     */
    OscPeerId intern(juce::StringRef host, juce::int64 nowMs);

    /// The peers currently known, most recently heard from first.
    std::vector<Peer> snapshot() const;

    /// The host `id` names, or empty when it names nothing.
    juce::String hostFor(OscPeerId id) const;

    int count() const;

    /// Bumped whenever a peer is added or evicted, and never for traffic from a
    /// peer already known.
    std::uint64_t generation() const {
        return generation_.load(std::memory_order_acquire);
    }

    /// Forget everyone. What the socket closing means: the peers behind a
    /// binding that no longer exists are not peers of the one that replaces it.
    void clear();

  private:
    struct Entry {
        bool used = false;
        juce::String host;
        juce::int64 firstSeenMs = 0;
        juce::int64 lastSeenMs = 0;
        std::uint64_t datagrams = 0;
    };

    std::array<Entry, kMaxPeers> entries_;
    std::atomic<std::uint64_t> generation_{0};
    mutable juce::CriticalSection lock_;
};

}  // namespace magda::osc
