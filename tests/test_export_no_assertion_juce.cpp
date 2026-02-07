#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;

/**
 * @brief Full Export Integration Test - Verify No Assertions
 *
 * These tests perform actual audio rendering to verify that no assertions
 * are triggered during export, especially the critical assertion in
 * tracktion_NodeRenderContext.cpp that checks isPlayContextActive().
 *
 * Note: This test is disabled in CI due to issue #611 (generator device export)
 * but should work for basic export scenarios.
 */
class ExportNoAssertionTest final : public juce::UnitTest {
  public:
    ExportNoAssertionTest() : juce::UnitTest("Export No Assertion Tests") {}

    void runTest() override {
        testExportWhileStopped();
        testExportWhilePlaying();
        testMultipleConsecutiveExports();
        testAssertionPreventionMechanism();
    }

  private:
    void testExportWhileStopped() {
        beginTest("Export while transport is stopped - no assertion");

        namespace te = tracktion;

        TracktionEngineWrapper engine;
        expect(engine.initialize(), "Engine should initialize");

        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Edit should exist");

        auto& transport = edit->getTransport();

        // Ensure transport is stopped
        transport.stop(false, false);
        te::freePlaybackContextIfNotRecording(transport);

        expect(!transport.isPlaying(), "Transport should not be playing");
        expect(!transport.isPlayContextActive(), "Play context should not be active");

        // Create a temporary file for export
        auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("test_export_stopped.wav");
        tempFile.deleteFile();

        // Setup render parameters
        te::Renderer::Parameters params(*edit);
        params.destFile = tempFile;
        params.audioFormat = engine.getEngine()->getAudioFileFormatManager().getWavFormat();
        params.bitDepth = 16;
        params.sampleRateForAudio = 44100.0;
        params.blockSizeForAudio = 512;
        params.time =
            te::TimeRange(te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(1.0));
        params.realTimeRender = false;

        // Critical assertion check
        expect(!transport.isPlayContextActive(),
               "Play context must be inactive before render");

        // Perform the render
        auto renderedFile = te::Renderer::renderToFile("Test Export", params);

        // Verify file was created and is valid
        expect(renderedFile.existsAsFile(), "Rendered file should exist");

        // Cleanup
        tempFile.deleteFile();

        engine.shutdown();
    }

    void testExportWhilePlaying() {
        beginTest("Export while transport is playing - stops and exports without assertion");

        namespace te = tracktion;

        TracktionEngineWrapper engine;
        expect(engine.initialize(), "Engine should initialize");

        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Edit should exist");

        auto& transport = edit->getTransport();

        // Start playback
        transport.play(false);
        juce::Thread::sleep(100);

        bool wasPlaying = transport.isPlaying();

        // This is what MainWindow::performExport does before rendering
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }
        te::freePlaybackContextIfNotRecording(transport);

        // Verify playback context is now freed
        expect(!transport.isPlayContextActive(),
               "Play context should be freed after stop");

        // Create a temporary file for export
        auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("test_export_playing.wav");
        tempFile.deleteFile();

        // Setup render parameters
        te::Renderer::Parameters params(*edit);
        params.destFile = tempFile;
        params.audioFormat = engine.getEngine()->getAudioFileFormatManager().getWavFormat();
        params.bitDepth = 24;
        params.sampleRateForAudio = 48000.0;
        params.blockSizeForAudio = 8192;
        params.time =
            te::TimeRange(te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(0.5));
        params.realTimeRender = false;

        // Critical assertion check
        expect(!transport.isPlayContextActive(),
               "Play context must be inactive before render");

        // Perform the render
        auto renderedFile = te::Renderer::renderToFile("Test Export", params);

        // Verify file was created
        expect(renderedFile.existsAsFile(), "Rendered file should exist");

        // Cleanup
        renderedFile.deleteFile();

        if (wasPlaying) {
            logMessage("Transport was playing before export - fix successfully prevented assertion");
        }

