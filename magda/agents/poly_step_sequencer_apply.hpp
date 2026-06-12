#pragma once

#include <juce_core/juce_core.h>

#include "core/ChainNodePath.hpp"
#include "poly_step_sequencer_agent.hpp"

namespace magda {

/**
 * @brief Apply a parsed PolyStepSequencer preset to a specific device path.
 *
 * Sets global parameters (numSteps, rate, swing, gateLength) via the
 * plugin's CachedValues, clears all steps, then writes each listed step
 * via the plugin's public setters (clearStep + setStepGate/Tie/Probability/
 * Velocity + addStepNote).
 *
 * Must be called on the message thread (TE asserts this for ValueTree
 * writes). The SoundDesignAgent wrapper handles the thread hop.
 *
 * Returns a one-line status (e.g. "applied 16 steps to Poly Sequencer") or
 * an error string starting with "()" if the path doesn't resolve to a
 * PolyStepSequencerPlugin.
 */
juce::String applyPolyStepSequencerPresetToPath(const PolyStepSequencerAgent::Preset& preset,
                                                const ChainNodePath& path);

}  // namespace magda
