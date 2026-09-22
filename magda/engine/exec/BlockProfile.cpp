#include "exec/BlockProfile.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>

namespace magda::engine {

namespace {

bool readEnabled() {
    const auto* value = std::getenv("MAGDA_ENGINE_PROFILE");
    const auto on = value != nullptr && value[0] != '\0' && value[0] != '0';
    if (on)
        std::atexit(BlockProfile::report);
    return on;
}

}  // namespace

const bool BlockProfile::enabled_ = readEnabled();
std::array<std::atomic<std::uint64_t>, BlockProfile::kKinds> BlockProfile::opNanos_{};
std::array<std::atomic<std::uint64_t>, BlockProfile::kKinds> BlockProfile::opCounts_{};
std::array<std::atomic<std::uint64_t>, BlockProfile::PhaseCount> BlockProfile::phaseNanos_{};
std::array<std::atomic<std::uint64_t>, BlockProfile::PhaseCount> BlockProfile::phaseCounts_{};

namespace {
std::mutex& deviceLock() {
    static std::mutex lock;
    return lock;
}
std::map<std::string, std::pair<std::uint64_t, std::uint64_t>>& deviceTallies() {
    static std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> tallies;
    return tallies;
}
}  // namespace

void BlockProfile::addDevice(const char* name, std::chrono::steady_clock::duration elapsed) {
    const std::scoped_lock lock(deviceLock());
    auto& tally = deviceTallies()[name != nullptr ? name : "device"];
    tally.first += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    ++tally.second;
}

void BlockProfile::report() {
    static const char* const kPhaseNames[PhaseCount] = {
        "beginBlock", "midiPrefix", "resolveParams", "drain (ops)", "serialTail", "wholeBlock"};

    const auto blocks = phaseCounts_[WholeBlock].load();
    std::fprintf(stderr, "\nengine-profile  %llu blocks\n",
                 static_cast<unsigned long long>(blocks));
    std::fprintf(stderr, "  %-18s %10s %10s\n", "phase", "us/block", "calls/blk");
    for (int phase = 0; phase < PhaseCount; ++phase) {
        const auto index = static_cast<std::size_t>(phase);
        if (phaseCounts_[index].load() == 0 || blocks == 0)
            continue;
        std::fprintf(stderr, "  %-18s %10.1f %10.2f\n", kPhaseNames[phase],
                     static_cast<double>(phaseNanos_[index].load()) / 1000.0 /
                         static_cast<double>(blocks),
                     static_cast<double>(phaseCounts_[index].load()) / static_cast<double>(blocks));
    }

    {
        const std::scoped_lock lock(deviceLock());
        std::fprintf(stderr, "  %-28s %10s %10s %10s\n", "device", "us/block", "per blk",
                     "us/each");
        for (const auto& [name, tally] : deviceTallies()) {
            if (blocks == 0)
                break;
            std::fprintf(stderr, "  %-28s %10.1f %10.2f %10.1f\n", name.c_str(),
                         static_cast<double>(tally.first) / 1000.0 / static_cast<double>(blocks),
                         static_cast<double>(tally.second) / static_cast<double>(blocks),
                         static_cast<double>(tally.first) / 1000.0 /
                             static_cast<double>(tally.second));
        }
    }

    std::fprintf(stderr, "  %-18s %10s %10s %10s\n", "op kind", "us/block", "ops/blk", "us/op");
    for (std::size_t kind = 0; kind < kKinds; ++kind) {
        const auto count = opCounts_[kind].load();
        if (count == 0 || blocks == 0)
            continue;
        const auto nanos = static_cast<double>(opNanos_[kind].load());
        std::fprintf(stderr, "  %-18s %10.1f %10.2f %10.1f\n", toString(static_cast<OpKind>(kind)),
                     nanos / 1000.0 / static_cast<double>(blocks),
                     static_cast<double>(count) / static_cast<double>(blocks),
                     nanos / 1000.0 / static_cast<double>(count));
    }
}

}  // namespace magda::engine
