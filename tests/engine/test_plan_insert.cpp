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
#include "io/CapturedInsert.hpp"
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

// =============================================================================
// Insert capture, which is how a bounce runs one (#2245)
// =============================================================================

namespace {

using magda::engine::CapturedInsert;
using magda::engine::RenderContext;

/// A live insert that answers with a ramp keyed to where the block is, so a
/// replay can be checked against the position it claims to be replaying rather
/// than against a constant that would match anywhere.
class RampingInsert final : public EngineInsert {
  public:
    explicit RampingInsert(int latency = 0) : latency_(latency) {}

    int latencySamples() const override {
        return latency_;
    }

    void send(const BlockInfo&, juce::dsp::AudioBlock<const float>,
              const juce::MidiBuffer&) override {
        ++sends;
    }

    void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override {
        const auto first = static_cast<float>(std::llround(block.startSeconds * 44100.0));

        for (std::size_t channel = 0; channel < audio.getNumChannels(); ++channel)
            for (std::size_t sample = 0; sample < audio.getNumSamples(); ++sample)
                audio.setSample(static_cast<int>(channel), static_cast<int>(sample),
                                (first + static_cast<float>(sample)) / 1000.0f);

        if (answersMidi)
            midi.addEvent(juce::MidiMessage::noteOn(1, static_cast<int>(first) % 128, 1.0f), 0);
    }

    bool answersMidi = false;
    int sends = 0;

  private:
    int latency_ = 0;
};

BlockInfo blockAtSample(std::int64_t first, int numSamples) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.playing = true;
    block.startSeconds = static_cast<double>(first) / 44100.0;
    block.endSeconds = static_cast<double>(first + numSamples) / 44100.0;
    return block;
}

/// A capture of [0, samples), taken in blocks of @p blockSize.
///
/// By pointer because a CapturedInsert holds an atomic and is therefore neither
/// copyable nor movable, which is the right shape for a thing an audio thread
/// writes and another thread reads.
std::unique_ptr<CapturedInsert> capture(RampingInsert& live, std::int64_t samples, int channels = 1,
                                        int blockSize = kBlockSize, double rate = 44100.0) {
    auto insert =
        std::make_unique<CapturedInsert>(CapturedInsert::Mode::Capturing, channels, &live);
    insert->setWindow({0, samples});
    insert->prepare(RenderContext{rate, blockSize, channels});

    juce::AudioBuffer<float> buffer(channels, blockSize);
    juce::MidiBuffer midi;

    for (std::int64_t at = 0; at < samples; at += blockSize) {
        const auto block = blockAtSample(at, blockSize);
        buffer.clear();
        midi.clear();
        insert->send(block, juce::dsp::AudioBlock<const float>(buffer), midi);
        insert->receive(block, juce::dsp::AudioBlock<float>(buffer), midi);
    }

    return insert;
}

}  // namespace

TEST_CASE("A capture replays what the hardware answered, at the position it answered it",
          "[engine][insert][capture]") {
    // The bounce path. An offline render cannot run the outside world faster
    // than real time, so it plays back what the live pass wrote down, and the
    // one thing that has to survive is where each sample was.
    RampingInsert live;
    auto insert = capture(live, 8 * kBlockSize);

    CHECK(live.sends == 8);
    CHECK(insert->covers(0, 8 * kBlockSize));

    // The bounce, over the same timeline in a different block size, which is
    // the case a capture indexed by call count would get wrong.
    constexpr int kRenderBlock = 96;
    insert->setMode(CapturedInsert::Mode::Playing);
    insert->prepare(RenderContext{44100.0, kRenderBlock, 1});

    juce::AudioBuffer<float> rendered(1, kRenderBlock);
    juce::MidiBuffer midi;
    rendered.clear();

    const auto at = blockAtSample(kBlockSize, kRenderBlock);
    insert->send(at, juce::dsp::AudioBlock<const float>(rendered), midi);
    insert->receive(at, juce::dsp::AudioBlock<float>(rendered), midi);

    // Nothing left the machine: there is nothing on the other end that could
    // answer at render speed, and a send that went out anyway would put the
    // render through somebody's monitors.
    CHECK(live.sends == 8);

    CHECK(rendered.getSample(0, 0) ==
          Catch::Approx(static_cast<float>(kBlockSize) / 1000.0f).margin(1e-5));
    CHECK(rendered.getSample(0, 10) ==
          Catch::Approx(static_cast<float>(kBlockSize + 10) / 1000.0f).margin(1e-5));
}

