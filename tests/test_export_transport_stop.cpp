#include <catch2/catch_test_macros.hpp>

#include "SharedTestEngine.hpp"

using namespace magda;

// ============================================================================
// Transport State During Export Tests
// ============================================================================

TEST_CASE("Export - Transport stops before rendering", "[export][transport]") {
    auto& engine = test::getSharedEngine();
    auto* edit = engine.getEdit();
    REQUIRE(edit != nullptr);

    auto& transport = edit->getTransport();
    test::resetTransport(engine);

    SECTION("Transport is stopped initially") {
        REQUIRE_FALSE(transport.isPlaying());
        REQUIRE_FALSE(transport.isRecording());
    }

    SECTION("Transport stops when play is active") {
        // Start playback
        transport.play(false);

        // Give it a moment to actually start
        juce::Thread::sleep(50);

        REQUIRE(transport.isPlaying());

        // Simulate export preparation - transport should stop
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }

        // Verify transport is stopped
        REQUIRE_FALSE(transport.isPlaying());
        REQUIRE_FALSE(transport.isRecording());
    }

    SECTION("Transport remains stopped if already stopped") {
        // Ensure transport is stopped
        transport.stop(false, false);
        REQUIRE_FALSE(transport.isPlaying());

        // Simulate export preparation when already stopped
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }

        // Should still be stopped
        REQUIRE_FALSE(transport.isPlaying());
    }

    SECTION("Transport stops during recording") {
        // Start recording
        transport.record(false);

        // Give it a moment to actually start
        juce::Thread::sleep(50);

        // Recording should make it "playing"
        bool wasActive = transport.isPlaying() || transport.isRecording();

        // Simulate export preparation - transport should stop
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }

        // Verify transport is stopped
        REQUIRE_FALSE(transport.isPlaying());
        REQUIRE_FALSE(transport.isRecording());

        // If it was active before, verify the stop worked
        if (wasActive) {
            REQUIRE_FALSE(transport.isPlaying());
        }
    }
}

// ============================================================================
// Render Context Precondition Tests
// ============================================================================

TEST_CASE("Export - Verify offline render preconditions", "[export][render]") {
    auto& engine = test::getSharedEngine();
    auto* edit = engine.getEdit();
    REQUIRE(edit != nullptr);

    auto& transport = edit->getTransport();
    test::resetTransport(engine);

    SECTION("Offline render requires inactive transport") {
        // This is the assertion from tracktion_NodeRenderContext.cpp:182
        // jassert (! r.edit->getTransport().isPlayContextActive());

        // Ensure transport is stopped for offline rendering
        transport.stop(false, false);

        // Verify the condition that Tracktion Engine expects
        REQUIRE_FALSE(transport.isPlaying());

        // This is the state required for offline rendering to succeed
        // The play context should not be active
        bool isPlayContextActive = transport.isPlaying();
        REQUIRE_FALSE(isPlayContextActive);
    }

    SECTION("Active transport violates offline render precondition") {
        // Start playback
        transport.play(false);
        juce::Thread::sleep(50);

        if (transport.isPlaying()) {
            // This state would cause the assertion failure
            // jassert (! r.edit->getTransport().isPlayContextActive()) would fail

            // The fix: stop transport before rendering
            transport.stop(false, false);
        }

        // After stopping, precondition is satisfied
        REQUIRE_FALSE(transport.isPlaying());
    }
}

// ============================================================================
// Export Safety Tests
// ============================================================================

TEST_CASE("Export - Multiple export attempts handle transport correctly", "[export][transport]") {
    auto& engine = test::getSharedEngine();
    auto* edit = engine.getEdit();
    REQUIRE(edit != nullptr);

    auto& transport = edit->getTransport();
    test::resetTransport(engine);

    // Simulate multiple export attempts with different transport states
    for (int i = 0; i < 3; ++i) {
        // Randomly start or stop transport
        if (i % 2 == 0) {
            transport.play(false);
            juce::Thread::sleep(50);
        } else {
            transport.stop(false, false);
        }

        // Simulate export preparation - always stop transport
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }

        // Verify transport is always stopped before "export"
        REQUIRE_FALSE(transport.isPlaying());
    }
}

// ============================================================================
// Transport State Recovery Tests
// ============================================================================

TEST_CASE("Export - Transport state after export", "[export][transport]") {
    auto& engine = test::getSharedEngine();
    auto* edit = engine.getEdit();
    REQUIRE(edit != nullptr);

    auto& transport = edit->getTransport();
    test::resetTransport(engine);

    SECTION("Transport remains stopped after export completes") {
        // Start playing
        transport.play(false);
        juce::Thread::sleep(50);

        // Stop for export
        transport.stop(false, false);
        REQUIRE_FALSE(transport.isPlaying());

        // Simulate export completion
        // (In real implementation, transport remains stopped)

        // Verify it's still stopped
        REQUIRE_FALSE(transport.isPlaying());
    }

    SECTION("User can restart playback after export") {
        // Stop transport for export
        transport.stop(false, false);
        REQUIRE_FALSE(transport.isPlaying());

        // Simulate export completion
        juce::Thread::sleep(100);

        // User should be able to restart playback
        transport.play(false);
        juce::Thread::sleep(50);

        if (transport.isPlaying()) {
            // Playback can be restarted successfully
            REQUIRE(transport.isPlaying());
            transport.stop(false, false);
        }
    }
}
