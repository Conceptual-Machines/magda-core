#include <tracktion_engine/tracktion_engine.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/GrooveStore.hpp"

/// @file The groove library's persistence without Tracktion, in Tracktion's format (#2761).

using magda::GrooveStore;
using magda::GrooveTemplateData;

namespace {

constexpr int kShippedGrooves = 176;
constexpr int kParameterizedGrooves = 21;

void writeSettings(const juce::File& file, const juce::String& grooves) {
    REQUIRE(file.replaceWithText("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n"
                                 "<VALUE name=\"GrooveTemplates\"><GROOVETEMPLATES>" +
                                 grooves + "</GROOVETEMPLATES></VALUE>\n</PROPERTIES>\n"));
}

const juce::XmlElement* savedGrooves(const juce::XmlElement& settings) {
    const auto* value = settings.getChildByAttribute("name", "GrooveTemplates");
    return value != nullptr ? value->getChildByName("GROOVETEMPLATES") : nullptr;
}

juce::StringArray namesOf(const GrooveStore& store) {
    juce::StringArray names;
    for (const auto& groove : store.grooves())
        names.add(groove.name);
    return names;
}

}  // namespace

TEST_CASE("An empty file is seeded as Tracktion seeds it", "[groove-store][2761]") {
    const juce::TemporaryFile file(".xml");
    const GrooveStore store(file.getFile());

    const auto names = namesOf(store);
    REQUIRE(names.size() == kShippedGrooves + kParameterizedGrooves + 2);
    CHECK(names[names.size() - 2] == "Basic 8th Swing");
    CHECK(names[names.size() - 1] == "Basic 16th Swing");
    CHECK(!store.grooves().front().parameterized);
    CHECK(store.grooves()[kShippedGrooves].parameterized);

    // Seeding is not a write: the file appears with the first upsert.
    CHECK(!file.getFile().exists());
}

TEST_CASE("A stored list is the list, and the swings it lacks", "[groove-store][2761]") {
    const juce::TemporaryFile file(".xml");

    SECTION("with a parameterized groove, the parameterized set is not added") {
        writeSettings(file.getFile(), R"(<GROOVETEMPLATE name="Mine" numberOfNotes="4"
            notesPerBeat="4" parameterized="1"><SHIFT delta="0.25"/></GROOVETEMPLATE>)");
        const GrooveStore store(file.getFile());

        CHECK(namesOf(store) == juce::StringArray{"Mine", "Basic 8th Swing", "Basic 16th Swing"});
        // Saved without its trailing zeros, which read back as on the grid.
        CHECK(store.grooves().front().latenessProportions ==
              std::vector<float>{0.25f, 0.0f, 0.0f, 0.0f});
    }

    SECTION("with none, it is") {
        writeSettings(file.getFile(), R"(<GROOVETEMPLATE name="Mine" numberOfNotes="2"
            notesPerBeat="2"><SHIFT delta="0.1"/></GROOVETEMPLATE>)");
        const GrooveStore store(file.getFile());

        CHECK(store.grooves().size() == 1 + kParameterizedGrooves + 2);
    }
}

TEST_CASE("A write is kept as Tracktion's updateTemplate keeps it", "[groove-store][2761]") {
    const juce::TemporaryFile file(".xml");
    writeSettings(file.getFile(), R"(<GROOVETEMPLATE name="Mine" numberOfNotes="2"
        notesPerBeat="2" parameterized="1"><SHIFT delta="0.1"/></GROOVETEMPLATE>)");
    GrooveStore store(file.getFile());

    REQUIRE(store.upsert({.name = "  Pushed  ",
                          .notesPerBeat = 12,
                          .parameterized = false,
                          .latenessProportions = {0.5f, 2.0f, 0.0f}}));
    const auto& pushed = store.grooves().back();
    CHECK(pushed.name == "Pushed");
    CHECK(pushed.notesPerBeat == 8);
    CHECK(pushed.parameterized);
    CHECK(pushed.latenessProportions == std::vector<float>{0.5f, 1.0f, 0.0f});

    // Replaced where it stood, and a single step is padded to the two a groove needs.
    REQUIRE(store.upsert({.name = "Mine", .latenessProportions = {0.3f}}));
    CHECK(store.grooves().front().name == "Mine");
    CHECK(store.grooves().front().latenessProportions == std::vector<float>{0.3f, 0.0f});

    // Only an exact name replaces; a clash after the trim is numbered from the base name.
    REQUIRE(store.upsert({.name = "Mine ", .latenessProportions = {0.1f, 0.2f}}));
    REQUIRE(store.upsert({.name = "Mine (2)", .latenessProportions = {0.1f, 0.2f}}));
    REQUIRE(store.upsert({.name = "Mine (2) ", .latenessProportions = {0.1f, 0.2f}}));
    CHECK(namesOf(store).contains("Mine (2)"));
    CHECK(namesOf(store).contains("Mine (3)"));

    REQUIRE(store.upsert(
        {.name = juce::String::repeatedString("x", 40), .latenessProportions = {0.1f, 0.2f}}));
    CHECK(store.grooves().back().name.length() == 32);

    CHECK(!store.upsert({.name = "Empty", .latenessProportions = {}}));
}

TEST_CASE("What is saved is what Tracktion saves, and reads back", "[groove-store][2761]") {
    const juce::TemporaryFile file(".xml");
    REQUIRE(file.getFile().replaceWithText(R"(<?xml version="1.0" encoding="UTF-8"?>
<PROPERTIES><VALUE name="Kept" val="1"/></PROPERTIES>)"));
    const std::vector<float> latenesses{0.12345f, -0.5f, 0.0f, 0.0f};
    {
        GrooveStore store(file.getFile());
        REQUIRE(
            store.upsert({.name = "Ahead", .notesPerBeat = 4, .latenessProportions = latenesses}));
    }

    // Written as the fork's upsert builds a template, then through its own createXml.
    tracktion::GrooveTemplate expected;
    expected.setName("Ahead");
    expected.setNumberOfNotes(static_cast<int>(latenesses.size()));
    expected.setNotesPerBeat(4);
    expected.setParameterized(true);
    for (int i = 0; i < static_cast<int>(latenesses.size()); ++i)
        expected.setLatenessProportion(i, latenesses[static_cast<size_t>(i)], 1.0f);
    const std::unique_ptr<juce::XmlElement> expectedXml(expected.createXml());

    const auto settings = juce::parseXML(file.getFile());
    REQUIRE(settings != nullptr);
    CHECK(settings->getChildByAttribute("name", "Kept") != nullptr);
    const auto* saved = savedGrooves(*settings);
    REQUIRE(saved != nullptr);
    const auto* ahead = saved->getChildByAttribute("name", "Ahead");
    REQUIRE(ahead != nullptr);
    CHECK(ahead->isEquivalentTo(expectedXml.get(), false));

    // The seeded list went with it, so a fresh read is the same list less the rounding.
    const GrooveStore reread(file.getFile());
    CHECK(reread.grooves().size() == kShippedGrooves + kParameterizedGrooves + 3);
    CHECK(reread.grooves().back().latenessProportions ==
          std::vector<float>{0.123f, -0.5f, 0.0f, 0.0f});
}
