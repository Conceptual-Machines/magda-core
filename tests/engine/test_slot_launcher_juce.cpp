#include <juce_core/juce_core.h>

#include <memory>

#include "JuceTestStateGuard.hpp"
#include "clip/ClipVoicePool.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "io/PrefetchThread.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineProject.hpp"
#include "magda/daw/engine/host/EngineRuntimeFactory.hpp"
#include "magda/daw/engine/host/SlotLauncher.hpp"
#include "plan/PlanCompiler.hpp"
#include "tap/LaunchTap.hpp"

/**
 * The app driving the session launcher (#2552).
 *
 * The engine half has been finished and null-diffed since #1894; what had never
 * existed is a click reaching it. So these run the whole path rather than the
 * translation alone: a launch becomes a request at a resolved beat, blocks are
 * rendered, and what comes back is read off the tap the block that advanced the
 * handle wrote -- which is also what the session view draws.
 */

namespace {

namespace host = magda::daw::engine_host;
namespace engine = magda::engine;

magda::DeviceInfo polySynth(magda::DeviceId id) {
    using PolySynth = magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;

    magda::DeviceInfo device;
    device.id = id;
    device.name = "Poly Synth";
    device.pluginId = PolySynth::xmlTypeName;
    device.deviceType = magda::DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    device.format = magda::PluginFormat::Internal;
    device.audioInputChannels = 0;
    device.audioOutputChannels = 2;

    const PolySynth metadata;
    for (auto index = 0; index < metadata.parameterCount(); ++index) {
        auto info = metadata.parameterInfo(index);
        info.currentValue = info.defaultValue;
        device.parameters.push_back(std::move(info));
    }

    return device;
}

/// The host's side of a launch, without a device under it: EngineHost needs one
/// opened before it has a session, and everything the launcher asks of it is
/// answered here instead.
class TestLaunchHost final : public host::LaunchHost {
  public:
    TestLaunchHost(engine::EngineSession& session, const engine::TempoMap& tempo)
        : session_(session), tempo_(tempo) {}

    engine::EngineSession* launchSession() override {
        return &session_;
    }

    const engine::TempoMap& launchTempo() const override {
        return tempo_;
    }

    double launchBeatsPerBar() const override {
        return 4.0;
    }

    bool launchTransportPlaying() const override {
        return playing;
    }

    void startLaunchTransport() override {
        playing = true;
    }

    bool playing = false;

  private:
    engine::EngineSession& session_;
    const engine::TempoMap& tempo_;
};

/// A project of instrument tracks, published and rolling, with the launcher
/// wired to it: everything a slot needs to sound.
struct Rig {
    static constexpr double kTempo = 120.0;

    explicit Rig(int trackCount) {
        auto& tracks = magda::TrackManager::getInstance();

        for (auto index = 0; index < trackCount; ++index) {
            const auto trackId = tracks.createTrack("Instrument " + juce::String(index + 1));
            trackIds.push_back(trackId);

            if (auto* track = tracks.getTrack(trackId); track != nullptr)
                track->chain.fxChainElements.emplace_back(
                    polySynth(static_cast<magda::DeviceId>(index + 1)));
        }
    }

    /// A four-beat MIDI clip holding one note, in @p sceneIndex of @p trackId.
    magda::ClipId slotClip(magda::TrackId trackId, int sceneIndex,
                           magda::LaunchQuantize quantize = magda::LaunchQuantize::None) {
        auto& clips = magda::ClipManager::getInstance();

        const auto clipId = clips.createMidiClipBeats(trackId, 0.0, 4.0, magda::ClipView::Session);
        clips.setClipSceneIndex(clipId, sceneIndex);
        clips.addMidiNote(
            clipId, magda::MidiNote{
                        .noteNumber = 60, .velocity = 100, .startBeat = 0.0, .lengthBeats = 4.0});

        if (auto* clip = clips.getClip(clipId); clip != nullptr)
            clip->launchQuantize = quantize;

        return clipId;
    }