TEST_CASE("A replay declares the round trip the capture was taken through",
          "[engine][insert][capture]") {
    // The alignment a replay would otherwise lose. A capture holds what the
    // hardware answered rather than what was sent to it, so the samples at t
    // are the send from t minus the round trip; during the live pass the
    // latency pass delays every parallel path to meet that. A replay declaring
    // no latency would leave those paths undelayed against a return that is
    // still a round trip behind, and the insert's path would come back late by
    // the whole of it.
    constexpr int kRoundTrip = 512;
    RampingInsert live(kRoundTrip);

    auto insert = capture(live, 4 * kBlockSize);
    CHECK(insert->latencySamples() == kRoundTrip);

    insert->setMode(CapturedInsert::Mode::Playing);
    insert->prepare(RenderContext{44100.0, kBlockSize, 1});
    CHECK(insert->latencySamples() == kRoundTrip);
}

TEST_CASE("A MIDI return is captured and replayed", "[engine][insert][capture]") {
    // The executor hands a MIDI-returning insert an empty audio block, so a
    // capture that took its span from the audio would record nothing and the
    // bounce would replay a capture it believed was empty. An insert answering
    // with MIDI into an instrument downstream would work live and be silent in
    // the file.
    RampingInsert live;
    live.answersMidi = true;

    CapturedInsert insert(CapturedInsert::Mode::Capturing, 1, &live);
    insert.setWindow({0, 4 * kBlockSize});
    insert.prepare(RenderContext{44100.0, kBlockSize, 1});

    juce::MidiBuffer midi;
    for (std::int64_t at = 0; at < 4 * kBlockSize; at += kBlockSize) {
        midi.clear();
        insert.receive(blockAtSample(at, kBlockSize), {}, midi);
    }

    // Recorded even though not one audio channel arrived.
    CHECK(insert.covers(0, 4 * kBlockSize));

    insert.setMode(CapturedInsert::Mode::Playing);

    midi.clear();
    insert.receive(blockAtSample(kBlockSize, kBlockSize), {}, midi);

    REQUIRE(midi.getNumEvents() == 1);

    // At its position within the block it is being replayed into, which is
    // where a consumer expects an event to be.
    for (const auto metadata : midi) {
        CHECK(metadata.samplePosition == 0);
        CHECK(juce::MidiMessage(metadata.data, metadata.numBytes).getNoteNumber() ==
              kBlockSize % 128);
    }
}

TEST_CASE("A gap in the capture is not reported as covered", "[engine][insert][capture]") {
    // A high-water mark would call this complete. The live pass seeked over the
    // middle of the window, so the render would write that silence into a file
    // somebody keeps, and the check that was supposed to stop it would have
    // approved it.
    RampingInsert live;

    CapturedInsert insert(CapturedInsert::Mode::Capturing, 1, &live);
    insert.setWindow({0, 8 * kBlockSize});
    insert.prepare(RenderContext{44100.0, kBlockSize, 1});

    juce::AudioBuffer<float> buffer(1, kBlockSize);
    juce::MidiBuffer midi;

    // The first two blocks, then a jump to the last two.
    for (const std::int64_t at : {0, kBlockSize, 6 * kBlockSize, 7 * kBlockSize}) {
        buffer.clear();
        insert.receive(blockAtSample(at, kBlockSize), juce::dsp::AudioBlock<float>(buffer), midi);
    }

    CHECK(insert.covers(0, 2 * kBlockSize));
    CHECK(insert.covers(6 * kBlockSize, 2 * kBlockSize));

    // And the whole window, which is what a bounce actually asks.
    CHECK_FALSE(insert.covers(0, 8 * kBlockSize));
    CHECK_FALSE(insert.covers(2 * kBlockSize, kBlockSize));
}

