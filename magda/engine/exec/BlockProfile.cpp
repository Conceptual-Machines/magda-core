#include "exec/BlockProfile.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace magda::engine {

namespace {

bool readEnabled() {
    const auto* value = std::getenv("MAGDA_ENGINE_PROFILE");
    const auto on = value != nullptr && value[0] != '\0' && value[0] != '0';
    if (on)
        std::atexit(BlockProfile::report);
    return on;
}

/// One thread's running sum. Atomic only so report() can read it while the thread lives; the
/// owning thread is the only writer, so a load and a store stand in for a locked add.
struct Tally {
    std::atomic<std::uint64_t> nanos{0};
    std::atomic<std::uint64_t> count{0};

    void add(std::chrono::steady_clock::duration elapsed) {
        const auto ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        nanos.store(nanos.load(std::memory_order_relaxed) + ns, std::memory_order_relaxed);
        count.store(count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
};

struct DeviceTally {
    std::atomic<std::uint64_t> key{0};
    char name[48]{};
    Tally tally;
};

constexpr std::size_t kDeviceSlots = 128;

struct ThreadTallies {
    std::array<Tally, BlockProfile::kKinds> ops;
    std::array<Tally, BlockProfile::PhaseCount> phases;
    std::array<DeviceTally, kDeviceSlots> devices;
    std::array<std::atomic<std::uint64_t>, BlockProfile::CounterCount> counters{};
};

std::mutex& registryLock() {
    static std::mutex lock;
    return lock;
}

/// Never freed: a worker that exits before the report still counts.
std::vector<ThreadTallies*>& registry() {
    static std::vector<ThreadTallies*> threads;
    return threads;
}

/// This thread's table. Allocated and registered on the thread's first tally, once.
ThreadTallies& mine() {
    thread_local ThreadTallies* tallies = [] {
        auto* made = new ThreadTallies;
        const std::scoped_lock lock(registryLock());
        registry().push_back(made);
        return made;
    }();
    return *tallies;
}

std::uint64_t hashName(const char* name) {
    std::uint64_t hash = 14695981039346656037ULL;  // FNV-1a
    for (; *name != '\0'; ++name)
        hash = (hash ^ static_cast<unsigned char>(*name)) * 1099511628211ULL;
    return hash == 0 ? 1 : hash;
}

}  // namespace

const bool BlockProfile::enabled_ = readEnabled();

void BlockProfile::addOp(OpKind kind, std::chrono::steady_clock::duration elapsed) {
    mine().ops[static_cast<std::size_t>(kind)].add(elapsed);
}

void BlockProfile::addPhase(Phase phase, std::chrono::steady_clock::duration elapsed) {
    mine().phases[static_cast<std::size_t>(phase)].add(elapsed);
}

void BlockProfile::addDevice(const char* name, std::chrono::steady_clock::duration elapsed) {
    if (name == nullptr)
        name = "device";

    auto& devices = mine().devices;
    const auto key = hashName(name);
    for (std::size_t probe = 0; probe < kDeviceSlots; ++probe) {
        auto& slot = devices[(key + probe) % kDeviceSlots];
        const auto held = slot.key.load(std::memory_order_relaxed);
        if (held == 0) {
            std::strncpy(slot.name, name, sizeof(slot.name) - 1);
            slot.key.store(key, std::memory_order_release);
        } else if (held != key) {
            continue;
        }
        slot.tally.add(elapsed);
        return;
    }
}

void BlockProfile::count(Counter counter, int amount) {
    auto& value = mine().counters[static_cast<std::size_t>(counter)];
    value.store(value.load(std::memory_order_relaxed) + static_cast<std::uint64_t>(amount),
                std::memory_order_relaxed);
}

void BlockProfile::report() {
    static const char* const kPhaseNames[PhaseCount] = {
        "beginBlock", "midiPrefix", "resolveParams", "drain (ops)",
        "serialTail", "wholeBlock", "callback"};
    static const char* const kCounterNames[CounterCount] = {
        "initial ready", "  of them caller-only", "workers signalled",
        "worker sleeps", "wakes without work",    "chains on workers"};

    std::array<std::uint64_t, kKinds> opNanos{}, opCounts{};
    std::array<std::uint64_t, PhaseCount> phaseNanos{}, phaseCounts{};
    std::array<std::uint64_t, CounterCount> counters{};
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> devices;

    {
        const std::scoped_lock lock(registryLock());
        for (const auto* tallies : registry()) {
            for (std::size_t kind = 0; kind < kKinds; ++kind) {
                opNanos[kind] += tallies->ops[kind].nanos.load();
                opCounts[kind] += tallies->ops[kind].count.load();
            }
            for (std::size_t phase = 0; phase < PhaseCount; ++phase) {
                phaseNanos[phase] += tallies->phases[phase].nanos.load();
                phaseCounts[phase] += tallies->phases[phase].count.load();
            }
            for (std::size_t counter = 0; counter < CounterCount; ++counter)
                counters[counter] += tallies->counters[counter].load();
            for (const auto& slot : tallies->devices) {
                if (slot.key.load(std::memory_order_acquire) == 0)
                    continue;
                auto& device = devices[slot.name];
                device.first += slot.tally.nanos.load();
                device.second += slot.tally.count.load();
            }
        }
    }

    // Per callback where the bench counted them, which covers an engine with no phases of its own.
    const auto counted =
        phaseCounts[Callback] != 0 ? phaseCounts[Callback] : phaseCounts[WholeBlock];
    const auto blocks = static_cast<double>(counted);
    std::fprintf(stderr, "\nengine-profile  %llu blocks\n",
                 static_cast<unsigned long long>(counted));
    if (blocks == 0)
        return;

    std::fprintf(stderr, "  %-18s %10s %10s\n", "phase", "us/block", "calls/blk");
    for (std::size_t phase = 0; phase < PhaseCount; ++phase)
        if (phaseCounts[phase] != 0)
            std::fprintf(stderr, "  %-18s %10.1f %10.2f\n", kPhaseNames[phase],
                         static_cast<double>(phaseNanos[phase]) / 1000.0 / blocks,
                         static_cast<double>(phaseCounts[phase]) / blocks);

    std::fprintf(stderr, "  %-22s %10s\n", "scheduling", "per blk");
    for (std::size_t counter = 0; counter < CounterCount; ++counter)
        std::fprintf(stderr, "  %-22s %10.2f\n", kCounterNames[counter],
                     static_cast<double>(counters[counter]) / blocks);

    std::fprintf(stderr, "  %-28s %10s %10s %10s\n", "device", "us/block", "per blk", "us/each");
    for (const auto& [name, tally] : devices)
        std::fprintf(stderr, "  %-28s %10.1f %10.2f %10.1f\n", name.c_str(),
                     static_cast<double>(tally.first) / 1000.0 / blocks,
                     static_cast<double>(tally.second) / blocks,
                     static_cast<double>(tally.first) / 1000.0 / static_cast<double>(tally.second));

    std::fprintf(stderr, "  %-18s %10s %10s %10s\n", "op kind", "us/block", "ops/blk", "us/op");
    for (std::size_t kind = 0; kind < kKinds; ++kind)
        if (opCounts[kind] != 0)
            std::fprintf(
                stderr, "  %-18s %10.1f %10.2f %10.1f\n", toString(static_cast<OpKind>(kind)),
                static_cast<double>(opNanos[kind]) / 1000.0 / blocks,
                static_cast<double>(opCounts[kind]) / blocks,
                static_cast<double>(opNanos[kind]) / 1000.0 / static_cast<double>(opCounts[kind]));
}

}  // namespace magda::engine
