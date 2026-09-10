#include <juce_core/juce_core.h>

#include <algorithm>
#include <memory>
#include <ranges>

#include "JuceTestStateGuard.hpp"
#include "clip/ClipVoicePool.hpp"
#include "core/SourcePool.hpp"
#include "exec/EngineDevice.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "io/PrefetchThread.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaChorusCompiledPlugin.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineProject.hpp"
#include "magda/daw/engine/host/EngineRuntimeFactory.hpp"
#include "magda/daw/engine/host/EngineTrace.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * What the app hands magda::engine at runtime (#2551).
 *
 * The corpus proves the engine renders what it is given; what it had never been
 * given is the app's own model, read off the singletons a running MAGDA keeps
 * it in. That translation is the whole of this slice, and it is the only thing
 * here that can be wrong on its own.
 *
 * The device callback is deliberately not covered. It needs an open audio
 * device, which CI does not have; everything up to it is a publish, and the
 * publish is what these run.
 */

namespace {

namespace host = magda::daw::engine_host;
namespace engine = magda::engine;

/// The Poly Synth as a project saves it. Every compiled synth MAGDA ships is
/// one of these, and it is the shortest path from a note to a sound.
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

/// A compiled effect, for a case that needs a second kind of device.
magda::DeviceInfo chorus(magda::DeviceId id) {
    using Chorus = magda::daw::audio::compiled::MagdaChorusCompiledPlugin;

    magda::DeviceInfo device;
    device.id = id;
    device.name = "Chorus";
    device.pluginId = Chorus::xmlTypeName;
    device.deviceType = magda::DeviceType::Effect;
    device.format = magda::PluginFormat::Internal;
    device.audioInputChannels = 2;
    device.audioOutputChannels = 2;

    return device;
}

/// Every note-on that reached the instrument, and when. Bound in the
/// instrument's place so that what is measured is what the chain delivered
/// rather than what came out of a synth.
class NoteCapture final : public engine::EngineDevice {
  public:
    struct Strike {
        int note = 0;
        int block = 0;
    };

    void process(engine::DeviceBlock& block) override {
        if (block.midiIn != nullptr)
            for (const auto entry : *block.midiIn) {
                const auto message = entry.getMessage();
                if (message.isNoteOn())
                    strikes.push_back({message.getNoteNumber(), blocks});
                else if (message.isNoteOff())
                    releases.push_back({message.getNoteNumber(), blocks});
            }

        block.audio.clear();
        ++blocks;
    }

    std::vector<Strike> strikes;
    std::vector<Strike> releases;
    int blocks = 0;
};

/// The app's own factory with the instrument swapped for a capture, so a case
/// can say which notes reached the chain rather than inferring it from a level.
class CapturingFactory final : public engine::RuntimeStateFactory {
  public:
    void attach(engine::ClipSnapshotFeed& clips, engine::ClipStreamFeed& streams,
                engine::LaunchHandleFeed& handles) {
        inner_.attach(clips, streams, handles);
    }

    void setModel(const std::vector<magda::TrackInfo>& tracks, const magda::TrackInfo& master) {
        inner_.setModel(tracks, master);
    }

    std::unique_ptr<engine::EngineDevice> createDevice(engine::DeviceKey) override {
        auto device = std::make_unique<NoteCapture>();
        capture = device.get();
        return device;
    }

    std::unique_ptr<engine::EngineAudioSource> createClipAudioSource(
        magda::TrackId trackId) override {
        return inner_.createClipAudioSource(trackId);
    }
    std::unique_ptr<engine::EngineMidiSource> createClipMidiSource(
        magda::TrackId trackId) override {
        return inner_.createClipMidiSource(trackId);
    }
    std::unique_ptr<engine::EngineAudioSource> createSessionAudioSource(
        magda::TrackId trackId) override {
        return inner_.createSessionAudioSource(trackId);
    }
    std::unique_ptr<engine::EngineMidiSource> createSessionMidiSource(
        magda::TrackId trackId) override {
        return inner_.createSessionMidiSource(trackId);
    }

    NoteCapture* capture = nullptr;

