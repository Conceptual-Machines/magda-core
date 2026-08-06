#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/ParallelPlanExecutor.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanLayout.hpp"
#include "exec/PlanValues.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanCrossfade.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file test_plan_crossfade.cpp
 * @brief The edges an edit moves, and what it takes to not hear them move.
 *
 * Two halves. The pass is a question about plans: which edges get a fade, which
 * are refused and counted, and what a fade does to a plan's shape when it
 * arrives and when it leaves. The ramp is a question about samples, and those
 * tests compare floats exactly: a fade is a straight line between two known
 * levels, so the first sample, the last one and the one past the end are all
 * numbers rather than tolerances.
 */

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::DeviceBlock;
using magda::engine::EngineAudioSource;
using magda::engine::EngineDevice;
using magda::engine::insertCrossfades;
using magda::engine::kCrossfadeSeconds;
using magda::engine::OpId;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::ParallelPlanExecutor;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanValues;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::RenderThreadPool;

namespace {

constexpr int kBlockSize = 64;
constexpr double kSampleRate = 44100.0;
constexpr double kSamplesPerBeat = 64.0;

/// The ramp the executor builds at this sample rate, in samples.
const int kFadeSamples = static_cast<int>(std::lround(kCrossfadeSeconds * kSampleRate));

BlockInfo blockAt(std::int64_t timelineSample, int numSamples) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.playing = true;
    block.startBeat = static_cast<double>(timelineSample) / kSamplesPerBeat;
    block.endBeat = static_cast<double>(timelineSample + numSamples) / kSamplesPerBeat;
    block.continuous = timelineSample > 0;
    return block;
}

TrackInfo makeTrack(TrackId id, TrackType type = TrackType::Audio) {
    TrackInfo track;
    track.id = id;
    track.type = type;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID, TrackType::Master);
    master.audioOutputDevice = {};
    return master;
}

DeviceInfo makeEffect(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Effect " + juce::String(id);
    device.deviceType = DeviceType::Effect;
    return device;
}

/// A steady level, so what comes out of the fade is a number rather than a
/// waveform to interpret.
class ConstantSource final : public EngineAudioSource {
  public:
    explicit ConstantSource(float level) : level_(level) {}

    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        juce::ignoreUnused(block);
        out.fill(level_);
    }

  private:
    float level_;
};

/// Scales its input, optionally reporting and producing latency, so a fade
/// between "with it" and "without it" is a fade between two levels.
class GainDevice final : public EngineDevice {
  public:
    explicit GainDevice(float gain, int latency = 0) : gain_(gain), latency_(latency) {}

    void prepare(const RenderContext& context) override {
        history_.assign(static_cast<std::size_t>(context.numChannels),
                        std::vector<float>(static_cast<std::size_t>(latency_), 0.0f));
        cursor_ = 0;
    }

    void process(DeviceBlock& block) override {
        block.audio.multiplyBy(gain_);
        if (latency_ <= 0)
            return;

        for (std::size_t channel = 0; channel < block.audio.getNumChannels(); ++channel) {
            auto* samples = block.audio.getChannelPointer(channel);
            auto& history = history_[channel];
            auto cursor = cursor_;
            for (int sample = 0; sample < block.block.numSamples; ++sample) {
                const auto held = history[static_cast<std::size_t>(cursor)];
                history[static_cast<std::size_t>(cursor)] = samples[sample];
                samples[sample] = held;
                cursor = (cursor + 1) % latency_;
            }
            if (channel + 1 == block.audio.getNumChannels())
                cursor_ = cursor;
        }
    }

    int latencySamples() const override {
        return latency_;
    }

  private:
    float gain_;
    int latency_;
    std::vector<std::vector<float>> history_;
    int cursor_ = 0;
};

/// The model, the objects behind it, and one edit at a time. Everything an edit
/// does not touch is the same object afterwards, which is what the runtime store
/// promises the differ and what a fade between two epochs depends on.
struct Bench {
    Bench() {
        track = makeTrack(1);
        bindings.clipAudio[track.id] = &source;
    }

