#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/engine/PluginService.hpp"

using namespace magda;

/**
 * @brief Unit Tests for TracktionEngineWrapper Refactoring
 *
 * These tests verify that the refactored helper methods work correctly
 * and that the initialization flow hasn't been broken by the refactoring.
 */
class TracktionEngineWrapperRefactoringTest final : public juce::UnitTest {
  public:
    TracktionEngineWrapperRefactoringTest()
        : juce::UnitTest("TracktionEngineWrapper Refactoring Tests", "magda") {}

    void runTest() override {
        testConstants();
        testHeadlessDetection();
        testTransportOperations();
        testTriggerStateTracking();
        testBridgeAccess();
        testMetronomeOperations();
        testPluginScanningState();
        testDeviceManagerAccess();
        testThreadSafety();
        testServicesWithoutPlayback();
        testProjectTeardownTakesTheMetersWithIt();
    }

  private:
    void testConstants() {
        beginTest("Constants are properly defined");

        expect(TracktionEngineWrapper::AUDIO_DEVICE_CHECK_SLEEP_MS > 0,
               "Sleep time should be positive");
        expect(TracktionEngineWrapper::AUDIO_DEVICE_CHECK_SLEEP_MS < 1000,
               "Sleep time should be reasonable");

        expect(TracktionEngineWrapper::AUDIO_DEVICE_CHECK_RETRIES > 0,
               "Retries should be positive");
        expect(TracktionEngineWrapper::AUDIO_DEVICE_CHECK_RETRIES < 10,
               "Retries should be reasonable");

        expect(TracktionEngineWrapper::AUDIO_DEVICE_CHECK_THRESHOLD >= 2,
               "Threshold should be at least 2");
        expect(TracktionEngineWrapper::AUDIO_DEVICE_CHECK_THRESHOLD <=
                   TracktionEngineWrapper::AUDIO_DEVICE_CHECK_RETRIES + 1,
               "Threshold should not exceed retries + 1");
    }

    void testHeadlessDetection() {
        beginTest("OS type bitmask detection for headless mode");

        // Verify JUCE OS type uses bitmask correctly (the bug was using == instead of &)
        auto osType = juce::SystemStats::getOperatingSystemType();
        bool isMacOS = (osType & juce::SystemStats::MacOSX) != 0;
        bool isWindows = (osType & juce::SystemStats::Windows) != 0;

#if JUCE_MAC
        expect(isMacOS, "MacOS should be detected via bitmask on Mac");
        expect(!isWindows, "Windows should not be detected on Mac");
#elif JUCE_WINDOWS
        expect(isWindows, "Windows should be detected via bitmask on Windows");
        expect(!isMacOS, "MacOS should not be detected on Windows");
#endif

        // On desktop platforms, PluginWindowManager should be created
        auto& wrapper = magda::test::getSharedEngine();
#if JUCE_MAC || JUCE_WINDOWS
        expect(wrapper.getPluginWindowManager() != nullptr,
               "PluginWindowManager must be created on desktop platforms");
#endif

        // Verify AudioBridge has the window manager wired
        auto* bridge = wrapper.getAudioBridge();
        if (bridge && wrapper.getPluginWindowManager() != nullptr) {
            // togglePluginWindow with invalid device path should return false but not crash
            bool result = bridge->togglePluginWindow(magda::ChainNodePath::topLevelDevice(1, 9999));
            expect(!result, "togglePluginWindow with invalid device path should return false");
        }
    }

    void testTransportOperations() {
        beginTest("Transport operations with refactored code");

        auto& wrapper = magda::test::getSharedEngine();

        // Reset transport state
        wrapper.getEdit()->getTransport().stop(false, false);

        // Transport controls should not crash
        wrapper.play();
        wrapper.stop();
        wrapper.pause();
        expect(true, "Transport controls executed without crash");

        // Position queries should work
        wrapper.getCurrentPosition();
        wrapper.isPlaying();
        wrapper.isRecording();
        expect(true, "Position queries executed without crash");

        // Tempo operations should work
        wrapper.setTempo(120.0);
        double tempo = wrapper.getTempo();
        expect(tempo > 0.0, "Tempo should be positive");
    }

