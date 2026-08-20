#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "param/ModRuntime.hpp"
#include "param/ParamTableCompiler.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_mod_feeds.cpp
 * @brief What reaches a modifier from outside the parameter system (#2120).
 *
 * Slice 4 built the trigger calls and had nothing to call them. This is the
 * two feeds that do, measured end to end through a session, because what they
 * are about is timing and timing is not visible from a call site.
 *
 * The claim under test is the distance rather than the gap. Both feeds
 * eventually reach their modifier; what differs is how many blocks later, and
 * that difference is a consequence of resolving parameters at the top of a
 * block rather than a detail of how the feed was written.
 *
 *  - A note is known before the block resolves, because the ops that produce
 *    MIDI read clips and input queues rather than audio. The prefix renders
 *    them first, so the modifier is resolved with the note that arrived in
 *    this block: distance zero.
 *  - A level is not, because it is what the ops are about to produce. The
 *    detector runs during the walk and the resolve that reads it is the next
 *    block's: distance one, always exactly one.
 *
 * The fork's ordinary MIDI gate is the later of the two as well (its
 * ADSRModifier reads a gate its applyToBuffer set during the previous block),
 * and its monitor path is the earlier. So the first of these is the fork's best
 * case made unconditional, and the second is its ordinary case.
 */

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::compileParamTable;
using magda::engine::compileRenderPlan;
using magda::engine::ModRuntime;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

constexpr double kSampleRate = 48000.0;

/// A quarter of a second, so a hundred-millisecond envelope stage is a few
/// blocks and a difference of one block is unmistakable.
constexpr int kBlock = 512;

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    track.mods = createDefaultMods(0);
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID);
    master.type = TrackType::Master;
    master.audioOutputDevice = {};
    master.volume = 1.0f;
    return master;
}

/// A device whose output is its only parameter, so what leaves the master is
/// what the modulation did to it.
class LevelDevice final : public magda::engine::EngineDevice {
  public:
    void process(magda::engine::DeviceBlock& block) override {
        block.audio.fill(block.params.size() > 0 ? block.params[0].value() : 0.0f);
    }
};

/// What the test queues for the next block, and what it saw of the blocks that
/// consumed it. Owned by the test rather than by the session, because the point
/// of the case is to put a note in one specific block.
struct ScriptedMidi {
    juce::MidiBuffer pending;
    int blocks = 0;
};

/// The session's end of it: a source that hands over whatever is queued and
/// clears the queue, so a note is played once.
class ScriptedMidiSource final : public magda::engine::EngineMidiSource {
  public:
    explicit ScriptedMidiSource(ScriptedMidi* script) : script_(script) {}

    void render(const BlockInfo&, juce::MidiBuffer& out) override {
        if (script_ == nullptr)
            return;

        out.addEvents(script_->pending, 0, -1, 0);
        script_->pending.clear();
        ++script_->blocks;
    }

  private:
    ScriptedMidi* script_ = nullptr;
};

class Factory final : public magda::engine::RuntimeStateFactory {
  public:
    explicit Factory(ScriptedMidi* midi) : midi_(midi) {}

    std::unique_ptr<magda::engine::EngineDevice> createDevice(magda::engine::DeviceKey) override {
        return std::make_unique<LevelDevice>();
    }

    std::unique_ptr<magda::engine::EngineMidiSource> createMidiInput(TrackId) override {
        return std::make_unique<ScriptedMidiSource>(midi_);
    }

  private:
    ScriptedMidi* midi_ = nullptr;
};

DeviceInfo makeDevice(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    device.mods = createDefaultMods(0);

    ParameterInfo info(0, "Level", "", 0.0f, 1.0f, 0.0f);
    info.currentValue = 0.0f;
    device.parameters.push_back(info);

    return device;
}

