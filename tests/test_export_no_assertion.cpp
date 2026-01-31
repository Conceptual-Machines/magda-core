#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;

// ============================================================================
// Full Export Integration Test - Verify No Assertions
// ============================================================================

TEST_CASE("Export - Full render without assertions", "[export][integration][no-assert]") {
    namespace te = tracktion;

    TracktionEngineWrapper engine;
    REQUIRE(engine.initialize());

    auto* edit = engine.getEdit();
    REQUIRE(edit != nullptr);

    auto& transport = edit->getTransport();

    SECTION("Export while transport is stopped - no assertion") {
        // Ensure transport is stopped
        transport.stop(false, false);
        te::freePlaybackContextIfNotRecording(transport);

        REQUIRE_FALSE(transport.isPlaying());
        REQUIRE_FALSE(transport.isPlayContextActive());

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

        // Critical assertion check - this must be false for render to work
        REQUIRE_FALSE(transport.isPlayContextActive());

        // Perform the render - this would trigger assertion if context is active
        // Using the simpler synchronous API instead of manually managing RenderTask
        auto renderedFile = te::Renderer::renderToFile("Test Export", params);

        // Verify file was created and is valid
        REQUIRE(renderedFile.existsAsFile());

        // Cleanup
        tempFile.deleteFile();
    }

    SECTION("Export while transport is playing - stops and exports without assertion") {
        // Start playback
        transport.play(false);
        juce::Thread::sleep(100);

        // Verify transport is playing
        bool wasPlaying = transport.isPlaying();

        // This is what MainWindow::performExport does before rendering
        if (transport.isPlaying()) {
            transport.stop(false, false);
        }
        te::freePlaybackContextIfNotRecording(transport);

        // Verify playback context is now freed
        REQUIRE_FALSE(transport.isPlayContextActive());

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
        params.blockSizeForAudio = 8192;  // Use larger block size like production
        params.time =
            te::TimeRange(te::TimePosition::fromSeconds(0.0), te::TimePosition::fromSeconds(0.5));
        params.realTimeRender = false;

        // Critical assertion check - this is what NodeRenderContext checks
        REQUIRE_FALSE(transport.isPlayContextActive());

        // Perform the render using the simpler synchronous API
        auto renderedFile = te::Renderer::renderToFile("Test Export", params);

        // Verify file was created
        REQUIRE(renderedFile.existsAsFile());

        // Cleanup
        renderedFile.deleteFile();

        // If transport was playing, verify the fix worked
        if (wasPlaying) {
            INFO("Transport was playing before export - fix successfully prevented assertion");
        }
    }

    SECTION("Multiple consecutive exports without assertions") {
        // Test that we can export multiple times without issues

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

            REQUIRE_FALSE(transport.isPlayContextActive());

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
            params.time = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                        te::TimePosition::fromSeconds(0.25));
            params.realTimeRender = false;

            std::atomic<float> progress{0.0f};
            auto renderTask = std::make_unique<te::Renderer::RenderTask>("Test Export", params,
                                                                         &progress, nullptr);

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

            REQUIRE(renderSucceeded);
            REQUIRE(tempFile.existsAsFile());

            tempFile.deleteFile();
        }
    }

    engine.shutdown();
}

// ============================================================================
// Assertion Prevention Test
// ============================================================================

TEST_CASE("Export - Verify assertion prevention mechanism", "[export][assertion]") {
    namespace te = tracktion;

    TracktionEngineWrapper engine;
    REQUIRE(engine.initialize());

    auto* edit = engine.getEdit();
    REQUIRE(edit != nullptr);

    auto& transport = edit->getTransport();

    SECTION("Assertion would fail without fix") {
        // Start playback - this allocates playback context
        transport.play(false);
        juce::Thread::sleep(100);

        if (transport.isPlaying()) {
            // At this point, isPlayContextActive() would return true
            // This is the state that causes the assertion failure

            // Just stopping is NOT enough:
            transport.stop(false, false);

            // After stop, context might still be active!
            // (depending on timing and engine state)

            // The FIX: explicitly free the context
            te::freePlaybackContextIfNotRecording(transport);
        }

        // Now the assertion precondition is satisfied
        REQUIRE_FALSE(transport.isPlayContextActive());
    }

    SECTION("freePlaybackContextIfNotRecording is safe to call multiple times") {
        // Stop transport
        transport.stop(false, false);

        // Free context multiple times - should be safe
        te::freePlaybackContextIfNotRecording(transport);
        REQUIRE_FALSE(transport.isPlayContextActive());

        te::freePlaybackContextIfNotRecording(transport);
        REQUIRE_FALSE(transport.isPlayContextActive());

        te::freePlaybackContextIfNotRecording(transport);
        REQUIRE_FALSE(transport.isPlayContextActive());
    }

    SECTION("Context remains freed after stop") {
        transport.play(false);
        juce::Thread::sleep(50);

        transport.stop(false, false);
        te::freePlaybackContextIfNotRecording(transport);

        REQUIRE_FALSE(transport.isPlayContextActive());

        // Wait a bit
        juce::Thread::sleep(100);

        // Should still be freed
        REQUIRE_FALSE(transport.isPlayContextActive());
    }

    engine.shutdown();
}
