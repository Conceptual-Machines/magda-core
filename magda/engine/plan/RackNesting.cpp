#include "plan/RackNesting.hpp"

#include <algorithm>

namespace magda::engine {

bool RackNesting::encloses(RackId rack) const {
    return std::ranges::find(open_, rack) != open_.end();
}

std::string RackNesting::cycle(RackId rack) const {
    std::string path;
    for (const auto open : open_)
        path += (path.empty() ? "R" : " > R") + std::to_string(open);
    path += " > R" + std::to_string(rack);

    return "rack " + std::to_string(rack) + " contains itself: " + path;
}

void RackNesting::open(RackId rack) {
    open_.push_back(rack);
}

void RackNesting::close(RackId rack) {
    // By identity rather than by position: a caller closing something other
    // than the innermost open rack has lost track of its own descent, and
    // popping regardless would hide that and leave the stack wrong from there
    // on.
    if (!open_.empty() && open_.back() == rack)
        open_.pop_back();
}

RackNesting::Scope::Scope(RackNesting& nesting, RackId rack) : nesting_(nesting), rack_(rack) {
    nesting_.open(rack);
}

RackNesting::Scope::~Scope() {
    nesting_.close(rack_);
}

}  // namespace magda::engine