    /// Compile and publish what the model now says, as EngineHost does.
    bool publish() {
        auto& trackManager = magda::TrackManager::getInstance();
        const auto& tracks = trackManager.getTracks();
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        if (master == nullptr)
            return false;

        factory.setModel(tracks, *master);

        const auto plan =
            std::make_shared<const engine::RenderPlan>(engine::compileRenderPlan(tracks, *master));

        engine::PlanValues values;
        engine::resolvePlanValues(*plan, tracks, *master, values);

        const auto published = session.publish(
            plan, context, engine::collectRuntimeStateIds(tracks, *master), std::move(values));

        session.publishClips(std::make_shared<const engine::ClipSnapshot>(
            engine::compileClipSnapshot(host::clipLanesFor(tracks), host::clipSources(), tempo)));

        return published.published;
    }

    /// Roll the transport, which is what advances a handle.
    void roll() {
        launchHost.playing = true;
        session.publishTransport(
            {.tempo = tempo, .request = {.generation = ++transportGeneration, .playing = true}});
    }

    void stopTransport() {
        launchHost.playing = false;
        session.publishTransport(
            {.tempo = tempo, .request = {.generation = ++transportGeneration, .playing = false}});
        launcher.transportStopped();
    }

    void restartTransport() {
        launchHost.playing = true;
        session.publishTransport(
            {.tempo = tempo, .request = {.generation = ++transportGeneration, .playing = true}});
        launcher.transportStarted();
    }

    /// Render @p blocks of 512 samples: 512 at 44100 is a little under
    /// a fiftieth of a beat at 120 bpm.
    void render(int blocks) {
        juce::AudioBuffer<float> output(2, context.maxBlockSize);
        for (auto block = 0; block < blocks; ++block)
            session.process(context.maxBlockSize, output);
    }

    float renderPeak(int blocks) {
        juce::AudioBuffer<float> output(2, context.maxBlockSize);
        auto peak = 0.0f;
        for (auto block = 0; block < blocks; ++block) {
            output.clear();
            session.process(context.maxBlockSize, output);
            peak = std::max(peak, output.getMagnitude(0, output.getNumSamples()));
        }
        return peak;
    }

    /// What the block that last advanced @p sceneIndex's handle on @p trackId
    /// published. The section hold lives here and nowhere in the model.
    engine::LaunchTap::Reading reading(magda::TrackId trackId, int sceneIndex) const {
        const auto* tap = session.launchTap(engine::SlotKey{trackId, sceneIndex});
        return tap == nullptr ? engine::LaunchTap::Reading{} : tap->read();
    }

    /// Blocks covering @p beats, rounded up.
    int blocksFor(double beats) const {
        const auto samples = beats * 60.0 / kTempo * context.sampleRate;
        return static_cast<int>(samples / context.maxBlockSize) + 1;
    }

    std::vector<magda::TrackId> trackIds;

    engine::RenderContext context{.sampleRate = 44100.0, .maxBlockSize = 512};
    engine::TempoMap tempo = host::tempoMapAt(kTempo, 4, 4);

    host::EngineFileReaders files;
    engine::PrefetchThread reader{false};
    host::EngineRuntimeFactory factory;
    engine::ClipVoicePool voices{files, reader, context};
    engine::EngineSession session{factory, nullptr, &voices};

    TestLaunchHost launchHost{session, tempo};
    host::SlotLauncher launcher{launchHost};
    std::uint64_t transportGeneration = 0;

    /// Last, so the feeds are attached before anything publishes into them.
    struct Attach {
        explicit Attach(Rig& rig) {
            rig.factory.attach(rig.session.clipFeed(), rig.voices.feed(),
                               rig.session.launchHandleFeed(), rig.session.liveInputs());
        }
    } attach{*this};
};

class SlotLauncherTest final : public juce::UnitTest {
  public:
    SlotLauncherTest() : juce::UnitTest("Slot Launcher Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testUnquantizedLaunchSounds(); });
        magda::test::runWithCleanJuceState([this] { testQuantizedLaunchWaitsForTheBar(); });
        magda::test::runWithCleanJuceState([this] { testOneSlotPerTrack(); });
        magda::test::runWithCleanJuceState([this] { testASceneStartsTogether(); });
        magda::test::runWithCleanJuceState([this] { testStoppingReturnsTheTrack(); });
        magda::test::runWithCleanJuceState([this] { testThePlayheadWrapsOnThePass(); });
        magda::test::runWithCleanJuceState([this] { testTransportRestartsSessionState(); });
        magda::test::runWithCleanJuceState([this] { testRapidTransportRestartIsNotMissed(); });
        magda::test::runWithCleanJuceState([this] { testInitialLaunchIsNotRelaunchedByTimer(); });
        magda::test::runWithCleanJuceState([this] { testInitialSceneIsNotRelaunchedByTimer(); });
        magda::test::runWithCleanJuceState([this] { testSelectionDoesNotBecomeLaunchIntent(); });
        magda::test::runWithCleanJuceState([this] { testStaleRememberedClipIsIgnored(); });
        magda::test::runWithCleanJuceState([this] { testMovedRememberedClipIsIgnored(); });
        magda::test::runWithCleanJuceState([this] { testExplicitStopClearsStoppedIntent(); });
        magda::test::runWithCleanJuceState([this] { testStoppedToggleClickRelaunches(); });
    }