TEST_CASE("A capture is sized to its window, not to a tape", "[engine][insert][capture]") {
    // Sizing for "long enough for anything" is a fifth of a gigabyte per stereo
    // insert at 44.1 kHz before anybody has asked for a render, and several
    // inserts would exhaust a machine merely by preparing.
    RampingInsert live;

    CapturedInsert insert(CapturedInsert::Mode::Capturing, 2, &live);
    insert.setWindow({44100, 44100});
    insert.prepare(RenderContext{44100.0, kBlockSize, 2});

    // Nothing outside the window is claimed, in either direction.
    CHECK_FALSE(insert.covers(0, 44100));
    CHECK_FALSE(insert.covers(44100, 44101));

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    juce::MidiBuffer midi;

    // A block before the window: written down for the part that overlaps and
    // no further, because a live pass runs over the whole arrangement and the
    // capture is one stretch of it.
    buffer.clear();
    insert.receive(blockAtSample(44100 - kBlockSize / 2, kBlockSize),
                   juce::dsp::AudioBlock<float>(buffer), midi);

    CHECK(insert.covers(44100, kBlockSize / 2));
    CHECK_FALSE(insert.covers(44100, kBlockSize));
}

TEST_CASE("A bounce past the end of the capture is silent, not the nearest thing captured",
          "[engine][insert][capture]") {
    // A range nobody played through the hardware has no answer, and inventing
    // one would put the wrong sound in a file somebody keeps.
    RampingInsert live;
    auto insert = capture(live, kBlockSize);

    insert->setMode(CapturedInsert::Mode::Playing);
    insert->prepare(RenderContext{44100.0, kBlockSize, 1});

    juce::AudioBuffer<float> buffer(1, kBlockSize);
    juce::MidiBuffer midi;
    buffer.clear();

    insert->receive(blockAtSample(kBlockSize * 4, kBlockSize), juce::dsp::AudioBlock<float>(buffer),
                    midi);

    for (int sample = 0; sample < kBlockSize; ++sample)
        CHECK(buffer.getSample(0, sample) == Catch::Approx(0.0f).margin(1e-9));
    CHECK(midi.getNumEvents() == 0);
}

TEST_CASE("A capture replayed at another rate is refused rather than shifted",
          "[engine][insert][capture][rate]") {
    // A capture is written during live playback at whatever rate the device is
    // open at, and read during a bounce whose rate is chosen at export. One
    // object, two rate domains.
    //
    // Positions and the round trip survive that, because both are held in
    // something both domains share. The samples do not: 44,100 numbers a second
    // handed one for one to something that wants 48,000 come out 8.8 per cent
    // slow and a tone and a half flat, and run out of window early. So this is
    // the one that is refused.
    RampingInsert live;
    auto insert = capture(live, 4 * kBlockSize, 1, kBlockSize, 44100.0);

    REQUIRE(insert->covers(0, 4 * kBlockSize));
    CHECK(insert->usable());

    insert->setMode(CapturedInsert::Mode::Playing);
    insert->prepare(RenderContext{48000.0, kBlockSize, 1});

    CHECK_FALSE(insert->usable());

    // Through covers(), because that is the preflight a bounce asks: a render
    // that went ahead would write slow, detuned audio into a file somebody
    // keeps, and nothing downstream could see it.
    CHECK_FALSE(insert->covers(0, 4 * kBlockSize));

    // And the same capture replayed at the rate it was taken at is fine, which
    // is what says the refusal is about the rates rather than about replaying.
    insert->prepare(RenderContext{44100.0, kBlockSize, 1});
    CHECK(insert->usable());
    CHECK(insert->covers(0, 4 * kBlockSize));
}

