#include <juce_core/juce_core.h>

#include <cmath>

#include "AssertionWatch.hpp"
#include "JuceTestStateGuard.hpp"
#include "magda/daw/api/magda_api.hpp"
#include "magda/daw/api/project_api.hpp"
#include "magda/daw/core/TempoMap.hpp"
#include "magda/daw/engine/MagdaAudioEngine.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#include "magda/daw/project/ProjectInfo.hpp"

/**
 * What MagdaAudioEngine answers with no Edit behind it (#2579).
 *
 * The fork under this engine is services only. Both halves of that are
 * asserted: the questions the Edit used to answer, and the Edit's absence,
 * which is what makes answering them here necessary.
 */

namespace {

/// Tighter than any rounding these conversions do, looser than exact
/// double equality on a value that travelled through a tempo map.
constexpr double kTolerance = 1.0e-9;

class MagdaAudioEngineTest final : public juce::UnitTest {
  public:
    MagdaAudioEngineTest() : juce::UnitTest("Magda Audio Engine Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testAnswersWithoutAnEdit(); });
        magda::test::runWithCleanJuceState([this] { testUnwiredMethodsNameThemselves(); });
        magda::test::runWithCleanJuceState([this] { testDestructionRunsTheShutdown(); });
    }

  private:
    void testAnswersWithoutAnEdit() {
        beginTest("The engine answers the transport model itself, from no Edit");

        magda::MagdaAudioEngine engine{magda::AudioEngineOptions{.headless = true}};
        expect(engine.initialize(), "The engine comes up headless");

        expect(engine.fork().getEdit() == nullptr, "initialisePlayback() was never called");
        expect(engine.fork().getAudioBridge() == nullptr, "and so nothing mirrors the model into");

        expect(engine.getAudioBridge() == nullptr, "There is no bridge to hand out");
        expect(engine.getMidiBridge() != nullptr, "The MidiBridge is a service and survives");
        expect(engine.hasActiveEdit(), "An initialised engine has a project to play");

        const auto* map = engine.tempoMap();
        expect(map != nullptr, "The app converts beats and seconds through the host's map");
        if (map == nullptr)
            return;

        engine.setTempo(140.0);
        expect(std::abs(engine.getTempo() - 140.0) < kTolerance, "The tempo is the host's");
        expect(std::abs(map->bpmAt(0.0) - 140.0) < kTolerance, "and the map follows it");
        expect(std::abs(map->beatToTime(140.0) - 60.0) < kTolerance,
               "140 beats at 140 bpm is a minute");

        engine.setLooping(true);
        expect(engine.isLooping(), "The loop is the host's too");

        expect(engine.getMagdaApi().project().getCurrentProjectInfo().tempo > 0.0,
               "The engine's own MagdaApi answers");

        engine.shutdown();

        expect(engine.getMidiBridge() == nullptr, "Shut down, the fork's services are gone");
        engine.shutdown();
    }

    /**
     * The last row of #2551's list: nothing on this engine may answer a
     * question it has not been wired for without saying so. The recording
     * surface forwarded into a fork with no Edit, which answered false and
     * empty in silence. The session launcher was on this list too until #2552
     * wired it, and every method it took is a line the list no longer carries.
     */
    void testUnwiredMethodsNameThemselves() {
        beginTest("Every method nothing answers yet names itself, with its issue");

        magda::MagdaAudioEngine engine{magda::AudioEngineOptions{.headless = true}};
        expect(engine.initialize(), "The engine comes up headless");

        constexpr magda::TrackId trackId = 1;
        constexpr int sceneIndex = 0;

        // The launcher answers these off its own taps now (#2552), and with no
        // project published there is nothing for them to find.
        expect(engine.getSessionPlayheadPosition() < 0.0, "No session playhead");
        expect(engine.getSessionPlayheadClipId() == magda::INVALID_CLIP_ID, "and no clip under it");
        expect(engine.getActiveClipPlayheadPositions().empty(), "and nothing launched");

        // Arrangement MIDI recording is wired, but with no armed MIDI input it
        // refuses the request and leaves the transport stopped.
        engine.record();
        engine.onTransportRecord(0.0);
        expect(!engine.isRecording(), "No eligible input does not start recording");
        engine.onTransportStopRecording();

        // A slot on a track the model does not hold is rejected.
        engine.armSessionSlotRecording(trackId, sceneIndex);
        expect(!engine.isSessionSlotRecordArmed(trackId, sceneIndex), "Arming a slot does not");
        expect(!engine.isSessionSlotRecording(trackId, sceneIndex), "and it is not recording");
        engine.beginArmedSessionSlotRecordings();
        expect(engine.getRecordingPreviews().empty(), "and there is nothing to draw");
        engine.onPunchRegionChanged(0.0, 1.0, true, true);
        engine.onPunchEnabledChanged(true, true);

        // The rest of the surface, each already named.
        expect(engine.getPluginWindowManager() == nullptr, "No window onto a fork instance");
        expect(engine.getInsertRenderCaptureService() == nullptr, "and no insert capture pass");
        expect(engine.getSamplerMediaReferences().empty(), "and no media list off an Edit");
        expect(engine.createTempoSequenceRippleCommand(magda::TempoSequenceRippleMode::Insert,
                                                       magda::BeatPosition{0.0},
                                                       magda::BeatPosition{4.0}) == nullptr,
               "and no ripple command");

        const auto named = magda::MagdaAudioEngine::unwiredMethods();
        for (const auto* wired :
             {"record", "onTransportRecord", "onTransportStopRecording", "getRecordingPreviews",
              "armSessionSlotRecording", "isSessionSlotRecordArmed", "isSessionSlotRecording",
              "beginArmedSessionSlotRecordings"})
            expect(!named.contains(wired), juce::String(wired) + " is wired through the host");
        for (const auto* method : {"onPunchRegionChanged", "onPunchEnabledChanged",
                                   "getPluginWindowManager", "getInsertRenderCaptureService",
                                   "getSamplerMediaReferences", "createTempoSequenceRippleCommand"})
            expect(named.contains(method), juce::String(method) + " says it is not wired");

        engine.shutdown();
    }

    void testDestructionRunsTheShutdown() {
        beginTest("Destroying the engine runs the shutdown the app never calls");

        auto& watch = magda::test::AssertionWatch::instance();
        watch.take();

        {
            magda::MagdaAudioEngine engine{magda::AudioEngineOptions{.headless = true}};
            expect(engine.initialize(), "The engine comes up headless");
            expect(engine.getMidiBridge() != nullptr, "with the fork's MidiBridge under it");

            // Destroyed with no shutdown() call, which is what the app does
            // (magda_daw_main.cpp: a plain daw_engine_.reset()).
        }

        // ~MidiBridge asserts that its live sink was cleared first, and the
        // sink is the engine: still installed, it is a destroyed object the
        // MIDI callback thread can still push a note through.
        for (const auto& fired : watch.take())
            expect(!fired.contains("MidiBridge.cpp"), fired);
    }
};

MagdaAudioEngineTest magdaAudioEngineTest;

}  // namespace