        engine.shutdown();
    }

    void testMultipleConsecutiveExports() {
        beginTest("Multiple consecutive exports without assertions");

        namespace te = tracktion;

        TracktionEngineWrapper engine;
        expect(engine.initialize(), "Engine should initialize");

        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Edit should exist");

        auto& transport = edit->getTransport();

        for (int i = 0; i < 3; ++i) {
            // Alternate between playing and stopped states
            if (i % 2 == 0) {
                transport.play(false);
                juce::Thread::sleep(50);
            }

            // Stop and free context (what performExport does)
            if (transport.isPlaying()) {
                transport.stop(false, false);
            }
            te::freePlaybackContextIfNotRecording(transport);

            expect(!transport.isPlayContextActive(),
                   "Play context should be inactive on iteration " + juce::String(i));

            // Create unique temp file
            auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("test_export_multi_" + juce::String(i) + ".wav");
            tempFile.deleteFile();

            // Setup and run render
            te::Renderer::Parameters params(*edit);
            params.destFile = tempFile;
            params.audioFormat = engine.getEngine()->getAudioFileFormatManager().getWavFormat();
            params.bitDepth = 16;
            params.sampleRateForAudio = 44100.0;
            params.blockSizeForAudio = 8192;
            params.time =
                te::TimeRange(te::TimePosition::fromSeconds(0.0),
                              te::TimePosition::fromSeconds(0.25));
            params.realTimeRender = false;

            std::atomic<float> progress{0.0f};
            auto renderTask =
                std::make_unique<te::Renderer::RenderTask>("Test Export", params, &progress,
                                                           nullptr);

            bool renderSucceeded = false;
            int maxIterations = 1000;
            int iteration = 0;

            while (iteration++ < maxIterations) {
                auto status = renderTask->runJob();

                if (status == juce::ThreadPoolJob::jobHasFinished) {
                    renderSucceeded = true;
                    break;
                }

                if (status == juce::ThreadPoolJob::jobNeedsRunningAgain) {
                    continue;
                }

                break;
            }

            expect(renderSucceeded, "Render should succeed on iteration " + juce::String(i));
            expect(tempFile.existsAsFile(), "Temp file should exist on iteration " + juce::String(i));

            tempFile.deleteFile();
        }

        engine.shutdown();
    }

    void testAssertionPreventionMechanism() {
        beginTest("Assertion prevention mechanism");

        namespace te = tracktion;

        TracktionEngineWrapper engine;
        expect(engine.initialize(), "Engine should initialize");

        auto* edit = engine.getEdit();
        expect(edit != nullptr, "Edit should exist");

        auto& transport = edit->getTransport();

        // Test: Assertion would fail without fix
        transport.play(false);
        juce::Thread::sleep(100);

        if (transport.isPlaying()) {
            // Just stopping is NOT enough
            transport.stop(false, false);

            // The FIX: explicitly free the context
            te::freePlaybackContextIfNotRecording(transport);
        }

        expect(!transport.isPlayContextActive(),
               "Play context should be freed after fix");

        // Test: freePlaybackContextIfNotRecording is safe to call multiple times
        transport.stop(false, false);

        te::freePlaybackContextIfNotRecording(transport);
        expect(!transport.isPlayContextActive(), "Context should be freed (call 1)");

        te::freePlaybackContextIfNotRecording(transport);
        expect(!transport.isPlayContextActive(), "Context should be freed (call 2)");

        te::freePlaybackContextIfNotRecording(transport);
        expect(!transport.isPlayContextActive(), "Context should be freed (call 3)");

        // Test: Context remains freed after stop
        transport.play(false);
        juce::Thread::sleep(50);

        transport.stop(false, false);
        te::freePlaybackContextIfNotRecording(transport);

        expect(!transport.isPlayContextActive(), "Context should be freed initially");

        juce::Thread::sleep(100);

        expect(!transport.isPlayContextActive(), "Context should remain freed after delay");

        engine.shutdown();
    }
};

// Register the test
static ExportNoAssertionTest exportNoAssertionTest;
