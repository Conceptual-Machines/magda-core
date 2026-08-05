#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "plan/RenderPlan.hpp"

namespace magda {
struct TrackInfo;
}

/**
 * @file PlanValues.hpp
 * @brief The values a plan's ops read, resolved out of the model.
 *
 * The plan carries no gain, no fader position, no send level and no mute: it is
 * topology, and topology changes at human speed. Everything a mixer move can
 * touch lives here instead, resolved into a flat per-op table off the audio
 * thread and read by index during the block. Moving a fader therefore never
 * compiles a plan.
 */

namespace magda::engine {

/**
 * @brief What one op needs from the model to render a block.
 *
 * Deliberately one shape for every value-carrying op: a Gain sets both channels
 * alike, a Fader splits volume across them through the pan law, a SendTap
 * carries the send level. The executor applies gains; it does not know what a
 * pan law is.
 */
struct OpValue {
    float gainLeft = 1.0f;
    float gainRight = 1.0f;

    /// The op contributes nothing this block. Silence rather than a zero gain,
    /// because the one thing that sets it (a rack chain taken out of the mix by
    /// mute or by a sibling's solo) gates the chain's MIDI as well as its
    /// audio, and no gain can silence a MIDI stream.
    bool silent = false;
};

/**
 * @brief What an op renders with when no table applies to the plan.
 *
 * Unity and audible, which is the only safe reading of values that were
 * resolved against something else: a gain nobody asked for is a mix nobody
 * mixed, and a silence nobody asked for is a track that has gone missing.
 */
inline constexpr OpValue kUnityValue{};

/**
 * @brief Per-op values for one compiled plan, indexed by OpId.
 *
 * Published to the audio thread as a whole; the executor never mutates it.
 * Carries the fingerprint of the plan it was resolved against, because values
 * and plans are published separately: a table left over from a plan that has
 * since been swapped would otherwise be applied by index to whatever op now
 * sits there.
 */
struct PlanValues {
    std::uint64_t planFingerprint = 0;
    std::vector<OpValue> ops;
};

/**
 * @brief Resolve the model into per-op values for a compiled plan.
 *
 * Runs off the audio thread. Reads the same model the plan was compiled from,
 * and matches ops to it by OpKey rather than by walking the model in compile
 * order, so the two cannot drift out of step.
 *
 * Returns one message per thing it could not resolve, in op order. As with the
 * compiler's diagnostics, nothing is dropped in silence.
 *
 * @param tracks  every non-master track, as passed to compileRenderPlan
 * @param master  the master track
 */
std::vector<std::string> resolvePlanValues(const RenderPlan& plan,
                                           const std::vector<TrackInfo>& tracks,
                                           const TrackInfo& master, PlanValues& values);

/**
 * @brief Gain a volume fader applies, given a linear volume from the model.
 *
 * Mirrors the current engine's fader, which stores a slider position rather
 * than a gain: the position clamps to [0, 1], so the fader tops out at +6 dB
 * and floors at -100 dB, and a value outside that range is clipped rather than
 * scaled. Exposed because the null-diff harness compares against exactly this.
 */
float faderGainFromVolume(float volume);

/** As faderGainFromVolume, for the model fields that store dB. */
float faderGainFromDecibels(float decibels);

/**
 * @brief Per-channel gains for the linear pan law, the only law MAGDA uses.
 *
 * Boosts the near side rather than attenuating the far one: centre is unity,
 * hard left is (2, 0). A pad or track mixed through a constant-power law would
 * sit 3 dB quieter than the same signal on a track, which is the bug this law
 * exists to avoid.
 */
void applyLinearPanLaw(float gain, float pan, float& left, float& right);

}  // namespace magda::engine
