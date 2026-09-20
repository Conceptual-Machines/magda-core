#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

#include "engine/PluginService.hpp"

TEST_CASE("Releasing the engine's list ends a running scan", "[plugin-service][2756]") {
    // The service outlives the engine, so a scan left running here would finish against a
    // KnownPluginList the engine has already destroyed.
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList list;
    auto& service = magda::PluginService::getInstance();

    service.useEngineList(formats, list);
    service.testBeginScan();
    REQUIRE(service.isScanRunning());

    service.forgetEngineList();

    CHECK_FALSE(service.isScanRunning());
    CHECK(service.knownList() == nullptr);
    CHECK(service.formats() == nullptr);
}

TEST_CASE("A released service answers with no plugins rather than crashing",
          "[plugin-service][2756]") {
    auto& service = magda::PluginService::getInstance();
    service.forgetEngineList();

    CHECK(service.knownTypes().isEmpty());
    CHECK(service.preferredTypes().isEmpty());
    CHECK(service.scanParameters("VST3-Surge-XT-1a2b3c4d", false).empty());

    // Listener registration is where a released list would be dereferenced.
    struct Listener : juce::ChangeListener {
        void changeListenerCallback(juce::ChangeBroadcaster*) override {}
    } listener;
    CHECK_NOTHROW(service.addListChangeListener(&listener));
    CHECK_NOTHROW(service.removeListChangeListener(&listener));
}
