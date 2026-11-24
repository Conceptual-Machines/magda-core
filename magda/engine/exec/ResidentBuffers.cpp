// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exec/ResidentBuffers.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#if JUCE_WINDOWS
    #include <malloc.h>
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace magda::engine {
namespace {

std::size_t pageSize() {
#if JUCE_WINDOWS
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return static_cast<std::size_t>(info.dwPageSize);
#else
    return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
}

float* allocateLocked(std::size_t bytes) {
    const auto page = pageSize();
#if JUCE_WINDOWS
    auto* block = _aligned_malloc(bytes, page);
    if (block == nullptr)
        return nullptr;
    std::memset(block, 0, bytes);
    VirtualLock(block, bytes);
#else
    void* block = nullptr;
    if (posix_memalign(&block, page, bytes) != 0)
        return nullptr;
    std::memset(block, 0, bytes);
    mlock(block, bytes);
#endif
    return static_cast<float*>(block);
}

}  // namespace

void LockedBlockRelease::operator()(float* block) const {
#if JUCE_WINDOWS
    VirtualUnlock(block, bytes);
    _aligned_free(block);
#else
    munlock(block, bytes);
    std::free(block);
#endif
}

void LockedAudioBuffer::ownStorage(int numChannels, int numSamples) {
    // Swapped in whole: setSize keeps a same-sized buffer pointing at the block freed below.
    juce::AudioBuffer<float> owned(numChannels, numSamples);
    owned.clear();
    juce::AudioBuffer<float>::operator=(std::move(owned));
    block_.reset();
}

void LockedAudioBuffer::prepare(int numChannels, int numSamples) {
    if (numChannels <= 0 || numSamples <= 0) {
        ownStorage(std::max(numChannels, 0), std::max(numSamples, 0));
        return;
    }

    // Each channel starts 16-byte aligned, as JUCE's own allocation does.
    const auto stride = (static_cast<std::size_t>(numSamples) + 3) & ~std::size_t{3};
    const auto page = pageSize();
    const auto used = stride * static_cast<std::size_t>(numChannels) * sizeof(float);
    const auto bytes = (used + page - 1) / page * page;

    std::unique_ptr<float, LockedBlockRelease> block(allocateLocked(bytes),
                                                     LockedBlockRelease{bytes});
    if (block == nullptr) {
        ownStorage(numChannels, numSamples);
        return;
    }

    std::vector<float*> channels(static_cast<std::size_t>(numChannels));
    for (std::size_t channel = 0; channel < channels.size(); ++channel)
        channels[channel] = block.get() + channel * stride;
    setDataToReferTo(channels.data(), numChannels, numSamples);
    block_ = std::move(block);
}

}  // namespace magda::engine
