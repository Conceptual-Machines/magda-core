#pragma once

#include "core/ParameterUtils.hpp"

/**
 * @file ParamSpec.hpp
 * @brief What one parameter is, as the audio thread needs to know it.
 *
 * The model's ParameterInfo says everything about a parameter: its name, its
 * unit, how to format it, which cell of the grid it draws in. Almost none of
 * that survives the trip to a render block, and what does is a handful of
 * numbers saying what a normalised position means. This is those numbers, plus
 * the two things the engine decides for itself.
 */

namespace magda::engine {

/**
 * @brief The audio-thread face of one parameter.
 *
 * Flat, copyable, no heap. Built off the audio thread by paramSpecFrom() and
 * read on it by the resolver every block.
 */
struct ParamSpec {
    /// What a normalised position means in this parameter's own units. The
    /// model's curve, not a copy of it: ParameterUtils owns the conversion and
    /// this is the part of a ParameterInfo it reads.
    magda::ParameterUtils::ParameterDomain domain;

    /**
     * @brief Whether modulation may reach this parameter.
     *
     * From ParameterInfo::modulatable. Enforced where the contributions are
     * added rather than where a link is made, because a link is a model edit
     * and this is what the block does: a contribution to a parameter that does
     * not take modulation adds nothing, and the parameter follows its base and
     * its automation alone.
     */
    bool modulatable = true;

    /**
     * @brief Whether the device reads the value inside the block or once for it.
     *
     * Off by default, and that default is a parity decision rather than a
     * performance one. The incumbent engine settles every parameter at the
     * block boundary, so a device resolved per sample against a curve the fork
     * reads once would differ from it by however much the curve moves across a
     * block, on every automated parameter, in every project. During the port
     * nothing opts in, and the null-diff corpus is what would say so if
     * something did.
     *
     * What opting in buys is a parameter that ramps rather than steps inside
     * the block, which matters where the step is audible: a filter cutoff swept
     * fast, a gain automated at the sample level. It is per parameter because
     * that is the granularity the cost is paid at.
     */
    bool segmentAccurate = false;

    /**
     * @brief Whether anything in the model actually declared this parameter.
     *
     * False for one slot only: the hole a device's window has to leave when its
     * parameter indices are sparse. A window is contiguous and indexed from
     * zero, because that is what lets a device be handed a span and read its own
     * parameters by the index it declared them at, so an index nothing declared
     * still costs a slot.
     *
     * What it must not cost is a value. A device cannot tell a fabricated zero
     * from a real one, and the first real project to host a plugin found what
     * that means: a project saved before the wet/dry pair was persisted has no
     * parameters at indices 0 and 1, and a hole publishing 0.0 there set the
     * pair to fully dry and fully attenuated -- silence, from a plugin whose own
     * chunk was perfectly restored. A sparse array is normal for a hosted plugin
     * (the fork's list is a filtered view of the instance's, and a project saves
     * what it had), so this is a shape the value layer has to carry rather than
     * a model to correct.
     *
     * Read where the block is resolved: an undeclared parameter publishes no
     * segments, so the device sees an empty view and keeps whatever its own
     * state put there. That is the same answer a device already gets for an
     * index past the end of its window, which is the same question one step
     * further out.
     */
    bool declared = true;
};

/** @brief The spec of the parameter @p info describes. Off the audio thread. */
ParamSpec paramSpecFrom(const magda::ParameterInfo& info);

}  // namespace magda::engine