    /// The chain, as a list of device ids. An insert, a removal and a reorder
    /// are then the same one-line edit.
    void setChain(const std::vector<DeviceId>& ids) {
        track.chain.fxChainElements.clear();
        for (const auto id : ids)
            track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(id)));
    }

    /// Binds a device, made once and shared by every plan that names it.
    void bindDevice(DeviceId id, float gain, int latency = 0) {
        owned.push_back(std::make_unique<GainDevice>(gain, latency));
        owned.back()->prepare(context);
        bindings.devices[id] = owned.back().get();
    }

    RenderPlan compile() const {
        return magda::engine::compileRenderPlan({track}, master);
    }

    TrackInfo track;
    TrackInfo master = makeMaster();
    ConstantSource source{1.0f};
    PlanBindings bindings;
    std::vector<std::unique_ptr<GainDevice>> owned;
    RenderContext context{kSampleRate, kBlockSize, 2};
};

/// The one op with this role and location.
OpId findOp(const RenderPlan& plan, OpRole role, TrackId trackId,
            DeviceId deviceId = INVALID_DEVICE_ID) {
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& key = plan.ops[i].key;
        if (key.role == role && key.trackId == trackId && key.deviceId == deviceId)
            return static_cast<OpId>(i);
    }
    INFO(magda::engine::dumpPlan(plan));
    FAIL("the plan has no op with that role");
    return magda::engine::INVALID_OP_ID;
}

std::vector<OpId> fadesIn(const RenderPlan& plan) {
    std::vector<OpId> fades;
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (plan.ops[i].kind == OpKind::Crossfade)
            fades.push_back(static_cast<OpId>(i));
    return fades;
}

/// One prepared plan, and the samples it renders. Held together because the
/// executor points into its plan and the next epoch prepares against both.
struct Epoch {
    Epoch(RenderPlan planIn, Bench& bench, const PlanExecutor* previous) : plan(std::move(planIn)) {
        INFO(magda::engine::dumpPlan(plan));
        for (const auto& message : validatePlan(plan))
            FAIL_CHECK(message);
        for (const auto& message :
             magda::engine::resolvePlanValues(plan, {bench.track}, bench.master, values))
            FAIL_CHECK(message);
        for (const auto& message : executor.prepare(plan, bench.bindings, bench.context, previous))
            FAIL_CHECK(message);
        REQUIRE(executor.isPrepared());
        output.setSize(bench.context.numChannels, kBlockSize);
    }

    /// Channel 0 of @p numBlocks blocks, as one stream, so where the fade got
    /// to is an index into it.
    std::vector<float> render(int numBlocks, std::int64_t from = 0) {
        std::vector<float> stream;
        for (int block = 0; block < numBlocks; ++block) {
            const auto position = from + (static_cast<std::int64_t>(block) * kBlockSize);
            executor.process(values, blockAt(position, kBlockSize), output);
            for (int sample = 0; sample < kBlockSize; ++sample)
                stream.push_back(output.getSample(0, sample));
        }
        return stream;
    }

    RenderPlan plan;
    PlanValues values;
    PlanExecutor executor;
    juce::AudioBuffer<float> output;
};

/// What one sample of a fade from @p before to @p after is, at @p position.
float rampLevel(float before, float after, int position) {
    if (position >= kFadeSamples)
        return after;
    return before +
           ((after - before) * (static_cast<float>(position) / static_cast<float>(kFadeSamples)));
}

}  // namespace

// --- the pass ---------------------------------------------------------------

TEST_CASE("A model republished unchanged is the plan the compiler produced",
          "[engine][plan][crossfade]") {
    Bench bench;
    bench.setChain({7});

    const auto before = bench.compile();
    const auto after = bench.compile();
    const auto faded = insertCrossfades(before, after);

    INFO(magda::engine::dumpPlan(faded.plan));
    CHECK(faded.inserted == 0);
    CHECK(faded.unfaded == 0);
    CHECK(planFingerprint(faded.plan) == planFingerprint(after));
}