  private:
    void testUnquantizedLaunchSounds() {
        beginTest("A launch with nothing to wait for starts on the next block");

        Rig rig(1);
        const auto clipId = rig.slotClip(rig.trackIds.front(), 0);
        expect(rig.publish(), "The project the launcher runs against is published");
        rig.roll();

        expect(rig.launcher.playState(clipId) == magda::SessionClipPlayState::Stopped,
               "Nothing is playing before the click");

        rig.launcher.launch(clipId);
        rig.render(1);

        expect(rig.launcher.playState(clipId) == magda::SessionClipPlayState::Playing,
               "and the first block after it is already sounding");

        const auto* track = magda::TrackManager::getInstance().getTrack(rig.trackIds.front());
        expect(track != nullptr && track->activeSessionClipId == clipId,
               "The track says what it is playing");
        expect(track != nullptr && track->playbackMode == magda::TrackPlaybackMode::Session,
               "and the session has taken it from the arrangement");
    }

    void testQuantizedLaunchWaitsForTheBar() {
        beginTest("A quantized launch is queued until the bar it was asked for");

        Rig rig(2);
        const auto rolling = rig.slotClip(rig.trackIds[0], 0);
        const auto queued = rig.slotClip(rig.trackIds[1], 0, magda::LaunchQuantize::OneBar);
        expect(rig.publish(), "The project is published");
        rig.roll();

        // Something has to be sounding first: the launcher starts the first
        // clip of a set where it was asked rather than holding it for a bar of
        // silence, which is the fork's rule too.
        rig.launcher.launch(rolling);
        rig.render(rig.blocksFor(0.5));

        rig.launcher.launch(queued);

        // Before any block has run: the click's own notification reads the
        // state, and a slot button blinks on it or never does (#2674).
        expect(rig.launcher.playState(queued) == magda::SessionClipPlayState::Queued,
               "The launcher answers for a click the audio thread has not seen yet");

        rig.render(1);

        expect(rig.launcher.playState(queued) == magda::SessionClipPlayState::Queued,
               "Half a beat in, the second clip is waiting");
        expect(rig.launcher.playState(rolling) == magda::SessionClipPlayState::Playing,
               "and the first is still playing under it");

        // Past the bar line the queue was resolved against.
        rig.render(rig.blocksFor(4.0));

        expect(rig.launcher.playState(queued) == magda::SessionClipPlayState::Playing,
               "and at the bar it starts");
    }

    void testOneSlotPerTrack() {
        beginTest("Launching a slot stops whatever its track was already playing");

        Rig rig(1);
        const auto trackId = rig.trackIds.front();
        const auto first = rig.slotClip(trackId, 0);
        const auto second = rig.slotClip(trackId, 1);
        expect(rig.publish(), "The project is published");
        rig.roll();

        rig.launcher.launch(first);
        rig.render(rig.blocksFor(0.5));
        expect(rig.launcher.playState(first) == magda::SessionClipPlayState::Playing,
               "The first slot is sounding");

        rig.launcher.launch(second);
        rig.render(1);

        // The engine renders every slot whose handle is playing
        // (SessionPlayback.hpp): one clip per track is the grid's rule, and
        // nothing enforces it unless the handover is asked for.
        expect(rig.launcher.playState(second) == magda::SessionClipPlayState::Playing,
               "and the second takes over");
        expect(rig.launcher.playState(first) == magda::SessionClipPlayState::Stopped,
               "leaving the first stopped rather than sounding under it");
    }

