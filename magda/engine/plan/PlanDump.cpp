#include "plan/PlanDump.hpp"

#include <sstream>

#include "plan/DumpFormat.hpp"

namespace magda::engine {
namespace {

using dump_format::padded;
using dump_format::rightAligned;

std::string describeInputs(const PlanOp& op) {
    if (op.inputs.empty())
        return "-";

    std::string text;
    for (std::size_t i = 0; i < op.inputs.size(); ++i) {
        if (i > 0)
            text += ",";
        const auto& input = op.inputs[i];
        text += input.valid() ? std::to_string(input.op) + ":" + std::to_string(input.port) : "-";
    }
    return text;
}

/// A port as "audio", with its width only when that is not the bus's: a mono
/// port is "audio/1", one carrying nothing "audio/0". Stereo stays bare so the
/// goldens written before ports had widths still say what they meant.
std::string describePort(const PortDesc& port) {
    auto text = std::string(toString(port.kind));
    if (port.kind == SignalKind::Audio && port.channels != 2)
        text += "/" + std::to_string(port.channels);
    return text;
}

std::string describeOutputs(const PlanOp& op) {
    if (op.outputs.empty())
        return "-";

    std::string text;
    for (std::size_t i = 0; i < op.outputs.size(); ++i) {
        if (i > 0)
            text += ",";
        text += describePort(op.outputs[i]);
    }
    return text;
}

}  // namespace

std::string dumpPlan(const RenderPlan& plan) {
    std::ostringstream out;
    out << "magda-render-plan v" << plan.version << "\n";
    out << "ops=" << plan.ops.size() << " outputs=" << plan.outputOps.size() << "\n";

    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& op = plan.ops[i];
        out << "[" << rightAligned(static_cast<int>(i), 3) << "] " << padded(toString(op.kind), 11)
            << " " << padded(toString(op.liveness), 5) << " " << padded(toString(op.key), 30)
            << " in=" << padded(describeInputs(op), 16)
            << " out=" << padded(describeOutputs(op), 11);
        if (i < plan.dependencyCounts.size())
            out << " deps=" << plan.dependencyCounts[i];
        out << "\n";
    }

    out << "ready=";
    for (std::size_t i = 0; i < plan.initialReadyOps.size(); ++i)
        out << (i > 0 ? "," : "") << plan.initialReadyOps[i];
    out << "\n";

    for (const auto& diagnostic : plan.diagnostics)
        out << "diagnostic: " << diagnostic << "\n";

    return out.str();
}

}  // namespace magda::engine
