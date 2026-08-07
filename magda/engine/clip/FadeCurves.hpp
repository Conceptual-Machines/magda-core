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
