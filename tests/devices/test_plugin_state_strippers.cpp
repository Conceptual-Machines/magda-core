#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/TracktionHelpers.hpp"

// Both strippers run on a plugin's saved ValueTree before it is restored into
// a rebuilt chain (#2150). What they take off and what they must leave behind
// is the difference between a restored device and a doubly-modulated one.

namespace {

namespace te = tracktion;

juce::ValueTree pluginTreeWithAssignmentsAtEveryDepth() {
    juce::ValueTree plugin("PLUGIN");
    plugin.setProperty(te::IDs::id, 101, nullptr);
    plugin.setProperty("type", "4osc", nullptr);
    plugin.appendChild(juce::ValueTree(te::IDs::MODIFIERASSIGNMENTS), nullptr);

    juce::ValueTree macros("MACROPARAMETERS");
    macros.setProperty(te::IDs::id, 202, nullptr);
    macros.appendChild(juce::ValueTree(te::IDs::MODIFIERASSIGNMENTS), nullptr);

    juce::ValueTree macro("MACROPARAMETER");
    macro.setProperty(te::IDs::id, 303, nullptr);
    macro.setProperty("value", 0.75, nullptr);
    macro.appendChild(juce::ValueTree(te::IDs::MODIFIERASSIGNMENTS), nullptr);
    macros.appendChild(macro, nullptr);

    plugin.appendChild(macros, nullptr);
    return plugin;
}

int countAssignments(const juce::ValueTree& tree) {
    int found = tree.hasType(te::IDs::MODIFIERASSIGNMENTS) ? 1 : 0;
    for (const auto& child : tree)
        found += countAssignments(child);
    return found;
}

}  // namespace

TEST_CASE("Stripping modifier assignments reaches every depth", "[plugin][state]") {
    auto plugin = pluginTreeWithAssignmentsAtEveryDepth();
    REQUIRE(countAssignments(plugin) == 3);

    magda::stripModifierAssignmentsRecursive(plugin);

    CHECK(countAssignments(plugin) == 0);
}

TEST_CASE("Stripping modifier assignments keeps everything else", "[plugin][state]") {
    auto plugin = pluginTreeWithAssignmentsAtEveryDepth();
    magda::stripModifierAssignmentsRecursive(plugin);

    // The subtree the assignments hung off is still there, with its properties.
    REQUIRE(plugin.getNumChildren() == 1);
    const auto macros = plugin.getChild(0);
    CHECK(macros.hasType(juce::Identifier("MACROPARAMETERS")));
    REQUIRE(macros.getNumChildren() == 1);
    const auto macro = macros.getChild(0);
    CHECK(macro.hasType(juce::Identifier("MACROPARAMETER")));
    CHECK(static_cast<double>(macro.getProperty("value")) == 0.75);
    // Only the assignments go; te::IDs::id is the other stripper's business.
    CHECK(plugin.hasProperty(te::IDs::id));
}

TEST_CASE("Stripping Tracktion ids reaches every depth", "[plugin][state]") {
    auto plugin = pluginTreeWithAssignmentsAtEveryDepth();

    magda::stripTracktionIdsRecursive(plugin);

    CHECK_FALSE(plugin.hasProperty(te::IDs::id));
    const auto macros = plugin.getChild(0);
    CHECK_FALSE(macros.hasProperty(te::IDs::id));
    CHECK_FALSE(macros.getChild(0).hasProperty(te::IDs::id));
    // The properties that are not ids stay.
    CHECK(plugin.getProperty("type").toString() == "4osc");
}

TEST_CASE("Both strippers tolerate an invalid tree", "[plugin][state]") {
    juce::ValueTree nothing;
    magda::stripModifierAssignmentsRecursive(nothing);
    magda::stripTracktionIdsRecursive(nothing);
    CHECK_FALSE(nothing.isValid());
}