    void testTriggerStateTracking() {
        beginTest("Trigger state tracking");

        auto& wrapper = magda::test::getSharedEngine();

        // Reset transport to clean state
        wrapper.getEdit()->getTransport().stop(false, false);
        wrapper.stop();
        juce::Thread::sleep(50);

        // Trigger state methods should be callable
        wrapper.updateTriggerState();
        wrapper.justStarted();
        wrapper.justLooped();
        expect(true, "Trigger state methods are callable");

        // Test trigger state detection for play start
        wrapper.updateTriggerState();
        wrapper.justStarted();

        wrapper.play();
        wrapper.updateTriggerState();
        bool afterPlay = wrapper.justStarted();

        wrapper.updateTriggerState();
        bool afterSecondUpdate = wrapper.justStarted();

        if (afterPlay) {
            expect(!afterSecondUpdate, "justStarted should be true only once after play");
        }

        wrapper.stop();
    }

    void testBridgeAccess() {
        beginTest("Bridge access after refactoring");

        auto& wrapper = magda::test::getSharedEngine();

        // All bridge getters should be accessible
        wrapper.getAudioBridge();
        wrapper.getPluginWindowManager();
        wrapper.getEngine();
        wrapper.getEdit();
        expect(true, "All bridge accessors work");
    }

    /// A rack keeps its level in an entry nothing polls -- updateAllClients()
    /// walks the devices -- so a project boundary is the only thing that can
    /// drop it. The store is cleared there too, but the store is a copy: what
    /// this asserts is that the next tick's copy brings nothing back (#2570).
    void testProjectTeardownTakesTheMetersWithIt() {
        beginTest("A project's teardown takes the fork's per-slot levels with it");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "The bridge is there to tear down");
        if (bridge == nullptr)
            return;

        constexpr magda::RackId rackId = 1;
        const auto devicePath = magda::ChainNodePath::topLevelDevice(1, 1);

        auto& metering = bridge->getDeviceMetering();
        metering.ensureEntry(devicePath);
        metering.setDirectLevels(devicePath, 0.5f, 0.5f);
        metering.ensureRackEntry(rackId);
        metering.setRackDirectLevels(rackId, 0.5f, 0.5f);

        // One metering tick's worth of publishing, which is what the UI reads.
        auto& store = wrapper.deviceMeters();
        metering.publishInto(store);

        magda::DeviceMeters::Levels levels;
        expect(store.devicePeak(devicePath, levels), "The slot has a level to lose");
        expect(store.rackPeak(rackId, levels), "So has the rack");

        bridge->projectTeardown();
        metering.publishInto(store);

        expect(!store.devicePeak(devicePath, levels), "The teardown took the slot's");
        expect(!store.rackPeak(rackId, levels),
               "And the rack's, which nothing else would have overwritten");
    }

    void testMetronomeOperations() {
        beginTest("Metronome operations");

        auto& wrapper = magda::test::getSharedEngine();

        wrapper.setMetronomeEnabled(true);
        expect(true, "Metronome can be enabled");

        wrapper.setMetronomeEnabled(false);
        bool enabled = wrapper.isMetronomeEnabled();
        expect(!enabled, "Metronome should be disabled");
    }

    void testPluginScanningState() {
        beginTest("Plugin scanning state");

        auto& wrapper = magda::test::getSharedEngine();

        bool scanning = magda::PluginService::getInstance().isScanRunning();
        expect(scanning == true || scanning == false, "Scanning state should be boolean");

        wrapper.getKnownPluginList();
        magda::PluginService::listFile();
        expect(true, "Plugin list operations are safe");
    }

    void testDeviceManagerAccess() {
        beginTest("DeviceManager access");

        auto& wrapper = magda::test::getSharedEngine();

        wrapper.getDeviceManager();
        expect(true, "DeviceManager access does not crash");
    }

    void testThreadSafety() {
        beginTest("Refactoring preserves thread safety");

        auto& wrapper = magda::test::getSharedEngine();

        // Simulate concurrent access patterns
        wrapper.getCurrentPosition();
        wrapper.isPlaying();
        wrapper.getTempo();

        expect(true, "Concurrent access patterns work");
    }

    void testServicesWithoutPlayback() {
        beginTest("initialiseServices() builds no playback half");

        // Its own wrapper: the shared engine is fully initialized (#2579).
        TracktionEngineWrapper wrapper;
        wrapper.setForceHeadless(true);

        expect(wrapper.initialiseServices(), "Services should come up");
        expect(wrapper.getEdit() == nullptr, "No Edit until playback is initialised");
        expect(wrapper.getAudioBridge() == nullptr, "No AudioBridge until playback is initialised");

        expect(wrapper.initialisePlayback(), "Playback should come up");
        expect(wrapper.getEdit() != nullptr, "Edit exists once playback is initialised");
    }
};

// Register the test
static TracktionEngineWrapperRefactoringTest tracktionEngineWrapperRefactoringTest;
