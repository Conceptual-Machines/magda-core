#pragma once

#include <juce_core/juce_core.h>

#include "core/ChainNodePath.hpp"
#include "step_sequencer_agent.hpp"

namespace magda {

/**
 * @brief Apply a parsed StepSequencer preset to a specific device path.
 *
 * Sets global parameters (numSteps, rate, swing, gateLength) via the
 * plugin's CachedValues, then writes each step via the plugin's public
 * setters (setStepNote / setStepOctaveShift / setStepGate / setStepAccent /
 * setStepGlide / setStepTie / clearStep).
 *
 * Must be called on the message thread (TE asserts this for ValueTree
 * writes). The SoundDesignAgent wrapper handles the thread hop.
 *
 * Returns a one-line status (e.g. "applied 16 steps to Step Sequencer") or
 * an error string starting with "()" if the path doesn't resolve to a
 * StepSequencerPlugin.
 */
juce::String applyStepSequencerPresetToPath(const StepSequencerAgent::Preset& preset,
                                            const ChainNodePath& path);

}  // namespace magda
