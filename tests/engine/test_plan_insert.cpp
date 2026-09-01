#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanLayout.hpp"
#include "exec/PlanValues.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file test_plan_insert.cpp
 * @brief A hardware insert as a send op and a return op (#2245).
 *
 * The whole claim of this slice is that an insert is not a special case. The
 * incumbent recognises one by where it sits in a chain and wires it there; the
 * plan already had ops that consume a signal and ops that produce one, so an
 * insert is one of each with the outside world between them.
 *
 * What that has to buy, and what these tests are for, is the alignment. There
 * is no insert-shaped code in the latency pass: the return op declares a round
 * trip the way a device declares its latency, and the same pass compensates it.
 * A chain with an insert lines up with a chain without one because nothing
 * doing the lining up knows what an insert is.
 */

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::DeviceKey;
using magda::engine::EngineInsert;
using magda::engine::OpId;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanValues;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;

namespace {

constexpr int kBlockSize = 64;

TrackInfo makeTrack(TrackId id) {
    TrackInfo track;
    track.id = id;
    track.type = TrackType::Media;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    return track;
}

TrackInfo makeMaster() {
    TrackInfo master;
    master.id = MASTER_TRACK_ID;
    master.type = TrackType::Master;
    master.name = "Master";
    return master;
}

DeviceInfo makeEffect(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    return device;
}

/// An external effect: audio out, audio back. The commonest insert there is,
/// and the one whose two ends are the same shape.
DeviceInfo makeExternalFx(DeviceId id) {
    auto device = makeEffect(id);
    device.name = "External FX";
    device.insert.sendType = InsertConfig::Endpoint::Audio;
    device.insert.returnType = InsertConfig::Endpoint::Audio;
    device.insert.sendDevice = "Out 3-4";
    device.insert.returnDevice = "In 3-4";
    return device;
}

/// An external instrument: MIDI out, audio back. The configuration whose ends
/// are not the same shape, which is the one that says the pair is two ops
/// rather than one op used twice.
DeviceInfo makeExternalInstrument(DeviceId id) {
    auto device = makeEffect(id);
    device.name = "External Instrument";
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.insert.sendType = InsertConfig::Endpoint::MIDI;
    device.insert.returnType = InsertConfig::Endpoint::Audio;
    device.insert.sendDevice = "MIDI Out 1";
    device.insert.returnDevice = "In 5-6";
    return device;
}

std::vector<OpId> opsWithRole(const RenderPlan& plan, OpRole role) {
    std::vector<OpId> found;
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (plan.ops[i].key.role == role)
            found.push_back(static_cast<OpId>(i));
    return found;
}

bool mentions(const std::vector<std::string>& messages, const std::string& fragment) {
    for (const auto& message : messages)
        if (message.find(fragment) != std::string::npos)
            return true;
    return false;
}

/// An insert with a declared round trip and nothing behind it. What it does
/// with the block does not matter to the plan tests; what it reports does.
class StubInsert final : public EngineInsert {
  public:
    explicit StubInsert(int latency = 0, float returned = 0.0f)
        : latency_(latency), returned_(returned) {}

    int latencySamples() const override {
        return latency_;
    }

    void send(const BlockInfo&, juce::dsp::AudioBlock<const float> audio,
              const juce::MidiBuffer& midi) override {
        sentAudioChannels = static_cast<int>(audio.getNumChannels());
        sentAudio =
            sentAudioChannels > 0 && audio.getNumSamples() > 0 ? audio.getSample(0, 0) : 0.0f;
        sentMidiEvents = midi.getNumEvents();
        ++sends;
    }

    void receive(const BlockInfo&, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override {
        ++receives;

        if (audio.getNumChannels() > 0)
            audio.fill(returned_);
        else
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
    }

    int sends = 0;
    int receives = 0;
    float sentAudio = 0.0f;
    int sentAudioChannels = 0;
    int sentMidiEvents = 0;

  private:
    int latency_ = 0;
    float returned_ = 0.0f;
};

}  // namespace

TEST_CASE("An insert compiles to a send and a return rather than a device",
          "[engine][plan][insert]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeExternalFx(7)));

    std::vector<TrackInfo> tracks{track};
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    INFO(magda::engine::dumpPlan(plan));

    const auto sends = opsWithRole(plan, OpRole::InsertSend);
    const auto returns = opsWithRole(plan, OpRole::InsertReturn);
    REQUIRE(sends.size() == 1);
    REQUIRE(returns.size() == 1);

    // And no device op at all, which is the claim: the plan has no
    // insert-shaped Device in it that something downstream has to know about.
    CHECK(opsWithRole(plan, OpRole::DeviceProcess).empty());

    const auto& sendOp = plan.ops[static_cast<std::size_t>(sends.front())];
    const auto& returnOp = plan.ops[static_cast<std::size_t>(returns.front())];

    CHECK(sendOp.kind == OpKind::InsertSend);
    CHECK(returnOp.kind == OpKind::InsertReturn);

    // A sink: what it writes leaves the machine.
    CHECK(sendOp.outputs.empty());

    // A source: what comes back is not derived from anything in the plan.
    CHECK(returnOp.inputs.empty());
    REQUIRE(returnOp.outputs.size() == 1);
    CHECK(returnOp.outputs.front().kind == magda::engine::SignalKind::Audio);

    // The audio the chain was carrying is what goes out.
    CHECK(sendOp.inputs[0].valid());
    CHECK_FALSE(sendOp.inputs[1].valid());

    CHECK(plan.diagnostics.empty());
}

TEST_CASE("The chain carries on from what came back", "[engine][plan][insert]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeExternalFx(7)));
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));

    std::vector<TrackInfo> tracks{track};
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    INFO(magda::engine::dumpPlan(plan));

    const auto returns = opsWithRole(plan, OpRole::InsertReturn);
    REQUIRE(returns.size() == 1);

    // The effect after the insert reads the return, not whatever the chain was
    // carrying before it. An insert that left the chain flowing past would be
    // an insert nobody could hear.
    const auto devices = opsWithRole(plan, OpRole::DeviceProcess);
    REQUIRE(devices.size() == 1);
    CHECK(plan.ops[static_cast<std::size_t>(devices.front())].inputs[0].op == returns.front());
}

