// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "io/CaptureRing.hpp"
#include "io/RecordDrain.hpp"

/**
 * @file CaptureHistory.hpp
 * @brief The capture ring with a rolling file behind it, so history is bounded by disk (#3052).
 *
 * The audio thread writes the ring as before and never waits. The record thread copies the
 * ring into a file used as a ring of its own, and a reader takes each frame from memory while
 * it is there and from the file once it has left.
 */

namespace magda::engine {

class CaptureHistory final : public RecordDrain {
  public:
    /// Frames one drain moves: more than enough to keep up, small enough to stay behind
    /// the ring by well under its length.
    static constexpr std::int64_t kDrainFrames = 1 << 16;
    /// Frames a drain waits for, so the file takes a few writes a second rather than one
    /// every time the record thread wakes.
    static constexpr std::int64_t kMinDrainFrames = 1 << 13;

    /// Memory only: every call goes to @p ring.
    explicit CaptureHistory(const CaptureRing& ring);

    /// @p diskFrames of history written through @p out into @p file, which is deleted with it.
    CaptureHistory(const CaptureRing& ring, juce::File file,
                   std::unique_ptr<juce::FileOutputStream> out, std::int64_t diskFrames);
    ~CaptureHistory() override;

    CaptureHistory(const CaptureHistory&) = delete;
    CaptureHistory& operator=(const CaptureHistory&) = delete;

    /// Record thread: the ring's newest frames into the file.
    bool drain() override;

    bool onDisk() const {
        return out_ != nullptr;
    }
    /// A write to the file failed; what reached it before still reads.
    bool failed() const {
        return failed_.load(std::memory_order_relaxed);
    }
    std::int64_t capacityFrames() const {
        return capacity_;
    }

    /// The ring's meter, reaching back as far as the file does. Any thread but the audio one.
    CaptureMeter meter(int slices) const;

    /// As CaptureRing::read, from the file for frames that have left the ring.
    bool read(std::int64_t firstFrame, std::int64_t frames, juce::AudioBuffer<float>& out) const;

    /// As CaptureRing::peaks, from the file's peaks for frames that have left the ring.
    std::vector<float> peaks(std::int64_t first, std::int64_t end, int slices) const;

    std::vector<CaptureMidiEvent> midiFrom(std::int64_t firstFrame) const {
        return ring_.midiFrom(firstFrame);
    }

  private:
    std::int64_t oldestOnDisk() const;
    std::int64_t oldestReadable() const;
    bool readDisk(std::int64_t firstFrame, std::int64_t frames, juce::AudioBuffer<float>& out,
                  int outOffset) const;
    bool writeDisk(std::int64_t firstFrame, int frames);

    const CaptureRing& ring_;
    juce::File file_;
    std::unique_ptr<juce::FileOutputStream> out_;
    int numChannels_;
    std::int64_t diskCapacity_ = 0;
    std::int64_t capacity_;

    // Frames [diskFrom_, diskWritten_) have reached the file, less what it has since lapped.
    std::atomic<std::int64_t> diskFrom_{0};
    std::atomic<std::int64_t> diskWritten_{0};
    std::atomic<bool> failed_{false};

    // A coarse peak per CaptureRing::kFramesPerPeak frames of the file.
    std::unique_ptr<std::atomic<float>[]> peaks_;
    std::int64_t peakSlots_ = 0;

    // Record thread only.
    juce::AudioBuffer<float> block_;
    std::vector<float> interleaved_;
    float chunkPeak_ = 0.0f;
};

}  // namespace magda::engine
