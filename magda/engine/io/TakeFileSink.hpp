#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "exec/RenderContext.hpp"
#include "io/AudioFileSink.hpp"
#include "io/RecordStream.hpp"

/**
 * @file TakeFileSink.hpp
 * @brief Where a take's samples land: one file per loop pass (#2461).
 *
 * The record thread's end of a RecordStream. A file is opened at the first
 * sample of a pass rather than when the take is armed, so a pass that captured
 * nothing leaves nothing behind, and the next one opens where the audio thread
 * said the last one ended.
 *
 * Boundaries travel on a lane of their own because the queue carries only
 * audio: where a loop wrapped is something only the audio thread knows, and a
 * chunk handed over here may straddle one.
 */

namespace magda::engine {

/** @brief One pass, once it is on disk. */
struct RecordedPass {
    juce::File file;
    std::int64_t samples = 0;
    double durationSeconds = 0.0;
};

class TakeFileSink final : public RecordSink {
  public:
    /**
     * @brief A sink writing passes of @p spec into @p directory.
     *
     * Files are named `<baseName>_<n>`, taking the first number nothing holds,
     * so two takes named alike cannot land on one file. @p context gives the
     * rate and the channel count they are written at, which is the width of the
     * input the take reads rather than the plan's.
     */
    TakeFileSink(juce::File directory, juce::String baseName, AudioFileSpec spec,
                 RenderContext context);

    /**
     * @brief End the pass being written at @p takeSample. On the audio thread.
     *
     * Counted from the take's first sample, so the record thread can split a
     * chunk it was handed whole. False when the lane is full, which is a disk
     * dozens of passes behind and a take already reporting lost samples.
     */
    bool markPassEnd(std::int64_t takeSample);

    bool writeAudio(juce::dsp::AudioBlock<const float> audio, int numSamples) override;

    void finish() override;

    /// The passes written, in order. After finish(), off the record thread.
    std::span<const RecordedPass> passes() const {
        return passes_;
    }

    /// Whether a file refused a write or would not open at all.
    bool failed() const {
        return failed_;
    }

  private:
    /// The boundary the current pass ends on, or nothing.
    std::optional<std::int64_t> peekBoundary() const;
    void popBoundary();

    /// Finish whatever file is open and count the pass. A pass with no samples
    /// has no file and is not one.
    void closePass();

    /// Write @p numSamples into the open pass, opening a file for it if this is
    /// its first. False once the take is broken.
    bool writeToPass(juce::dsp::AudioBlock<const float> audio, int numSamples);

    /// The first name in the take's series that nothing holds.
    juce::File nextFile();

    juce::File directory_;
    juce::String baseName_;
    AudioFileSpec spec_;
    RenderContext context_;

    std::unique_ptr<AudioFileSink> pass_;
    juce::File passFile_;
    int nextIndex_ = 1;

    std::vector<RecordedPass> passes_;

    /// Samples handed to the sink so far, which is what a boundary is counted
    /// in. Not the same as the samples the take offered, once it has lost any.
    std::int64_t written_ = 0;

    bool failed_ = false;

    /// Outstanding pass ends. A loop pass is seconds of audio, so a lane this
    /// deep is a disk that stopped writing rather than one running behind.
    static constexpr std::size_t kBoundaryCapacity = 64;

    std::array<std::int64_t, kBoundaryCapacity> boundaries_{};
    std::atomic<std::uint64_t> boundaryWrite_{0};
    std::atomic<std::uint64_t> boundaryRead_{0};
};

}  // namespace magda::engine
