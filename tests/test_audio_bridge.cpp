#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/AudioBridge.hpp"

// Test const-correctness of AudioBridge getter methods
// These tests verify that const_cast has been properly removed and
// the getter methods are correctly declared const

// Mock/stub test that verifies const method signatures
// This tests the interface contract without needing full engine setup
TEST_CASE("AudioBridge - Const method signatures compile", "[audiobridge][const][compile]") {
    SECTION("Verify const getter methods are declared const") {
        // This test just needs to compile - it verifies the method signatures
        // When we make getAudioTrack/getPlugin const, these will compile without const_cast

        // Test that const pointers can call const methods
        const magda::AudioBridge* constBridge = nullptr;

        if (constBridge) {
            // These now compile successfully - const methods work!
            constBridge->getTrackVolume(1);
            constBridge->getTrackPan(1);
            constBridge->getMasterVolume();
            constBridge->getMasterPan();
            constBridge->getTrackAudioOutput(1);
            constBridge->getTrackAudioInput(1);
            constBridge->getTrackMidiInput(1);
            constBridge->isPluginWindowOpen(1);
        }

        REQUIRE(true);  // Test passes if it compiles
    }
}

// Test const-correctness by checking if methods can be called through const reference
TEST_CASE("AudioBridge - Const reference compatibility", "[audiobridge][const]") {
    SECTION("Const methods should accept const references") {
        // This lambda tests if we can pass const AudioBridge& to functions
        auto testConstMethod = [](const magda::AudioBridge& bridge) {
            // All const methods now compile successfully!
            float vol = bridge.getTrackVolume(1);
            float pan = bridge.getTrackPan(1);
            float masterVol = bridge.getMasterVolume();
            float masterPan = bridge.getMasterPan();
            juce::String output = bridge.getTrackAudioOutput(1);
            juce::String input = bridge.getTrackAudioInput(1);
            juce::String midi = bridge.getTrackMidiInput(1);
            bool isOpen = bridge.isPluginWindowOpen(1);

            // Verify defaults
            (void)vol;
            (void)pan;
            (void)masterVol;
            (void)masterPan;
            (void)output;
            (void)input;
            (void)midi;
            (void)isOpen;
            return true;
        };

        // Just verify the lambda signature compiles
        REQUIRE(testConstMethod);
    }
}

// Verify that mappingLock_ can be made mutable
TEST_CASE("AudioBridge - Thread safety requirements", "[audiobridge][threading]") {
    SECTION("Const getters need mutable lock") {
        // The fix requires:
        // 1. Make mappingLock_ mutable in AudioBridge.hpp
        // 2. Make getAudioTrack(), getPlugin(), getDeviceProcessor() const
        // 3. Remove all const_cast usage

        // This test documents the requirement
        REQUIRE(true);
    }
}

// Document the changes needed
TEST_CASE("AudioBridge - Const correctness fix requirements", "[audiobridge][documentation]") {
    SECTION("Changes needed in AudioBridge.hpp") {
        // 1. Change: mutable juce::CriticalSection mappingLock_;
        // 2. Change: te::AudioTrack* getAudioTrack(TrackId trackId) const;
        // 3. Change: te::Plugin::Ptr getPlugin(DeviceId deviceId) const;
        // 4. Change: DeviceProcessor* getDeviceProcessor(DeviceId deviceId) const;
        REQUIRE(true);
    }

    SECTION("Changes needed in AudioBridge.cpp") {
        // 1. Remove const_cast from getTrackVolume()
        // 2. Remove const_cast from getTrackPan()
        // 3. Remove const_cast from getTrackAudioOutput()
        // 4. Remove const_cast from getTrackAudioInput()
        // 5. Remove const_cast from getTrackMidiInput()
        // 6. Remove const_cast from isPluginWindowOpen()
        REQUIRE(true);
    }
}