TEST_CASE("An external instrument sends MIDI and gets audio back", "[engine][plan][insert]") {
    // The configuration whose two ends are different shapes, which is why the
    // return declares its port from what comes back rather than from what went
    // out.
    auto track = makeTrack(1);
    track.midiInputDevice = "all";
    track.chain.fxChainElements.push_back(makeDeviceElement(makeExternalInstrument(7)));

    std::vector<TrackInfo> tracks{track};
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    INFO(magda::engine::dumpPlan(plan));

    const auto sends = opsWithRole(plan, OpRole::InsertSend);
    const auto returns = opsWithRole(plan, OpRole::InsertReturn);
    REQUIRE(sends.size() == 1);
    REQUIRE(returns.size() == 1);

    const auto& sendOp = plan.ops[static_cast<std::size_t>(sends.front())];
    const auto& returnOp = plan.ops[static_cast<std::size_t>(returns.front())];

    // MIDI out, audio back.
    CHECK_FALSE(sendOp.inputs[0].valid());
    CHECK(sendOp.inputs[1].valid());
    CHECK(returnOp.outputs.front().kind == magda::engine::SignalKind::Audio);

    CHECK(plan.diagnostics.empty());
}

TEST_CASE("A MIDI return replaces the chain's MIDI and leaves its audio alone",
          "[engine][plan][insert]") {
    auto insert = makeExternalFx(7);
    insert.insert.returnType = InsertConfig::Endpoint::MIDI;
    insert.insert.returnDevice = "MIDI In 1";

    auto track = makeTrack(1);
    track.midiInputDevice = "all";
    track.chain.fxChainElements.push_back(makeDeviceElement(insert));

    std::vector<TrackInfo> tracks{track};
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    INFO(magda::engine::dumpPlan(plan));

    const auto returns = opsWithRole(plan, OpRole::InsertReturn);
    REQUIRE(returns.size() == 1);
    CHECK(plan.ops[static_cast<std::size_t>(returns.front())].outputs.front().kind ==
          magda::engine::SignalKind::Midi);
}

TEST_CASE("A half configured insert is reported rather than guessed at", "[engine][plan][insert]") {
    SECTION("an end with a type and no device named") {
        auto insert = makeExternalFx(7);
        insert.insert.returnDevice = {};

        auto track = makeTrack(1);
        track.chain.fxChainElements.push_back(makeDeviceElement(insert));

        std::vector<TrackInfo> tracks{track};
        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        CHECK(mentions(plan.diagnostics, "the return names no hardware device"));
    }

    SECTION("a send with nothing coming back") {
        auto insert = makeExternalFx(7);
        insert.insert.returnType = InsertConfig::Endpoint::None;
        insert.insert.returnDevice = {};

        auto track = makeTrack(1);
        track.chain.fxChainElements.push_back(makeDeviceElement(insert));
        track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));

        std::vector<TrackInfo> tracks{track};
        const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
        INFO(magda::engine::dumpPlan(plan));

        CHECK(mentions(plan.diagnostics, "declares no return"));

        // The send still happens, because audio really does leave the machine,
        // and the chain carries on with what it had. That is the failure that
        // leaves a person still hearing their track; the diagnostic is what
        // stops it reading as an insert that worked.
        CHECK(opsWithRole(plan, OpRole::InsertSend).size() == 1);
        CHECK(opsWithRole(plan, OpRole::InsertReturn).empty());
    }
}

