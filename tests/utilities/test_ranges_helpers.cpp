#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <ranges>
#include <string>
#include <vector>

#include "../../magda/daw/core/RangesHelpers.hpp"

/**
 * The two adaptors that let ranges reach the JUCE types (#2144). Both exist
 * because the standard library will not take those types as they are:
 * juce::ValueTree is not a range, and juce::Array is not a ranges::to target.
 */

using magda::children;
using magda::toJuce;

namespace {

juce::ValueTree makeTree(std::initializer_list<int> orders) {
    juce::ValueTree root("root");
    for (int order : orders) {
        juce::ValueTree child("item");
        child.setProperty("order", order, nullptr);
        root.appendChild(child, nullptr);
    }
    return root;
}

}  // namespace

TEST_CASE("children() views a ValueTree in order", "[ranges]") {
    const auto root = makeTree({3, 1, 2});

    std::vector<int> orders;
    for (const auto& child : children(root))
        orders.push_back(static_cast<int>(child.getProperty("order")));

    REQUIRE(orders == std::vector<int>{3, 1, 2});
}

TEST_CASE("children() composes with the range adaptors", "[ranges]") {
    const auto root = makeTree({3, 1, 2});

    auto evens = children(root) | std::views::filter([](const juce::ValueTree& c) {
                     return static_cast<int>(c.getProperty("order")) % 2 == 0;
                 });

    REQUIRE(std::ranges::distance(evens) == 1);
    REQUIRE(static_cast<int>((*evens.begin()).getProperty("order")) == 2);
}

TEST_CASE("children() of a childless or invalid tree is empty", "[ranges]") {
    REQUIRE(std::ranges::empty(children(juce::ValueTree("root"))));
    REQUIRE(std::ranges::empty(children(juce::ValueTree())));
}

TEST_CASE("children() outlives the tree it was given", "[ranges]") {
    // The range holds a ValueTree by value, so a temporary source is safe.
    auto range = children(makeTree({7}));
    REQUIRE(std::ranges::distance(range) == 1);
    REQUIRE(static_cast<int>((*range.begin()).getProperty("order")) == 7);
}

TEST_CASE("toJuce() collects into a StringArray", "[ranges]") {
    const std::vector<int> ids{1, 2, 3};

    const auto names =
        toJuce<juce::StringArray>(ids | std::views::transform([](int id) {
                                      return juce::String("track ") + juce::String(id);
                                  }));

    REQUIRE(names.size() == 3);
    REQUIRE(names[0] == "track 1");
    REQUIRE(names[2] == "track 3");
}

TEST_CASE("toJuce() collects into an Array and an Array<var>", "[ranges]") {
    const auto root = makeTree({3, 1, 2});

    const auto orders = toJuce<juce::Array<int>>(
        children(root) | std::views::transform([](const juce::ValueTree& c) {
            return static_cast<int>(c.getProperty("order"));
        }));
    REQUIRE(orders.size() == 3);
    REQUIRE(orders[0] == 3);

    const auto vars = toJuce<juce::Array<juce::var>>(
        std::views::iota(0, 3) | std::views::transform([](int i) { return juce::var(i); }));
    REQUIRE(vars.size() == 3);
    REQUIRE(static_cast<int>(vars[2]) == 2);
}

TEST_CASE("toJuce() takes an unsized range", "[ranges]") {
    const std::vector<int> ids{1, 2, 3, 4};

    const auto odd =
        toJuce<juce::Array<int>>(ids | std::views::filter([](int id) { return id % 2 == 1; }));

    REQUIRE(odd.size() == 2);
    REQUIRE(odd[1] == 3);
}
