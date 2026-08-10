#pragma once

#include "clip/MidiEventList.hpp"
#include "core/ClipInfo.hpp"

/**
 * @file MidiClipCompiler.hpp
 * @brief One MIDI clip's model, turned into the messages it plays.
 *
 * Where the curve densification, the MPE channel assignment and the same-pitch
 * overlap rule live. Off the audio thread, once per snapshot compile.
 *
 * ## How dense a curve is
 *
 * The model holds a curve as a few points with a shape between them; a synth
 * reads messages, so something has to decide how many. **On every change of the
 * quantised value, and no closer together than the floor.**
 *
 * Not the 1/16-beat grid the sync layer uses when it writes into Tracktion, and
 * not per sample or per block either.
 *
 * Per block is not available: RenderContext.hpp requires a shorter block to
 * render identically, so a curve resolved per callback would make the offline
 * render disagree with playback.
 *
 * Per sample is the wrong axis. The ceiling is the value resolution rather than
 * the sample rate: a CC is seven bits, so a full sweep has 128 distinct values
 * however many samples it crosses, and pitch bend is fourteen. Emitting on every
 * change is therefore already the densest message set that means anything, and
 * for pitch bend it is far too dense to afford against a port budget counted in
 * bytes. That is what the floor bounds.
 *
 * The 1/16 grid is anchored to the wrong axis, which makes it both too dense and
 * too sparse depending on the curve. A ramp of one unit over eight bars is 512
 * near-identical messages on the grid and two here; a pitch-bend dive over a
 * hundred milliseconds gets three grid points at 120 BPM, an audible staircase,
 * and about a hundred here. The grid also moves with tempo, running at 8 Hz at
 * 30 BPM and 128 Hz at 480 for the same drawn curve, when smoothness is a
 * wall-clock property.
 *
 * Emitting on value change is what makes both come out right with no number to
 * tune: every message changes something, and nothing that changes is missed
 * until the floor bites. The incumbent's constant-segment guard and its Step
 * handling stop being special cases and fall out.
 *
 * A user's own points are never subject to the floor, only the interpolation
 * between them: the flood risk is densification, and a point somebody placed is
 * not densification.
 *
 * This is a deliberate divergence from the incumbent and #2040 compares MIDI
 * event streams as their own artifact because of it. It does not walk back into
 * #1193: a millisecond floor caps a controller at about ten messages per block,
 * and caps it in wall-clock rather than in beats, which the grid never did.
 */

namespace magda::engine {

/// The floor between two messages of one densified curve. Fast enough that the
/// coarsest step it can produce on a pitch bend is about four cents, slow enough
/// that one controller costs about ninety bytes of the port budget per block.
constexpr double kCurveFloorSeconds = 0.001;

/// A backstop on how much work one segment can ask of the compile. Reached only
/// by a segment minutes long, where the floor alone would allow more points than
/// anything could hear.
constexpr int kMaxCurveStepsPerSegment = 4096;

/**
 * @brief Compile @p clip's notes and curves into the list that plays them.
 *
 * @p curveFloorBeats is kCurveFloorSeconds in the clip's own domain, converted
 * by the caller because it owns the tempo map. Zero or less disables the floor,
 * which is what a test that wants every value change asks for.
 *
 * Groove is deliberately absent: it is anchored to the project grid, so a looped
 * clip grooves each pass differently and the lookup has to happen per pass, at
 * emit time (GrooveTemplate.hpp).
 */
MidiEventList compileMidiEvents(const ClipInfo& clip, double curveFloorBeats);

}  // namespace magda::engine
