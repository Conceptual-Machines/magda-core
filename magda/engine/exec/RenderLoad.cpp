// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exec/RenderLoad.hpp"

namespace magda::engine {

namespace {

thread_local bool renderingAhead = false;

constexpr std::uint64_t kClaimed = 1ULL << 31;
constexpr std::uint64_t kDeviceMask = (1ULL << 28) - 1;

std::uint64_t toNanos(std::chrono::steady_clock::duration elapsed) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    return ns > 0 ? static_cast<std::uint64_t>(ns) : 0;
}

}  // namespace

RenderLoad::AheadScope::AheadScope() : previous_(renderingAhead) {
    renderingAhead = true;
}

RenderLoad::AheadScope::~AheadScope() {
    renderingAhead = previous_;
}

// Track in the high word; the segment, the device plus one and a claimed bit below, so no key is 0.
std::uint64_t RenderLoad::encode(TrackId track, ChainSegment segment, DeviceId device) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(track)) << 32) | kClaimed |
           (static_cast<std::uint64_t>(segment) << 28) |
           (static_cast<std::uint64_t>(device + 1) & kDeviceMask);
}

RenderLoad::Entry RenderLoad::decode(std::uint64_t key) {
    Entry entry;
    entry.track = static_cast<TrackId>(static_cast<std::uint32_t>(key >> 32));
    entry.segment = static_cast<ChainSegment>((key >> 28) & 0x3U);
    entry.device = static_cast<DeviceId>(key & kDeviceMask) - 1;
    return entry;
}

void RenderLoad::addOp(const PlanOp& op, std::chrono::steady_clock::duration elapsed) {
    const auto device = op.kind == OpKind::Device ? op.key.deviceId : INVALID_DEVICE_ID;
    const auto segment = op.kind == OpKind::Device ? op.key.segment : ChainSegment::Fx;
    const auto key = encode(op.key.trackId, segment, device);
    const auto ns = toNanos(elapsed);
    const auto thread = renderingAhead ? Ahead : Callback;

    // Fibonacci hashing spreads the track ids, which sit in the key's high word.
    const auto start = static_cast<std::size_t>((key * 11400714819323198485ULL) >> 53);
    for (std::size_t probe = 0; probe < kSlots; ++probe) {
        auto& slot = slots_[(start + probe) % kSlots];
        auto held = slot.key.load(std::memory_order_acquire);
        if (held == 0 && slot.key.compare_exchange_strong(held, key, std::memory_order_acq_rel))
            held = key;
        if (held != key)
            continue;
        slot.nanos[static_cast<std::size_t>(thread)].fetch_add(ns, std::memory_order_relaxed);
        return;
    }
}

void RenderLoad::addBudget(int numSamples, double sampleRate) {
    if (numSamples <= 0 || sampleRate <= 0.0)
        return;
    budgetNanos_.fetch_add(static_cast<std::uint64_t>(numSamples * 1.0e9 / sampleRate),
                           std::memory_order_relaxed);
}

void RenderLoad::addAheadBlock(std::chrono::steady_clock::duration elapsed, int numSamples,
                               double sampleRate) {
    const auto ns = toNanos(elapsed);
    aheadNanos_.fetch_add(ns, std::memory_order_relaxed);
    if (numSamples <= 0 || sampleRate <= 0.0)
        return;

    const auto share = static_cast<double>(ns) / (numSamples * 1.0e9 / sampleRate);
    auto worst = aheadWorst_.load(std::memory_order_relaxed);
    while (share > worst &&
           !aheadWorst_.compare_exchange_weak(worst, share, std::memory_order_relaxed)) {
    }
}

RenderLoad::AheadShare RenderLoad::takeAhead() {
    return {.busyMs =
                static_cast<double>(aheadNanos_.exchange(0, std::memory_order_relaxed)) / 1.0e6,
            .worstBlock = aheadWorst_.exchange(0.0, std::memory_order_relaxed)};
}

}  // namespace magda::engine
