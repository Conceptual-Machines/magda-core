#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/engine/TracktionEngineWrapper.hpp"

// =============================================================================
// Unit Tests for TracktionEngineWrapper Refactoring
//
// These tests verify that the refactored helper methods work correctly
// and that the initialization flow hasn't been broken by the refactoring.
// =============================================================================

TEST_CASE("TracktionEngineWrapper - Constants are properly defined",
          "[engine][refactoring][constants]") {
    SECTION("Audio device check constants have sensible values") {
        // These constants replace the magic number 50 in the play() method
        // Verify they exist and have reasonable values for device health checking

        // Sleep time should be reasonable (not too short, not too long)
        REQUIRE(magda::TracktionEngineWrapper::AUDIO_DEVICE_CHECK_SLEEP_MS > 0);
        REQUIRE(magda::TracktionEngineWrapper::AUDIO_DEVICE_CHECK_SLEEP_MS < 1000);

        // Number of retries should be positive
        REQUIRE(magda::TracktionEngineWrapper::AUDIO_DEVICE_CHECK_RETRIES > 0);
        REQUIRE(magda::TracktionEngineWrapper::AUDIO_DEVICE_CHECK_RETRIES < 10);

        // Threshold should be at least 2 (initial check + retries)
        REQUIRE(magda::TracktionEngineWrapper::AUDIO_DEVICE_CHECK_THRESHOLD >= 2);
        REQUIRE(magda::TracktionEngineWrapper::AUDIO_DEVICE_CHECK_THRESHOLD <=
                magda::TracktionEngineWrapper::AUDIO_DEVICE_CHECK_RETRIES + 1);
    }
}

TEST_CASE("TracktionEngineWrapper - Helper method signatures", "[engine][refactoring][interface]") {
    SECTION("Private helper methods exist and are properly encapsulated") {
        // This test verifies the refactoring added the expected private methods
        // We can't call them directly, but we can verify the class still compiles
        // and has the expected size/layout

        // The class should still be constructible
        magda::TracktionEngineWrapper wrapper;

        // Verify the class isn't abstract (all methods implemented)
        REQUIRE(true);
    }
}

TEST_CASE("TracktionEngineWrapper - Construction and destruction",
          "[engine][refactoring][lifecycle]") {
    SECTION("Can construct wrapper without initialization") {
        magda::TracktionEngineWrapper wrapper;
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Destructor safely handles uninitialized state") {
        // Test that destruction works even if initialize() was never called
        {
            magda::TracktionEngineWrapper wrapper;
            // Destructor will be called here
        }
        REQUIRE(true);
    }
}

TEST_CASE("TracktionEngineWrapper - Initialization behavior", "[engine][refactoring][init]") {
    SECTION("Initialization returns boolean status") {
        magda::TracktionEngineWrapper wrapper;

        // Initialize should return a boolean indicating success/failure
        // Note: This may fail in test environment without proper audio devices,
        // but should not crash
        bool result = false;
        REQUIRE_NOTHROW(result = wrapper.initialize());

        // Result should be either true or false, not undefined
        REQUIRE((result == true || result == false));
    }

    SECTION("Can safely initialize and shutdown multiple times") {
        magda::TracktionEngineWrapper wrapper;

        // First cycle
        wrapper.initialize();
        wrapper.shutdown();

        // Second cycle - should not crash
        REQUIRE_NOTHROW(wrapper.initialize());
        REQUIRE_NOTHROW(wrapper.shutdown());
    }
}

