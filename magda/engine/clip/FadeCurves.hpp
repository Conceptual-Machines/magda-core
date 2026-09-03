#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cstddef>

#include "core/ClipInfo.hpp"

/**
 * @file FadeCurves.hpp
 * @brief The four fade shapes, and the ramp that is not a fade.
 *
 * The shapes are the incumbent's, sample for sample. FadeCurve's values are
 * pinned project-file integers that happen to equal Tracktion's own
 * (ClipInfo.hpp), and the curves behind them have to match too: a fade that
 * differs by a hair is a null-diff render that never nulls (#2040).
 *
 * A fade is a gain envelope over a stretch of the timeline, so nothing here
 * knows about blocks or streams. Where a curve is applied is the voice's
 * business (ClipVoice.hpp); what it is worth at a point along itself is this.
 */

namespace magda::engine {

/**
 * @brief Gain at @p alpha along a fade of this shape, rising from 0 to 1.
 *
 * @p alpha runs 0 to 1 across the fade. A fade out is the same curve read
 * backwards, which is what passing 1 - alpha gets, and not a separate shape.
 */
float fadeGain(FadeCurve curve, float alpha);

/**
 * @brief Where a speed ramp has got to, @p alpha along itself.
 *
 * The other thing a clip's fade pair can mean (ClipInfo::fadeInBehaviour). A
 * speed ramp is not a gain envelope at all: the material accelerates into the
 * clip and decelerates out of it, pitch and all, the way a tape machine starts
 * and stops. So the curve is read as a position rather than as a level, and what
 * comes back is the proportion of the ramp's own stretch that has been consumed.
 *
 * These are the incumbent's shapes, which are the integrals of the gain curves
 * above rather than the curves themselves: what the gain curve is worth at a
 * point is the *rate* the material runs at there, and where the material has got
 * to is the area under that. A rising ramp therefore ends at 1 and begins at a
 * half, which is not a mistake: a ramp that ran the material from the very start
 * of the region would arrive at the far end half a region behind where the clip
 * says it should be. Starting ahead is what makes it land in the right place.
 *
 * @p alpha runs 0 to 1 across the ramp, and @p rising says which edge it is.
 */
double fadeRampPosition(FadeCurve curve, double alpha, bool rising);

/**
 * @brief Return the start of a voice to zero without fading what follows it.
 *
 * The launch ramp (ClipInfo::launchFadeSamples), and deliberately not a fade:
 * it subtracts the offset the first sample starts at, decaying that correction
 * over the ramp's length, so a transient sitting on top of the offset survives
 * intact. A gain fade would flatten the transient along with the step.
 *
 * What it is for is the discontinuity of starting mid-material: a locate into
 * the middle of a clip, a loop wrap into one, a voice that begins where the
 * file happens to be at full swing. A clip starting at its own edge has no
 * offset to remove and this costs it nothing, which is why it can be applied
 * wherever a voice begins rather than only where somebody decided it clicks.
 *
 * It carries across blocks, which is why it is a small object rather than a
 * function. A ramp that stopped at the end of the block it started in would
 * decay over min(length, block) samples, so the same clip would come out
 * differently at 128 samples a block and at 1024, and an offline render at one
 * block size would disagree with playback at another. Block size is an I/O
 * batching concept and never a precision one (RenderContext.hpp), and this is
 * the state that keeps that true here.
 *
 * A length of 0 does nothing at all, which is how the leading transient is
 * preserved exactly.
 */
class StartDeClick {
  public:
    /// The most channels one voice renders. Two today; sized for a little more
    /// so that a wider clip is a compile-time decision rather than a heap
    /// allocation on the audio thread.
    static constexpr std::size_t kMaxChannels = 8;

    /// Take the step out of @p audio, and remember enough to go on doing it in
    /// the blocks after this one. The offset is read from the first sample of
    /// each channel, once, here.
    void begin(juce::dsp::AudioBlock<float> audio, int fadeSamples);

    /// Carry on. Does nothing once the ramp has run out, so a caller can ask
    /// every block without checking.
    void advance(juce::dsp::AudioBlock<float> audio);

    /// Whether there is any correction left to apply.
    bool active() const {
        return done_ < length_;
    }

    /// Forget the ramp, for a voice that is starting over.
    void reset() {
        length_ = 0;
        done_ = 0;
    }

  private:
    void applyFrom(juce::dsp::AudioBlock<float> audio, int alreadyDone);

    std::array<float, kMaxChannels> offsets_{};
    int length_ = 0;
    int done_ = 0;
};

/**
 * @brief Return the end of a voice to zero without fading what came before it.
 *
 * The other edge of StartDeClick, and the same trick read backwards. Where a
 * start subtracts the step the material begins on, a stop carries the step it
 * ends on: the last sample that sounded is held and decayed to zero over the
 * ramp, added on top of the silence that follows. The material itself is never
 * attenuated, so a clip stopped a sample before its own tail ends keeps that
 * tail exactly as far as it got.
 *
 * What it is for is the discontinuity of ending mid-material: a session slot
 * stopped on a beat, a track handed from the arrangement to the session, a
 * launch that cuts the arrangement off mid-note. None of those end on a zero
 * crossing except by luck, and the step left behind is a click whose energy is
 * spread across the spectrum rather than confined below the ramp's own rate.
 *
 * It carries across blocks, for the reason StartDeClick does: a ramp that
 * stopped at the end of the block it began in would decay over min(length,
 * block) samples, and the same stop would come out differently at 128 samples a
 * block and at 1024.
 *
 * A held value of zero costs nothing, which is why a source can stop through
 * this unconditionally rather than deciding first whether it clicks.
 */
class StopDeClick {
  public:
    /// The most channels one source renders, as StartDeClick sizes it.
    static constexpr std::size_t kMaxChannels = StartDeClick::kMaxChannels;

    /**
     * @brief Remember where @p audio ended.
     *
     * Called with what sounded, every block a source produces anything, so a
     * stop landing on the first sample of some later block still knows what it
     * is stepping down from.
     *
     * Does nothing while a ramp is running: the buffer then holds the value
     * being decayed rather than a signal, and remembering that would bend the
     * rest of the ramp. Safe to call unconditionally, which is how a source
     * that does not track its own ramps wants to call it.
     */
    void push(juce::dsp::AudioBlock<float> audio);

    /**
     * @brief Start decaying at @p offset samples into @p audio.
     *
     * The value decayed is the sample before @p offset when the stop lands
     * inside a block that sounded, and the one @ref push last saw when it lands
     * on the block's first sample. Those are the same sample; which side of a
     * callback boundary it fell on is not something a stop should be able to
     * hear.
     */
    void begin(juce::dsp::AudioBlock<float> audio, int offset, int fadeSamples);

    /// Carry on from the start of @p audio. Does nothing once the ramp has run
    /// out, so a caller can ask every block without checking.
    void advance(juce::dsp::AudioBlock<float> audio);

    /// Whether there is any decay left to add.
    bool active() const {
        return done_ < length_;
    }

    /// Forget the ramp and what it was decaying, for a source starting over.
    void reset() {
        held_.fill(0.0f);
        length_ = 0;
        done_ = 0;
    }

  private:
    /// Remember the sample before @p upTo, whatever a ramp is doing.
    void hold(juce::dsp::AudioBlock<float> audio, int upTo);

    void applyFrom(juce::dsp::AudioBlock<float> audio, int offset, int alreadyDone);

    std::array<float, kMaxChannels> held_{};
    int length_ = 0;
    int done_ = 0;
};

}  // namespace magda::engine