TEST_CASE("A device inserted mid-chain fades the edge it displaced", "[engine][plan][crossfade]") {
    Bench bench;
    bench.setChain({7});
    const auto before = bench.compile();

    bench.setChain({7, 8});
    const auto after = bench.compile();

    const auto faded = insertCrossfades(before, after);
    INFO(magda::engine::dumpPlan(faded.plan));

    const auto fades = fadesIn(faded.plan);
    REQUIRE(fades.size() == 1);
    CHECK(faded.inserted == 1);
    CHECK(faded.unfaded == 0);

    // Keyed to the edge it sits on: the op it feeds, and the slot it fills.
    const auto& fade = faded.plan.ops[static_cast<std::size_t>(fades.front())];
    CHECK(fade.key.trackId == 1);
    CHECK(fade.key.role == OpRole::EdgeCrossfade);
    CHECK(magda::engine::crossfadeConsumerRole(fade.key.index) == OpRole::TrackFader);
    CHECK(magda::engine::crossfadeSlot(fade.key.index) == 0);

    // The edge as it was on one side and as it is on the other.
    CHECK(fade.inputs[0].op == findOp(faded.plan, OpRole::DeviceMeter, 1, 7));
    CHECK(fade.inputs[1].op == findOp(faded.plan, OpRole::DeviceMeter, 1, 8));

    // And the consumer reads the fade rather than either of them.
    const auto fader = findOp(faded.plan, OpRole::TrackFader, 1);
    CHECK(faded.plan.ops[static_cast<std::size_t>(fader)].inputs[0].op == fades.front());

    CHECK(validatePlan(faded.plan).empty());
}

TEST_CASE("A device removed has nothing to fade from", "[engine][plan][crossfade]") {
    // The old side is the device that is gone. Fading from an op the new plan
    // does not contain would mean rendering it, which is the whole thing this
    // design exists to avoid.
    Bench bench;
    bench.setChain({7, 8});
    const auto before = bench.compile();

    bench.setChain({7});
    const auto after = bench.compile();

    const auto faded = insertCrossfades(before, after);
    INFO(magda::engine::dumpPlan(faded.plan));

    CHECK(faded.inserted == 0);
    CHECK(faded.unfaded == 1);
}

TEST_CASE("A reorder fades the one edge that can be faded", "[engine][plan][crossfade]") {
    // Two devices swapped. The edge into the chain's new first device can fade,
    // because what it used to read is still upstream of it; the other two edges
    // read something that has itself changed, or something that now comes after
    // them, and neither is the old signal.
    Bench bench;
    bench.setChain({7, 8});
    const auto before = bench.compile();

    bench.setChain({8, 7});
    const auto after = bench.compile();

    const auto faded = insertCrossfades(before, after);
    INFO(magda::engine::dumpPlan(faded.plan));

    CHECK(faded.inserted == 1);
    CHECK(faded.unfaded == 2);
    CHECK(validatePlan(faded.plan).empty());
}

TEST_CASE("A send changes what a sum is, and a sum is not an edge", "[engine][plan][crossfade]") {
    auto source = makeTrack(1);
    auto destination = makeTrack(2);
    const auto master = makeMaster();

    const auto before = magda::engine::compileRenderPlan({source, destination}, master);

    SendInfo send;
    send.destTrackId = destination.id;
    send.level = 0.5f;
    source.sends.push_back(send);
    const auto after = magda::engine::compileRenderPlan({source, destination}, master);

    const auto faded = insertCrossfades(before, after);
    INFO(magda::engine::dumpPlan(faded.plan));

    CHECK(faded.inserted == 0);
    CHECK(faded.unfaded > 0);
}

TEST_CASE("An edit during a fade fades from where the last one was heading",
          "[engine][plan][crossfade]") {
    Bench bench;
    const auto first = bench.compile();

    bench.setChain({7});
    const auto second = insertCrossfades(first, bench.compile());
    REQUIRE(second.inserted == 1);

    // Still running, which is what the executor would say.
    std::vector<char> running(second.plan.ops.size(), 0);
    running[static_cast<std::size_t>(fadesIn(second.plan).front())] = 1;

    bench.setChain({7, 8});
    const auto third = insertCrossfades(second.plan, bench.compile(), running);
    INFO(magda::engine::dumpPlan(third.plan));

    const auto fades = fadesIn(third.plan);
    REQUIRE(fades.size() == 1);
    CHECK(third.inserted == 1);

    // From device 7, which is where the first fade was heading, and never from
    // the fade itself: fades collapse rather than stack.
    const auto& fade = third.plan.ops[static_cast<std::size_t>(fades.front())];
    CHECK(fade.inputs[0].op == findOp(third.plan, OpRole::DeviceMeter, 1, 7));
    CHECK(fade.inputs[1].op == findOp(third.plan, OpRole::DeviceMeter, 1, 8));
    CHECK(validatePlan(third.plan).empty());
}