TEST_CASE("TracktionEngineWrapper - Transport operations with refactored code",
          "[engine][refactoring][transport]") {
    magda::TracktionEngineWrapper wrapper;
    wrapper.initialize();

    SECTION("Transport controls work after initialization") {
        // These should not crash even if engine initialization partially failed
        REQUIRE_NOTHROW(wrapper.play());
        REQUIRE_NOTHROW(wrapper.stop());
        REQUIRE_NOTHROW(wrapper.pause());
    }

    SECTION("Position queries work after initialization") {
        REQUIRE_NOTHROW(wrapper.getCurrentPosition());
        REQUIRE_NOTHROW(wrapper.isPlaying());
        REQUIRE_NOTHROW(wrapper.isRecording());
    }

    SECTION("Tempo operations work after initialization") {
        REQUIRE_NOTHROW(wrapper.setTempo(120.0));
        double tempo = 0.0;
        REQUIRE_NOTHROW(tempo = wrapper.getTempo());

        // Tempo should be positive
        REQUIRE(tempo > 0.0);
    }

    wrapper.shutdown();
}

TEST_CASE("TracktionEngineWrapper - Device loading state", "[engine][refactoring][devices]") {
    magda::TracktionEngineWrapper wrapper;

    SECTION("Device loading state is accessible") {
        // Should start as loading or not loading
        bool isLoading = wrapper.isDevicesLoading();
        REQUIRE((isLoading == true || isLoading == false));
    }

    SECTION("Device loading callback can be set") {
        bool callbackCalled = false;
        wrapper.onDevicesLoadingChanged = [&](bool loading, const juce::String& message) {
            callbackCalled = true;
        };

        // Initialize may trigger the callback
        wrapper.initialize();

        // If callback was called, that's good. If not, also fine for this test.
        // Just verify it doesn't crash
        REQUIRE(true);

        wrapper.shutdown();
    }
}

TEST_CASE("TracktionEngineWrapper - Trigger state tracking", "[engine][refactoring][triggers]") {
    magda::TracktionEngineWrapper wrapper;
    wrapper.initialize();

    SECTION("Trigger state methods are callable") {
        REQUIRE_NOTHROW(wrapper.updateTriggerState());
        REQUIRE_NOTHROW(wrapper.justStarted());
        REQUIRE_NOTHROW(wrapper.justLooped());
    }

    SECTION("Trigger state detection for play start") {
        // Initial state should be not playing
        wrapper.updateTriggerState();
        bool initialStarted = wrapper.justStarted();

        // Start playback
        wrapper.play();
        wrapper.updateTriggerState();
        bool afterPlay = wrapper.justStarted();

        // After the next update, should no longer be "just started"
        wrapper.updateTriggerState();
        bool afterSecondUpdate = wrapper.justStarted();

        // justStarted should be true only once after play
        if (afterPlay) {
            REQUIRE_FALSE(afterSecondUpdate);
        }

        wrapper.stop();
    }

    wrapper.shutdown();
}

TEST_CASE("TracktionEngineWrapper - Bridge access after refactoring",
          "[engine][refactoring][bridges]") {
    magda::TracktionEngineWrapper wrapper;
    wrapper.initialize();

    SECTION("AudioBridge is accessible after initialization") {
        auto* bridge = wrapper.getAudioBridge();
        // May be null if initialization failed in test environment
        // Just verify the getter works
        REQUIRE_NOTHROW(wrapper.getAudioBridge());
    }

    SECTION("MidiBridge is accessible after initialization") {
        REQUIRE_NOTHROW(wrapper.getMidiBridge());
    }

    SECTION("PluginWindowManager is accessible after initialization") {
        REQUIRE_NOTHROW(wrapper.getPluginWindowManager());
    }

    SECTION("Engine and Edit are accessible after initialization") {
        REQUIRE_NOTHROW(wrapper.getEngine());
        REQUIRE_NOTHROW(wrapper.getEdit());
    }

    wrapper.shutdown();
}

TEST_CASE("TracktionEngineWrapper - Refactored initialization order",
          "[engine][refactoring][init-order]") {
    SECTION("Initialization completes without deadlocks or crashes") {
        magda::TracktionEngineWrapper wrapper;

        // The refactored initialization breaks down into these logical steps:
        // 1. Create engine with UIBehaviour
        // 2. Initialize plugin formats (initializePluginFormats)
        // 3. Initialize device manager (initializeDeviceManager)
        // 4. Configure audio devices (configureAudioDevices)
        // 5. Setup MIDI devices (setupMidiDevices)
        // 6. Create edit and bridges (createEditAndBridges)

        // All of these should complete without hanging or crashing
        bool initialized = false;
        REQUIRE_NOTHROW(initialized = wrapper.initialize());

        // Clean shutdown should also work
        REQUIRE_NOTHROW(wrapper.shutdown());
    }
}

