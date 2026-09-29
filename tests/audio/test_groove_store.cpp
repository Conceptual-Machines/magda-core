
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/GrooveStore.hpp"

/// @file The groove library's persistence in MAGDA's own file, and the one-time import (#2761).

using magda::GrooveStore;
using magda::GrooveTemplateData;

namespace {

constexpr int kShippedGrooves = 176;
constexpr int kParameterizedGrooves = 21;

void writeGrooves(const juce::File& file, const juce::String& grooves) {
    REQUIRE(file.replaceWithText("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<GROOVETEMPLATES>" +
                                 grooves + "</GROOVETEMPLATES>\n"));
}

/// The legacy Settings.xml layout: the list as the one child of the GrooveTemplates VALUE.
void writeLegacySettings(const juce::File& file, const juce::String& grooves) {
    REQUIRE(file.replaceWithText("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n"
                                 "<VALUE name=\"GrooveTemplates\"><GROOVETEMPLATES>" +
                                 grooves + "</GROOVETEMPLATES></VALUE>\n</PROPERTIES>\n"));
}

juce::StringArray namesOf(const GrooveStore& store) {
    juce::StringArray names;
    for (const auto& groove : store.grooves())
        names.add(groove.name);
    return names;
}

}  // namespace

TEST_CASE("An empty file is seeded with the shipped grooves", "[groove-store][2761]") {
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
        writeGrooves(file.getFile(), R"(<GROOVETEMPLATE name="Mine" numberOfNotes="4"
            notesPerBeat="4" parameterized="1"><SHIFT delta="0.25"/></GROOVETEMPLATE>)");
        const GrooveStore store(file.getFile());

        CHECK(namesOf(store) == juce::StringArray{"Mine", "Basic 8th Swing", "Basic 16th Swing"});
        // Saved without its trailing zeros, which read back as on the grid.
        CHECK(store.grooves().front().latenessProportions ==
              std::vector<float>{0.25f, 0.0f, 0.0f, 0.0f});
    }

    SECTION("with none, it is") {
        writeGrooves(file.getFile(), R"(<GROOVETEMPLATE name="Mine" numberOfNotes="2"
            notesPerBeat="2"><SHIFT delta="0.1"/></GROOVETEMPLATE>)");
        const GrooveStore store(file.getFile());

        CHECK(store.grooves().size() == 1 + kParameterizedGrooves + 2);
    }
}

TEST_CASE("A write is canonicalised as the groove editor expects", "[groove-store][2761]") {
    const juce::TemporaryFile file(".xml");
    writeGrooves(file.getFile(), R"(<GROOVETEMPLATE name="Mine" numberOfNotes="2"
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

TEST_CASE("The groove store saves its own file and reads it back", "[groove-store][2761]") {
    const juce::TemporaryFile file(".xml");
    const std::vector<float> latenesses{0.12345f, -0.5f, 0.0f, 0.0f};
    {
        GrooveStore store(file.getFile());
        REQUIRE(
            store.upsert({.name = "Ahead", .notesPerBeat = 4, .latenessProportions = latenesses}));
    }

    const auto expectedXml = juce::parseXML(
        R"(<GROOVETEMPLATE name="Ahead" numberOfNotes="4" notesPerBeat="4" parameterized="1"><SHIFT delta="0.123"/><SHIFT delta="-0.5"/></GROOVETEMPLATE>)");
    REQUIRE(expectedXml != nullptr);

    const auto saved = juce::parseXML(file.getFile());
    REQUIRE(saved != nullptr);
    REQUIRE(saved->hasTagName("GROOVETEMPLATES"));
    const auto* ahead = saved->getChildByAttribute("name", "Ahead");
    REQUIRE(ahead != nullptr);
    CHECK(ahead->isEquivalentTo(expectedXml.get(), false));

    // The seeded list went with it, so a fresh read is the same list less the rounding.
    const GrooveStore reread(file.getFile());
    CHECK(reread.grooves().size() == kShippedGrooves + kParameterizedGrooves + 3);
    CHECK(reread.grooves().back().latenessProportions ==
          std::vector<float>{0.123f, -0.5f, 0.0f, 0.0f});
}

TEST_CASE("The legacy Settings.xml list is imported once", "[groove-store][2761]") {
    const juce::TemporaryFile file(".xml");
    const juce::TemporaryFile legacy(".xml");
    writeLegacySettings(legacy.getFile(), R"(<GROOVETEMPLATE name="Mine" numberOfNotes="4"
        notesPerBeat="4" parameterized="1"><SHIFT delta="0.25"/></GROOVETEMPLATE>)");

    {
        const GrooveStore store(file.getFile(), legacy.getFile());
        CHECK(namesOf(store) == juce::StringArray{"Mine", "Basic 8th Swing", "Basic 16th Swing"});
    }
    // Imported means saved, so the legacy file is not read again and is left as it was.
    CHECK(file.getFile().existsAsFile());
    CHECK(juce::parseXML(legacy.getFile())->getChildByAttribute("name", "GrooveTemplates") !=
          nullptr);

    writeLegacySettings(legacy.getFile(), R"(<GROOVETEMPLATE name="Other" numberOfNotes="2"
        notesPerBeat="2" parameterized="1"><SHIFT delta="0.1"/></GROOVETEMPLATE>)");
    const GrooveStore again(file.getFile(), legacy.getFile());
    CHECK(namesOf(again).contains("Mine"));
    CHECK(!namesOf(again).contains("Other"));
}

TEST_CASE("A legacy file with no groove list imports nothing", "[groove-store][2761]") {
    const juce::TemporaryFile file(".xml");
    const juce::TemporaryFile legacy(".xml");
    REQUIRE(legacy.getFile().replaceWithText(
        "<PROPERTIES><VALUE name=\"Kept\" val=\"1\"/></PROPERTIES>"));

    const GrooveStore store(file.getFile(), legacy.getFile());
    CHECK(store.grooves().size() == kShippedGrooves + kParameterizedGrooves + 2);
    CHECK(!file.getFile().exists());
}
