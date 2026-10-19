#include <catch2/catch_test_macros.hpp>

#include "music/GrooveLibrary.hpp"

using magda::GrooveLibrary;
using magda::GrooveTemplateData;

namespace {

/**
 * @brief Clears the library on both entry and exit.
 *
 * It is a singleton, so a case that fails past its own cleanup would otherwise leave a
 * store, or an onChanged capturing a dead stack slot, installed for whatever runs next.
 */
struct EmptyLibrary {
    EmptyLibrary() {
        reset();
    }
    ~EmptyLibrary() {
        reset();
    }

    static void reset() {
        auto& library = GrooveLibrary::getInstance();
        library.forgetStore();
        library.import({});
    }
};

GrooveTemplateData swing(const juce::String& name = "Swing") {
    return {.name = name,
            .notesPerBeat = 2,
            .parameterized = true,
            .latenessProportions = {0.0f, 0.66f}};
}

}  // namespace

TEST_CASE("The library holds the grooves rather than asking an engine", "[groove-library][2757]") {
    const EmptyLibrary guard;
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
    const EmptyLibrary guard;
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
}

TEST_CASE("A groove with nothing to apply is refused", "[groove-library][2757]") {
    const EmptyLibrary guard;
    auto& library = GrooveLibrary::getInstance();
    library.import({});

    CHECK_FALSE(library.upsert({.name = "", .latenessProportions = {0.0f, 0.5f}}));
    CHECK_FALSE(library.upsert({.name = "Empty", .latenessProportions = {}}));
    CHECK(library.names().isEmpty());
}

TEST_CASE("A write the store refuses does not enter the library", "[groove-library][2757]") {
    const EmptyLibrary guard;
    // The store persists the list, so a groove it would not keep must not appear to be
    // there until the next restart says otherwise.
    auto& library = GrooveLibrary::getInstance();
    library.setStore([] { return std::vector<GrooveTemplateData>{}; },
                     [](const GrooveTemplateData&) { return false; });

    CHECK_FALSE(library.upsert(swing()));
    CHECK(library.names().isEmpty());
}

TEST_CASE("The library takes back what the store kept, not what it was asked for",
          "[groove-library][2757]") {
    // The store trims and deduplicates the name and forces the parameterized flag to its
    // own mode, so the library keeps what the store returned rather than the request.
    std::vector<GrooveTemplateData> stored;
    auto& library = GrooveLibrary::getInstance();
    library.setStore([&stored] { return stored; },
                     [&stored](const GrooveTemplateData& groove) {
                         auto canonical = groove;
                         canonical.name = groove.name.trim() + " (2)";
                         canonical.parameterized = true;
                         stored = {canonical};
                         return true;
                     });

    auto asked = swing("  Swing  ");
    asked.parameterized = false;
    CHECK(library.upsert(asked));

    REQUIRE(library.names().size() == 1);
    CHECK(library.names()[0] == "Swing (2)");
    REQUIRE(library.find("Swing (2)") != nullptr);
    CHECK(library.find("Swing (2)")->parameterized);
    CHECK(library.find("  Swing  ") == nullptr);
}

TEST_CASE("A groove is looked up as the time bend it plays", "[groove-library][3044]") {
    const EmptyLibrary guard;
    auto& library = GrooveLibrary::getInstance();
    library.import({swing(),
                    {.name = "Fixed",
                     .notesPerBeat = 2,
                     .parameterized = false,
                     .latenessProportions = {0.0f, 0.5f}}});

    const auto swung = library.bend("Swing", 4.0, 0.25f);
    REQUIRE(swung.has_value());
    CHECK(swung->on);
    CHECK(swung->amount == 0.25f);

    // A table that is not parameterized carries its amount itself.
    const auto fixed = library.bend("Fixed", 4.0, 0.25f);
    REQUIRE(fixed.has_value());
    CHECK(fixed->amount == 1.0f);

    CHECK_FALSE(library.bend("Missing", 4.0).has_value());
}

TEST_CASE("The list outlives the engine that filled it", "[groove-library][2757]") {
    const EmptyLibrary guard;
    // forgetStore is engine teardown; the grooves are the app's and stay.
    auto& library = GrooveLibrary::getInstance();
    library.import({swing()});
    library.setStore([] { return std::vector<GrooveTemplateData>{swing()}; },
                     [](const GrooveTemplateData&) { return true; });
    library.forgetStore();

    CHECK(library.names() == juce::StringArray{"Swing"});

    // With no store behind it, an upsert is still taken.
    CHECK(library.upsert(swing("Shuffle")));
    CHECK(library.names().size() == 2);
}