  private:
    host::EngineRuntimeFactory inner_;
};

class EngineHostPublishTest final : public juce::UnitTest {
  public:
    EngineHostPublishTest() : juce::UnitTest("Engine Host Publish Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testLanesSplitBySection(); });
        magda::test::runWithCleanJuceState([this] { testMidiClipReachesAnInstrument(); });
        magda::test::runWithCleanJuceState([this] { testNoteMovedWhileRolling(); });
        magda::test::runWithCleanJuceState([this] { testReplacedPluginIsRebuilt(); });
        magda::test::runWithCleanJuceState([this] { testClearedProjectIsRebuilt(); });
        magda::test::runWithCleanJuceState([this] { testDroppedKeyIsStillRebuilt(); });
    }

  private:
    void testLanesSplitBySection() {
        beginTest("Clip lanes split the model's two sections apart");

        auto& clips = magda::ClipManager::getInstance();
        const auto trackId = magda::TrackManager::getInstance().createTrack("Track");
        const auto arrangement =
            clips.createMidiClipBeats(trackId, 0.0, 4.0, magda::ClipView::Arrangement);
        const auto session = clips.createMidiClipBeats(trackId, 0.0, 4.0, magda::ClipView::Session);

        const auto lanes = host::clipLanesFor(magda::TrackManager::getInstance().getTracks());
        expect(lanes.size() == 1, "One lane per track");
        if (lanes.size() != 1)
            return;

        // A session clip is a slot positioned by scene and an arrangement clip
        // is positioned by beat. The compiler's guard exists to catch a caller
        // that confused them, and this is the caller deciding which is which.
        expect(lanes[0].trackId == trackId, "The lane names its track");
        expect(lanes[0].clips.size() == 1 && lanes[0].clips[0].id == arrangement,
               "The arrangement clip is on the timeline");
        expect(lanes[0].session.size() == 1 && lanes[0].session[0].id == session,
               "The session clip is a slot");
    }

    void testMidiClipReachesAnInstrument() {
        beginTest("A MIDI clip on an instrument track sounds through the app's model");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = trackManager.createTrack("Instrument");
        auto* track = trackManager.getTrack(trackId);
        expect(track != nullptr, "The track exists");
        if (track == nullptr)
            return;

        track->chain.fxChainElements.emplace_back(polySynth(1));

        auto& clips = magda::ClipManager::getInstance();
        const auto clipId = clips.createMidiClipBeats(trackId, 0.0, 4.0);
        expect(clips.addMidiNote(clipId, magda::MidiNote{.noteNumber = 60,
                                                         .velocity = 100,
                                                         .startBeat = 0.0,
                                                         .lengthBeats = 2.0}),
               "The clip holds a note");

        const auto& tracks = trackManager.getTracks();
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(master != nullptr, "The master exists");
        if (master == nullptr)
            return;

        const magda::engine::RenderContext context{.sampleRate = 44100.0, .maxBlockSize = 512};
        const auto tempo = host::tempoMapAt(120.0, 4, 4);

        host::EngineFileReaders files;
        magda::engine::PrefetchThread reader(false);
        host::EngineRuntimeFactory factory;
        factory.setModel(tracks, *master);

        // The instrumentation the app runs under MAGDA_ENGINE_TRACE_MIDI
        // (#2568), exercised here so a trace nobody can read is a failing test
        // rather than an empty log in the middle of a repro.
        host::EngineTrace trace;
        factory.traceInto(trace);

        magda::engine::ClipVoicePool voices(files, reader, context);
        magda::engine::EngineSession session(factory, nullptr, &voices);
        factory.attach(session.clipFeed(), voices.feed(), session.launchHandleFeed());

        const auto plan = std::make_shared<const magda::engine::RenderPlan>(
            magda::engine::compileRenderPlan(tracks, *master));

        magda::engine::PlanValues values;
        magda::engine::resolvePlanValues(*plan, tracks, *master, values);

        const auto published =
            session.publish(plan, context, magda::engine::collectRuntimeStateIds(tracks, *master),
                            std::move(values));
        expect(published.published, "The plan the app compiled is one the engine takes");
        expect(factory.unbuilt().empty(), "Every device the model names was built");

        session.publishClips(
            std::make_shared<const magda::engine::ClipSnapshot>(magda::engine::compileClipSnapshot(
                host::clipLanesFor(tracks), host::clipSources(), tempo)));
        session.publishTransport({.tempo = tempo, .request = {.generation = 1, .playing = true}});

        // Half a second at 120 bpm, which is well inside the note.
        juce::AudioBuffer<float> output(2, context.maxBlockSize);
        auto peak = 0.0f;
        for (auto block = 0; block < 43; ++block) {
            session.process(context.maxBlockSize, output);
            peak = std::max(peak, output.getMagnitude(0, context.maxBlockSize));
        }

        // What this pins is the translation rather than the level: a project
        // the app was holding reached a compiled instrument and came back as
        // audio.
        expect(peak > 0.0f, "The note made a sound");

        const auto traced = trace.drain();
        expect(traced.joinIntoString("\n").contains("note-on  60"),
               "The trace records the note-on that reached the instrument");
    }