    void testASceneStartsTogether() {
        beginTest("A scene launches its slots together, and empties stop their tracks");

        Rig rig(3);
        const auto first = rig.slotClip(rig.trackIds[0], 0);
        const auto second = rig.slotClip(rig.trackIds[1], 0);

        // The third track's slot 0 is empty and its slot 1 holds a clip, so the
        // scene has something to stop.
        const auto elsewhere = rig.slotClip(rig.trackIds[2], 1);
        expect(rig.publish(), "The project is published");
        rig.roll();

        rig.launcher.launch(elsewhere);
        rig.render(rig.blocksFor(0.5));
        expect(rig.launcher.playState(elsewhere) == magda::SessionClipPlayState::Playing,
               "The third track is playing something from another scene");

        rig.launcher.launchScene(rig.trackIds, 0);
        rig.render(1);

        expect(rig.launcher.playState(first) == magda::SessionClipPlayState::Playing,
               "The scene's first slot sounds");
        expect(rig.launcher.playState(second) == magda::SessionClipPlayState::Playing,
               "and so does its second");
        expect(rig.launcher.playState(elsewhere) == magda::SessionClipPlayState::Stopped,
               "and the track whose slot was empty stopped");
    }

    void testStoppingReturnsTheTrack() {
        beginTest("Stopping a slot gives its track back to the arrangement");

        Rig rig(1);
        const auto clipId = rig.slotClip(rig.trackIds.front(), 0);
        expect(rig.publish(), "The project is published");
        rig.roll();

        const auto trackId = rig.trackIds.front();

        rig.launcher.launch(clipId);
        rig.render(rig.blocksFor(0.5));
        expect(rig.launcher.playState(clipId) == magda::SessionClipPlayState::Playing,
               "The slot is sounding");
        expect(rig.reading(trackId, 0).holdsSection, "and holding its track while it does");

        rig.launcher.stop(clipId);
        rig.render(1);

        expect(rig.launcher.playState(clipId) == magda::SessionClipPlayState::Stopped,
               "and the stop lands on the next block");

        // The hold outlives the sound, so a plain stop would leave the
        // arrangement under this track silent for as long as the session had
        // it. Releasing the section is what gives it back (#2302).
        expect(!rig.reading(trackId, 0).holdsSection, "The session has let the track go");

        const auto* track = magda::TrackManager::getInstance().getTrack(trackId);
        expect(track != nullptr && track->activeSessionClipId == magda::INVALID_CLIP_ID,
               "and the model says it is playing nothing");
        expect(track != nullptr && track->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "so the lane it publishes is the arrangement's again");
    }

    void testThePlayheadWrapsOnThePass() {
        beginTest("The playhead runs inside one pass of the slot, not past it");

        Rig rig(1);
        const auto clipId = rig.slotClip(rig.trackIds.front(), 0);
        expect(rig.publish(), "The project is published");
        rig.roll();

        rig.launcher.launch(clipId);
        rig.render(rig.blocksFor(1.0));

        // A beat at 120 bpm is half a second, inside a four-beat slot.
        const auto early = rig.launcher.playheadSeconds(clipId);
        expect(early > 0.0 && early < 2.0, "A beat in, the playhead is inside the pass");
        expect(rig.launcher.playheadClip() == clipId, "and the session playhead follows it");

        // Five beats: the run is into its second pass, and what the slot is
        // playing is the top of the clip again.
        rig.render(rig.blocksFor(4.0));

        const auto wrapped = rig.launcher.playheadSeconds(clipId);
        expect(wrapped >= 0.0 && wrapped < 2.0,
               "and past the end of the clip it has wrapped rather than run on");

        const auto playing = rig.launcher.playheads();
        expect(playing.size() == 1 && playing.contains(clipId),
               "Every sounding slot is in the set the view draws");
    }

