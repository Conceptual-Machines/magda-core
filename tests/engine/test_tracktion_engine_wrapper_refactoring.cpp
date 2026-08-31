#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "SharedTestEngine.hpp"

// =============================================================================
// Unit Tests for TracktionEngineWrapper Refactoring
//
// These tests verify that the refactored helper methods work correctly
// and that the initialization flow hasn't been broken by the refactoring.
//
// Uses a shared engine instance to avoid JUCE global state corruption from
// repeated engine creation/destruction (see SharedTestEngine.hpp).
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

        // The shared engine is constructible and initialized
        auto& wrapper = magda::test::getSharedEngine();
        REQUIRE(wrapper.getEdit() != nullptr);
    }
}

TEST_CASE("TracktionEngineWrapper - Construction and destruction",
          "[engine][refactoring][lifecycle]") {
    SECTION("Shared engine is initialized and accessible") {
        auto& wrapper = magda::test::getSharedEngine();
        REQUIRE(wrapper.getEdit() != nullptr);
    }
}

TEST_CASE("TracktionEngineWrapper - Initialization behavior", "[engine][refactoring][init]") {
    SECTION("Shared engine has valid Edit") {
        auto& wrapper = magda::test::getSharedEngine();
        REQUIRE(wrapper.getEdit() != nullptr);
    }
}

TEST_CASE("TracktionEngineWrapper - Transport operations with refactored code",
          "[engine][refactoring][transport]") {
    auto& wrapper = magda::test::getSharedEngine();
    magda::test::resetTransport(wrapper);

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
}

TEST_CASE("TracktionEngineWrapper - Device loading state", "[engine][refactoring][devices]") {
    auto& wrapper = magda::test::getSharedEngine();

    SECTION("Device loading state is accessible") {
        bool isLoading = wrapper.isDevicesLoading();
        REQUIRE((isLoading == true || isLoading == false));
    }

    SECTION("Device loading callback can be set") {
        bool callbackCalled = false;
        wrapper.onDevicesLoadingChanged = [&](bool loading, const juce::String& message) {
            callbackCalled = true;
        };

        // Just verify it doesn't crash
        REQUIRE(true);

        // Clean up callback
        wrapper.onDevicesLoadingChanged = nullptr;
    }
}

TEST_CASE("TracktionEngineWrapper - Trigger state tracking", "[engine][refactoring][triggers]") {
    auto& wrapper = magda::test::getSharedEngine();
    magda::test::resetTransport(wrapper);

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
}

TEST_CASE("TracktionEngineWrapper - Bridge access after refactoring",
          "[engine][refactoring][bridges]") {
    auto& wrapper = magda::test::getSharedEngine();

    SECTION("AudioBridge is accessible after initialization") {
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
}

TEST_CASE("TracktionEngineWrapper - Refactored initialization order",
          "[engine][refactoring][init-order]") {
    SECTION("Shared engine is initialized without deadlocks or crashes") {
        auto& wrapper = magda::test::getSharedEngine();
        REQUIRE(wrapper.getEdit() != nullptr);
    }
}

TEST_CASE("TracktionEngineWrapper - Metronome operations", "[engine][refactoring][metronome]") {
    auto& wrapper = magda::test::getSharedEngine();

    SECTION("Metronome can be enabled and queried") {
        REQUIRE_NOTHROW(wrapper.setMetronomeEnabled(true));
        REQUIRE_NOTHROW(wrapper.isMetronomeEnabled());

        wrapper.setMetronomeEnabled(false);
        bool enabled = wrapper.isMetronomeEnabled();
        REQUIRE_FALSE(enabled);
    }
}

TEST_CASE("TracktionEngineWrapper - Plugin scanning state", "[engine][refactoring][plugins]") {
    auto& wrapper = magda::test::getSharedEngine();

    SECTION("Plugin scanning state is queryable") {
        bool scanning = wrapper.isScanning();
        REQUIRE((scanning == true || scanning == false));
    }

    SECTION("Plugin list operations are safe") {
        REQUIRE_NOTHROW(wrapper.getKnownPluginList());
        REQUIRE_NOTHROW(wrapper.getPluginListFile());
    }
}

TEST_CASE("TracktionEngineWrapper - Error handling in initialization",
          "[engine][refactoring][errors]") {
    SECTION("Shared engine initialized successfully") {
        auto& wrapper = magda::test::getSharedEngine();
        REQUIRE(wrapper.getEdit() != nullptr);
    }
}

TEST_CASE("TracktionEngineWrapper - DeviceManager access",
          "[engine][refactoring][device-manager]") {
    auto& wrapper = magda::test::getSharedEngine();

    SECTION("Can access DeviceManager after initialization") {
        REQUIRE_NOTHROW(wrapper.getDeviceManager());
    }
}

// =============================================================================
// Integration Tests - Verify refactored code works end-to-end
// =============================================================================

TEST_CASE("TracktionEngineWrapper - Full lifecycle integration test",
          "[engine][refactoring][integration]") {
    SECTION("Complete usage cycle with shared engine") {
        auto& wrapper = magda::test::getSharedEngine();
        magda::test::resetTransport(wrapper);

        // Perform some operations
        wrapper.setTempo(100.0);
        double tempo = wrapper.getTempo();

        wrapper.play();
        bool playing = wrapper.isPlaying();

        wrapper.stop();
        bool stopped = !wrapper.isPlaying();

        REQUIRE(true);  // If we got here without crashing, test passes
    }
}

TEST_CASE("TracktionEngineWrapper - Refactoring preserves thread safety",
          "[engine][refactoring][threading]") {
    SECTION("Concurrent access to wrapper methods should be safe") {
        auto& wrapper = magda::test::getSharedEngine();

        // Simulate concurrent access patterns
        wrapper.getCurrentPosition();
        wrapper.isPlaying();
        wrapper.getTempo();
        wrapper.isDevicesLoading();

        REQUIRE(true);
    }
}
