#pragma once

#include <span>

#include "exec/RenderContext.hpp"
#include "param/ModLfo.hpp"

/**
 * @file ModFollower.hpp
 * @brief The envelope follower: the amplitude of something else, with an
 *        envelope on it.
 *
 * The one modifier that is not a function of time. An LFO, an envelope and a
 * random walk all answer "how far along are we"; this answers "how loud is
 * that", and the that is audio the modifier does not own. That is the thing
 * slice 4 could not build and the reason this one has to: a follower with no
 * source is a modifier with nothing to follow.
 *
 * Two halves, and they meet at a single number per block.
 *
 * The detector is the source's half. It takes the block of audio the source
 * produced, applies the follower's own input gain and its optional band limits,
 * and reduces the whole block to one peak. Per follower rather than per source,
 * because two followers on one track can be listening to different parts of the
 * spectrum, which is the point of the band limits.
 *
 * The envelope is this half. It runs the fork's own one-pole attack, hold and
 * release over that peak, sample by sample across the block, which is what
 * makes a follower's attack a time rather than a block count.
 *
 * ## Where the source's audio comes from, and when
 *
 * A block behind. The engine resolves every parameter at the top of a block and
 * then walks the ops, so the audio a follower is following does not exist yet
 * when the follower is asked what it is worth: the source's ops have not run.
 * What the follower reads is therefore the block before this one, deterministic
 * and always exactly one block.
 *
 * The alternative is to resolve a follower's targets after its source's
 * subgraph has rendered, which splits parameter resolution into two passes and
 * makes the order a plan-shaped question rather than a table-shaped one. That
 * is a structural change and it is not this slice's: the lag is bounded, it is
 * the same lag the fork has whenever the source track is ordered after the
 * destination, and it is written down here rather than discovered later.
 */

namespace magda::engine {

/** @brief What the model says one envelope follower is. */
struct FollowerSettings {
    /// Applied to the source before the band limits and before detection, so
    /// what the filters and the peak see is the gained signal. The fork holds
    /// TE's own gain at unity and does this on the source side for the same
    /// reason: a gain after detection cannot be band limited.
    float gainDb = 0.0f;

    /// How fast the envelope rises to a louder source, in milliseconds.
    float attackMs = 100.0f;

    /// How long it stays at a peak before it is allowed to fall, in
    /// milliseconds. Held at the sample level, which is what makes a short hold
    /// audible at all.
    float holdMs = 0.0f;

    /// How fast it falls to a quieter source, in milliseconds.
    float releaseMs = 500.0f;

    /// Band-limit the source before detection, so a follower can track the bass
    /// of a track rather than the whole of it. Before detection rather than
    /// after, because a detected level has no frequency content left to filter.
    bool highPass = false;
    float highPassHz = 200.0f;
    bool lowPass = false;
    float lowPassHz = 2000.0f;
};

/**
 * @brief One second-order section, transposed direct form II.
 *
 * The filter juce::IIRFilter is, coefficient order and state layout included,
 * because that is what the fork band-limits with: a follower that rolled off
 * differently would be tracking a different part of the spectrum, and the two
 * engines have to agree about which part. Its own rather than borrowed, so the
 * engine's DSP does not reach into the UI framework.
 */
struct FollowerBiquad {
    /// Normalised, in JUCE's order: feed-forward b0, b1, b2 then feedback a1,
    /// a2, all already divided through by a0.
    float c0 = 1.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f, c4 = 0.0f;

    float v1 = 0.0f, v2 = 0.0f;

    void reset();
    float process(float in);
};

/** @brief A low-pass at @p frequency, on JUCE's coefficients. */
FollowerBiquad followerLowPass(double sampleRate, double frequency);

/** @brief A high-pass at @p frequency, on JUCE's coefficients. */
FollowerBiquad followerHighPass(double sampleRate, double frequency);

/** @brief Where one envelope follower has got to. */
struct FollowerState {
    /// The envelope itself, 0 to 1. What the modifier publishes.
    float envelope = 0.0f;

    /// Samples left before the envelope may start falling again.
    int holdLeft = 0;

    /// The band-limit filters and the cutoffs they are currently set to, so a
    /// block only recomputes coefficients when the model has moved one.
    FollowerBiquad highPass;
    FollowerBiquad lowPass;
    float highPassHz = 0.0f;
    float lowPassHz = 0.0f;

    /// The sample rate the coefficients and the time constants were worked out
    /// at. A device swap or a sample-rate change makes both stale.
    double sampleRate = 0.0;

    /**
     * @brief The peak the last block's detection left for this one.
     *
     * The whole of the one-block lag, in one number: the source renders, the
     * detector reduces it to this, and the next block's resolve is where it is
     * read (the file comment says why).
     */
    float sourcePeak = 0.0f;
};

/**
 * @brief Reduce one block of source audio to the peak this follower detects.
 *
 * On the audio thread, after the source's ops have rendered. @p mono is the
 * source's block downmixed to one channel, which is what the fork's tap hands
 * over as well (FollowerSourceTapPlugin averages the channels).
 *
 * Stores the result on @p state, where the next block's advance reads it.
 */
void detectFollowerSource(FollowerState& state, const FollowerSettings& settings,
                          std::span<const float> mono, double sampleRate, std::span<float> scratch);

/**
 * @brief Advance @p state over one block and publish the envelope.
 *
 * On the audio thread, once per block, from the table's resolution order.
 * Returns the envelope, 0 to 1: 0 is silence at the source, which is also what
 * the whole modulation system reads as a modifier doing nothing, so a follower
 * with nothing to follow contributes nothing.
 *
 * The peak is held flat across the block, which is what the fork does with an
 * externally fed follower: the detection already reduced the block to one
 * number and the envelope's job is the time constant rather than the waveform.
 */
float advanceFollower(FollowerState& state, const FollowerSettings& settings,
                      const BlockInfo& block, const ModTiming& timing);

}  // namespace magda::engine