    void testTransportRestartsSessionState() {
        beginTest("Transport Stop keeps session mode and Play relaunches every active track");

        Rig rig(2);
        const auto first = rig.slotClip(rig.trackIds[0], 0);
        const auto second = rig.slotClip(rig.trackIds[1], 0, magda::LaunchQuantize::OneBar);
        expect(rig.publish(), "The project is published");
        rig.roll();
        rig.launcher.launchScene(rig.trackIds, 0);
        expect(rig.renderPeak(8) > 0.0f, "The launched scene produces engine output");
        const auto elapsedBeforeStop = rig.reading(rig.trackIds[0], 0).elapsedBeats;

        rig.stopTransport();
        expect(rig.launcher.playState(first) == magda::SessionClipPlayState::Stopped,
               "The UI reports stopped before the audio callback acknowledges it");
        expect(rig.launcher.playheadSeconds(first) < 0.0,
               "The UI hides the stale playhead before acknowledgement");
        rig.render(1);

        rig.launcher.processStateEvents();
        rig.launcher.processStateEvents();
        for (const auto trackId : rig.trackIds) {
            const auto* track = magda::TrackManager::getInstance().getTrack(trackId);
            expect(track != nullptr && track->playbackMode == magda::TrackPlaybackMode::Session,
                   "Session mode survives stopped state-event ticks");
        }

        rig.restartTransport();
        expect(rig.launcher.playState(first) == magda::SessionClipPlayState::Queued &&
                   rig.launcher.playState(second) == magda::SessionClipPlayState::Queued,
               "Both retained scene slots are queued before acknowledgement");
        expect(rig.renderPeak(2) > 0.0f, "Play relaunches both tracks into engine output");
        const auto firstRestarted = rig.reading(rig.trackIds[0], 0);
        const auto secondRestarted = rig.reading(rig.trackIds[1], 0);
        expect(firstRestarted.playing && secondRestarted.playing,
               "Both scene handles are playing after restart");
        expectWithinAbsoluteError(firstRestarted.elapsedBeats, secondRestarted.elapsedBeats, 1.0e-9,
                                  "Both tracks restart on the same engine beat");
        expect(firstRestarted.elapsedBeats > 0.0 && firstRestarted.elapsedBeats < elapsedBeforeStop,
               "The restarted scene advances again from its beginning");
    }

    void testRapidTransportRestartIsNotMissed() {
        beginTest("Stop and Play between state-event ticks still relaunches");

        Rig rig(1);
        const auto clipId = rig.slotClip(rig.trackIds.front(), 0);
        expect(rig.publish(), "The project is published");
        rig.roll();
        rig.launcher.launch(clipId);
        rig.render(10);
        const auto elapsedBeforeStop = rig.reading(rig.trackIds.front(), 0).elapsedBeats;

        rig.stopTransport();
        rig.restartTransport();
        expect(rig.launcher.playState(clipId) == magda::SessionClipPlayState::Queued,
               "The restart is queued despite the stale playing tap");
        expect(rig.renderPeak(2) > 0.0f, "The rapid restart produces engine output");
        expect(rig.reading(rig.trackIds.front(), 0).elapsedBeats < elapsedBeforeStop,
               "The rapid restart begins a new run from the start");
    }

    void testInitialLaunchIsNotRelaunchedByTimer() {
        beginTest("A launch that starts transport is not relaunched by the next UI tick");

        Rig rig(1);
        const auto clipId = rig.slotClip(rig.trackIds.front(), 0);
        expect(rig.publish(), "The project is published");

        rig.launcher.launch(clipId);
        rig.roll();
        rig.render(2);
        const auto before = rig.reading(rig.trackIds.front(), 0).elapsedBeats;
        rig.launcher.processStateEvents();
        rig.render(2);
        const auto after = rig.reading(rig.trackIds.front(), 0).elapsedBeats;

        expect(before > 0.0 && after > before,
               "The existing run advances instead of restarting on the first state tick");
    }

    void testInitialSceneIsNotRelaunchedByTimer() {
        beginTest("A scene that starts transport is not relaunched by the next UI tick");

        Rig rig(2);
        rig.slotClip(rig.trackIds[0], 0);
        rig.slotClip(rig.trackIds[1], 0);
        expect(rig.publish(), "The project is published");

        rig.launcher.launchScene(rig.trackIds, 0);
        rig.roll();
        rig.render(2);
        const auto firstBefore = rig.reading(rig.trackIds[0], 0).elapsedBeats;
        const auto secondBefore = rig.reading(rig.trackIds[1], 0).elapsedBeats;
        rig.launcher.processStateEvents();
        rig.render(2);

        expect(rig.reading(rig.trackIds[0], 0).elapsedBeats > firstBefore &&
                   rig.reading(rig.trackIds[1], 0).elapsedBeats > secondBefore,
               "Both existing scene runs advance instead of restarting");
    }

