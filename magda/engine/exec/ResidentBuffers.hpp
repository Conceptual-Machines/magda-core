// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstddef>
#include <memory>

namespace magda::engine {

/** @brief Unlocks and frees a LockedAudioBuffer's block of @p bytes. */
struct LockedBlockRelease {
    std::size_t bytes = 0;
    void operator()(float* block) const;
};

/** @brief An AudioBuffer whose samples sit on pages of their own, written and locked while it
 *  lives, so memory pressure can't evict what a render thread reads (#3402).
 *
 *  A lock that fails (a Linux RLIMIT_MEMLOCK, a Windows working set) leaves the pages written
 *  but evictable. Resizing it as a plain AudioBuffer drops the lock. */
class LockedAudioBuffer : public juce::AudioBuffer<float> {
  public:
    LockedAudioBuffer() = default;
    LockedAudioBuffer(LockedAudioBuffer&&) noexcept = default;
    LockedAudioBuffer& operator=(LockedAudioBuffer&&) noexcept = default;

    /** @brief Allocates, zeroes and locks @p numChannels of @p numSamples. Not for the audio
     *  thread. */
    void prepare(int numChannels, int numSamples);

  private:
    void ownStorage(int numChannels, int numSamples);

    std::unique_ptr<float, LockedBlockRelease> block_;
};

/** @brief Reserves @p bytes in an empty @p buffer and writes them, so a render thread doesn't
 *  map them on first use. A cleared allocation is calloc'd and stays unmapped. */
inline void ensureSizeResident(juce::MidiBuffer& buffer, std::size_t bytes) {
    buffer.ensureSize(bytes);
    if (!buffer.data.isEmpty() || bytes == 0)
        return;
    buffer.data.insertMultiple(0, 0, static_cast<int>(bytes));
    buffer.data.clearQuick();
}

}  // namespace magda::engine