TEST_CASE("A bypassed insert is not in the plan at all", "[engine][plan][insert]") {
    auto insert = makeExternalFx(7);
    insert.bypassed = true;

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(insert));

    std::vector<TrackInfo> tracks{track};
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());

    // The same thing bypass does to a device: the op is not emitted and the
    // chain flows past. Nothing leaves the machine, which is what a person
    // bypassing an insert is asking for.
    CHECK(opsWithRole(plan, OpRole::InsertSend).empty());
    CHECK(opsWithRole(plan, OpRole::InsertReturn).empty());
    CHECK(plan.diagnostics.empty());
}

TEST_CASE("An insert's round trip is compensated by the same pass as everything else",
          "[engine][plan][insert][latency]") {
    // The claim this slice rests on. Two tracks meeting at the master, one of
    // them through an insert with a round trip: the other one has to be held
    // back by exactly that, and nothing in the latency pass knows what an
    // insert is.
    auto withInsert = makeTrack(1);
    withInsert.chain.fxChainElements.push_back(makeDeviceElement(makeExternalFx(7)));

    std::vector<TrackInfo> tracks{withInsert, makeTrack(2)};
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    INFO(magda::engine::dumpPlan(plan));

    const auto returns = opsWithRole(plan, OpRole::InsertReturn);
    REQUIRE(returns.size() == 1);

    constexpr int kRoundTrip = 512;

    std::vector<int> latency(plan.ops.size(), 0);
    latency[static_cast<std::size_t>(returns.front())] = kRoundTrip;

    const auto resolved = magda::engine::resolveLayout(plan, latency);

    // The insert's own path carries the round trip.
    const auto portOf = [&](OpId op) {
        return static_cast<std::size_t>(resolved.portOffsets[static_cast<std::size_t>(op)]);
    };
    CHECK(resolved.latency.portLatency[portOf(returns.front())] == kRoundTrip);

    // And the track that never went near the hardware is delayed to meet it.
    // Asserted as a total rather than op by op, because which edge the delay
    // lands on is the compiler's business: what a person hears is that the two
    // tracks arrive together.
    auto longestDelay = 0;
    for (const auto samples : resolved.latency.delaySamples)
        longestDelay = std::max(longestDelay, samples);

    CHECK(longestDelay == kRoundTrip);
    CHECK(resolved.latency.outputLatency == kRoundTrip);
}

TEST_CASE("A chain with no insert in it is not delayed by one", "[engine][plan][insert][latency]") {
    // The mirror of the case above, and it is worth stating: a latency pass
    // that compensated an insert by always holding something back would pass
    // that test and be wrong.
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());

    const auto resolved = magda::engine::resolveLayout(plan, std::vector<int>(plan.ops.size(), 0));

    for (const auto samples : resolved.latency.delaySamples)
        CHECK(samples == 0);
    CHECK(resolved.latency.outputLatency == 0);
}

TEST_CASE("An insert with nothing behind it is reported", "[engine][plan][insert]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeExternalFx(7)));

    std::vector<TrackInfo> tracks{track};
    auto master = makeMaster();
    const auto plan = magda::engine::compileRenderPlan(tracks, master);

    PlanExecutor executor;
    const auto messages = executor.prepare(plan, PlanBindings{}, RenderContext{44100.0, 64, 2});

    // Unlike a device that failed to load, which passes audio through: there is
    // no passing through an insert. A render that went ahead silently would be
    // a render of a project with the outboard unplugged, and nothing in the
    // audio would say so.
    CHECK(mentions(messages, "no insert bound"));
}

TEST_CASE("What the chain carries goes out, and what comes back is what it carries on with",
          "[engine][exec][insert]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeExternalFx(7)));

    std::vector<TrackInfo> tracks{track};
    auto master = makeMaster();
    const auto plan = magda::engine::compileRenderPlan(tracks, master);
    INFO(magda::engine::dumpPlan(plan));

    StubInsert insert(0, 0.25f);
    PlanBindings bindings;
    bindings.inserts[DeviceKey{ChainSegment::Fx, 7}] = &insert;

    PlanValues values;
    const auto valueMessages = magda::engine::resolvePlanValues(plan, tracks, master, values);
    CHECK(valueMessages.empty());

    const RenderContext context{44100.0, kBlockSize, 2};
    PlanExecutor executor;
    // The track has no clips, so the clip source is unbound and says so. That
    // is about this fixture rather than about the insert, and the insert is
    // what has to have nothing to report.
    const auto messages = executor.prepare(plan, bindings, context);
    CHECK_FALSE(mentions(messages, "insert"));

    juce::AudioBuffer<float> output(2, kBlockSize);
    output.clear();

    BlockInfo block;
    block.numSamples = kBlockSize;
    block.playing = true;

    executor.process(values, block, output);

    // Both halves ran, once each.
    CHECK(insert.sends == 1);
    CHECK(insert.receives == 1);

    // And what came back is what reached the output, rather than the silence
    // the track was carrying into the insert.
    CHECK(output.getSample(0, 0) == Catch::Approx(0.25f).margin(1e-5));
}
