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

TEST_CASE("A reattached engine does not inherit the previous one's queued discovery",
          "[plugin-service][2756]") {
    // Joining the discovery thread only guarantees its callAsync was queued. A shutdown and
    // re-initialise inside one message-queue drain leaves that callback holding the old
    // engine's results and callbacks, with the new engine's pointers non-null again.
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList list;
    auto& service = magda::PluginService::getInstance();

    service.useEngineList(formats, list);
    const auto queuedUnder = service.testAttachment();
    REQUIRE(service.testWouldAcceptWorkFrom(queuedUnder));

    service.forgetEngineList();
    CHECK_FALSE(service.testWouldAcceptWorkFrom(queuedUnder));

    juce::AudioPluginFormatManager nextFormats;
    juce::KnownPluginList nextList;
    service.useEngineList(nextFormats, nextList);
    CHECK_FALSE(service.testWouldAcceptWorkFrom(queuedUnder));

    service.forgetEngineList();
}
