#include <tracktion_engine/tracktion_engine.h>

#include <catch2/catch_test_macros.hpp>

#include "audio/plugins/InsertConfigBridge.hpp"
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

TEST_CASE("Which way an insert's configuration needs moving", "[external-insert][insert]") {
    namespace audio = magda::daw::audio;
    using audio::InsertSyncDirection;

    magda::InsertConfig none;

    magda::InsertConfig configured;
    configured.sendType = magda::InsertConfig::Endpoint::Audio;
    configured.returnType = magda::InsertConfig::Endpoint::Audio;
    configured.sendDevice = "Out 3-4";
    configured.returnDevice = "In 3-4";

    SECTION("a project that carries it is the authority") {
        // The model is what the native engine compiles a send op and a return
        // op from, so what the fork plays has to be the same insert (#2245).
        magda::InsertConfig other;
        other.sendType = magda::InsertConfig::Endpoint::MIDI;
        other.sendDevice = "Somewhere Else";

        CHECK(audio::insertSyncDirectionFor(configured, other) == InsertSyncDirection::ToPlugin);
        CHECK(audio::insertSyncDirectionFor(configured, none) == InsertSyncDirection::ToPlugin);
    }

    SECTION("a project saved before the model carried it is filled from the plugin") {
        // The case that decides whether any of this is reachable. Every project
        // anybody already has keeps its routing only in the ValueTree the
        // plugin restores, and a model left inactive would make the compiler
        // emit an ordinary Device op for an insert.
        CHECK(audio::insertSyncDirectionFor(none, configured) == InsertSyncDirection::ToModel);
    }

    SECTION("a plugin that resolved to nothing is not read back") {
        // An insert whose ports this machine does not have has no device at
        // either end, and writing that into the model would erase what the
        // project says the next time it is saved.
        CHECK(audio::insertSyncDirectionFor(none, none) == InsertSyncDirection::Neither);
    }
}