TEST_CASE("A faded plan is a plan in every other respect", "[engine][plan][crossfade]") {
    Bench bench;
    bench.setChain({7});
    const auto before = bench.compile();

    bench.setChain({7, 8});
    const auto after = bench.compile();
    const auto faded = insertCrossfades(before, after);
    INFO(magda::engine::dumpPlan(faded.plan));

    for (const auto& problem : validatePlan(faded.plan))
        FAIL_CHECK(problem);

    // The scheduling constants are what the parallel executor copies in every
    // block and cannot work out for itself, so inserting an op mid-plan has to
    // rebake them rather than leave the compiler's behind.
    CHECK(carriesSchedule(faded.plan));
    CHECK(faded.plan.ops.size() == after.ops.size() + static_cast<std::size_t>(faded.inserted));

    REQUIRE_FALSE(faded.plan.outputOps.empty());
    for (const auto outputOp : faded.plan.outputOps)
        CHECK(faded.plan.ops[static_cast<std::size_t>(outputOp)].kind == OpKind::Output);
}

// --- the ramp ---------------------------------------------------------------

TEST_CASE("A fade starts at the old signal and ends exactly where it said",
          "[engine][plan][crossfade]") {
    // Unity through an empty chain, then half through a device: the fade is a
    // straight line between two numbers, so every sample of it is one.
    Bench bench;
    bench.bindDevice(7, 0.5f);
    const auto before = bench.compile();

    bench.setChain({7});
    const auto faded = insertCrossfades(before, bench.compile());
    REQUIRE(faded.inserted == 1);

    Epoch epoch(faded.plan, bench, nullptr);
    CHECK(epoch.executor.activeCrossfades() == 1);

    const auto blocks = (kFadeSamples / kBlockSize) + 2;
    const auto stream = epoch.render(blocks);

    CHECK(stream.front() == 1.0f);
    for (int sample = 0; sample + 1 < kFadeSamples; ++sample) {
        INFO("sample " << sample);
        REQUIRE(stream[static_cast<std::size_t>(sample)] >
                stream[static_cast<std::size_t>(sample) + 1]);
    }

    // One sample short of the end it is still fading, and one past it the edge
    // is simply the new one. The length is a count of samples, so a block
    // boundary landing inside it changes nothing.
    CHECK(stream[static_cast<std::size_t>(kFadeSamples) - 1] ==
          rampLevel(1.0f, 0.5f, kFadeSamples - 1));
    CHECK(stream[static_cast<std::size_t>(kFadeSamples) - 1] != 0.5f);
    for (auto sample = static_cast<std::size_t>(kFadeSamples); sample < stream.size(); ++sample) {
        INFO("sample " << sample);
        REQUIRE(stream[sample] == 0.5f);
    }

    CHECK(epoch.executor.activeCrossfades() == 0);
}

TEST_CASE("A plan republished mid-fade picks the fade up where it was",
          "[engine][plan][crossfade]") {
    Bench bench;
    bench.bindDevice(7, 0.5f);
    const auto before = bench.compile();

    bench.setChain({7});
    const auto faded = insertCrossfades(before, bench.compile());
    Epoch fading(faded.plan, bench, nullptr);

    const auto first = fading.render(1);
    REQUIRE(first.front() == 1.0f);

    // The same model again, while the fade is still running. Nothing structural
    // changed, so the fade is re-emitted on the same key and the executor
    // adopts the ramp rather than starting it again.
    const auto again =
        insertCrossfades(fading.plan, bench.compile(), fading.executor.unfinishedCrossfades());
    INFO(magda::engine::dumpPlan(again.plan));
    REQUIRE(again.inserted == 1);

    Epoch resumed(again.plan, bench, &fading.executor);
    CHECK(resumed.executor.carriedCrossfades() == 1);

    const auto second = resumed.render(1);
    CHECK(second.front() == rampLevel(1.0f, 0.5f, kBlockSize));
    CHECK(second.front() != 1.0f);
}

