#pragma once

#include <vector>

#include "plan/RenderPlan.hpp"

namespace magda {
struct TrackInfo;
}

namespace magda::engine {

/** Compile-time switches that change which ops the plan contains. */
struct CompileOptions {
    /// Emit a level tap after each device slot, feeding the chain UI's meters.
    /// Off in golden tests that are not about metering.
    bool deviceMeters = true;
};

/**
 * @brief Compile the MAGDA track model into a render plan.
 *
 * Deterministic: the same model always compiles to the same plan, op for op and
 * index for index, so plans are golden-testable and the differ sees churn only
 * where the model actually changed.
 *
 * The compiler reads structure only — chain layout, routing, sends, bypass and
 * chain power. Values (fader positions, gains, send levels, clip contents) stay
 * out of the plan and reach ops through snapshots.
 *
 * Anything the compiler cannot express lands in RenderPlan::diagnostics rather
 * than being silently dropped.
 *
 * @param tracks  every non-master track, in project order
 * @param master  the master track (id MASTER_TRACK_ID)
 */
RenderPlan compileRenderPlan(const std::vector<TrackInfo>& tracks, const TrackInfo& master,
                             const CompileOptions& options = {});

}  // namespace magda::engine
