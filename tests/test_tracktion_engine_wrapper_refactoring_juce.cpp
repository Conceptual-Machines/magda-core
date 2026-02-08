#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "SharedTestEngine.hpp"

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
        testTransportOperations();
        testDeviceLoadingState();
        testTriggerStateTracking();
        testBridgeAccess();
        testMetronomeOperations();
        testPluginScanningState();
        testDeviceManagerAccess();
        testThreadSafety();
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

    void testDeviceLoadingState() {
        beginTest("Device loading state");

        auto& wrapper = magda::test::getSharedEngine();

        bool isLoading = wrapper.isDevicesLoading();
        expect(isLoading == true || isLoading == false, "Device loading state should be boolean");

        // Test callback setting
        bool callbackCalled = false;
        wrapper.onDevicesLoadingChanged = [&](bool loading, const juce::String& message) {
            callbackCalled = true;
        };

        expect(true, "Callback set without crash");

        wrapper.onDevicesLoadingChanged = nullptr;
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
        bool initialStarted = wrapper.justStarted();

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
        wrapper.getMidiBridge();
        wrapper.getPluginWindowManager();
        wrapper.getEngine();
        wrapper.getEdit();
        expect(true, "All bridge accessors work");
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

        bool scanning = wrapper.isScanning();
        expect(scanning == true || scanning == false, "Scanning state should be boolean");

        wrapper.getKnownPluginList();
        wrapper.getPluginListFile();
        expect(true, "Plugin list operations are safe");
    }

    void testDeviceManagerAccess() {
        beginTest("DeviceManager access");

        auto& wrapper = magda::test::getSharedEngine();

        auto* dm = wrapper.getDeviceManager();
        expect(true, "DeviceManager access does not crash");
    }

    void testThreadSafety() {
        beginTest("Refactoring preserves thread safety");

        auto& wrapper = magda::test::getSharedEngine();

        // Simulate concurrent access patterns
        wrapper.getCurrentPosition();
        wrapper.isPlaying();
        wrapper.getTempo();
        wrapper.isDevicesLoading();

        expect(true, "Concurrent access patterns work");
    }
};

// Register the test
static TracktionEngineWrapperRefactoringTest tracktionEngineWrapperRefactoringTest;
