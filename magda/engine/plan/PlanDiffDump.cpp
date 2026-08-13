#include "plan/PlanDiffDump.hpp"

#include <sstream>

#include "plan/DumpFormat.hpp"

namespace magda::engine {
namespace {

using dump_format::padded;
using dump_format::rightAligned;
using dump_format::writeLine;

}  // namespace

std::string dumpPlanDiff(const RenderPlan& oldPlan, const RenderPlan& newPlan,
                         const PlanDiff& diff) {
    std::ostringstream out;
    out << "magda-plan-diff v1\n";
    out << "ops=" << newPlan.ops.size() << " carried=" << diff.carried
        << " retired=" << diff.retired.size() << "\n";

    for (std::size_t i = 0; i < newPlan.ops.size(); ++i) {
        const auto from = i < diff.carriedFrom.size() ? diff.carriedFrom[i] : INVALID_OP_ID;

        std::ostringstream line;
        line << "[" << rightAligned(static_cast<int>(i), 3) << "] "
             << padded(from == INVALID_OP_ID ? "new" : "carry", 7) << " "
             << padded(toString(newPlan.ops[i].key), 30);

        if (from != INVALID_OP_ID)
            line << " from=" << toString(oldPlan.ops[static_cast<std::size_t>(from)].key);

        writeLine(out, line.str());
    }

    for (const auto op : diff.retired)
        out << "retired " << toString(oldPlan.ops[static_cast<std::size_t>(op)].key) << "\n";

    return out.str();
}

}  // namespace magda::engine
