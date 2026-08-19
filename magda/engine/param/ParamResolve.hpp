#pragma once

#include <span>

#include "param/ParamBlock.hpp"
#include "param/ParamSpec.hpp"
#include "param/ParamTable.hpp"

/**
 * @file ParamResolve.hpp
 * @brief The lanes writing a parameter, and the one place they are resolved.
 *
 * Three things write a parameter and they do not write the same thing.
 *
 * The base is what the parameter is: the stored value, moved by a knob, a
 * control surface, a preset load or a host write. Automation is a curve playing
 * over it, which covers as much of a block as it covers and no more. Modulation
 * is what the modifiers linked to the parameter add to whatever the first two
 * settled on.
 *
 * They are separate lanes rather than one value written by turns, and that is
 * the whole point of the arrangement. The incumbent engine has one value and
 * several writers, which is why a host write lands on a parameter under an
 * active modifier and disappears: the modifier writes next and there was only
 * ever one place to write. Here a host write goes into a lane modulation never
 * touches, so nothing can overwrite it, and the bug class is gone rather than
 * patched.
 *
 * Precedence, in full:
 *
 *  - Automation replaces the base wherever a lane covers the block, and the
 *    base is what the parameter is everywhere else. A lane that covers half a
 *    block leaves the other half to the base, which is what an automation clip
 *    starting mid-block does.
 *  - Modulation is added to whichever of the two applied, in the normalised
 *    domain, with each link's depth and polarity applied at the link.
 *  - One clamp, after every contribution, and then the parameter's own scale.
 *    A device never receives a value outside its range and never has to check.
 *    Both happen where the stream is read rather than here, at the position
 *    being asked about rather than at the ends of a segment, which is the only
 *    place either is correct once a ramp is involved (ParamBlock.hpp).
 *  - A stepped parameter is quantised by the scale, which is where the steps
 *    are, and never ramped, because there is nothing between two of its values.
 *
 * Which authority state a lane is in is not decided here. The model owns that
 * (AutomationStateMachine), and a lane handed to a block is one the model has
 * already decided is playing; read mode is what the port ships, and write, touch
 * and latch change what the model does with a gesture rather than what a block
 * does with a lane.
 */

namespace magda::engine {

/**
 * @brief What one modulation link adds to a parameter this block.
 *
 * Block-rate, because the modifiers it comes from are: the incumbent engine
 * advances a modifier once per block and every parity case in the corpus is
 * measured against that. A modifier that ran per sample would be a difference
 * from the fork on every modulated parameter, which is a decision for after the
 * port rather than during it.
 */
struct ModContribution {
    /// The modifier's output, 0 to 1. An inactive modifier outputs 0, which is
    /// what invertOutput exists to make usable for a level curve.
    float value = 0.0f;

    /// The link's depth, -1 to 1. Per link rather than per modifier, because
    /// one modifier drives several parameters by different amounts.
    float amount = 0.0f;

    /// Whether the modifier's 0 to 1 maps to -1 to +1 before the depth is
    /// applied. Per link, like the depth.
    bool bipolar = false;
};

/** @brief The lanes writing one parameter over one block. */
struct ParamSources {
    /// The stored value, normalised. What the parameter is where no automation
    /// covers it.
    float base = 0.0f;

    /// The curve playing over it, normalised, in this block's sample domain.
    /// Empty when no lane covers any of the block.
    std::span<const ParamSegment> automation;

    /// One past the last sample the lane covers. The block length for a lane
    /// that runs to the end of it, and less for a lane that stops inside it,
    /// where the base takes over again. Ignored when there is no lane.
    int automationEnd = 0;

    /// The links reaching this parameter. Empty for a parameter nothing
    /// modulates, which is almost all of them.
    std::span<const ModContribution> modulation;
};

/**
 * @brief Resolve one parameter's lanes into what its device reads.
 *
 * On the audio thread, once per parameter per block, after
 * ResolvedParams::beginBlock(). Writes into @p out at @p param and touches
 * nothing else.
 *
 * The only place the precedence above is implemented. A device that wanted to
 * know whether its cutoff came from a curve or a knob would have to be told,
 * and nothing tells it.
 */
void resolveParam(ResolvedParams& out, int param, const ParamSpec& spec,
                  const ParamSources& sources);

/**
 * @brief Resolve every parameter of @p table for one block (#2117).
 *
 * On the audio thread, once per block, before any device runs. Walks the
 * table's resolution order, so a macro driving a macro driving a device
 * parameter needs no second pass: whatever a parameter reads has already been
 * resolved when it is reached.
 *
 * @p scratch is where one parameter's contributions are gathered, and it has to
 * hold ParamTable::maxLinksPerParam of them. Passed in rather than owned
 * because this runs on the audio thread and the room has to have been found
 * before the block started; a parameter with more links than there is room for
 * keeps the ones that fit, which cannot happen against a scratch sized from the
 * table it is used with.
 *
 * A table whose size does not match what @p out was prepared for is refused
 * whole rather than resolved partly: the two are indexed by the same ParamId,
 * and a mismatch means they were prepared against different plans.
 */
void resolveParams(const ParamTable& table, ResolvedParams& out, std::span<ModContribution> scratch,
                   int numSamples);

}  // namespace magda::engine
