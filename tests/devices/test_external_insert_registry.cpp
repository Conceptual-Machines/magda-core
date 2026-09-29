
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

TEST_CASE("ExternalInsert has a registry spec for native hardware routing",
          "[external-insert][registry]") {
    namespace audio = magda::daw::audio;

    const auto* byKind = audio::findInternalPluginSpecWithTag("external-insert");
    REQUIRE(byKind != nullptr);

    SECTION("identity keeps the persisted insert id") {
        REQUIRE(juce::String(byKind->pluginId) == "insert");
        REQUIRE(byKind->createMode == audio::InternalPluginCreateMode::SavedStateOrFresh);
    }

    SECTION("no processor and not an instrument kind (FX/Instrument split is per-device)") {
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