TEST_CASE("A spent fade leaves at the next publish", "[engine][plan][crossfade]") {
    Bench bench;
    bench.bindDevice(7, 0.5f);
    const auto before = bench.compile();

    bench.setChain({7});
    const auto compiled = bench.compile();
    const auto faded = insertCrossfades(before, compiled);
    Epoch fading(faded.plan, bench, nullptr);

    fading.render((kFadeSamples / kBlockSize) + 2);
    REQUIRE(fading.executor.activeCrossfades() == 0);

    const auto retired =
        insertCrossfades(fading.plan, bench.compile(), fading.executor.unfinishedCrossfades());
    INFO(magda::engine::dumpPlan(retired.plan));

    CHECK(retired.inserted == 0);
    CHECK(fadesIn(retired.plan).empty());
    CHECK(planFingerprint(retired.plan) == planFingerprint(compiled));

    Epoch settled(retired.plan, bench, &fading.executor);
    CHECK(settled.executor.activeCrossfades() == 0);
    CHECK(settled.render(1).front() == 0.5f);
}

TEST_CASE("A fade whose sides do not arrive together does not run", "[engine][plan][crossfade]") {
    // The edit moved latency along the edge, so the new side is compensated by
    // a delay line that starts flushed. Fading into it would fade in from that
    // many samples of silence, which is louder than the step it was smoothing.
    Bench bench;
    bench.bindDevice(7, 1.0f, 32);
    const auto before = bench.compile();

    bench.setChain({7});
    const auto faded = insertCrossfades(before, bench.compile());
    REQUIRE(faded.inserted == 1);

    Epoch epoch(faded.plan, bench, nullptr);
    CHECK(epoch.executor.activeCrossfades() == 0);

    // The new side, from the first sample: the device is holding 32 samples of
    // its own silence, and the fade is not putting the old signal over them.
    const auto stream = epoch.render(2);
    CHECK(stream.front() == 0.0f);
    CHECK(stream[32] == 1.0f);
}

TEST_CASE("A fade produces the latency of the side it is becoming", "[engine][plan][crossfade]") {
    // The other direction of the same rule, built by hand because a compiler
    // that has been asked for it cannot produce an old side more latent than
    // the new one: taking the maximum here would delay every parallel path
    // downstream, for as long as an old side nobody is fading to stayed in the
    // plan.
    using magda::engine::PlanOp;
    using magda::engine::PortRef;
    using magda::engine::SignalKind;

    constexpr int kDeviceLatency = 48;

    RenderPlan plan;
    const auto add = [&plan](OpKind kind, magda::engine::OpKey key, std::vector<PortRef> inputs,
                             std::vector<SignalKind> outputs) {
        PlanOp op;
        op.kind = kind;
        op.key = key;
        op.inputs = std::move(inputs);
        op.outputs = std::move(outputs);
        plan.ops.push_back(std::move(op));
        return static_cast<OpId>(plan.ops.size() - 1);
    };

    const auto clipOne =
        add(OpKind::ClipAudio,
            {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::ClipAudio, 0}, {},
            {SignalKind::Audio});
    const auto device =
        add(OpKind::Device, {1, INVALID_RACK_ID, INVALID_CHAIN_ID, 7, OpRole::DeviceProcess, 0},
            {PortRef{clipOne, 0}, magda::engine::noInput(), magda::engine::noInput()},
            {SignalKind::Audio});
    const auto fade =
        add(OpKind::Crossfade,
            {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::EdgeCrossfade,
             magda::engine::crossfadeIndex(OpRole::TrackFader, 0)},
            {PortRef{device, 0}, PortRef{clipOne, 0}}, {SignalKind::Audio});
    const auto fader =
        add(OpKind::Fader,
            {1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::TrackFader, 0},
            {PortRef{fade, 0}, magda::engine::noInput()}, {SignalKind::Audio});

    const auto clipTwo =
        add(OpKind::ClipAudio,
            {2, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::ClipAudio, 0}, {},
            {SignalKind::Audio});

    const magda::engine::OpKey mixKey{MASTER_TRACK_ID,   INVALID_RACK_ID,         INVALID_CHAIN_ID,
                                      INVALID_DEVICE_ID, OpRole::TrackAudioInput, 0};
    auto delayKey = mixKey;
    delayKey.role = OpRole::MixInputDelay;
    delayKey.index = 0;
    const auto delayOne = add(OpKind::Delay, delayKey, {PortRef{fader, 0}}, {SignalKind::Audio});
    delayKey.index = 1;
    const auto delayTwo = add(OpKind::Delay, delayKey, {PortRef{clipTwo, 0}}, {SignalKind::Audio});

    const auto mix = add(OpKind::MixAudio, mixKey, {PortRef{delayOne, 0}, PortRef{delayTwo, 0}},
                         {SignalKind::Audio});
    plan.outputOps.push_back(add(OpKind::Output,
                                 {MASTER_TRACK_ID, INVALID_RACK_ID, INVALID_CHAIN_ID,
                                  INVALID_DEVICE_ID, OpRole::HardwareOutput, 0},
                                 {PortRef{mix, 0}}, {}));
    bakeScheduling(plan);

    INFO(magda::engine::dumpPlan(plan));
    for (const auto& problem : validatePlan(plan))
        FAIL_CHECK(problem);

    std::vector<int> deviceLatency(plan.ops.size(), 0);
    deviceLatency[static_cast<std::size_t>(device)] = kDeviceLatency;

    const auto offsets = magda::engine::portOffsetsOf(plan);
    const auto latency = magda::engine::resolvePlanLatency(plan, offsets, deviceLatency);

    // The new side is the undelayed one, so the fade produces nothing and the
    // two paths into the master already agree: no compensation, anywhere.
    CHECK(latency.portLatency[static_cast<std::size_t>(offsets[static_cast<std::size_t>(fade)])] ==
          0);
    CHECK(latency.delaySamples[static_cast<std::size_t>(delayOne)] == 0);
    CHECK(latency.delaySamples[static_cast<std::size_t>(delayTwo)] == 0);
    CHECK(latency.outputLatency == 0);

    // And the ramp is refused, because the sides do not arrive together.
    ConstantSource one(1.0f), two(0.25f);
    GainDevice latent(1.0f, kDeviceLatency);
    const RenderContext context{kSampleRate, kBlockSize, 2};
    latent.prepare(context);

    PlanBindings bindings;
    bindings.clipAudio[1] = &one;
    bindings.clipAudio[2] = &two;
    bindings.devices[7] = &latent;

    PlanExecutor executor;
    for (const auto& message : executor.prepare(plan, bindings, context))
        FAIL_CHECK(message);
    CHECK(executor.activeCrossfades() == 0);
    CHECK(executor.latencySamples() == 0);
}

