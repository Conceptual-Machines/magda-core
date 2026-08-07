#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

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
 * @brief Return the start of @p audio to zero without fading what follows it.
 *
 * The launch ramp (ClipInfo::launchFadeSamples), and deliberately not a fade:
 * it subtracts the offset the first sample starts at, decaying that correction
 * over @p fadeSamples, so a transient sitting on top of the offset survives
 * intact. A gain fade would flatten the transient along with the step.
 *
 * What it is for is the discontinuity of starting mid-material: a locate into
 * the middle of a clip, a loop wrap into one, a voice that begins where the
 * file happens to be at full swing. A clip starting at its own edge has no
 * offset to remove and this costs it nothing, which is why it can be applied
 * wherever a voice begins rather than only where somebody decided it clicks.
 *
 * @p fadeSamples of 0 does nothing at all, which is how the leading transient
 * is preserved exactly.
 */
void applyStartDeClick(juce::dsp::AudioBlock<float> audio, int fadeSamples);

}  // namespace magda::engine
