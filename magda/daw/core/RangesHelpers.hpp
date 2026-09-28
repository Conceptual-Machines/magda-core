#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <cstddef>
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

/** @brief Remove every child of a type (#2150).
 *
 *  juce::ValueTree removes one child or all of them; clearing one kind is what
 *  a device does to its own state before writing it out again.
 */
inline void removeChildrenWithType(juce::ValueTree& tree, const juce::Identifier& type,
                                   juce::UndoManager* undoManager = nullptr) {
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (tree.getChild(i).hasType(type))
            tree.removeChild(i, undoManager);
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

/** @brief Collect a range into a std container (#2149).
 *
 *  std::ranges::to is libstdc++ 14, and Linux CI builds with GCC 13.3, which is
 *  the floor the README documents.
 */
template <class C, std::ranges::input_range R> C toStd(R&& range) {
    C out;
    if constexpr (std::ranges::sized_range<R> && requires { out.reserve(std::size_t{}); })
        out.reserve(static_cast<std::size_t>(std::ranges::size(range)));
    for (auto&& value : range)
        out.insert(out.end(), std::forward<decltype(value)>(value));
    return out;
}

namespace detail {

template <class C> struct ToJuceClosure {
    template <std::ranges::input_range R> friend C operator|(R&& range, ToJuceClosure) {
        return toJuce<C>(std::forward<R>(range));
    }
};

template <class C> struct ToStdClosure {
    template <std::ranges::input_range R> friend C operator|(R&& range, ToStdClosure) {
        return toStd<C>(std::forward<R>(range));
    }
};

}  // namespace detail

/** @brief The pipe forms, so a pipeline ends where it reads: `| toStd<V>()`. */
template <class C> auto toJuce() {
    return detail::ToJuceClosure<C>{};
}

template <class C> auto toStd() {
    return detail::ToStdClosure<C>{};
}

}  // namespace magda