/// A track with a device and one modifier of @p type wired to its parameter.
TrackInfo trackWithModifier(ModType type, LFOTriggerMode trigger) {
    auto track = makeTrack(1);
    track.volume = 1.0f;
    track.pan = 0.0f;
    track.chain.fxChainElements.push_back(makeDeviceElement(makeDevice(7)));
    track.mods = createDefaultMods(1);
    track.mods[0].type = type;
    track.mods[0].triggerMode = trigger;

    // Not running, so a note-triggered modifier starts shut. What is being
    // measured is which block opens it, and one that was already open would
    // measure nothing.
    track.mods[0].running = false;

    // Instant up, held there: what is being measured is which block the
    // envelope opens in, so the stage lengths must not be part of the answer.
    track.mods[0].envAttackMs = 0.0f;
    track.mods[0].envDecayMs = 0.0f;
    track.mods[0].envSustain = 1.0f;
    track.mods[0].envReleaseMs = 0.0f;

    track.mods[0].links.push_back(ModLink{
        ControlTarget::pluginParam(ChainNodePath::topLevelDevice(1, 7), 0), 1.0f, false, true});
    return track;
}

/// A session over @p tracks, with @p midi bound as track 1's live MIDI input.
struct Session {
    Session(std::vector<TrackInfo> trackList, ScriptedMidi* midi)
        : tracks(std::move(trackList)), factory(midi), session(factory) {
        master = makeMaster();
        plan = std::make_shared<const magda::engine::RenderPlan>(compileRenderPlan(tracks, master));

        magda::engine::PlanValues values;
        magda::engine::resolvePlanValues(*plan, tracks, master, values);

        const auto ids = magda::engine::collectRuntimeStateIds(tracks, master);
        const magda::engine::RenderContext context{kSampleRate, kBlock, 2};
        published = session.publish(plan, context, ids, std::move(values)).published;
    }

    /// One block, and the first sample the master produced.
    float render() {
        juce::AudioBuffer<float> output(2, kBlock);
        session.process(kBlock, output);
        return output.getSample(0, 0);
    }

    std::vector<TrackInfo> tracks;
    TrackInfo master;
    std::shared_ptr<const magda::engine::RenderPlan> plan;
    Factory factory;
    magda::engine::EngineSession session;
    bool published = false;
};

}  // namespace

TEST_CASE("The MIDI a block has is known before the block resolves", "[engine][mod][feeds]") {
    // The prefix is what makes a note-triggered modifier hear the note in the
    // block it arrived in. Which ops are in it is decided at prepare time, from
    // the plan: outputs all MIDI, and every producer already in the set.
    auto track = trackWithModifier(ModType::Envelope, LFOTriggerMode::MIDI);
    track.recordArmed = true;

    const std::vector<TrackInfo> tracks{track};
    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);

    // The track's chain-head MIDI is a merge of sources, so it is in the
    // prefix; the device that reads it is not, because it produces audio.
    bool sawMidiInput = false;
    bool sawDevice = false;
    for (const auto& op : plan.ops) {
        if (op.key.role == magda::engine::OpRole::TrackMidiInput ||
            op.key.role == magda::engine::OpRole::LiveMidiInput)
            sawMidiInput = true;
        if (op.kind == magda::engine::OpKind::Device)
            sawDevice = true;
    }

    CHECK(sawMidiInput);
    CHECK(sawDevice);
}

TEST_CASE("A note reaches its modifier in the block it arrived in", "[engine][mod][feeds]") {
    auto track = trackWithModifier(ModType::Envelope, LFOTriggerMode::MIDI);
    track.recordArmed = true;
    track.inputMonitor = InputMonitorMode::In;
    track.midiInputDevice = "test";

    ScriptedMidi midi;
    Session session({track}, &midi);
    REQUIRE(session.published);

    // Shut before the note: a MIDI-triggered envelope waits, and a modifier at
    // rest contributes nothing.
    CHECK(session.render() == approx(0.0f));

    // The note lands in the next block. The prefix renders the MIDI ops before
    // the parameters resolve, so the envelope opens in this block rather than
    // in the one after: distance zero.
    midi.pending.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    CHECK(session.render() == approx(1.0f));
}

