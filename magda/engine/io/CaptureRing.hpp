#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "transport/TimeDomains.hpp"

/**
 * @file CaptureRing.hpp
 * @brief The master's recent audio, kept so it can be taken afterwards (#2311).
 *
 * The SampleRing contract, longer and in stereo: the writer never waits and
 * overwrites what nobody took. Each block also leaves a stamp of its beats.
 */

namespace magda::engine {

class TempoMap;

/** @brief One block as the ring holds it. */
struct CaptureStamp {
    std::int64_t frame = 0;  ///< counted from the ring's first write
    int numSamples = 0;
    BeatRange beats;
    bool playing = false;
};

/** @brief A played stretch of the ring: its beats and its frames. */
struct CaptureRun {
    BeatRange beats;
    std::int64_t firstFrame = 0;
    std::int64_t frames = 0;
};

/// The latest stretch played without a stop, loop wrap or locate, or none.
std::optional<CaptureRun> latestPass(const std::vector<CaptureStamp>& stamps);

/// The frame @p beat was heard at during @p run, clamped to it: in a loop,
/// earlier passes hold the same beats at older frames.
std::int64_t frameAt(const std::vector<CaptureStamp>& stamps, const CaptureRun& run, double beat);

/** @brief The bar line @p bars bars back from @p endBeat, the bar it falls in counting as one. */
double barsBefore(const TempoMap& map, double endBeat, int bars);

class CaptureRing {
  public:
    CaptureRing(int numChannels, std::int64_t capacityFrames);

    int numChannels() const {
        return numChannels_;
    }
    std::int64_t capacityFrames() const {
        return capacity_;
    }

    /// Audio thread, once per rendered stretch. A @p block of one channel fills both sides.
    void write(const juce::AudioBuffer<float>& block, BeatRange beats, bool playing);

    /// Every stamp still wholly in the ring, oldest first. Any thread.
    std::vector<CaptureStamp> stamps() const;

    /// Copy @p frames frames from @p firstFrame into @p out; false if any was overwritten.
    bool read(std::int64_t firstFrame, std::int64_t frames, juce::AudioBuffer<float>& out) const;

  private:
    struct Stamp {
        std::atomic<std::int64_t> frame{0};
        std::atomic<int> numSamples{0};
        std::atomic<double> beatStart{0.0};
        std::atomic<double> beatEnd{0.0};
        std::atomic<bool> playing{false};
    };

    /// The oldest frame still safe to read while @p margin more are being written.
    std::int64_t oldestKept(std::int64_t written, std::int64_t margin = 0) const;

    int numChannels_;
    std::int64_t capacity_;
    std::vector<std::unique_ptr<std::atomic<float>[]>> channels_;
    std::unique_ptr<Stamp[]> stamps_;
    std::int64_t stampCapacity_;
    std::atomic<std::int64_t> written_{0};
    std::atomic<std::int64_t> stampCount_{0};
};

}  // namespace magda::engine