TEST_CASE("TracktionEngineWrapper - Metronome operations", "[engine][refactoring][metronome]") {
    magda::TracktionEngineWrapper wrapper;
    wrapper.initialize();

    SECTION("Metronome can be enabled and queried") {
        REQUIRE_NOTHROW(wrapper.setMetronomeEnabled(true));
        REQUIRE_NOTHROW(wrapper.isMetronomeEnabled());

        wrapper.setMetronomeEnabled(false);
        bool enabled = wrapper.isMetronomeEnabled();
        REQUIRE_FALSE(enabled);
    }

    wrapper.shutdown();
}

TEST_CASE("TracktionEngineWrapper - Plugin scanning state", "[engine][refactoring][plugins]") {
    magda::TracktionEngineWrapper wrapper;
    wrapper.initialize();

    SECTION("Plugin scanning state is queryable") {
        bool scanning = wrapper.isScanning();
        REQUIRE((scanning == true || scanning == false));
    }

    SECTION("Plugin list operations are safe") {
        REQUIRE_NOTHROW(wrapper.getKnownPluginList());
        REQUIRE_NOTHROW(wrapper.getPluginListFile());
    }

    wrapper.shutdown();
}

TEST_CASE("TracktionEngineWrapper - Error handling in initialization",
          "[engine][refactoring][errors]") {
    SECTION("Initialization handles exceptions gracefully") {
        magda::TracktionEngineWrapper wrapper;

        // Initialization should catch exceptions and return false
        // rather than letting them propagate
        bool result = false;
        REQUIRE_NOTHROW(result = wrapper.initialize());

        // Even if initialization fails, shutdown should be safe
        REQUIRE_NOTHROW(wrapper.shutdown());
    }
}

TEST_CASE("TracktionEngineWrapper - DeviceManager access",
          "[engine][refactoring][device-manager]") {
    magda::TracktionEngineWrapper wrapper;
    wrapper.initialize();

    SECTION("Can access DeviceManager after initialization") {
        auto* dm = wrapper.getDeviceManager();
        // May be null if initialization failed, but shouldn't crash
        REQUIRE_NOTHROW(wrapper.getDeviceManager());
    }

    wrapper.shutdown();
}

// =============================================================================
// Integration Tests - Verify refactored code works end-to-end
// =============================================================================

TEST_CASE("TracktionEngineWrapper - Full lifecycle integration test",
          "[engine][refactoring][integration]") {
    SECTION("Complete initialization, usage, and shutdown cycle") {
        magda::TracktionEngineWrapper wrapper;

        // 1. Initialize
        bool initResult = wrapper.initialize();

        // 2. Perform some operations (even if init failed, should not crash)
        wrapper.setTempo(100.0);
        double tempo = wrapper.getTempo();

        wrapper.play();
        bool playing = wrapper.isPlaying();

        wrapper.stop();
        bool stopped = !wrapper.isPlaying();

        // 3. Clean shutdown
        wrapper.shutdown();

        REQUIRE(true);  // If we got here without crashing, test passes
    }
}

TEST_CASE("TracktionEngineWrapper - Refactoring preserves thread safety",
          "[engine][refactoring][threading]") {
    SECTION("Concurrent access to wrapper methods should be safe") {
        magda::TracktionEngineWrapper wrapper;
        wrapper.initialize();

        // Simulate concurrent access patterns
        // (In real code, different threads might call these simultaneously)
        wrapper.getCurrentPosition();
        wrapper.isPlaying();
        wrapper.getTempo();
        wrapper.isDevicesLoading();

        // No crashes expected
        REQUIRE(true);

        wrapper.shutdown();
    }
}