    void testSelectionDoesNotBecomeLaunchIntent() {
        beginTest("Transport Play does not launch a merely selected Session clip");

        Rig rig(1);
        const auto selected = rig.slotClip(rig.trackIds.front(), 0);
        magda::ClipManager::getInstance().setSelectedClip(selected);
        expect(rig.publish(), "The project is published");

        rig.restartTransport();
        expect(rig.renderPeak(2) == 0.0f, "The selected clip produces no engine output");
        expect(rig.launcher.playState(selected) == magda::SessionClipPlayState::Stopped &&
                   !rig.reading(rig.trackIds.front(), 0).playing,
               "The UI and engine both report the selected clip stopped");
    }

    void testStaleRememberedClipIsIgnored() {
        beginTest("A deleted remembered slot is not replaced by another selection");

        Rig rig(1);
        const auto active = rig.slotClip(rig.trackIds.front(), 0);
        const auto other = rig.slotClip(rig.trackIds.front(), 1);
        expect(rig.publish(), "The project is published");
        rig.roll();
        rig.launcher.launch(active);
        rig.render(1);
        rig.stopTransport();
        rig.render(1);

        magda::ClipManager::getInstance().setSelectedClip(other);
        magda::ClipManager::getInstance().deleteClip(active);
        expect(rig.publish(), "The deleted slot is removed from the published model");
        rig.restartTransport();
        rig.render(2);

        expect(!rig.reading(rig.trackIds.front(), 1).playing,
               "The unrelated selected slot remains stopped");
        const auto* track = magda::TrackManager::getInstance().getTrack(rig.trackIds.front());
        expect(track != nullptr && track->activeSessionClipId == magda::INVALID_CLIP_ID &&
                   track->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "The stale intent is cleared and its track returns to Arrangement");
    }

    void testMovedRememberedClipIsIgnored() {
        beginTest("A remembered slot moved to another track is not relaunched");

        Rig rig(2);
        const auto active = rig.slotClip(rig.trackIds[0], 0);
        expect(rig.publish(), "The project is published");
        rig.roll();
        rig.launcher.launch(active);
        rig.render(1);
        rig.stopTransport();
        rig.render(1);

        magda::ClipManager::getInstance().moveClipToTrack(active, rig.trackIds[1]);
        expect(rig.publish(), "The moved slot is republished on its new track");
        rig.restartTransport();
        rig.render(2);

        expect(!rig.reading(rig.trackIds[1], 0).playing,
               "The moved slot is not launched from stale source-track intent");
        const auto* source = magda::TrackManager::getInstance().getTrack(rig.trackIds[0]);
        expect(source != nullptr && source->activeSessionClipId == magda::INVALID_CLIP_ID &&
                   source->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "The stale source-track intent is repaired");
    }

    void testExplicitStopClearsStoppedIntent() {
        beginTest("An explicit track stop while transport is stopped clears restart intent");

        Rig rig(1);
        const auto clipId = rig.slotClip(rig.trackIds.front(), 0);
        expect(rig.publish(), "The project is published");
        rig.roll();
        rig.launcher.launch(clipId);
        rig.render(1);
        rig.stopTransport();
        rig.render(1);

        rig.launcher.stopTrack(rig.trackIds.front());
        const auto* track = magda::TrackManager::getInstance().getTrack(rig.trackIds.front());
        expect(track != nullptr && track->activeSessionClipId == magda::INVALID_CLIP_ID &&
                   track->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "The explicit stop returns the track immediately");
        rig.restartTransport();
        rig.render(2);
        expect(!rig.reading(rig.trackIds.front(), 0).playing,
               "Play does not relaunch the explicitly stopped slot");
    }

    void testStoppedToggleClickRelaunches() {
        beginTest("Clicking a retained Toggle slot while stopped relaunches it");

        Rig rig(1);
        const auto clipId = rig.slotClip(rig.trackIds.front(), 0);
        if (auto* clip = magda::ClipManager::getInstance().getClip(clipId); clip != nullptr)
            clip->launchMode = magda::LaunchMode::Toggle;
        expect(rig.publish(), "The project is published");
        rig.roll();
        rig.launcher.launch(clipId);
        rig.render(1);
        rig.stopTransport();
        rig.render(1);

        rig.launcher.launch(clipId);
        rig.roll();
        expect(rig.renderPeak(2) > 0.0f, "The stopped Toggle slot starts again on click");
    }
};

SlotLauncherTest slotLauncherTest;

}  // namespace
