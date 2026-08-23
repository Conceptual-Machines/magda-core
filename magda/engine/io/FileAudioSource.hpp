#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "exec/EngineDevice.hpp"
#include "io/AudioFileReader.hpp"
#include "io/ClipPlacement.hpp"

/**
 * @file FileAudioSource.hpp
 * @brief One placed file, read straight through, for a caller that may wait.
 *
 * The offline twin of StreamingAudioSource, and deliberately the same thing
 * with one piece removed. Playback cannot wait for a disk, so it reads through
 * a prefetch stream that hands over whatever arrived in time and counts what
 * did not. An offline render has no callback to starve: waiting for the disk
 * costs the render wall-clock time and costs the audio nothing, so there is no
 * stream, no thread, no chunk pool and no underrun to report.
 *
 * What the two share is the mapping (io/ClipPlacement), and sharing it is the
 * point. A bounce that worked out its own file positions could disagree with
 * what was played by a sample, and the only way anyone would find out is by
 * listening to the render.
 *
 * Never on the audio thread. It reads a file where it stands.
 */

namespace magda::engine {

class FileAudioSource final : public EngineAudioSource {
  public:
    /// The reader is owned elsewhere, and is this source's alone while it
    /// exists. One file backing two consumers is two readers over it, not one
    /// shared between them: a reader seeks and reads as one operation, so two
    /// of them interleaving lands both somewhere neither asked for. The path
    /// that makes this concrete is not exotic, it is a stem export, which is
    /// one render per stem over the same files and is the obvious thing to run
    /// in parallel.
    FileAudioSource(AudioFileReader& reader, const ClipPlacement& placement);

    void prepare(const RenderContext& context) override;

    /**
     * @brief Render the part of this block the clip covers.
     *
     * The same shape as StreamingAudioSource::render, block for block: the rest
     * of the block is silence, a block the transport is not moving through
     * renders nothing, and the file position comes from where the block is on
     * the timeline rather than from where the last read finished. Deriving it
     * from the timeline is what makes an offline render of a range identical
     * whatever block size it was pulled at, since no block's output depends on
     * how the ones before it were cut.
     *
     * Rate conversion is not done here either. The file's own samples come out
     * one per output sample, and a file recorded at another rate plays at the
     * wrong pitch until the clip layer resamples it (#1890).
     */
    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override;

    /// The file sample that plays at a moment on the timeline. The same
    /// mapping StreamingAudioSource uses, at this source's placement and rate.
    std::int64_t sourceSampleAt(double seconds) const {
        return engine::sourceSampleAt(placement_, seconds, sampleRate_);
    }

  private:
    AudioFileReader& reader_;
    ClipPlacement placement_;
    double sampleRate_ = 44100.0;

    /// What the reader fills, because AudioFileReader reads into a buffer and
    /// render() is handed a block. Sized in prepare() and grown if a caller
    /// pulls a longer block than it said it would, which is legal here and
    /// nowhere else: allocating is what the audio thread cannot do, and this is
    /// not the audio thread.
    juce::AudioBuffer<float> scratch_;
};

}  // namespace magda::engine