TEST_CASE("A replay reports the round trip as the render's own samples",
          "[engine][insert][capture][rate]") {
    // The round trip is a duration, not a count. Stored as 512 samples and
    // reported into a graph at another rate it would delay every parallel path
    // by a different length of time than the capture is actually behind.
    //
    // The rate guard above refuses a mismatched replay, so this is checked
    // through the arithmetic rather than through a render: what matters is that
    // the figure is derived from a duration, so that it stays right the day a
    // resampling replay makes a mismatch renderable.
    constexpr int kRoundTrip = 512;
    RampingInsert live(kRoundTrip);

    auto insert = capture(live, 4 * kBlockSize, 1, kBlockSize, 44100.0);
    CHECK(insert->latencySamples() == kRoundTrip);

    insert->setMode(CapturedInsert::Mode::Playing);
    insert->prepare(RenderContext{44100.0, kBlockSize, 1});
    CHECK(insert->latencySamples() == kRoundTrip);

    // 512 samples at 44.1 kHz is 11.61 ms, which is 557 samples at 48 kHz.
    insert->prepare(RenderContext{48000.0, kBlockSize, 1});
    CHECK(insert->latencySamples() == 557);
}

TEST_CASE("Progress is a counter rather than a walk", "[engine][insert][capture]") {
    // Polled from whatever thread is drawing it while the audio thread writes
    // the bitmap, so a walk of the bitmap would be a data race and O(window)
    // per poll besides.
    RampingInsert live;

    CapturedInsert insert(CapturedInsert::Mode::Capturing, 1, &live);
    insert.setWindow({0, 8 * kBlockSize});
    insert.prepare(RenderContext{44100.0, kBlockSize, 1});

    CHECK(insert.capturedSamples() == 0);

    juce::AudioBuffer<float> buffer(1, kBlockSize);
    juce::MidiBuffer midi;

    for (const std::int64_t at : {0, kBlockSize}) {
        buffer.clear();
        insert.receive(blockAtSample(at, kBlockSize), juce::dsp::AudioBlock<float>(buffer), midi);
    }

    CHECK(insert.capturedSamples() == 2 * kBlockSize);

    // A second pass over the same stretch is not more captured. Counted on the
    // transition, so a total can never claim more of the window than it holds.
    buffer.clear();
    insert.receive(blockAtSample(0, kBlockSize), juce::dsp::AudioBlock<float>(buffer), midi);
    CHECK(insert.capturedSamples() == 2 * kBlockSize);
}

TEST_CASE("MIDI past the reserved capacity makes the capture unusable",
          "[engine][insert][capture]") {
    // ensureSize is a reservation and addEvent grows past it, which on the
    // audio thread is an allocation. And a capture that allocated its way out
    // would still be one with events missing from it, reporting success: the
    // bounce would be short of notes with nothing to say so.
    class ChattyInsert final : public EngineInsert {
      public:
        void send(const BlockInfo&, juce::dsp::AudioBlock<const float>,
                  const juce::MidiBuffer&) override {}

        void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                     juce::MidiBuffer& midi) override {
            audio.clear();

            // A wall of events, which is what a controller dump or a long
            // SysEx stream looks like from in here.
            for (int index = 0; index < block.numSamples; ++index)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 7, index % 128), index);
        }
    };

    ChattyInsert live;

    CapturedInsert insert(CapturedInsert::Mode::Capturing, 1, &live);
    insert.setWindow({0, 44100 * 60});
    insert.prepare(RenderContext{44100.0, kBlockSize, 1});

    CHECK(insert.usable());

    juce::AudioBuffer<float> buffer(1, kBlockSize);
    juce::MidiBuffer midi;

    for (std::int64_t at = 0; at < 44100 * 60 && insert.usable(); at += kBlockSize) {
        buffer.clear();
        midi.clear();
        insert.receive(blockAtSample(at, kBlockSize), juce::dsp::AudioBlock<float>(buffer), midi);
    }

    // It ran out, and said so rather than growing.
    CHECK_FALSE(insert.usable());
    CHECK_FALSE(insert.covers(0, kBlockSize));
}
