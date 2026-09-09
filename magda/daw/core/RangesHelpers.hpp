#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <ranges>
#include <utility>

namespace magda {

/** @brief A range over a ValueTree's children (#2144).
 *
 *  juce::ValueTree is not a std::ranges::range: its Iterator is not a
 *  std::input_iterator. Taken by value because the handle is ref-counted, so
 *  the returned range keeps the tree alive past the call.
 */
inline auto children(juce::ValueTree tree) {
    return std::views::iota(0, tree.getNumChildren()) |
           std::views::transform([tree](int i) { return tree.getChild(i); });
}

/** @brief Collect a range into a JUCE container (#2144).
 *
 *  std::ranges::to rejects juce::Array, StringArray and Array<var>: they grow
 *  through add(), not push_back() or insert().
 */
template <class C, std::ranges::input_range R> C toJuce(R&& range) {
    C out;
    if constexpr (std::ranges::sized_range<R> && requires { out.ensureStorageAllocated(0); })
        out.ensureStorageAllocated(static_cast<int>(std::ranges::size(range)));
    for (auto&& value : range)
        out.add(std::forward<decltype(value)>(value));
    return out;
}

}  // namespace magda
