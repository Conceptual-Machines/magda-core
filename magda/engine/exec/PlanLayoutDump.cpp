#include "exec/PlanLayoutDump.hpp"

#include <sstream>

namespace magda::engine {
namespace {

std::string padded(std::string text, std::size_t width) {
    if (text.size() < width)
        text.append(width - text.size(), ' ');
    return text;
}

std::string rightAligned(int value, std::size_t width) {
    auto text = std::to_string(value);
    if (text.size() < width)
        text.insert(text.begin(), static_cast<long>(width - text.size()), ' ');
    return text;
}

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
        text += outputs[i] == SignalKind::Midi ? "m" : "a";
        text += std::to_string(layout.portSlots[port]);
    }
    return text;
}

/// The latency reaching an op's first output port, which is the number that
/// says where in the graph a delay had to go.
///
/// An op with no output ports has no latency to report rather than a latency
/// of zero, and printing zero for it would read as "nothing reaches this",
/// which of the hardware output is the opposite of true.
std::string describeLatency(const RenderPlan& plan, const PlanLatency& latency,
                            const std::vector<int>& portOffsets, std::size_t op) {
    if (plan.ops[op].outputs.empty())
        return "-";

    const auto port = static_cast<std::size_t>(portOffsets[op]);
    return port < latency.portLatency.size() ? std::to_string(latency.portLatency[port]) : "-";
}

/// Trailing spaces would not survive the repo's whitespace hook, which would
/// rewrite every golden the first time one was committed.
void writeTrimmed(std::ostringstream& out, std::string line) {
    while (!line.empty() && line.back() == ' ')
        line.pop_back();
    out << line << "\n";
}

}  // namespace

std::string dumpPlanLayout(const RenderPlan& plan, const std::vector<int>& deviceLatency) {
    const auto portOffsets = portOffsetsOf(plan);
    const auto latency = resolvePlanLatency(plan, portOffsets, deviceLatency);
    const auto layout = assignBuffers(plan, portOffsets, latency.delaySamples);

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

        writeTrimmed(out, line.str());
    }

    return out.str();
}

}  // namespace magda::engine
