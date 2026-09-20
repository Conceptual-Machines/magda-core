#include <juce_core/juce_core.h>

#include <vector>

#include "SharedTestEngine.hpp"
#include "magda/daw/engine/host/GrooveEntries.hpp"
#include "magda/daw/music/GrooveLibrary.hpp"

namespace {

using magda::daw::engine_host::EngineHost;
using magda::daw::engine_host::grooveSetFrom;

class GrooveEntriesTests final : public juce::UnitTest {
  public:
    GrooveEntriesTests() : juce::UnitTest("Groove Entries", "magda") {}

    void runTest() override {
        testEntriesReachTheSet();
        testNothingSuppliedGroovesNothing();
        testShippedParameterizedGroovesAreImported();
    }

    void testShippedParameterizedGroovesAreImported() {
        beginTest("The shipped parameterized grooves reach the library");

        // Tracktion's manager hides every parameterized groove behind its active list,
        // which is off by default, so these two ship with the fork and were missing from
        // the library -- and from the native engine -- until the import turns it on.
        magda::test::getSharedEngine();

        const auto names = magda::GrooveLibrary::getInstance().names();
        expect(names.contains("Basic 8th Swing"), "The 8th swing preset is in the library");
        expect(names.contains("Basic 16th Swing"), "and so is the 16th");

        const auto* groove = magda::GrooveLibrary::getInstance().find("Basic 8th Swing");
        expect(groove != nullptr, "and it is findable by name");
        if (groove != nullptr) {
            expect(groove->parameterized, "and it kept the flag it ships with");
            expect(!groove->latenessProportions.empty(), "and the lateness it grooves by");
        }
    }

    void testEntriesReachTheSet() {
        beginTest("The app's entries compile into the engine's set");

        // numNotes comes off the lateness table, which the conversion also moves out of
        // the entry: reading it after the move would compile every groove as empty.
        std::vector<EngineHost::GrooveEntry> entries{{.name = "Swing",
                                                      .latenesses = {0.0f, 0.66f},
                                                      .notesPerBeat = 2,
                                                      .parameterized = true},
                                                     {.name = "Shuffle",
                                                      .latenesses = {0.0f, 0.3f, 0.0f, 0.3f},
                                                      .notesPerBeat = 4,
                                                      .parameterized = false}};

        const auto set = grooveSetFrom(entries);
        expect(set.size() == 2, "Both grooves are in the set");
        expect(set.contains("Swing"), "and the first is findable by name");
        expect(set.contains("Shuffle"), "and so is the second");

        // An empty table compiles to the identity, so a groove that kept its latenesses
        // displaces and one that lost them does not.
        const auto swing = set.compile("Swing", 1.0f);
        expect(swing.maxDisplacementBeats() > 0.0, "The lateness survived the conversion");

        const auto missing = set.compile("Nothing", 1.0f);
        expect(missing.maxDisplacementBeats() == 0.0, "An unknown name is the identity");
    }

    void testNothingSuppliedGroovesNothing() {
        beginTest("No entries is the identity");

        const auto set = grooveSetFrom({});
        expect(set.size() == 0, "Nothing supplied is an empty set");
        expect(set.compile("Swing", 1.0f).maxDisplacementBeats() == 0.0, "and grooves nothing");
    }
};

GrooveEntriesTests grooveEntriesTests;

}  // namespace
