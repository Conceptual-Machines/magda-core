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

RackNesting::Scope::Scope(RackNesting& nesting, RackId rack) : nesting_(nesting) {
    nesting_.open_.push_back(rack);
}

RackNesting::Scope::~Scope() {
    nesting_.open_.pop_back();
}

}  // namespace magda::engine
