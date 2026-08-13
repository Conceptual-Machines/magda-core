#include "plan/PlanDiffDump.hpp"

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

        // Trailing spaces would not survive the repo's whitespace hook, which
        // would rewrite every golden the first time one was committed.
        auto text = line.str();
        while (!text.empty() && text.back() == ' ')
            text.pop_back();

        out << text << "\n";
    }

    for (const auto op : diff.retired)
        out << "retired " << toString(oldPlan.ops[static_cast<std::size_t>(op)].key) << "\n";

    return out.str();
}

}  // namespace magda::engine
