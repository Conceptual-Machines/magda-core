#include "exec/PlanLayoutDump.hpp"

#include <sstream>

#include "plan/DumpFormat.hpp"

namespace magda::engine {
namespace {

using dump_format::padded;
using dump_format::rightAligned;
using dump_format::writeLine;

/// An op's output ports, as the arena and slot each renders into.
std::string describePorts(const RenderPlan& plan, const BufferLayout& layout,
                          const std::vector<int>& portOffsets, std::size_t op) {
    const auto& outputs = plan.ops[op].outputs;
    if (outputs.empty())
        return "-";

    std::string text;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        if (i > 0)
            text += ",";

        const auto port = static_cast<std::size_t>(portOffsets[op]) + i;
        text += outputs[i].kind == SignalKind::Midi ? "m" : "a";
        text += std::to_string(layout.portSlots[port]);
        if (outputs[i].kind == SignalKind::Audio && outputs[i].channels != 2)
            text += "/" + std::to_string(outputs[i].channels);
    }
    return text;
}

/// The latency reaching an op's first output port, which is the number that
/// says where in the graph a delay had to go.
///
/// An op with no output ports has no latency to report rather than a latency
/// of zero, and printing zero for it would read as "nothing reaches this",
/// which for the hardware output is the opposite of true.
std::string describeLatency(const RenderPlan& plan, const PlanLatency& latency,
                            const std::vector<int>& portOffsets, std::size_t op) {
    if (plan.ops[op].outputs.empty())
        return "-";

    const auto port = static_cast<std::size_t>(portOffsets[op]);
    return port < latency.portLatency.size() ? std::to_string(latency.portLatency[port]) : "-";
}

}  // namespace

std::string dumpPlanLayout(const RenderPlan& plan, const std::vector<int>& deviceLatency) {
    // The same call PlanExecutor::prepare makes, which is what makes the
    // golden pin the path the engine runs rather than a copy of it.
    const auto prepared = resolveLayout(plan, deviceLatency);
    const auto& portOffsets = prepared.portOffsets;
    const auto& latency = prepared.latency;
    const auto& layout = prepared.buffers;

    std::ostringstream out;
    out << "magda-plan-layout v1\n";
    out << "audioSlots=" << layout.numAudioSlots << " midiSlots=" << layout.numMidiSlots
        << " outputLatency=" << latency.outputLatency << "\n";

    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& op = plan.ops[i];

        std::ostringstream line;
        line << "[" << rightAligned(static_cast<int>(i), 3) << "] " << padded(toString(op.kind), 11)
             << " " << padded(toString(op.key), 30)
             << " lat=" << padded(describeLatency(plan, latency, portOffsets, i), 7)
             << " ports=" << padded(describePorts(plan, layout, portOffsets, i), 11);

        // Only where set: a fixture that provokes neither should say nothing
        // about them rather than print two columns of zeroes.
        if (op.kind == OpKind::Delay)
            line << " hold=" << latency.delaySamples[i];
        if (i < layout.writesInPlace.size() && layout.writesInPlace[i] != 0)
            line << " inplace";
        if (i < layout.elided.size() && layout.elided[i] != 0)
            line << " elided";

        writeLine(out, line.str());
    }

    return out.str();
}

}  // namespace magda::engine
