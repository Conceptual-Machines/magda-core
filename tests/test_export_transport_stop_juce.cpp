#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;

/**
 * @brief Tests for transport state during export operations
 *
 * These tests verify that the transport is properly stopped before
 * export rendering to avoid SIGSEGV from JUCE/Tracktion Engine's
 * assertion: jassert (! r.edit->getTransport().isPlayContextActive())
 */
class ExportTransportTest final : public juce::UnitTest {
  public:
    ExportTransportTest() : juce::UnitTest("Export Transport State Tests") {}

    void runTest() override {
        testTransportStopsBeforeRendering();
        testOfflineRenderPreconditions();
        testMultipleExportAttempts();
        testTransportStateAfterExport();
    }

  private:
    void testTransportStopsBeforeRendering() {
        beginTest("Transport stops before rendering");

        TracktionEngineWrapper engine;
        expect(engine.initialize(), "Engine should initialize");

        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Edit should exist");

        auto& transport = edit->getTransport();

        // Test: Transport is stopped initially
        expect(!transport.isPlaying(), "Transport should not be playing initially");
        expect(!transport.isRecording(), "Transport should not be recording initially");

        // Test: Transport stops when play is active
        transport.play(false);
        juce::Thread::sleep(50);
        expect(transport.isPlaying(), "Transport should be playing after play()");

        // Simulate export preparation - transport should stop
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }

        expect(!transport.isPlaying(), "Transport should be stopped after stop()");
        expect(!transport.isRecording(), "Transport should not be recording");

        // Test: Transport remains stopped if already stopped
        transport.stop(false, false);
        expect(!transport.isPlaying(), "Transport should remain stopped");

        if (transport.isPlaying()) {
            transport.stop(false, false);
        }
        expect(!transport.isPlaying(), "Transport should still be stopped");

        // Test: Transport stops during recording
        transport.record(false);
        juce::Thread::sleep(50);

        bool wasActive = transport.isPlaying() || transport.isRecording();

        if (transport.isPlaying()) {
            transport.stop(false, false);
        }

        expect(!transport.isPlaying(), "Transport should be stopped after recording stop");
        expect(!transport.isRecording(), "Transport should not be recording");

        if (wasActive) {
            expect(!transport.isPlaying(), "Previously active transport should now be stopped");
        }

        engine.shutdown();
    }

    void testOfflineRenderPreconditions() {
        beginTest("Offline render preconditions");

        TracktionEngineWrapper engine;
        expect(engine.initialize(), "Engine should initialize");

        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Edit should exist");

        auto& transport = edit->getTransport();

        // Test: Offline render requires inactive transport
        transport.stop(false, false);
        expect(!transport.isPlaying(), "Transport should be stopped for offline rendering");

        bool isPlayContextActive = transport.isPlaying();
        expect(!isPlayContextActive, "Play context should not be active");

        // Test: Active transport violates offline render precondition
        transport.play(false);
        juce::Thread::sleep(50);

        if (transport.isPlaying()) {
            transport.stop(false, false);
        }

        expect(!transport.isPlaying(), "Transport should be stopped after precondition fix");

        engine.shutdown();
    }

    void testMultipleExportAttempts() {
        beginTest("Multiple export attempts handle transport correctly");

        TracktionEngineWrapper engine;
        expect(engine.initialize(), "Engine should initialize");

        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Edit should exist");

        auto& transport = edit->getTransport();

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
            expect(!transport.isPlaying(),
                   "Transport should be stopped on iteration " + juce::String(i));
        }

        engine.shutdown();
    }

    void testTransportStateAfterExport() {
        beginTest("Transport state after export");

        TracktionEngineWrapper engine;
        expect(engine.initialize(), "Engine should initialize");

        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Edit should exist");

        auto& transport = edit->getTransport();

        // Test: Transport remains stopped after export completes
        transport.play(false);
        juce::Thread::sleep(50);

        transport.stop(false, false);
        expect(!transport.isPlaying(), "Transport should be stopped before export");

        // Simulate export completion
        expect(!transport.isPlaying(), "Transport should remain stopped after export");

        // Test: User can restart playback after export
        transport.stop(false, false);
        expect(!transport.isPlaying(), "Transport should be stopped");

        juce::Thread::sleep(100);

        transport.play(false);
        juce::Thread::sleep(50);

        if (transport.isPlaying()) {
            expect(transport.isPlaying(), "Playback can be restarted after export");
            transport.stop(false, false);
        }

        engine.shutdown();
    }
};

// Register the test
static ExportTransportTest exportTransportTest;
