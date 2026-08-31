#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

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
 * two insert ops already resolve to, which is the point of the interface being
 * what it is: a render binds one of these instead of the live one and nothing
 * else in the engine changes.
 *
 * ## Two modes, and why they are exclusive
 *
 * **Capturing** is the live pass. What comes back from the hardware is written
 * down and passed through untouched: the person listening hears the outboard,
 * and the render that follows has something to replay.
 *
 * **Playing** is the bounce. Nothing is sent, because there is nothing on the
 * other end that could answer in time, and what comes back is what was captured
 * at the same timeline position.
 *
 * A single object cannot be doing both, and the mode says which. It is the same
 * split the incumbent's capture plugin has and for the same reason: a capture
 * taken while replaying a capture is a copy of itself.
 *
 * ## A window, not a tape
 *
 * A capture covers a declared stretch of timeline and nothing outside it. That
 * is what the incumbent's capture plugin does, and here it is also what keeps
 * the memory honest: sizing for "long enough for anything" is a fifth of a
 * gigabyte per stereo insert at 44.1 kHz before anybody has asked for a render,
 * and several inserts would exhaust a machine merely by preparing. The window
 * is the range the export asked for, so what is allocated is what is going to
 * be used.
 *
 * ## Position, not order
 *
 * Everything is addressed by timeline sample rather than by how many blocks
 * have gone by. A bounce does not visit the timeline in the block sizes the
 * live pass did, and may not visit it in the same order at all, so a capture
 * indexed by call count would come back smeared.
 *
 * @ref covers is what a bounce asks before it starts, and it answers about
 * every sample rather than about the furthest one reached. A high-water mark
 * would call a capture complete after a seek jumped over the middle of it, and
 * the render would then write the silence in that gap into a file somebody
 * keeps.
 *
 * ## Latency
 *
 * A capture holds what the hardware answered at the position it answered it,
 * which is the send from a round trip earlier. During the live pass the latency
 * pass delays every parallel path to meet that, and a replay has to be aligned
 * the same way or the insert's path comes back late by the whole round trip. So
 * the round trip is remembered when the capture is taken and declared again
 * while replaying: the graph then compensates a bounce exactly as it
 * compensated the pass the capture came from.
 */
class CapturedInsert final : public EngineInsert {
  public:
    enum class Mode {
        Capturing,  ///< the live pass: write down what comes back, pass it through
        Playing,    ///< the bounce: send nothing, answer from what was written down
    };

    /// The stretch of timeline a capture covers, in samples from zero.
    struct Window {
        std::int64_t firstSample = 0;
        std::int64_t numSamples = 0;

        bool contains(std::int64_t first, std::int64_t count) const {
            return count >= 0 && first >= firstSample && first + count <= firstSample + numSamples;
        }
    };

    /// @p live is what the hardware is behind this insert, and it is only used
    /// while capturing. Null is a capture with nothing to capture, which comes
    /// back as silence and is what a machine with the outboard unplugged has.
    CapturedInsert(Mode mode, int numChannels, EngineInsert* live = nullptr);

    /**
     * @brief The stretch of timeline to capture. Off the audio thread, before
     *        prepare, which is what sizes the storage.
     */
    void setWindow(const Window& window) {
        window_ = window;
    }

    const Window& window() const {
        return window_;
    }

    /**
     * @brief Switch between writing a capture down and playing it back.
     *
     * Off the audio thread, between renders. One object rather than two,
     * because the capture belongs to neither mode: it is what the live pass
     * wrote and what the bounce reads, and handing it between two objects would
     * copy the one thing here worth not copying.
     */
    void setMode(Mode mode) {
        mode_ = mode;
    }

    Mode mode() const {
        return mode_;
    }

    void prepare(const RenderContext& context) override;
    void reset() override;

    /// The round trip, declared in both modes. See the note on latency above:
    /// a replay that declared none would leave the insert's path late by the
    /// whole round trip, because the capture holds what came back rather than
    /// what was sent.
    int latencySamples() const override;

    void send(const BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
              const juce::MidiBuffer& midi) override;

    void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override;

    /**
     * @brief Whether every sample of [first, first + count) was captured.
     *
     * The question a bounce asks before it starts. Answered about every sample
     * rather than about the furthest one reached, so a pass that seeked over
     * the middle of the range says so.
     */
    bool covers(std::int64_t first, std::int64_t count) const;

    /// How many samples of the window have been captured, for a caller
    /// reporting progress. Not what @ref covers is asked, and deliberately not
    /// usable as it: a total says nothing about where the gaps are.
    std::int64_t capturedSamples() const;

  private:
    /// Where @p block sits on the timeline, in samples.
    ///
    /// From seconds, which is what a block carries and what recorded material
    /// is measured in. A capture indexed by how many blocks have gone by would
    /// come back smeared the moment a bounce used a different block size, which
    /// it always does.
    std::int64_t timelineSampleOf(const BlockInfo& block) const;

    void markCaptured(std::int64_t first, std::int64_t count);

    Mode mode_;
    int numChannels_ = 2;
    EngineInsert* live_ = nullptr;

    Window window_;
    double sampleRate_ = 44100.0;

    /// The round trip the capture was taken through, remembered so a replay can
    /// declare the same one without the live insert being around.
    int capturedLatency_ = 0;

    /// One vector per channel, indexed from the window's first sample.
    std::vector<std::vector<float>> samples_;

    /// The MIDI a MIDI-returning insert answered with, at absolute timeline
    /// positions. Reserved at prepare, because adding to it happens on the
    /// audio thread.
    juce::MidiBuffer midi_;

    /// Whether each sample of the window has been written, indexed from the
    /// window's first.
    ///
    /// Per sample rather than per page, and exactly rather than approximately.
    /// A page marked when part of it arrived would call a gap covered, and a
    /// page marked only when all of it arrived would refuse a range that really
    /// was captured -- a block landing half inside the window makes both of
    /// those happen at once. What this costs is a byte against the sixteen a
    /// stereo sample already occupies here, which is not the thing to be clever
    /// about when the answer decides whether a file gets silence in it.
    std::vector<char> written_;
};

}  // namespace magda::engine
