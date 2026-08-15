#include "osc/OscPeers.hpp"

#include <algorithm>

namespace magda::osc {

OscPeerId OscPeers::intern(juce::StringRef host, juce::int64 nowMs) {
    if (host.isEmpty())
        return kNoOscPeer;

    const juce::ScopedLock lock(lock_);

    for (int i = 0; i < kMaxPeers; ++i) {
        auto& entry = entries_[static_cast<std::size_t>(i)];
        if (entry.used && entry.host == host) {
            entry.lastSeenMs = nowMs;
            ++entry.datagrams;
            return i;
        }
    }

    // A free slot first; then the peer that has never said anything usable and
    // has been quiet longest; and only if every slot holds a real surface, the
    // quietest of those.
    //
    // The middle rule is the one that matters. Without it a flood of spoofed
    // source addresses walks the table and pushes the user's tablet out of it,
    // which drops its surface and re-snapshots it when the next real packet
    // arrives. Sacrificing the peers that have only ever sent noise costs
    // nothing but a line in the settings list.
    int chosen = -1;
    bool chosenAnswerable = true;
    juce::int64 oldest = 0;

    for (int i = 0; i < kMaxPeers; ++i) {
        const auto& entry = entries_[static_cast<std::size_t>(i)];
        if (!entry.used) {
            chosen = i;
            chosenAnswerable = false;
            break;
        }
        const bool better = chosen < 0 || (chosenAnswerable && !entry.answerable) ||
                            (chosenAnswerable == entry.answerable && entry.lastSeenMs < oldest);
        if (better) {
            chosen = i;
            chosenAnswerable = entry.answerable;
            oldest = entry.lastSeenMs;
        }
    }

    auto& entry = entries_[static_cast<std::size_t>(chosen)];
    // A surface is losing its slot, so whatever was being sent to it has to
    // stop. Nothing else here moves the generation: see the header.
    const bool displacedASurface = entry.used && entry.answerable;

    entry.used = true;
    entry.answerable = false;
    entry.host = host;  // the one allocation, once per surface per session
    entry.firstSeenMs = nowMs;
    entry.lastSeenMs = nowMs;
    entry.datagrams = 1;

    if (displacedASurface)
        generation_.fetch_add(1, std::memory_order_release);
    return chosen;
}

void OscPeers::markAnswerable(OscPeerId id) {
    if (id < 0 || id >= kMaxPeers)
        return;

    {
        const juce::ScopedLock lock(lock_);
        auto& entry = entries_[static_cast<std::size_t>(id)];
        if (!entry.used || entry.answerable)
            return;
        entry.answerable = true;
    }

    // The transition, and only the transition: a fader mid-gesture calls this on
    // every packet, and the projector must not rebuild its fleet for each one.
    generation_.fetch_add(1, std::memory_order_release);
}

std::vector<OscPeers::Peer> OscPeers::snapshot() const {
    const juce::ScopedLock lock(lock_);

    std::vector<Peer> peers;
    peers.reserve(kMaxPeers);
    for (int i = 0; i < kMaxPeers; ++i) {
        const auto& entry = entries_[static_cast<std::size_t>(i)];
        if (!entry.used)
            continue;
        peers.push_back(Peer{.id = i,
                             .host = entry.host,
                             .firstSeenMs = entry.firstSeenMs,
                             .lastSeenMs = entry.lastSeenMs,
                             .datagrams = entry.datagrams,
                             .answerable = entry.answerable});
    }

    std::sort(peers.begin(), peers.end(),
              [](const Peer& a, const Peer& b) { return a.lastSeenMs > b.lastSeenMs; });
    return peers;
}

juce::String OscPeers::hostFor(OscPeerId id) const {
    if (id < 0 || id >= kMaxPeers)
        return {};

    const juce::ScopedLock lock(lock_);
    const auto& entry = entries_[static_cast<std::size_t>(id)];
    return entry.used ? entry.host : juce::String();
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
    }
    generation_.fetch_add(1, std::memory_order_release);
}

}  // namespace magda::osc