TEST_CASE("A faded plan renders the same on both executors", "[engine][plan][crossfade]") {
    // Bit-identical, at every thread count, across the ramp and past its end: a
    // fade is a stateful op like any other, and the claim the parallel executor
    // makes is that scheduling is the only thing it changes.
    const auto renderThrough = [](int numThreads) {
        Bench bench;
        bench.bindDevice(7, 0.5f);
        const auto before = bench.compile();
        bench.setChain({7});
        const auto faded = insertCrossfades(before, bench.compile());
        REQUIRE(faded.inserted == 1);

        PlanValues values;
        for (const auto& message :
             magda::engine::resolvePlanValues(faded.plan, {bench.track}, bench.master, values))
            FAIL_CHECK(message);

        std::unique_ptr<RenderThreadPool> pool;
        if (numThreads > 0)
            pool = std::make_unique<RenderThreadPool>(numThreads);

        ParallelPlanExecutor executor(pool.get());
        for (const auto& message : executor.prepare(faded.plan, bench.bindings, bench.context))
            FAIL_CHECK(message);

        juce::AudioBuffer<float> output(bench.context.numChannels, kBlockSize);
        std::vector<float> stream;
        for (int block = 0; block < (kFadeSamples / kBlockSize) + 3; ++block) {
            executor.process(
                values, blockAt(static_cast<std::int64_t>(block) * kBlockSize, kBlockSize), output);
            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                for (int sample = 0; sample < kBlockSize; ++sample)
                    stream.push_back(output.getSample(channel, sample));
        }
        return stream;
    };

    const auto reference = renderThrough(0);
    REQUIRE_FALSE(reference.empty());

    for (const auto threads : {1, 2, 4}) {
        INFO("threads: " << threads);
        const auto actual = renderThrough(threads);
        REQUIRE(actual.size() == reference.size());
        for (std::size_t sample = 0; sample < reference.size(); ++sample) {
            INFO("sample " << sample);
            REQUIRE(actual[sample] == reference[sample]);
        }
    }
}
