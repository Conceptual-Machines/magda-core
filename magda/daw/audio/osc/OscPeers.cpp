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

    // A free slot, or the one whose surface has been quiet longest. Eviction by
    // last-heard is what keeps a real surface from being pushed out by a spoofed
    // source address arriving once.
    int chosen = -1;
    juce::int64 oldest = 0;
    for (int i = 0; i < kMaxPeers; ++i) {
        const auto& entry = entries_[static_cast<std::size_t>(i)];
        if (!entry.used) {
            chosen = i;
            break;
        }
        if (chosen < 0 || entry.lastSeenMs < oldest) {
            chosen = i;
            oldest = entry.lastSeenMs;
        }
    }

    auto& entry = entries_[static_cast<std::size_t>(chosen)];
    entry.used = true;
    entry.host = host;  // the one allocation, once per surface per session
    entry.firstSeenMs = nowMs;
    entry.lastSeenMs = nowMs;
    entry.datagrams = 1;

    generation_.fetch_add(1, std::memory_order_release);
    return chosen;
}

std::vector<OscPeers::Peer> OscPeers::snapshot() const {
    const juce::ScopedLock lock(lock_);

    std::vector<Peer> peers;
    peers.reserve(kMaxPeers);
    for (int i = 0; i < kMaxPeers; ++i) {
        const auto& entry = entries_[static_cast<std::size_t>(i)];
        if (!entry.used)
            continue;
        peers.push_back(Peer{i, entry.host, entry.firstSeenMs, entry.lastSeenMs, entry.datagrams});
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
