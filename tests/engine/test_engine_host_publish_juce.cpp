#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <memory>
#include <ranges>

#include "JuceTestStateGuard.hpp"
#include "clip/ClipVoicePool.hpp"
#include "core/SourcePool.hpp"
#include "exec/EngineDevice.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "io/PrefetchThread.hpp"
#include "magda/daw/audio/MidiBridge.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaChorusCompiledPlugin.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineProject.hpp"
#include "magda/daw/engine/host/EngineRuntimeFactory.hpp"
#include "magda/daw/engine/host/EngineTrace.hpp"
#include "magda/daw/engine/host/LiveMidiSources.hpp"
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
                engine::LaunchHandleFeed& handles, const engine::LiveInputFeed& liveInputs,
                host::LiveMidiSources& sources) {
        inner_.attach(clips, streams, handles, liveInputs, sources);
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
        magda::test::runWithCleanJuceState([this] { testExternalKeysNamesOnlyExternals(); });
        magda::test::runWithCleanJuceState([this] { testPadPluginIsItsOwnKey(); });
        magda::test::runWithCleanJuceState([this] { testClearedProjectIsRebuilt(); });
        magda::test::runWithCleanJuceState([this] { testDroppedKeyIsStillRebuilt(); });
        magda::test::runWithCleanJuceState([this] { testMetersReadWhatWasRendered(); });
        magda::test::runWithCleanJuceState([this] { testAuditionReachesAnIdleTrack(); });
        magda::test::runWithCleanJuceState([this] { testDeviceMidiReachesOnlyItsOwnTrack(); });
        magda::test::runWithCleanJuceState([this] { testRouteRemovalPanicsTheInput(); });
        magda::test::runWithCleanJuceState([this] { testDeviceConnectedAfterAPublishResolves(); });
        magda::test::runWithCleanJuceState([this] { testRouteChangesAreAPlanChange(); });
        magda::test::runWithCleanJuceState([this] { testOnlyTrackMetersAreTapped(); });
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
        host::LiveMidiSources sources;
        host::EngineRuntimeFactory factory;
        factory.setModel(tracks, *master);

        // The instrumentation the app runs under MAGDA_ENGINE_TRACE_MIDI
        // (#2568), exercised here so a trace nobody can read is a failing test
        // rather than an empty log in the middle of a repro.
        host::EngineTrace trace;
        factory.traceInto(trace);

        magda::engine::ClipVoicePool voices(files, reader, context);
        magda::engine::EngineSession session(factory, nullptr, &voices);
        factory.attach(session.clipFeed(), voices.feed(), session.launchHandleFeed(),
                       session.liveInputs(), sources);

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
        host::LiveMidiSources sources;
        CapturingFactory factory;
        factory.setModel(tracks, *master);

        engine::ClipVoicePool voices(files, reader, context);
        engine::EngineSession session(factory, nullptr, &voices);
        factory.attach(session.clipFeed(), voices.feed(), session.launchHandleFeed(),
                       session.liveInputs(), sources);

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
    static magda::engine::DeviceKey firstFxSlot() {
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

    void testExternalKeysNamesOnlyExternals() {
        beginTest("The keys a save asks about are the model's external plugins");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = trackManager.createTrack("Instrument");
        trackManager.addDeviceToTrack(trackId, polySynth(magda::DeviceId{1}));

        // A plugin that is a file on this machine rather than a class this
        // build holds, which is the whole of what makes it external.
        auto external = polySynth(magda::DeviceId{2});
        external.name = "Some VST";
        external.format = magda::PluginFormat::VST3;
        trackManager.addDeviceToTrack(trackId, external);

        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(master != nullptr, "The master track is there to publish");

        host::EngineRuntimeFactory factory;
        factory.setModel(trackManager.getTracks(), *master);

        const auto keys = factory.externalKeys();
        expect(keys.size() == 1, "The compiled synth is not one of these");
        expect(keys.front() == engine::DeviceKey{magda::ChainSegment::Fx, magda::DeviceId{2}},
               "And the VST is");

        // What a single-slot capture asks before it reaches for a plugin: the
        // compiled synth has no chunk, and asking about one would report the
        // absence as a state that could not be read.
        expect(factory.isExternalKey(keys.front()), "The VST is one by key too");
        expect(
            !factory.isExternalKey(engine::DeviceKey{magda::ChainSegment::Fx, magda::DeviceId{1}}),
            "The compiled synth is not");
        expect(
            !factory.isExternalKey(engine::DeviceKey{magda::ChainSegment::Fx, magda::DeviceId{99}}),
            "And neither is a key the model does not carry");
    }

    void testPadPluginIsItsOwnKey() {
        beginTest("A plugin on a Drum Grid's pad is an external key of its own");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = trackManager.createTrack("Drums");

        auto grid = polySynth(magda::DeviceId{1});
        grid.name = "Drum Grid";

        auto pads = std::make_unique<magda::RackInfo>();
        pads->id = 1;
        {
            magda::ChainInfo pad;
            pad.id = 1;

            auto sampler = polySynth(magda::DeviceId{2});
            sampler.name = "Padded VST";
            sampler.format = magda::PluginFormat::VST3;
            pad.elements.emplace_back(std::move(sampler));

            pads->chains.push_back(std::move(pad));
        }
        grid.pads.reset(std::move(pads));

        trackManager.addDeviceToTrack(trackId, grid);

        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(master != nullptr, "The master track is there to publish");

        host::EngineRuntimeFactory factory;
        factory.setModel(trackManager.getTracks(), *master);

        // A pad's patch rides along in the grid's own state (#2207), so a
        // single-slot capture is handed the grid's path. The plugin with a
        // chunk to read is the one on the pad, and it is a device of its own
        // here rather than something the grid's key stands for.
        const auto padKey = engine::DeviceKey{magda::ChainSegment::Fx, magda::DeviceId{2}};

        const auto keys = factory.externalKeys();
        expect(keys.size() == 1, "The grid itself is a device this build holds");
        expect(keys.front() == padKey, "And the pad's plugin is the external one");
        expect(factory.isExternalKey(padKey), "Asked by key too");
        expect(
            !factory.isExternalKey(engine::DeviceKey{magda::ChainSegment::Fx, magda::DeviceId{1}}),
            "The grid is not one");
    }

    void testClearedProjectIsRebuilt() {
        beginTest("A torn-down project is every device it held named for rebuild");

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

        // What EngineHost does on a declared project teardown (#2576), while
        // the outgoing project is still the model.
        factory.forgetBuiltDevices();

        const auto rebuild = factory.devicesToRebuild();
        expect(rebuild.size() == 1 && rebuild.contains(firstFxSlot()),
               "Every device the store holds is the previous project's");
    }

    void testMetersReadWhatWasRendered() {
        beginTest("A track's meter and the master's read what the engine rendered");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = trackManager.createTrack("Instrument");
        auto* track = trackManager.getTrack(trackId);
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(track != nullptr && master != nullptr, "The track and the master exist");
        if (track == nullptr || master == nullptr)
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
        const magda::engine::RenderContext context{.sampleRate = 44100.0, .maxBlockSize = 512};
        const auto tempo = host::tempoMapAt(120.0, 4, 4);

        host::EngineFileReaders files;
        magda::engine::PrefetchThread reader(false);
        host::LiveMidiSources sources;
        host::EngineRuntimeFactory factory;
        factory.setModel(tracks, *master);

        magda::engine::ClipVoicePool voices(files, reader, context);
        magda::engine::EngineSession session(factory, nullptr, &voices);
        factory.attach(session.clipFeed(), voices.feed(), session.launchHandleFeed(),
                       session.liveInputs(), sources);

        const auto plan = std::make_shared<const magda::engine::RenderPlan>(
            magda::engine::compileRenderPlan(tracks, *master));

        magda::engine::PlanValues values;
        magda::engine::resolvePlanValues(*plan, tracks, *master, values);
        expect(session
                   .publish(plan, context, magda::engine::collectRuntimeStateIds(tracks, *master),
                            std::move(values))
                   .published,
               "The plan is published");

        session.publishClips(
            std::make_shared<const magda::engine::ClipSnapshot>(magda::engine::compileClipSnapshot(
                host::clipLanesFor(tracks), host::clipSources(), tempo)));
        session.publishTransport({.tempo = tempo, .request = {.generation = 1, .playing = true}});

        juce::AudioBuffer<float> output(2, context.maxBlockSize);
        for (auto block = 0; block < 43; ++block)
            session.process(context.maxBlockSize, output);

        // The key the host asks under has to be the one the compiler emitted,
        // which a null here is the only symptom of.
        auto* trackTap = session.meterTap(magda::engine::trackMeterKey(trackId));
        auto* masterTap = session.meterTap(magda::engine::trackMeterKey(magda::MASTER_TRACK_ID));
        expect(trackTap != nullptr && masterTap != nullptr, "Both meters are bound");
        if (trackTap == nullptr || masterTap == nullptr)
            return;

        expect(trackTap->read().loudest() > 0.0f, "The track's meter read the note");
        expect(masterTap->read().loudest() > 0.0f, "The master's meter read it too");

        // Destructive, which is what stops the frame rate deciding how much of
        // the signal a meter ever sees.
        expect(trackTap->read().loudest() == 0.0f, "A second read takes nothing twice");
    }

    /// A track with a Poly Synth on it, which is the shortest path from a note
    /// to a level. DeviceIds are unique within a section across the whole
    /// project, so two tracks cannot share one.
    static magda::TrackId synthTrack(const juce::String& name, magda::DeviceId deviceId,
                                     magda::InputMonitorMode monitor,
                                     const juce::String& midiInput) {
        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = trackManager.createTrack(name);
        auto* track = trackManager.getTrack(trackId);
        if (track == nullptr)
            return magda::INVALID_TRACK_ID;

        track->chain.fxChainElements.emplace_back(polySynth(deviceId));
        track->inputMonitor = monitor;
        track->midiInputDevice = midiInput;
        return trackId;
    }

    static juce::MidiBuffer noteOn(int note) {
        juce::MidiBuffer buffer;
        buffer.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
        return buffer;
    }

    void testAuditionReachesAnIdleTrack() {
        beginTest("A preview sounds on a track that is monitoring nothing");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = synthTrack("Instrument", 1, magda::InputMonitorMode::Off, {});
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(trackId != magda::INVALID_TRACK_ID && master != nullptr,
               "The track and the master exist");
        if (trackId == magda::INVALID_TRACK_ID || master == nullptr)
            return;

        const auto& tracks = trackManager.getTracks();
        const magda::engine::RenderContext context{.sampleRate = 44100.0, .maxBlockSize = 512};

        host::EngineFileReaders files;
        magda::engine::PrefetchThread reader(false);
        host::LiveMidiSources sources;
        host::EngineRuntimeFactory factory;
        factory.setModel(tracks, *master);

        magda::engine::ClipVoicePool voices(files, reader, context);
        magda::engine::EngineSession session(factory, nullptr, &voices);
        factory.attach(session.clipFeed(), voices.feed(), session.launchHandleFeed(),
                       session.liveInputs(), sources);
        session.liveInputs().prepare(0, context.maxBlockSize);

        // Without the option the track is neither armed nor monitoring, so
        // there is no input op for the preview to reach (#2579).
        const auto plan = std::make_shared<const magda::engine::RenderPlan>(
            magda::engine::compileRenderPlan(tracks, *master, {.auditionMidi = true}));

        magda::engine::PlanValues values;
        magda::engine::resolvePlanValues(*plan, tracks, *master, values);
        expect(session
                   .publish(plan, context, magda::engine::collectRuntimeStateIds(tracks, *master),
                            std::move(values))
                   .published,
               "The plan is published");

        const auto played = noteOn(60);
        const std::array<magda::engine::LiveMidiStream, 1> streams{
            magda::engine::LiveMidiStream{sources.auditionSourceFor(trackId), &played}};

        juce::AudioBuffer<float> output(2, context.maxBlockSize);
        session.process(context.maxBlockSize, output, {{}, streams});
        for (auto block = 0; block < 43; ++block)
            session.process(context.maxBlockSize, output);

        auto* trackTap = session.meterTap(magda::engine::trackMeterKey(trackId));
        auto* masterTap = session.meterTap(magda::engine::trackMeterKey(magda::MASTER_TRACK_ID));
        expect(trackTap != nullptr && masterTap != nullptr, "Both meters are bound");
        if (trackTap == nullptr || masterTap == nullptr)
            return;

        expect(trackTap->read().loudest() > 0.0f, "The note played on the track sounded");
        expect(masterTap->read().loudest() > 0.0f, "And the master heard it");
    }

    void testDeviceMidiReachesOnlyItsOwnTrack() {
        beginTest("A device's live MIDI sounds on the track routed to it and on no other");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto routed = synthTrack("Monitoring", 1, magda::InputMonitorMode::In, "all");
        const auto idle = synthTrack("Idle", 2, magda::InputMonitorMode::Off, {});
        // Monitoring with no device named, which the fork routes to every
        // input (MidiInputRouter::updateMidiInputRouting) and so must this.
        const auto unnamed = synthTrack("Unnamed", 3, magda::InputMonitorMode::In, {});
        // Auto and unarmed: routed on the fork, where TE's monitor mode still
        // decides what is heard, and audible here the moment it is in a route
        // table.
        const auto automatic = synthTrack("Auto", 4, magda::InputMonitorMode::Auto, "all");
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(routed != magda::INVALID_TRACK_ID && idle != magda::INVALID_TRACK_ID &&
                   unnamed != magda::INVALID_TRACK_ID && automatic != magda::INVALID_TRACK_ID &&
                   master != nullptr,
               "The tracks and the master exist");
        if (routed == magda::INVALID_TRACK_ID || idle == magda::INVALID_TRACK_ID ||
            unnamed == magda::INVALID_TRACK_ID || automatic == magda::INVALID_TRACK_ID ||
            master == nullptr)
            return;

        const auto& tracks = trackManager.getTracks();
        const magda::engine::RenderContext context{.sampleRate = 44100.0, .maxBlockSize = 512};

        host::EngineFileReaders files;
        magda::engine::PrefetchThread reader(false);

        // The device list is what an "all" route resolves against, so a
        // device a test plays has to be in one.
        host::LiveMidiSources sources;
        const juce::MidiDeviceInfo keyboard{"Test Device", "test-device"};
        sources.registerAvailableDevices({keyboard});
        const auto device = sources.sourceFor(keyboard.identifier);

        host::EngineRuntimeFactory factory;
        factory.setModel(tracks, *master);

        magda::engine::ClipVoicePool voices(files, reader, context);
        magda::engine::EngineSession session(factory, nullptr, &voices);
        factory.attach(session.clipFeed(), voices.feed(), session.launchHandleFeed(),
                       session.liveInputs(), sources);
        session.liveInputs().prepare(0, context.maxBlockSize);

        const auto plan = std::make_shared<const magda::engine::RenderPlan>(
            magda::engine::compileRenderPlan(tracks, *master, {.auditionMidi = true}));

        magda::engine::PlanValues values;
        magda::engine::resolvePlanValues(*plan, tracks, *master, values);
        expect(session
                   .publish(plan, context, magda::engine::collectRuntimeStateIds(tracks, *master),
                            std::move(values))
                   .published,
               "The plan is published");

        const auto played = noteOn(60);
        const std::array<magda::engine::LiveMidiStream, 1> streams{
            magda::engine::LiveMidiStream{device, &played}};

        juce::AudioBuffer<float> output(2, context.maxBlockSize);
        session.process(context.maxBlockSize, output, {{}, streams});
        for (auto block = 0; block < 43; ++block)
            session.process(context.maxBlockSize, output);

        auto* routedTap = session.meterTap(magda::engine::trackMeterKey(routed));
        auto* idleTap = session.meterTap(magda::engine::trackMeterKey(idle));
        auto* unnamedTap = session.meterTap(magda::engine::trackMeterKey(unnamed));
        auto* autoTap = session.meterTap(magda::engine::trackMeterKey(automatic));
        expect(routedTap != nullptr && idleTap != nullptr && unnamedTap != nullptr &&
                   autoTap != nullptr,
               "The meters are bound");
        if (routedTap == nullptr || idleTap == nullptr || unnamedTap == nullptr ||
            autoTap == nullptr)
            return;

        expect(routedTap->read().loudest() > 0.0f, "The track routed to the device heard it");
        expect(unnamedTap->read().loudest() > 0.0f,
               "So did the track monitoring with no device named");

        // The audition op every track now carries reads its own source alone;
        // kAnyLiveMidiSource here would have merged the two.
        expect(idleTap->read().loudest() == 0.0f, "The track monitoring nothing did not");

        // What monitorsInput() gates and receivesLiveMidiInput() does not: Auto
        // without an arm is the UI's activity light, not an audible input.
        expect(autoTap->read().loudest() == 0.0f, "Nor did the unarmed Auto track");

        // The store keeps the input it built, so a monitor switched on after
        // the publish reaches it through the route table, not a new source.
        if (auto* track = trackManager.getTrack(idle))
            track->inputMonitor = magda::InputMonitorMode::In;
        factory.refreshMidiRoutes(trackManager.getTracks());

        session.process(context.maxBlockSize, output, {{}, streams});
        for (auto block = 0; block < 43; ++block)
            session.process(context.maxBlockSize, output);

        expect(idleTap->read().loudest() > 0.0f,
               "Switched to monitor In after the publish, the track hears the device");
    }

    void testRouteRemovalPanicsTheInput() {
        beginTest("A source leaving a route table panics the input that read it");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = synthTrack("Monitoring", 1, magda::InputMonitorMode::In, "all");
        const auto* master = trackManager.getTrack(magda::MASTER_TRACK_ID);
        expect(trackId != magda::INVALID_TRACK_ID && master != nullptr,
               "The track and the master exist");
        if (trackId == magda::INVALID_TRACK_ID || master == nullptr)
            return;

        const magda::engine::RenderContext context{.sampleRate = 44100.0, .maxBlockSize = 512};

        host::EngineFileReaders files;
        magda::engine::PrefetchThread reader(false);
        host::LiveMidiSources sources;
        sources.registerAvailableDevices({juce::MidiDeviceInfo{"Test Device", "test-device"}});

        host::EngineRuntimeFactory factory;
        factory.setModel(trackManager.getTracks(), *master);

        magda::engine::ClipVoicePool voices(files, reader, context);
        magda::engine::EngineSession session(factory, nullptr, &voices);
        factory.attach(session.clipFeed(), voices.feed(), session.launchHandleFeed(),
                       session.liveInputs(), sources);
        session.liveInputs().prepare(0, context.maxBlockSize);

        // The input the store would bind, over the same route table the
        // publisher rewrites.
        auto input = factory.createMidiInput(trackId);
        expect(input != nullptr, "The track has a live MIDI input");
        if (input == nullptr)
            return;

        const magda::engine::BlockInfo block{};
        juce::MidiBuffer events;

        input->render(block, events);
        expect(!input->raisedAllNotesOff(), "A block with the route unchanged raises nothing");

        // A note-off for whatever the device is holding will never arrive
        // through a route that has gone, and the mutation publishes no
        // topology, so nothing else can panic the instrument.
        if (auto* track = trackManager.getTrack(trackId))
            track->inputMonitor = magda::InputMonitorMode::Off;
        factory.refreshMidiRoutes(trackManager.getTracks());

        events.clear();
        input->render(block, events);
        expect(input->raisedAllNotesOff(), "Losing the route panics the block that follows it");

        events.clear();
        input->render(block, events);
        expect(!input->raisedAllNotesOff(), "Once, and not on every block after it");

        // Adding one back takes nothing away, so a chord held on another
        // source keeps sounding.
        if (auto* track = trackManager.getTrack(trackId))
            track->inputMonitor = magda::InputMonitorMode::In;
        factory.refreshMidiRoutes(trackManager.getTracks());

        events.clear();
        input->render(block, events);
        expect(!input->raisedAllNotesOff(), "A source arriving raises no panic");
    }

    void testDeviceConnectedAfterAPublishResolves() {
        beginTest("A device plugged in after the publish resolves to the source it pushes under");

        host::LiveMidiSources sources;
        sources.registerAvailableDevices({});

        const juce::MidiDeviceInfo keystep{"Keystep", "juce-identifier-42"};

        // What the fork's selectors store for a hardware input, which is what
        // a project holds and what a route names.
        const auto forkId = "midiin_" + juce::String::toHexString(keystep.identifier.hashCode());

        // Against the empty snapshot the route is unresolvable, so it falls
        // through to a source of its own -- and events from the device arrive
        // under its JUCE identifier, which is a different one.
        expect(sources.resolveRoute(forkId) != sources.sourceFor(keystep.identifier),
               "Before the refresh the route and the device are two sources");

        sources.registerAvailableDevices({keystep});

        expect(sources.resolveRoute(forkId) == sources.sourceFor(keystep.identifier),
               "Refreshed, the route resolves to the source its notes arrive under");

        // The QWERTY keyboard is in no device list there is, so an "all" route
        // holds it through every scan.
        const auto qwerty = sources.registerVirtualDevice(magda::qwertyMidiDeviceId());
        const auto connected = sources.deviceSources();
        expect(std::ranges::find(connected, qwerty) != connected.end() && connected.size() == 2,
               "An all route is the connected device and the virtual one");

        sources.registerAvailableDevices({});
        const auto unplugged = sources.deviceSources();

        // What raises the panic: the source leaves the route table, and the
        // note-off for whatever it was holding is never coming.
        expect(unplugged.size() == 1 && unplugged.front() == qwerty,
               "Unplugged, it leaves the all route and the keyboard stays");
    }

    void testRouteChangesAreAPlanChange() {
        beginTest("What the compiler reads for a track's input is what a values publish watches");

        auto& trackManager = magda::TrackManager::getInstance();
        const auto trackId = synthTrack("Instrument", 1, magda::InputMonitorMode::Off, {});
        auto* track = trackManager.getTrack(trackId);
        expect(track != nullptr, "The track exists");
        if (track == nullptr)
            return;

        const auto compiledFrom = host::inputRoutingOf(trackManager.getTracks());

        // A mixer move is what a values publish is for.
        track->volume = 0.5f;
        expect(host::inputRoutingOf(trackManager.getTracks()) == compiledFrom,
               "A fader move is not a routing change");

        // Each of these is an edge or an op the plan holds, so none of them can
        // be carried by a values publish.
        track->midiInputDevice = "track:2";
        expect(host::inputRoutingOf(trackManager.getTracks()) != compiledFrom, "A MIDI route is");

        track->midiInputDevice = "";
        track->audioInputDevice = "Input 1";
        expect(host::inputRoutingOf(trackManager.getTracks()) != compiledFrom,
               "So is an audio input");

        track->audioInputDevice = "";
        track->inputMonitor = magda::InputMonitorMode::In;
        expect(host::inputRoutingOf(trackManager.getTracks()) != compiledFrom,
               "So is the monitor switch that decides whether either is read");
    }

    void testOnlyTrackMetersAreTapped() {
        beginTest("A meter nobody collects is declined");

        host::EngineRuntimeFactory factory;

        magda::engine::OpKey deviceMeter;
        deviceMeter.trackId = 1;
        deviceMeter.deviceId = 1;
        deviceMeter.role = magda::engine::OpRole::DeviceMeter;

        expect(factory.createMeter(magda::engine::trackMeterKey(1)) != nullptr,
               "A track's output level is read");
        expect(factory.createMeter(deviceMeter) == nullptr,
               "A device slot's is not: the chain UI reads DeviceMeteringManager");
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
