#include "param/ParamTable.hpp"

#include <algorithm>

namespace magda::engine {

std::span<const ParamLink> ParamTable::linksFor(ParamId param) const {
    if (param < 0 || param + 1 >= static_cast<ParamId>(linkOffsets.size()))
        return {};

    const auto first = static_cast<std::size_t>(linkOffsets[static_cast<std::size_t>(param)]);
    const auto last = static_cast<std::size_t>(linkOffsets[static_cast<std::size_t>(param) + 1]);
    if (last <= first || last > links.size())
        return {};

    return std::span<const ParamLink>{links}.subspan(first, last - first);
}

}  // namespace magda::engine
