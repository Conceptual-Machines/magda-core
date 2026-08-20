#pragma once

#include <span>

#include "ModInfo.hpp"

/**
 * @file ModCurve.hpp
 * @brief The shape a modulator has at a phase, read in one place.
 *
 * A modulator's cycle is drawn once and read three times: by the editor's
 * preview, by the fork's lock-free snapshot on the audio thread, and by the
 * native engine's LFO (#2119). Three readings of one curve is three chances for
 * the dot, the sound and the render to disagree, so the arithmetic lives here
 * and the three of them call it.
 *
 * Pure: no allocation, no locks, no state. Safe on the audio thread, which is
 * what lets the snapshot delegate rather than keep its own copy.
 *
 * Phase in, 0 to 1, and level out, 0 to 1. What a level means is the caller's:
 * ModInfo::invertOutput turns a drawn level into the attenuation a device
 * receives, and that happens where the modulator's output is applied rather
 * than here, because the curve the user drew is the one this returns.
 */

namespace magda::modcurve {

/** @brief One cycle of a built-in waveform. */
float waveform(LFOWaveform wave, float phase);

/** @brief One cycle of a curve preset, for a custom curve with nothing drawn on it. */
float preset(CurvePreset shape, float phase);

/**
 * @brief One cycle of a drawn curve.
 *
 * The points are in phase order and the curve wraps: a phase past the last
 * point is in the run that closes the cycle at the first. Each point carries
 * how the run leaving it is shaped, which is a step, a hard corner, or a bend
 * whose tension and handles are read through magda::curvemath so the audible
 * curve is the drawn one.
 *
 * An empty list has no shape to report and answers the middle, which is what a
 * modulator with a Custom waveform and no points would otherwise have to
 * invent for itself.
 */
float points(std::span<const CurvePointData> points, float phase);

/**
 * @brief The level a modulator of this shape has at @p phase.
 *
 * The whole question in one call: a built-in waveform, or a drawn curve, or the
 * preset behind a Custom waveform nobody has drawn on yet.
 */
float shapeAt(LFOWaveform wave, CurvePreset shape, std::span<const CurvePointData> points,
              float phase);

/**
 * @brief Where a cycle ends, which is what a one-shot holds once it is through.
 *
 * The last drawn point's own value rather than the curve read just short of 1:
 * reading near the end of the cycle interpolates back towards the first point,
 * which is the run that closes the loop and not a place a one-shot ever
 * arrives at.
 */
float endValue(LFOWaveform wave, CurvePreset shape, std::span<const CurvePointData> points);

}  // namespace magda::modcurve
