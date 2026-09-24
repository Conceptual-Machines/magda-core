#include <juce_core/juce_core.h>

#include <cmath>

#include "AssertionWatch.hpp"
#include "JuceTestStateGuard.hpp"
#include "magda/daw/api/magda_api.hpp"
#include "magda/daw/api/project_api.hpp"
#include "magda/daw/audio/insert_capture/InsertRenderCapture.hpp"
#include "magda/daw/core/TempoMap.hpp"
#include "magda/daw/engine/MagdaAudioEngine.hpp"
#include "magda/daw/engine/PluginService.hpp"
#include "magda/daw/music/GrooveLibrary.hpp"
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

/// Whether the MIDI service offers the keyboard, which only an attached engine lends it.
bool listsQwerty() {
    for (const auto& device : magda::MidiBridge::getInstance().getAvailableMidiInputs())
        if (device.id == magda::qwertyMidiDeviceId())
            return true;
    return false;
}

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

        // No Tracktion engine lends these (#2761): the plugin list and the grooves are the
        // app's services' own.
        auto& plugins = magda::PluginService::getInstance();
        expect(plugins.formats() != nullptr && plugins.knownList() != nullptr,
               "The plugin service has a list of its own");
        expect(magda::GrooveLibrary::getInstance().find("Basic 8th Swing") != nullptr,
               "The groove library holds the store's list");

        expect(engine.hasActiveEdit(), "An initialised engine has a project to play");

        // MIDI is the app's service; what the engine lends it is the QWERTY keyboard,
        // which the system's device list never holds (#2759).
        magda::MidiBridge::getInstance().setQwertyEnabled(true);
        expect(listsQwerty(), "The engine lent the service its virtual inputs");

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

        auto& project = engine.getMagdaApi().project();
        expect(project.getCurrentProjectInfo().tempo > 0.0, "The engine's own MagdaApi answers");
        const auto originalLoopStart = project.getCurrentProjectInfo().loopStartBeats;
        const auto originalLoopEnd = project.getCurrentProjectInfo().loopEndBeats;
        project.setLoopRange(4.0, 12.0);
        const auto loop = engine.getLoopRegionBeats();
        expect(std::abs(loop.start.value - 4.0) < kTolerance,
               "ProjectApi loop start reaches the engine");
        expect(std::abs(loop.end.value - 12.0) < kTolerance,
               "ProjectApi loop end reaches the engine");
        project.setLoopRange(originalLoopStart, originalLoopEnd);

        engine.shutdown();

        expect(!listsQwerty(), "Shut down, the service has no engine's devices to offer");
        expect(plugins.formats() == nullptr, "and the plugin service has let go of its list");
        magda::MidiBridge::getInstance().setQwertyEnabled(false);
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

        // The rest of the surface, each already named. The window manager, the media list
        // and the ripple left the interface with #2757, so nothing here asks for them.
        // The capture pass is the host's (#2279), and a project with no hardware insert
        // has nothing to capture.
        const auto* capture = engine.getInsertRenderCapture();
        expect(capture != nullptr && !capture->exportNeedsCapturePass(),
               "The insert capture pass answers, with nothing to capture");

        const auto named = magda::MagdaAudioEngine::unwiredMethods();
        for (const auto* wired :
             {"record", "onTransportRecord", "onTransportStopRecording", "getRecordingPreviews",
              "armSessionSlotRecording", "isSessionSlotRecordArmed", "isSessionSlotRecording",
              "beginArmedSessionSlotRecordings"})
            expect(!named.contains(wired), juce::String(wired) + " is wired through the host");
        for (const auto* wired : {"onPunchRegionChanged", "onPunchEnabledChanged"})
            expect(!named.contains(wired), juce::String(wired) + " is wired through the host");
        expect(!named.contains("getInsertRenderCapture"), "getInsertRenderCapture is wired");

        // #2757 took these off the interface, so the engine no longer answers for them at all.
        for (const auto* gone : {"getPluginWindowManager", "getSamplerMediaReferences",
                                 "createTempoSequenceRippleCommand"})
            expect(!named.contains(gone), juce::String(gone) + " no longer reports unwired");

        engine.shutdown();
    }

    void testDestructionRunsTheShutdown() {
        beginTest("Destroying the engine runs the shutdown the app never calls");

        auto& watch = magda::test::AssertionWatch::instance();
        watch.take();

        {
            magda::MagdaAudioEngine engine{magda::AudioEngineOptions{.headless = true}};
            expect(engine.initialize(), "The engine comes up headless");

            // Destroyed with no shutdown() call, which is what the app does
            // (magda_daw_main.cpp: a plain daw_engine_.reset()).
        }

        // forgetEngine() asserts that the live sink was cleared first, and the sink is
        // the engine: still installed, it is a destroyed object the MIDI callback thread
        // can still push a note through. The service outlives the engine, so that assert
        // is the only thing watching for it (#2759).
        for (const auto& fired : watch.take())
            expect(!fired.contains("MidiBridge.cpp"), fired);
    }
};

MagdaAudioEngineTest magdaAudioEngineTest;

}  // namespace
