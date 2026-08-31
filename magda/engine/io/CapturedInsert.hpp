#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "exec/EngineDevice.hpp"

namespace magda::engine {

/**
 * @file CapturedInsert.hpp
 * @brief An insert a bounce can run (#2245).
 *
 * An offline render cannot run the outside world faster than real time, so a
 * bounce either runs the insert at real time or plays back a capture of it, and
 * this is the second. The incumbent does the same thing with a hidden plugin
 * sitting behind the insert; here it is an implementation of the interface the
 * two insert ops already resolve to, which is the whole point of the interface
 * being what it is: a render binds one of these instead of the live one and
 * nothing else in the engine changes.
 *
 * ## Two modes, and why they are exclusive
 *
 * **Capturing** is the live pass. What comes back from the hardware is written
 * down, timeline sample by timeline sample, and passed through untouched: the
 * person listening hears the outboard, and the render that follows has
 * something to replay.
 *
 * **Playing** is the bounce. Nothing is sent, because there is nothing on the
 * other end that could answer in time, and what comes back is what was captured
 * at the same timeline position.
 *
 * A single object cannot be doing both, and the mode says which. It is the same
 * split the incumbent's capture plugin has and for the same reason: a capture
 * taken while replaying a capture is a copy of itself.
 *
 * ## Position, not order
 *
 * Everything here is addressed by timeline sample rather than by how many
 * blocks have gone by. A bounce does not visit the timeline in the same block
 * sizes the live pass did, and an offline render may not visit it in the same
 * order at all, so a capture indexed by call count would come back smeared. The
 * block says where it is and that is what is read.
 *
 * Samples outside what was captured come back as silence rather than as the
 * nearest thing captured: a bounce of a range nobody played through the
 * hardware has no answer, and inventing one would put the wrong sound in a file
 * somebody keeps.
 */
class CapturedInsert final : public EngineInsert {
  public:
    enum class Mode {
        Capturing,  ///< the live pass: write down what comes back, pass it through
        Playing,    ///< the bounce: send nothing, answer from what was written down
    };

    /// @p live is what the hardware is behind this insert, and it is only used
    /// while capturing. Null is a capture with nothing to capture, which comes
    /// back as silence and is what a machine with the outboard unplugged has.
    CapturedInsert(Mode mode, int numChannels, EngineInsert* live = nullptr);

    /**
     * @brief Switch between writing a capture down and playing it back.
     *
     * Off the audio thread, between renders, which is when a bounce starts and
     * when it ends. One object rather than two, because the capture belongs to
     * neither mode: it is what the live pass wrote and what the bounce reads,
     * and handing it between two objects would be a copy of the one thing here
     * worth not copying.
     *
     * Switching to Playing keeps what was captured; switching back to Capturing
     * does not clear it either, because a second live pass writes over the
     * positions it visits and the rest is still what was heard there.
     */
    void setMode(Mode mode) {
        mode_ = mode;
    }

    Mode mode() const {
        return mode_;
    }

    void prepare(const RenderContext& context) override;
    void reset() override;

    /// The live insert's, while capturing. Zero while playing: a capture is
    /// already aligned to the timeline it was taken against, so compensating
    /// for the round trip a second time would move it.
    int latencySamples() const override;

    void send(const BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
              const juce::MidiBuffer& midi) override;

    void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override;

    /// What has been captured, in timeline samples from zero. Grows as the live
    /// pass reaches further into the timeline.
    std::int64_t capturedSamples() const {
        return captured_;
    }

    /// Whether every sample of [first, first + count) has been captured, which
    /// is the question a bounce asks before it starts: a render that ran off the
    /// end of the capture would write silence into a file rather than say so.
    bool covers(std::int64_t first, std::int64_t count) const;

  private:
    /// Where @p block sits on the timeline, in samples.
    ///
    /// Derived from seconds, which is what a block carries and what recorded
    /// material is measured in. A capture indexed by how many blocks have gone
    /// by would come back smeared the moment a bounce used a different block
    /// size, which it always does.
    std::int64_t timelineSampleOf(const BlockInfo& block) const;

    Mode mode_;
    int numChannels_ = 2;
    EngineInsert* live_ = nullptr;

    /// Interleaved by channel, indexed by timeline sample. Grown off the audio
    /// thread is impossible here, so it is reserved at prepare and a capture
    /// that runs past it stops rather than allocating.
    std::vector<std::vector<float>> samples_;
    std::int64_t captured_ = 0;
    double sampleRate_ = 44100.0;
};

}  // namespace magda::engine