    void testNoteMovedWhileRolling() {
        beginTest("A note whose pitch shifts while it sounds carries on at the new pitch");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = trackManager.createTrack("Instrument");
        auto* track = trackManager.getTrack(trackId);
        expect(track != nullptr, "The track exists");
        if (track == nullptr)
            return;

        track->chain.fxChainElements.emplace_back(polySynth(1));

        // Long enough that the edit lands well inside it.
        auto& clips = magda::ClipManager::getInstance();
        const auto clipId = clips.createMidiClipBeats(trackId, 0.0, 8.0);
        clips.addMidiNote(clipId,
                          magda::MidiNote{.noteNumber = 60, .startBeat = 2.0, .lengthBeats = 4.0});

        const auto& tracks = trackManager.getTracks();
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        if (master == nullptr)
            return;

        const engine::RenderContext context{.sampleRate = 44100.0, .maxBlockSize = 512};
        const auto tempo = host::tempoMapAt(120.0, 4, 4);

        host::EngineFileReaders files;
        engine::PrefetchThread reader(false);
        CapturingFactory factory;
        factory.setModel(tracks, *master);

        engine::ClipVoicePool voices(files, reader, context);
        engine::EngineSession session(factory, nullptr, &voices);
        factory.attach(session.clipFeed(), voices.feed(), session.launchHandleFeed());

        const auto plan =
            std::make_shared<const engine::RenderPlan>(engine::compileRenderPlan(tracks, *master));

        engine::PlanValues values;
        engine::resolvePlanValues(*plan, tracks, *master, values);
        session.publish(plan, context, engine::collectRuntimeStateIds(tracks, *master),
                        std::move(values));

        const auto publishClips = [&] {
            session.publishClips(
                std::make_shared<const engine::ClipSnapshot>(engine::compileClipSnapshot(
                    host::clipLanesFor(tracks), host::clipSources(), tempo)));
        };

        publishClips();
        session.publishTransport({.tempo = tempo, .request = {.generation = 1, .playing = true}});

        juce::AudioBuffer<float> output(2, context.maxBlockSize);
        const auto render = [&](int blocks) {
            for (auto block = 0; block < blocks; ++block)
                session.process(context.maxBlockSize, output);
        };

        // Beat 3, which is a beat into the note.
        render(130);

        auto* clip = clips.getClip(clipId);
        if (clip == nullptr || clip->midiNotes.size() != 1)
            return;

        clip->midiNotes[0].noteNumber = 64;
        publishClips();

        render(130);

        expect(factory.capture != nullptr, "The instrument slot was bound");
        if (factory.capture == nullptr)
            return;

        const auto& strikes = factory.capture->strikes;
        for (const auto& strike : strikes)
            logMessage("note-on " + juce::String(strike.note) + " at block " +
                       juce::String(strike.block));

        // The old pitch, then the new one taking over where the edit landed.
        // Before #2568 the shift ended the old pitch and struck nothing, so the
        // rest of the note was silence.
        expect(strikes.size() == 2, "The old pitch and then the new one");
        if (strikes.size() != 2)
            return;

        expect(strikes[0].note == 60, "It started at the pitch it was written at");
        expect(strikes[1].note == 64, "It carries on at the pitch it was shifted to");

        const auto releases = factory.capture->releases;
        expect(
            std::ranges::any_of(releases, [](const auto& release) { return release.note == 60; }),
            "The pitch it left is released rather than left hanging");
    }

