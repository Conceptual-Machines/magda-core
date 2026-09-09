#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <ranges>
#include <vector>

#include "magda/daw/core/RangesHelpers.hpp"

// The two ValueTree adaptors (#2144, #2150). juce::ValueTree is not a
// std::ranges::range and removes one child or all of them, so state code that
// wants "the children like this" or "none of that kind" goes through these.

namespace {

juce::ValueTree treeWith(std::initializer_list<const char*> childTypes) {
    juce::ValueTree tree("ROOT");
    int index = 0;
    for (const auto* type : childTypes) {
        juce::ValueTree child{juce::Identifier(type)};
        child.setProperty("idx", index++, nullptr);
        tree.appendChild(child, nullptr);
    }
    return tree;
}

std::vector<int> indicesOf(auto range) {
    std::vector<int> out;
    for (const auto& child : range)
        out.push_back(static_cast<int>(child.getProperty("idx")));
    return out;
}

}  // namespace

TEST_CASE("children() walks a ValueTree in order", "[ranges][valuetree]") {
    const auto tree = treeWith({"STEP", "NOTE", "STEP"});
    CHECK(indicesOf(magda::children(tree)) == std::vector<int>{0, 1, 2});
}

TEST_CASE("children() pipes into a view", "[ranges][valuetree]") {
    const auto tree = treeWith({"STEP", "NOTE", "STEP"});
    const auto isStep = [](const juce::ValueTree& child) {
        return child.hasType(juce::Identifier("STEP"));
    };
    CHECK(indicesOf(magda::children(tree) | std::views::filter(isStep)) == std::vector<int>{0, 2});
}

TEST_CASE("children() of a childless or invalid tree is empty", "[ranges][valuetree]") {
    CHECK(std::ranges::empty(magda::children(treeWith({}))));
    CHECK(std::ranges::empty(magda::children(juce::ValueTree())));
}

TEST_CASE("removeChildrenWithType() takes every child of that type", "[ranges][valuetree]") {
    auto tree = treeWith({"STEP", "NOTE", "STEP", "STEP"});

    magda::removeChildrenWithType(tree, juce::Identifier("STEP"));

    REQUIRE(tree.getNumChildren() == 1);
    CHECK(tree.getChild(0).hasType(juce::Identifier("NOTE")));
}

TEST_CASE("removeChildrenWithType() keeps the order of what is left", "[ranges][valuetree]") {
    auto tree = treeWith({"NOTE", "STEP", "NOTE", "STEP", "NOTE"});

    magda::removeChildrenWithType(tree, juce::Identifier("STEP"));

    CHECK(indicesOf(magda::children(tree)) == std::vector<int>{0, 2, 4});
}

TEST_CASE("removeChildrenWithType() is a no-op when nothing matches", "[ranges][valuetree]") {
    auto tree = treeWith({"NOTE", "NOTE"});

    magda::removeChildrenWithType(tree, juce::Identifier("STEP"));

    CHECK(tree.getNumChildren() == 2);
}

TEST_CASE("removeChildrenWithType() leaves nested children of that type alone",
          "[ranges][valuetree]") {
    auto tree = treeWith({"NOTE"});
    auto nested = tree.getChild(0);
    nested.appendChild(juce::ValueTree("STEP"), nullptr);

    magda::removeChildrenWithType(tree, juce::Identifier("STEP"));

    CHECK(tree.getChild(0).getNumChildren() == 1);
}
