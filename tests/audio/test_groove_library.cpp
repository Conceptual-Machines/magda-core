#include <catch2/catch_test_macros.hpp>

#include "music/GrooveLibrary.hpp"

using magda::GrooveLibrary;
using magda::GrooveTemplateData;

namespace {

GrooveTemplateData swing(const juce::String& name = "Swing") {
    return {.name = name,
            .notesPerBeat = 2,
            .parameterized = true,
            .latenessProportions = {0.0f, 0.66f}};
}

}  // namespace

TEST_CASE("The library holds the grooves rather than asking an engine", "[groove-library][2757]") {
    // The list is the app's, so it answers the same whichever engine renders -- which is
    // what let a groove exist under one and not the other.
    auto& library = GrooveLibrary::getInstance();
    library.import({swing(), swing("Shuffle")});

    CHECK(library.names() == juce::StringArray{"Swing", "Shuffle"});
    REQUIRE(library.find("Swing") != nullptr);
    CHECK(library.find("Swing")->latenessProportions == std::vector<float>{0.0f, 0.66f});
    CHECK(library.find("Nothing") == nullptr);

    library.import({});
    CHECK(library.names().isEmpty());
}

TEST_CASE("An upsert replaces the groove of that name", "[groove-library][2757]") {
    auto& library = GrooveLibrary::getInstance();
    library.import({swing()});

    auto harder = swing();
    harder.latenessProportions = {0.0f, 0.9f};
    CHECK(library.upsert(harder));

    CHECK(library.names().size() == 1);
    REQUIRE(library.find("Swing") != nullptr);
    CHECK(library.find("Swing")->latenessProportions == std::vector<float>{0.0f, 0.9f});

    CHECK(library.upsert(swing("Shuffle")));
    CHECK(library.names().size() == 2);

    library.import({});
}

TEST_CASE("A groove with nothing to apply is refused", "[groove-library][2757]") {
    auto& library = GrooveLibrary::getInstance();
    library.import({});

    CHECK_FALSE(library.upsert({.name = "", .latenessProportions = {0.0f, 0.5f}}));
    CHECK_FALSE(library.upsert({.name = "Empty", .latenessProportions = {}}));
    CHECK(library.names().isEmpty());
}

TEST_CASE("A write the engine refuses does not enter the library", "[groove-library][2757]") {
    // The fork persists the list, so a groove it would not keep must not appear to be
    // there until the next restart says otherwise.
    auto& library = GrooveLibrary::getInstance();
    library.import({});
    library.setWriter([](const GrooveTemplateData&) { return false; });

    CHECK_FALSE(library.upsert(swing()));
    CHECK(library.names().isEmpty());

    library.setWriter([](const GrooveTemplateData&) { return true; });
    CHECK(library.upsert(swing()));
    CHECK(library.names() == juce::StringArray{"Swing"});

    library.forgetWriter();
    library.import({});
}

TEST_CASE("The list outlives the engine that filled it", "[groove-library][2757]") {
    // forgetWriter is engine teardown; the grooves are the app's and stay.
    auto& library = GrooveLibrary::getInstance();
    library.import({swing()});
    library.setWriter([](const GrooveTemplateData&) { return true; });
    library.forgetWriter();

    CHECK(library.names() == juce::StringArray{"Swing"});

    // With nothing to persist through, an upsert is still taken.
    CHECK(library.upsert(swing("Shuffle")));
    CHECK(library.names().size() == 2);

    library.import({});
}