TEST_CASE("A level reaches its modifier the block after the one it was in",
          "[engine][mod][feeds]") {
    // The follower's side of the same question. The source's audio does not
    // exist when the block resolves, so the detector runs during the walk and
    // the resolve that reads it is the next block's.
    auto track = trackWithModifier(ModType::Follower, LFOTriggerMode::Free);

    const std::vector<TrackInfo> tracks{track};
    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);
    const auto table = compileParamTable(plan, tracks, master, {});
    REQUIRE(table.modifiers.size() == 1);

    ModRuntime mods;
    mods.prepare(table, magda::engine::RenderContext{kSampleRate, kBlock, 2});

    BlockInfo block;
    block.numSamples = kBlock;

    magda::engine::ResolvedParams values;
    values.prepare(table.size());
    std::vector<magda::engine::ModContribution> links(
        static_cast<std::size_t>(std::max(table.maxLinksPerParam, 1)));
    std::vector<magda::engine::ParamSegment> segments(
        static_cast<std::size_t>(magda::engine::ResolvedParams::kDefaultSegmentCapacity));

    const auto resolve = [&] {
        magda::engine::resolveParams(table, values, links, segments, block, &mods);
        return mods.value(0);
    };

    // Nothing detected, nothing published.
    CHECK(resolve() == approx(0.0f));

    // The block's audio, handed over after the block resolved, which is where
    // the executor hands it over. This block's value was already settled, so
    // the level shows up in the next one.
    const std::vector<float> loud(static_cast<std::size_t>(kBlock), 1.0f);
    mods.detectSource(0, table, loud);
    CHECK(resolve() > 0.0f);
}

TEST_CASE("A modifier that listens to nothing carries no source", "[engine][mod][feeds]") {
    // The rule that decides whether an edge is emitted at all. A free-running
    // LFO on a sidechained device is not a reason to carry that track's audio
    // anywhere, and a plan that carried it would be paying for a dependency
    // nothing reads.
    const auto sourceOf = [](ModType type, LFOTriggerMode trigger) {
        const std::vector<TrackInfo> tracks{trackWithModifier(type, trigger)};
        const auto master = makeMaster();
        const auto plan = compileRenderPlan(tracks, master);
        const auto table = compileParamTable(plan, tracks, master, {});
        REQUIRE(table.modifiers.size() == 1);
        return table.modifiers.front().source;
    };

    CHECK(sourceOf(ModType::LFO, LFOTriggerMode::Free) == INVALID_TRACK_ID);
    CHECK(sourceOf(ModType::LFO, LFOTriggerMode::Transport) == INVALID_TRACK_ID);
    CHECK(sourceOf(ModType::LFO, LFOTriggerMode::MIDI) == 1);
    CHECK(sourceOf(ModType::LFO, LFOTriggerMode::Audio) == 1);

    // A follower listens whatever its trigger mode says: the trigger fields
    // belong to the kinds that have a phase.
    CHECK(sourceOf(ModType::Follower, LFOTriggerMode::Free) == 1);
}

TEST_CASE("A note only reaches the modifiers waiting for one", "[engine][mod][feeds]") {
    // One list of listeners per track, sorted by what each is waiting for, so
    // an audio-triggered modifier is not opened by a note that happened to
    // reach the same track.
    const std::vector<TrackInfo> tracks{trackWithModifier(ModType::LFO, LFOTriggerMode::Audio)};
    const auto master = makeMaster();
    const auto plan = compileRenderPlan(tracks, master);
    const auto table = compileParamTable(plan, tracks, master, {});
    REQUIRE(table.modifiers.size() == 1);

    ModRuntime mods;
    mods.prepare(table, magda::engine::RenderContext{kSampleRate, kBlock, 2});

    CHECK(mods.listensFor(0, table) == magda::engine::ModListen::Audio);

    const auto listeners = mods.listenersOf(1);
    REQUIRE(listeners.size() == 1);
    CHECK(listeners.front() == 0);
}
