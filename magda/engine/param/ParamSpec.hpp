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
     * Off by default: a parameter settles at the block boundary, so opting in
     * changes what every automated parameter of that device does across a
     * block.
     *
     * What opting in buys is a parameter that ramps rather than steps inside
     * the block, which matters where the step is audible: a filter cutoff swept
     * fast, a gain automated at the sample level. It is per parameter because
     * that is the granularity the cost is paid at.
     */
    bool segmentAccurate = false;
};

/** @brief The spec of the parameter @p info describes. Off the audio thread. */
ParamSpec paramSpecFrom(const magda::ParameterInfo& info);

}  // namespace magda::engine
