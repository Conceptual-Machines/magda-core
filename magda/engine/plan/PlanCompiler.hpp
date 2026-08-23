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
 * The compiler reads structure only: chain layout, routing, sends, bypass and
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

/**
 * @brief Whether anything the compiler will emit for @p track consumes MIDI.
 *
 * The rule behind "no consumer, no source": a track whose live chain reads no
 * MIDI gets no ClipMidi op, because nothing downstream would read it. Bypass and
 * chain power are part of the answer, so a bypassed instrument does not keep a
 * MIDI source alive with nothing to feed.
 *
 * Exposed because the null-diff harness has to put its capture device on exactly
 * the tracks this returns true for. Deciding that separately would be a second
 * opinion about what consumes MIDI, and the two would drift the first time a
 * device type was added: the incumbent leg would capture on tracks the plan
 * emits nothing for, or miss ones it does.
 */
bool chainConsumesMidi(const TrackInfo& track);

}  // namespace magda::engine