    /// The key a track's first FX slot has.
    static constexpr magda::engine::DeviceKey firstFxSlot() {
        return magda::engine::DeviceKey{magda::ChainSegment::Fx, 1};
    }

    void testReplacedPluginIsRebuilt() {
        beginTest("A slot whose plugin changed is a device the store must rebuild");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = trackManager.createTrack("Instrument");
        auto* track = trackManager.getTrack(trackId);
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(track != nullptr && master != nullptr, "The track and the master exist");
        if (track == nullptr || master == nullptr)
            return;

        track->chain.fxChainElements.emplace_back(polySynth(1));

        host::EngineRuntimeFactory factory;
        factory.setModel(trackManager.getTracks(), *master);
        expect(factory.devicesToRebuild().empty(), "Nothing has been built to rebuild");
        expect(factory.createDevice(firstFxSlot()) != nullptr, "The catalog builds the synth");

        // The retention contract: the same model published again asks for nothing.
        factory.setModel(trackManager.getTracks(), *master);
        expect(factory.devicesToRebuild().empty(), "A device that did not change is kept");

        // The same DeviceId, a different plugin: a slot swapped within a
        // project, and #2572 across one.
        track->chain.fxChainElements[0] = chorus(1);
        factory.setModel(trackManager.getTracks(), *master);

        const auto rebuild = factory.devicesToRebuild();
        expect(rebuild.size() == 1 && rebuild.contains(firstFxSlot()),
               "The slot's new plugin is named for rebuild");
    }

    void testClearedProjectIsRebuilt() {
        beginTest("A cleared project is every device it held named for rebuild");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = trackManager.createTrack("Instrument");
        auto* track = trackManager.getTrack(trackId);
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(track != nullptr && master != nullptr, "The track and the master exist");
        if (track == nullptr || master == nullptr)
            return;

        track->chain.fxChainElements.emplace_back(polySynth(1));

        host::EngineRuntimeFactory factory;
        factory.setModel(trackManager.getTracks(), *master);
        expect(factory.createDevice(firstFxSlot()) != nullptr, "The catalog builds the synth");
        expect(!host::modelHoldsNoDevices(), "A project with a device in it is not a teardown");

        // What a project load does before it restores anything, and the only
        // moment the empty model is visible.
        trackManager.clearAllTracks();
        expect(host::modelHoldsNoDevices(), "A cleared project names no device anywhere");

        factory.forgetBuiltDevices();
        const auto rebuild = factory.devicesToRebuild();
        expect(rebuild.size() == 1 && rebuild.contains(firstFxSlot()),
               "Every device the store holds is the previous project's");
    }

    void testDroppedKeyIsStillRebuilt() {
        beginTest("A key the model dropped is still one the store may hold");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = trackManager.createTrack("Instrument");
        auto* track = trackManager.getTrack(trackId);
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(track != nullptr && master != nullptr, "The track and the master exist");
        if (track == nullptr || master == nullptr)
            return;

        track->chain.fxChainElements.emplace_back(polySynth(1));

        host::EngineRuntimeFactory factory;
        factory.setModel(trackManager.getTracks(), *master);
        expect(factory.createDevice(firstFxSlot()) != nullptr, "The catalog builds the synth");

        // The device deleted, and the publish that would have evicted it
        // refused: the store only evicts after a swap, so it still holds one.
        track->chain.fxChainElements.clear();
        factory.setModel(trackManager.getTracks(), *master);

        factory.forgetBuiltDevices();
        const auto rebuild = factory.devicesToRebuild();
        expect(rebuild.contains(firstFxSlot()),
               "The key is named for rebuild rather than forgotten with the model");
    }
};

EngineHostPublishTest engineHostPublishTest;

}  // namespace
