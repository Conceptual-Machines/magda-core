#include <catch2/catch_test_macros.hpp>

#include "audio/plugins/InternalPluginRegistry.hpp"

// Phase 1 of the External FX / External Instrument feature: the hardware insert
// is registered as a single internal device kind (ExternalInsert) backed by
// te::InsertPlugin (xmlTypeName "insert"). These checks lock the model/registry
// wiring; instantiation + the send/return picker UI come in later phases.

using namespace magda;

TEST_CASE("ExternalInsert resolves from its string id", "[external-insert][registry]") {
    namespace audio = magda::daw::audio;
    REQUIRE(audio::internalPluginHasTag("insert", "external-insert"));
    REQUIRE(audio::internalPluginHasTag("Insert", "external-insert"));

    const auto* spec = audio::findInternalPluginSpec("insert");
    REQUIRE(spec != nullptr);
    REQUIRE(juce::String(spec->displayName) == "External Insert");
}

TEST_CASE("ExternalInsert has a registry spec wired to te::InsertPlugin",
          "[external-insert][registry]") {
    namespace audio = magda::daw::audio;
    namespace te = tracktion::engine;

    const auto* byKind = audio::findInternalPluginSpecWithTag("external-insert");
    REQUIRE(byKind != nullptr);

    SECTION("identity points at the TE insert plugin") {
        REQUIRE(juce::String(byKind->pluginId) == te::InsertPlugin::xmlTypeName);
        REQUIRE(byKind->createMode == audio::InternalPluginCreateMode::SavedStateOrFresh);
        REQUIRE(byKind->matchesPlugin != nullptr);
    }

    SECTION("no processor and not an instrument kind (FX/Instrument split is per-device)") {
        REQUIRE(byKind->createProcessor == nullptr);
        REQUIRE(byKind->isInstrument == false);
    }

    SECTION("addable on a track, not in a rack, hidden until the picker UI lands") {
        REQUIRE(byKind->canCreateOnTrack == true);
        REQUIRE(byKind->canCreateDetached == false);
        REQUIRE(byKind->showInBrowser == false);
    }

    SECTION("pluginId lookup resolves to the same spec") {
        const auto* byId = audio::findInternalPluginSpec(juce::String("insert"));
        REQUIRE(byId == byKind);
    }
}
