#pragma once

#include <vector>

#include "plan/RenderPlan.hpp"

/**
 * @file PlanLayout.hpp
 * @brief What a plan becomes when it meets the instances behind it.
 *
 * The plan is topology and the value table is the model; between them sits a
 * third thing, and this is it: everything that follows from binding a plan to
 * the objects that will render it. How many samples each delay holds comes from
 * what a loaded plugin reports, and which ports can share a buffer follows from
 * that. Neither is in the plan, because neither is knowable when it is
 * compiled, and neither is a value, because both decide how memory is laid out
 * before the first block runs.
 *
 * Both passes run in full on every prepare. They are linear and they run off
 * the audio thread; skipping them for locality is how graph engines end up with
 * wrong-latency bugs nobody can reproduce.
 *
 * Both take a plan validatePlan has passed, and lean on it: dependency order,
 * ports that exist, and delays that sit on exactly one connected edge each.
 */

namespace magda::engine {

/** Flat index of every op's first output port; one entry past the end. */
std::vector<int> portOffsetsOf(const RenderPlan& plan);

/**
 * @brief Where a prepared plan's latency lands, in samples.
 *
 * The rule is the one the current engine uses everywhere it has more than one
 * path meeting: every input of a fan-in is delayed up to the longest of them,
 * and nothing is ever pulled early. It applies uniformly, which is what makes
 * sends, sidechains, group tracks and parallel rack chains all come out right
 * without any of them being a special case: a send tap carries whatever its
 * source accumulated, and the mix it lands in aligns it with everything else
 * arriving there, the destination track's own signal included.
 */
struct PlanLatency {
    /// Samples each Delay op holds back. Zero for every other op, and zero for
    /// a delay whose edge turned out to be the longest one.
    std::vector<int> delaySamples;

    /// Latency reaching each flat output port.
    std::vector<int> portLatency;

    /// What the plan's output is delayed by, which is what a host compensating
    /// for the engine has to know.
    int outputLatency = 0;
};

/**
 * @brief Resolve latency for @p plan.
 *
 * @param deviceLatency  per op: what a Device op's bound instance reports, and
 *                       zero everywhere else.
 */
PlanLatency resolvePlanLatency(const RenderPlan& plan, const std::vector<int>& portOffsets,
                               const std::vector<int>& deviceLatency);

/**
 * @brief Which buffer each output port renders into.
 *
 * Ports share buffers when no schedule can have both live at once. That is a
 * question about the dependency DAG and not about the order the ops happen to
 * sit in: the reference executor's straight walk is only one of the orders the
 * parallel executor may pick, and a slot reused on the strength of that walk is
 * a race in the other ones. So the test is transitive dependency, and two ports
 * share only when every op that reads the first must have finished before the
 * op that writes the second can start.
 */
struct BufferLayout {
    /// Arena slot per flat output port. Audio ports index the audio arena and
    /// MIDI ports the MIDI arena; which one follows from the port's kind.
    std::vector<int> portSlots;

    /// Per op: its first output port is the same buffer as one of its inputs,
    /// so the op works in place and the copy that would feed it is skipped.
    /// Only set where the op is the last thing to read that input.
    std::vector<char> writesInPlace;

    /// Per op: the op writes nothing, because it is a delay that turned out to
    /// need no samples. Its output port is its input's port, which is what
    /// makes compensation cost nothing where there is none to do.
    std::vector<char> elided;

    int numAudioSlots = 0;
    int numMidiSlots = 0;
};

BufferLayout assignBuffers(const RenderPlan& plan, const std::vector<int>& portOffsets,
                           const std::vector<int>& delaySamples);

/**
 * @brief Everything a prepare resolves, in the order it has to be resolved.
 *
 * The three passes above run one after another and each takes the one before
 * it, so every caller ran the same three lines. Two of them doing it
 * separately is how a step added here reaches the executor and not the golden
 * that is supposed to be pinning it.
 */
struct PreparedLayout {
    std::vector<int> portOffsets;
    PlanLatency latency;
    BufferLayout buffers;
};

/**
 * @brief Resolve @p plan against the latency its bound instances report.
 *
 * @param deviceLatency  per op: what a Device op's bound instance reports, and
 *                       zero everywhere else.
 */
PreparedLayout resolveLayout(const RenderPlan& plan, const std::vector<int>& deviceLatency);

}  // namespace magda::engine
