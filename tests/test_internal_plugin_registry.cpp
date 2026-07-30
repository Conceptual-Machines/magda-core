#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/core/DeviceCatalogMetadata.hpp"

namespace audio = magda::daw::audio;

namespace {

void registerTestDevicePack(audio::InternalPluginRegistry& registry) {
    audio::InternalPluginSpec spec;
    spec.pluginId = "test-external-pack-device";
    spec.displayName = "Test External Pack Device";
    registry.registerPlugin(spec);
}

const bool testDevicePackRegistered = audio::registerDevicePack(registerTestDevicePack);

}  // namespace

TEST_CASE("Linked device packs populate the process registry", "[internal-plugin-registry]") {
    REQUIRE(testDevicePackRegistered);
    const auto* spec = audio::findInternalPluginSpec("test-external-pack-device");
    REQUIRE(spec != nullptr);
    REQUIRE(juce::String(spec->displayName) == "Test External Pack Device");
}

TEST_CASE("Device catalog metadata resolves compiled and registered devices",
          "[internal-plugin-registry][metadata]") {
    const auto grit = magda::daw::findDeviceCatalogMetadata("magda_grit");
    REQUIRE(grit);
    CHECK(juce::String(grit.displayName) == "Grit");
    CHECK(juce::String(grit.browserCategory) == "Distortion");
    CHECK(juce::String(grit.description).contains("no effect in Sine mode"));

    const auto registered = magda::daw::findDeviceCatalogMetadata("test-external-pack-device");
    REQUIRE(registered);
    CHECK(juce::String(registered.displayName) == "Test External Pack Device");

    CHECK_FALSE(magda::daw::findDeviceCatalogMetadata("unknown-device"));
}

#if defined(MAGDA_PRO_DEVICES_ENABLED) && MAGDA_PRO_DEVICES_ENABLED
TEST_CASE("Optional pro pack registers its browser device through the SDK hook",
          "[internal-plugin-registry][pro-devices]") {
    const auto* spec = audio::findInternalPluginSpec("magda-pro-stub");
    REQUIRE(spec != nullptr);
    REQUIRE(juce::String(spec->displayName) == "Pro Pack Stub");
    REQUIRE(spec->showInBrowser);
    REQUIRE(spec->createDevice != nullptr);
}
#endif

TEST_CASE("Device packs can register plugins without changing the core catalog",
          "[internal-plugin-registry]") {
    audio::InternalPluginRegistry registry;
    static constexpr const char* aliases[] = {"third-party-legacy", "THIRD_PARTY_OLD"};
    static constexpr const char* tags[] = {"instrument", "third-party"};

    audio::InternalPluginSpec spec;
    spec.pluginId = "third-party-device";
    spec.displayName = "Third Party Device";
    spec.loadAliases = aliases;
    spec.loadAliasCount = 2;
    spec.tags = tags;
    spec.tagCount = 2;

    REQUIRE(registry.registerPlugin(spec));

    const auto* canonical = registry.find("THIRD-PARTY-DEVICE");
    REQUIRE(canonical != nullptr);
    REQUIRE(registry.findForLoadType("Third-Party-Legacy") == canonical);
    REQUIRE(registry.findForLoadType("third_party_old") == canonical);
    REQUIRE(audio::internalPluginHasTag(*canonical, "THIRD-PARTY"));
}

TEST_CASE("Device registration rejects ambiguous ids and aliases", "[internal-plugin-registry]") {
    audio::InternalPluginRegistry registry;
    static constexpr const char* firstAliases[] = {"legacy-device"};

    audio::InternalPluginSpec first;
    first.pluginId = "device";
    first.loadAliases = firstAliases;
    first.loadAliasCount = 1;
    REQUIRE(registry.registerPlugin(first));

    audio::InternalPluginSpec duplicateId;
    duplicateId.pluginId = "DEVICE";
    REQUIRE_FALSE(registry.registerPlugin(duplicateId));

    audio::InternalPluginSpec aliasCollidesWithId;
    static constexpr const char* collidingAliases[] = {"device"};
    aliasCollidesWithId.pluginId = "other-device";
    aliasCollidesWithId.loadAliases = collidingAliases;
    aliasCollidesWithId.loadAliasCount = 1;
    REQUIRE_FALSE(registry.registerPlugin(aliasCollidesWithId));

    audio::InternalPluginSpec idCollidesWithAlias;
    idCollidesWithAlias.pluginId = "LEGACY-DEVICE";
    REQUIRE_FALSE(registry.registerPlugin(idCollidesWithAlias));
}

TEST_CASE("Registered plugin pointers remain stable as packs append devices",
          "[internal-plugin-registry]") {
    audio::InternalPluginRegistry registry;

    audio::InternalPluginSpec first;
    first.pluginId = "first-device";
    REQUIRE(registry.registerPlugin(first));
    const auto* firstPointer = registry.find("first-device");

    static constexpr const char* ids[] = {
        "device-0", "device-1", "device-2", "device-3", "device-4",
        "device-5", "device-6", "device-7", "device-8", "device-9",
    };
    for (const auto* id : ids) {
        audio::InternalPluginSpec next;
        next.pluginId = id;
        REQUIRE(registry.registerPlugin(next));
    }

    REQUIRE(registry.find("first-device") == firstPointer);
    REQUIRE(registry.getAll().front() == firstPointer);
}

TEST_CASE("Device packs can contribute parameter aliases", "[internal-plugin-registry]") {
    audio::InternalPluginRegistry registry;

    audio::InternalParameterAliasSpec alias;
    alias.pluginKey = "third-party-device";
    alias.alias = "tone";
    alias.paramIndex = 2;
    alias.paramName = "Colour";

    REQUIRE(registry.registerParameterAlias(alias));
    REQUIRE_FALSE(registry.registerParameterAlias(alias));
    REQUIRE(registry.getParameterAliases().size() == 1);
    REQUIRE(registry.getParameterAliases().front().paramIndex == 2);
}
