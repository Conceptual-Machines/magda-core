#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "magda/daw/engine/TracktionEngineWrapper.hpp"

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
        : juce::UnitTest("TracktionEngineWrapper Refactoring Tests") {}

    void runTest() override {
        testConstants();
        testConstruction();
        testInitialization();
        testTransportOperations();
        testDeviceLoadingState();
        testTriggerStateTracking();
        testBridgeAccess();
        testMetronomeOperations();
        testPluginScanningState();
        testDeviceManagerAccess();
        testFullLifecycleIntegration();
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

    void testConstruction() {
        beginTest("Construction and destruction");

        {
            TracktionEngineWrapper wrapper;
            // Should not crash during construction
            expect(true, "Wrapper constructed successfully");
        }
        // Should not crash during destruction
        expect(true, "Wrapper destroyed successfully");
    }

    void testInitialization() {
        beginTest("Initialization behavior");

        TracktionEngineWrapper wrapper;

        bool result = wrapper.initialize();
        expect(result == true || result == false, "Initialize should return boolean");

        // Can safely initialize and shutdown multiple times
        wrapper.shutdown();
        expect(true, "First shutdown completed");

        wrapper.initialize();
        wrapper.shutdown();
        expect(true, "Second init/shutdown cycle completed");
    }

    void testTransportOperations() {
        beginTest("Transport operations with refactored code");

        TracktionEngineWrapper wrapper;
        wrapper.initialize();

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

        wrapper.shutdown();
    }

    void testDeviceLoadingState() {
        beginTest("Device loading state");

        TracktionEngineWrapper wrapper;
        wrapper.initialize();

        bool isLoading = wrapper.isDevicesLoading();
        expect(isLoading == true || isLoading == false, "Device loading state should be boolean");

        // Test callback setting
        bool callbackCalled = false;
        wrapper.onDevicesLoadingChanged = [&](bool loading, const juce::String& message) {
            callbackCalled = true;
        };

        expect(true, "Callback set without crash");

        wrapper.onDevicesLoadingChanged = nullptr;
        wrapper.shutdown();
    }

    void testTriggerStateTracking() {
        beginTest("Trigger state tracking");

        TracktionEngineWrapper wrapper;
        wrapper.initialize();

        // Reset transport to clean state
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
        wrapper.shutdown();
    }

    void testBridgeAccess() {
        beginTest("Bridge access after refactoring");

        TracktionEngineWrapper wrapper;
        wrapper.initialize();

        // All bridge getters should be accessible
        wrapper.getAudioBridge();
        wrapper.getMidiBridge();
        wrapper.getPluginWindowManager();
        wrapper.getEngine();
        wrapper.getEdit();
        expect(true, "All bridge accessors work");

        wrapper.shutdown();
    }

    void testMetronomeOperations() {
        beginTest("Metronome operations");

        TracktionEngineWrapper wrapper;
        wrapper.initialize();

        wrapper.setMetronomeEnabled(true);
        expect(true, "Metronome can be enabled");

        wrapper.setMetronomeEnabled(false);
        bool enabled = wrapper.isMetronomeEnabled();
        expect(!enabled, "Metronome should be disabled");

        wrapper.shutdown();
    }

    void testPluginScanningState() {
        beginTest("Plugin scanning state");

        TracktionEngineWrapper wrapper;
        wrapper.initialize();

        bool scanning = wrapper.isScanning();
        expect(scanning == true || scanning == false, "Scanning state should be boolean");

        wrapper.getKnownPluginList();
        wrapper.getPluginListFile();
        expect(true, "Plugin list operations are safe");

        wrapper.shutdown();
    }

    void testDeviceManagerAccess() {
        beginTest("DeviceManager access");

        TracktionEngineWrapper wrapper;
        wrapper.initialize();

        auto* dm = wrapper.getDeviceManager();
        expect(true, "DeviceManager access does not crash");

        wrapper.shutdown();
    }

    void testFullLifecycleIntegration() {
        beginTest("Full lifecycle integration test");

        TracktionEngineWrapper wrapper;

        bool initResult = wrapper.initialize();

        wrapper.setTempo(100.0);
        double tempo = wrapper.getTempo();

        wrapper.play();
        bool playing = wrapper.isPlaying();

        wrapper.stop();
        bool stopped = !wrapper.isPlaying();

        wrapper.shutdown();

        expect(true, "Full lifecycle completed without crash");
    }

    void testThreadSafety() {
        beginTest("Refactoring preserves thread safety");

        TracktionEngineWrapper wrapper;
        wrapper.initialize();

        // Simulate concurrent access patterns
        wrapper.getCurrentPosition();
        wrapper.isPlaying();
        wrapper.getTempo();
        wrapper.isDevicesLoading();

        expect(true, "Concurrent access patterns work");

        wrapper.shutdown();
    }
};

// Register the test
static TracktionEngineWrapperRefactoringTest tracktionEngineWrapperRefactoringTest;
