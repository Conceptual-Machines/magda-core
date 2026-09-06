#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "exec/OfflineRender.hpp"
#include "exec/RenderContext.hpp"
#include "io/PcmQuantiser.hpp"

/**
 * @file AudioFileSink.hpp
 * @brief What turns a render into a file (#2447).
 *
 * Everything renderOffline produced until now went into a buffer a test held.
 * An export, a bounce and a freeze are this sink pointed at different files.
 *
 * A fixed-point target goes through PcmQuantiser (#2248) and reaches the writer
 * as codes, never as float: a writer handed float rounds it itself, after the
 * dither and outside the shaper's feedback, which is the one place a second
 * rounding must not happen.
 *
 * What the sink is handed is what the file holds. The range, the tail and the
 * plan's latency are all settled before anything reaches here
 * (exec/OfflineRender), so there is no pre-roll in the file and no second pass
 * to cut one off -- the incumbent's export writes the file, reads it back and
 * rewrites it shorter, and none of that survives.
 */

namespace magda::engine {

/// What the writer's own stream saw, which outlives the writer. Defined in the
/// .cpp: nothing outside this unit has a use for it.
struct StreamReport;

/// The containers a render can be stored in, which is what the export dialog
/// already offers.
enum class AudioFileFormat : std::uint8_t {
    wav,
    flac,
};

/** The file's format, which is not the render's. */
struct AudioFileSpec {
    AudioFileFormat format = AudioFileFormat::wav;

    /// 16 or 24 for fixed point, 32 for float. FLAC holds the first two.
    int bitDepth = 24;

    /// Unset lets the depth decide, which is what a caller with no opinion
    /// wants: TPDF wherever the target quantises, nothing at 32-bit float.
    std::optional<DitherMode> dither;
};

/// What a depth is dithered with when the spec does not say.
DitherMode defaultDitherFor(int bitDepth);

class AudioFileSink final : public OfflineRenderSink {
  public:
    /**
     * @brief A sink writing @p destination, or null.
     *
     * Null for a spec no format holds (FLAC above 24 bits, a depth nothing
     * writes), a format that will not take the render's shape, and a file that
     * could not be opened. Refused here rather than at the first block, so a
     * render nothing can store is never run.
     *
     * Nothing is written to @p destination itself until close(). The render
     * goes to a file beside it, so a refusal here and a failure later both
     * leave whatever was already there -- which, for an export somebody is
     * re-rendering, is the last one that worked.
     *
     * The rate and the channel count come from @p context rather than from the
     * caller. They are what the plan was prepared with, and a header that
     * disagrees is a file that plays at the wrong speed or the wrong width.
     */
    static std::unique_ptr<AudioFileSink> create(const juce::File& destination,
                                                 const AudioFileSpec& spec,
                                                 const RenderContext& context);

    ~AudioFileSink() override;

    /// Neither copied nor moved: it owns an open file.
    AudioFileSink(const AudioFileSink&) = delete;
    AudioFileSink& operator=(const AudioFileSink&) = delete;
    AudioFileSink(AudioFileSink&&) = delete;
    AudioFileSink& operator=(AudioFileSink&&) = delete;

    /// Off the audio thread, as the sink contract already says: this allocates
    /// on its first block and writes a file on every one.
    void write(const juce::AudioBuffer<float>& block, int numSamples) override;

    /**
     * @brief Finish the file, put it at the destination, and say whether it
     *        holds the render.
     *
     * Idempotent, and the destructor calls it. False means the render did not
     * reach the disk whole -- a block refused, or a finish that ran out of
     * space -- and in that case the destination is left exactly as it was.
     *
     * A render that was cancelled is not a render that failed: what the sink
     * was handed is written, and a short bounce is the caller's to keep or
     * delete, which is what the render's own contract already says.
     */
    bool close();

    /// Samples per channel handed to the writer.
    std::int64_t samplesWritten() const {
        return samplesWritten_;
    }

  private:
    AudioFileSink(std::unique_ptr<juce::AudioFormatWriter> writer,
                  std::unique_ptr<juce::TemporaryFile> temporary,
                  std::shared_ptr<StreamReport> report, int numChannels,
                  std::optional<PcmQuantiser> quantiser);

    /// Room for @p numSamples of codes, and the per-channel pointers into it.
    void resizeCodes(int numSamples);

    std::unique_ptr<juce::AudioFormatWriter> writer_;

    /// Where the render is written, and what puts it at the destination once
    /// there is a whole file to put there.
    std::unique_ptr<juce::TemporaryFile> temporary_;

    /// Shared with the writer's stream, which the writer destroys: what it says
    /// about the last of the file is only readable after that.
    std::shared_ptr<StreamReport> report_;

    int numChannels_ = 2;

    /// Unset for a float target, where there is no grid to land on.
    std::optional<PcmQuantiser> quantiser_;

    /// The block, quantised. A copy because the buffer belongs to the renderer
    /// and is reused, and quantising is done in place.
    juce::AudioBuffer<float> scratch_;

    std::vector<int> codes_;
    std::vector<int*> codeChannels_;

    /// Samples per channel the code buffer holds, which is also its stride: a
    /// shorter block writes the front of each channel's room and leaves the
    /// pointers where they were.
    int codeCapacity_ = 0;

    std::int64_t samplesWritten_ = 0;
    bool failed_ = false;
    bool closed_ = false;
};

}  // namespace magda::engine
