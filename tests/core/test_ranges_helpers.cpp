#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <ranges>
#include <set>
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

// The two collect helpers (#2144, #2149). std::ranges::to rejects juce::Array
// and StringArray, and is libstdc++ 14, past the GCC 13 floor in the README.

TEST_CASE("toStd() collects a pipeline into a vector", "[ranges][collect]") {
    const std::vector<int> source{1, 2, 3, 4, 5};
    const auto isOdd = [](int n) { return n % 2 != 0; };
    const auto doubled = [](int n) { return n * 2; };

    CHECK(magda::toStd<std::vector<int>>(source | std::views::filter(isOdd) |
                                         std::views::transform(doubled)) ==
          std::vector<int>{2, 6, 10});
}

TEST_CASE("toStd() reads as the last stage of the pipe", "[ranges][collect]") {
    const std::vector<int> source{1, 2, 3};
    const auto negated = [](int n) { return -n; };

    CHECK((source | std::views::transform(negated) | magda::toStd<std::vector<int>>()) ==
          std::vector<int>{-1, -2, -3});
}

TEST_CASE("toStd() fills a node container through insert()", "[ranges][collect]") {
    const std::vector<int> source{3, 1, 3, 2};

    CHECK((source | magda::toStd<std::set<int>>()) == std::set<int>{1, 2, 3});
}

TEST_CASE("toStd() of an empty pipeline is empty", "[ranges][collect]") {
    const std::vector<int> source{1, 3, 5};
    const auto isEven = [](int n) { return n % 2 == 0; };

    CHECK((source | std::views::filter(isEven) | magda::toStd<std::vector<int>>()).empty());
}

TEST_CASE("toStd() collects move-only elements", "[ranges][collect]") {
    const std::vector<int> source{1, 2, 3};
    const auto boxed = [](int n) { return std::make_unique<int>(n); };

    const auto boxes =
        source | std::views::transform(boxed) | magda::toStd<std::vector<std::unique_ptr<int>>>();

    REQUIRE(boxes.size() == 3);
    CHECK(*boxes[1] == 2);
}

TEST_CASE("toJuce() collects into a JUCE container", "[ranges][collect]") {
    const std::vector<int> source{1, 2, 3};
    const auto asString = [](int n) { return juce::String(n); };

    const auto names =
        source | std::views::transform(asString) | magda::toJuce<juce::StringArray>();

    REQUIRE(names.size() == 3);
    CHECK(names[2] == "3");
}
