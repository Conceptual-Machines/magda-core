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
 *
 * ## Two sample rates, and which one each number is in
 *
 * A capture is written during live playback, at whatever rate the audio device
 * is open at, and read during a bounce, whose rate is chosen at export. One of
 * these objects therefore lives through two rate domains: written in one, read
 * in the other. Everything below is a sample count, and a sample count means
 * nothing without saying which.
 *
 *  - @ref captureRate_ is the rate the recording was made at. The audio array,
 *    the window's bounds and the MIDI positions are all in its samples. It is
 *    set once, when a capture prepares, and a later prepare never moves it: the
 *    data it describes is already written.
 *  - @ref renderRate_ is the rate the graph running now is at. Every prepare
 *    sets it, in both modes.
 *
 * During the live pass they are equal. During a bounce they need not be, and
 * the three things that follow from that are handled differently because they
 * are different problems:
 *
 *  - **Position** is converted through seconds, which both domains share, using
 *    the capture's own rate. Correct at any render rate.
 *  - **The round trip** is stored in seconds for the same reason and reported
 *    against the render rate, so it means the same instant either side.
 *  - **The samples themselves** cannot be converted without a resampler: a
 *    capture at 44.1 kHz handed one for one to a 48 kHz render plays 8.8 per
 *    cent slow and a tone and a half flat, and runs out of window early. So
 *    that is the one that is refused rather than converted. A Playing instance
 *    prepared at a rate the capture was not taken at is unusable, @ref covers
 *    says so, and the preflight refuses the bounce instead of writing slow,
 *    detuned audio into a file somebody keeps. Re-capturing at the export rate
 *    is a live pass, which is a thing the host can do.
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
    ///
    /// Read while the capture is running, from whatever thread is drawing the
    /// progress, so it is a counter rather than a walk of the bitmap: the walk
    /// would be a data race with the audio thread writing it, and O(window) on
    /// every poll besides.
    std::int64_t capturedSamples() const {
        return captured_.load(std::memory_order_relaxed);
    }

    /// Whether this capture can be replayed at all.
    ///
    /// False for a capture asked to play into a render at a rate it was not
    /// taken at, and for one whose MIDI outgrew what was reserved for it. Both
    /// are refusals rather than approximations: what they would otherwise
    /// produce is a bounce that is wrong in a way nothing downstream can see.
    bool usable() const {
        return !rateMismatch_ && !midiOverflowed_;
    }

  private:
    /// Where @p block sits in the capture, in capture-rate samples.
    ///
    /// Through seconds, which is what a block carries, what recorded material
    /// is measured in, and the one domain both rates share. A capture indexed
    /// by how many blocks have gone by would come back smeared the moment a
    /// bounce used a different block size, which it always does; one indexed
    /// with the render's rate would come back smeared whenever the two rates
    /// differ, and further wrong the deeper into the timeline it got.
    std::int64_t captureSampleOf(const BlockInfo& block) const;

    void markCaptured(std::int64_t first, std::int64_t count);

    Mode mode_;
    int numChannels_ = 2;
    EngineInsert* live_ = nullptr;

    /// In capture-rate samples, like everything it bounds.
    Window window_;

    /// The rate the recording was made at, and the rate every stored position
    /// is in. Set once, when a capture prepares; a later prepare never moves
    /// it, because the data it describes is already written.
    double captureRate_ = 44100.0;

    /// The rate the graph running now is at. Set by every prepare, both modes.
    double renderRate_ = 44100.0;

    /// The round trip the capture was taken through, in seconds.
    ///
    /// Seconds rather than samples because it is reported into whatever rate
    /// the graph is running at: stored as a count it would mean a different
    /// duration in a bounce at another rate, and every parallel path would be
    /// delayed by slightly the wrong amount.
    double roundTripSeconds_ = 0.0;

    /// Set when a replay was prepared at a rate the capture was not taken at.
    bool rateMismatch_ = false;

    /// Set when the MIDI a capture answered with outgrew what was reserved for
    /// it. The alternative is growing the buffer on the audio thread, and a
    /// capture that allocated its way out of the problem would still be one
    /// with events missing from it.
    bool midiOverflowed_ = false;

    /// One vector per channel, indexed from the window's first sample.
    std::vector<std::vector<float>> samples_;

    /// The MIDI a MIDI-returning insert answered with, at absolute capture-rate
    /// positions. Reserved at prepare and bounded at every append, because
    /// adding to it happens on the audio thread.
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

    /// How many of those bytes are set.
    ///
    /// Kept as a counter rather than recomputed, because it is polled from
    /// another thread while the audio thread writes the bitmap: a walk would be
    /// a data race and O(window) per poll. @ref covers reads the bitmap
    /// instead, and is asked between passes rather than during one.
    std::atomic<std::int64_t> captured_{0};
};

}  // namespace magda::engine
