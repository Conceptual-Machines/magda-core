#include "osc/OscPeers.hpp"

#include <algorithm>
#include <limits>

namespace magda::osc {

int OscPeers::slotForUnvalidatedHost() const {
    int chosen = -1;
    juce::int64 oldest = 0;

    for (int i = 0; i < kMaxPeers; ++i) {
        const auto& entry = entries_[static_cast<std::size_t>(i)];
        if (!entry.used)
            return i;
        // A peer being answered is off limits here. See the header: letting an
        // unvalidated host take one means a single spoofed packet drops a live
        // surface, and the surface coming back takes the slot again and is
        // re-snapshotted, which is the churn repeated at a level up.
        if (entry.answerable())
            continue;
        if (chosen < 0 || entry.lastSeenMs < oldest) {
            chosen = i;
            oldest = entry.lastSeenMs;
        }
    }
    return chosen;
}

OscPeers::Arrival OscPeers::intern(juce::StringRef host, juce::int64 nowMs) {
    if (host.isEmpty())
        return {};

    const juce::ScopedLock lock(lock_);

    for (int i = 0; i < kMaxPeers; ++i) {
        auto& entry = entries_[static_cast<std::size_t>(i)];
        if (entry.used && entry.host == host) {
            entry.lastSeenMs = nowMs;
            ++entry.datagrams;
            return Arrival{.id = entry.id, .answerable = entry.answerable()};
        }
    }

    const int chosen = slotForUnvalidatedHost();
    if (chosen < 0)
        return {};  // every slot is a surface; this host waits for `admit`

    // No id: see the header. A host that has said nothing MAGDA understood is
    // counted so the settings list can show it, and named so it can be
    // recognised on its next datagram, but it is not given a number, so a
    // spoofed flood cannot consume them.
    auto& entry = entries_[static_cast<std::size_t>(chosen)];
    entry.used = true;
    entry.id = kNoOscPeer;
    entry.host = host;  // the one allocation, once per host per session
    entry.firstSeenMs = nowMs;
    entry.lastSeenMs = nowMs;
    entry.datagrams = 1;

    // No generation bump either: nothing that is answered has changed. One noise
    // entry replacing another must not make the projector walk its fleet.
    return {};
}

OscPeerId OscPeers::admit(juce::StringRef host, juce::int64 nowMs) {
    if (host.isEmpty())
        return kNoOscPeer;

    // Unreachable in any real sense — see `OscPeerId` for the arithmetic — but
    // stated rather than assumed, because the alternative at the bound is
    // handing a live surface's id to somebody else.
    if (nextId_ == std::numeric_limits<OscPeerId>::max())
        return kNoOscPeer;

    OscPeerId admitted = kNoOscPeer;
    bool changed = false;

    {
        const juce::ScopedLock lock(lock_);

        for (int i = 0; i < kMaxPeers; ++i) {
            auto& entry = entries_[static_cast<std::size_t>(i)];
            if (!entry.used || entry.host != host)
                continue;
            entry.lastSeenMs = nowMs;
            if (!entry.answerable()) {
                entry.id = nextId_++;
                changed = true;
            }
            admitted = entry.id;
            break;
        }

        if (admitted == kNoOscPeer) {
            // `intern` found nowhere to put this host, which means every slot is
            // a surface. It has proved itself now, so it outranks the one that
            // has been quiet longest — this is the only path that displaces a
            // peer being answered.
            int chosen = slotForUnvalidatedHost();
            if (chosen < 0) {
                juce::int64 oldest = 0;
                for (int i = 0; i < kMaxPeers; ++i) {
                    const auto& entry = entries_[static_cast<std::size_t>(i)];
                    if (chosen < 0 || entry.lastSeenMs < oldest) {
                        chosen = i;
                        oldest = entry.lastSeenMs;
                    }
                }
            }

            // Whatever was here belonged to another host, so its count and its
            // id go with it. This datagram is the first from the one taking
            // over, and it gets an id of its own.
            auto& entry = entries_[static_cast<std::size_t>(chosen)];
            entry.used = true;
            entry.id = nextId_++;
            entry.host = host;
            entry.firstSeenMs = nowMs;
            entry.lastSeenMs = nowMs;
            entry.datagrams = 1;
            admitted = entry.id;
            changed = true;
        }
    }

    // A surface appearing, or one being replaced by another. Either way the
    // projector has a fleet to rebuild; anything short of that leaves it alone.
    if (changed)
        generation_.fetch_add(1, std::memory_order_release);
    return admitted;
}

std::vector<OscPeers::Peer> OscPeers::snapshot() const {
    const juce::ScopedLock lock(lock_);

    std::vector<Peer> peers;
    peers.reserve(kMaxPeers);
    for (int i = 0; i < kMaxPeers; ++i) {
        const auto& entry = entries_[static_cast<std::size_t>(i)];
        if (!entry.used)
            continue;
        peers.push_back(Peer{.id = entry.id,
                             .host = entry.host,
                             .firstSeenMs = entry.firstSeenMs,
                             .lastSeenMs = entry.lastSeenMs,
                             .datagrams = entry.datagrams,
                             .answerable = entry.answerable()});
    }

    std::sort(peers.begin(), peers.end(),
              [](const Peer& a, const Peer& b) { return a.lastSeenMs > b.lastSeenMs; });
    return peers;
}

juce::String OscPeers::hostFor(OscPeerId id) const {
    if (id == kNoOscPeer)
        return {};  // nobody, and every unadmitted entry would match it

    // A scan of eight rather than an index, because an id is not a slot: see
    // the header on why it cannot be one.
    const juce::ScopedLock lock(lock_);
    for (const auto& entry : entries_)
        if (entry.id == id)
            return entry.host;
    return {};
}

int OscPeers::count() const {
    const juce::ScopedLock lock(lock_);
    return static_cast<int>(std::count_if(entries_.begin(), entries_.end(),
                                          [](const Entry& entry) { return entry.used; }));
}

void OscPeers::clear() {
    {
        const juce::ScopedLock lock(lock_);
        for (auto& entry : entries_)
            entry = Entry{};
        // `nextId_` deliberately keeps climbing. Resetting it would hand the
        // ids of the peers just forgotten to whoever connects after the rebind,
        // which is the reuse the ids exist to avoid.
    }
    generation_.fetch_add(1, std::memory_order_release);
}

}  // namespace magda::osc
