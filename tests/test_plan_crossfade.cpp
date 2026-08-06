#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <memory>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/ParallelPlanExecutor.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RenderThreadPool.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanCrossfade.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::CrossfadedPlan;
using magda::engine::DeviceBlock;
using magda::engine::EngineAudioSource;
using magda::engine::EngineDevice;
using magda::engine::OpId;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::ParallelPlanExecutor;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanValues;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;

namespace {

constexpr int kBlockSize = 64;
constexpr double kSampleRate = 44100.0;

/// The fade the executor will run for this context, which is what the render
/// tests measure against rather than a number written out twice.
const int kFadeSamples =
    static_cast<int>(std::lround(magda::engine::kCrossfadeSeconds * kSampleRate));

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

RenderPlan compile(const std::vector<TrackInfo>& tracks) {
    return magda::engine::compileRenderPlan(tracks, makeMaster());
}

BlockInfo block(int numSamples) {
    BlockInfo info;
    info.numSamples = numSamples;
    info.playing = true;
    info.continuous = true;
    return info;
}

/// The op with this key location and role, by name: an edit moves every op
/// after it, so an index says nothing across one.
OpId opWith(const RenderPlan& plan, OpRole role, TrackId trackId,
            DeviceId deviceId = INVALID_DEVICE_ID) {
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& key = plan.ops[i].key;
        if (key.role == role && key.trackId == trackId && key.deviceId == deviceId)
            return static_cast<OpId>(i);
    }
    return magda::engine::INVALID_OP_ID;
}

/// Every fade op in a plan.
std::vector<OpId> fadesIn(const RenderPlan& plan) {
    std::vector<OpId> fades;
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (plan.ops[i].kind == OpKind::Crossfade)
            fades.push_back(static_cast<OpId>(i));
    return fades;
}

/// A steady level, so what a fade is between reads straight off the output.
class ConstantSource final : public EngineAudioSource {
  public:
    explicit ConstantSource(float level) : level_(level) {}

    void render(const BlockInfo&, juce::dsp::AudioBlock<float> out) override {
        out.fill(level_);
    }

  private:
    float level_;
};

/// Scales its input, and reports whatever latency it was built with.
class GainDevice final : public EngineDevice {
  public:
    explicit GainDevice(float gain, int latency = 0) : gain_(gain), latency_(latency) {}

    void process(DeviceBlock& block) override {
        block.audio.multiplyBy(gain_);
    }

    int latencySamples() const override {
        return latency_;
    }

  private:
    float gain_;
    int latency_;
};

/**
 * @brief Compile, fade, resolve, prepare and render, across a swap.
 *
 * The whole point of the pass is what happens between two plans, so the fixture
 * is two of everything: an executor for the plan that is playing and one for the
 * plan replacing it, prepared against the first the way a session prepares an
 * epoch against the one it is displacing.
 */
struct Swap {
    explicit Swap(std::vector<TrackInfo> tracksIn) : tracks(std::move(tracksIn)) {
        output.setSize(2, kBlockSize);
    }

    /// Bind a track's clip source, and every device by ID. Owned here, and
    /// shared between the two executors exactly as the runtime store shares
    /// them between epochs.
    void bindSource(TrackId trackId, std::unique_ptr<EngineAudioSource> source) {
        source->prepare(context);
        bindings.clipAudio[trackId] = source.get();
        sources.push_back(std::move(source));
    }

    void bindDevice(DeviceId deviceId, std::unique_ptr<EngineDevice> device) {
        device->prepare(context);
        bindings.devices[deviceId] = device.get();
        devices.push_back(std::move(device));
    }

    /// Publish the model as it stands. The first call prepares from nothing;
    /// every call after it runs the pass against the plan that is playing and
    /// prepares against the executor rendering it.
    const CrossfadedPlan& publish() {
        auto compiled = compile(tracks);
        faded = live == nullptr ? CrossfadedPlan{compiled, 0, 0}
                                : magda::engine::insertCrossfades(*livePlan, compiled,
                                                                  live->unfinishedCrossfades());

        INFO(magda::engine::dumpPlan(faded.plan));
        for (const auto& problem : magda::engine::validatePlan(faded.plan))
            FAIL_CHECK(problem);
        CHECK(magda::engine::carriesSchedule(faded.plan));

        plans.push_back(std::make_unique<RenderPlan>(faded.plan));
        auto& published = *plans.back();

        PlanValues resolved;
        for (const auto& message :
             magda::engine::resolvePlanValues(published, tracks, makeMaster(), resolved))
            FAIL_CHECK(message);

        auto prepared = std::make_unique<PlanExecutor>();
        for (const auto& message : prepared->prepare(published, bindings, context, live.get()))
            FAIL_CHECK(message);

        previous = std::move(live);
        live = std::move(prepared);
        livePlan = &published;
        values = std::move(resolved);
        return faded;
    }

    /// One block through whatever is live, returning the first sample of it.
    float render(int numSamples = kBlockSize) {
        live->process(values, block(numSamples), output);
        return output.getSample(0, 0);
    }

    float outputSample(int sample) const {
        return output.getSample(0, sample);
    }

    std::vector<TrackInfo> tracks;
    RenderContext context{kSampleRate, kBlockSize, 2};
    PlanBindings bindings;
    std::vector<std::unique_ptr<EngineAudioSource>> sources;
    std::vector<std::unique_ptr<EngineDevice>> devices;

    /// Plans outlive their executors, which point into them.
    std::vector<std::unique_ptr<RenderPlan>> plans;
    const RenderPlan* livePlan = nullptr;
    CrossfadedPlan faded;

    std::unique_ptr<PlanExecutor> live, previous;
    PlanValues values;
    juce::AudioBuffer<float> output;
};

}  // namespace

// --- the pass ----------------------------------------------------------------

TEST_CASE("A plan recompiled from an unchanged model gets no fades", "[engine][plan][crossfade]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    const auto before = compile(tracks);
    const auto result = magda::engine::insertCrossfades(before, compile(tracks));

    INFO(magda::engine::dumpPlan(result.plan));
    CHECK(result.inserted == 0);
    CHECK(result.unfaded == 0);

    // Not merely equivalent: the same plan, so nothing resolved against it has
    // to be resolved again.
    CHECK(magda::engine::planFingerprint(result.plan) == magda::engine::planFingerprint(before));
}

TEST_CASE("Inserting a device fades the edge it displaced", "[engine][plan][crossfade]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    const auto before = compile(tracks);

    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    const auto result = magda::engine::insertCrossfades(before, compile(tracks));

    INFO(magda::engine::dumpPlan(result.plan));
    REQUIRE(result.inserted == 1);
    CHECK(result.unfaded == 0);
    CHECK(magda::engine::validatePlan(result.plan).empty());

    // The fader is the op that now reads somewhere else, so the fade is on its
    // audio slot, keyed to it.
    const auto fades = fadesIn(result.plan);
    REQUIRE(fades.size() == 1);
    const auto& fade = result.plan.ops[static_cast<std::size_t>(fades.front())];
    CHECK(fade.key.trackId == 1);
    CHECK(fade.key.role == OpRole::EdgeCrossfade);
    CHECK(magda::engine::crossfadeConsumerRole(fade.key.index) == OpRole::TrackFader);
    CHECK(magda::engine::crossfadeSlot(fade.key.index) == 0);

    // Old side: device 7's meter, which is what the fader read before. New
    // side: device 8's, which is what it reads now.
    CHECK(fade.inputs[0].op == opWith(result.plan, OpRole::DeviceMeter, 1, 7));
    CHECK(fade.inputs[1].op == opWith(result.plan, OpRole::DeviceMeter, 1, 8));

    const auto fader = opWith(result.plan, OpRole::TrackFader, 1);
    CHECK(result.plan.ops[static_cast<std::size_t>(fader)].inputs[0].op == fades.front());
}

TEST_CASE("Removing a device leaves its edge unfaded", "[engine][plan][crossfade]") {
    // Nothing in the new plan computes what the removed device was putting out,
    // so there is no old side to fade from. Counted rather than passed over: it
    // is the honest answer for this edit and a test has to be able to see the
    // difference between it and nothing having changed.
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    const auto before = compile(tracks);

    tracks[0].chain.fxChainElements.clear();
    const auto result = magda::engine::insertCrossfades(before, compile(tracks));

    INFO(magda::engine::dumpPlan(result.plan));
    CHECK(result.inserted == 0);
    CHECK(result.unfaded == 1);
}

TEST_CASE("Reordering two devices fades only the edge whose old signal survives",
          "[engine][plan][crossfade]") {
    // Swap 7 and 8 and three edges move. Two of them cannot be faded and for two
    // different reasons, which is what makes this the test that pins both:
    //
    // - device 8 used to read device 7's output and now reads the track's. The
    //   op carrying the old signal now sits after the op that would read it,
    //   which is what dependency order catches.
    // - the fader used to read device 8's output and now reads device 7's.
    //   Device 8's meter is still in the plan under the same key and still
    //   carries, but device 8 is processing the dry signal now, so it is not the
    //   old signal under any reading.
    //
    // What is left is device 7's own input, which really did go from the track's
    // signal to device 8's output, and that one fades.
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    const auto before = compile(tracks);

    std::swap(tracks[0].chain.fxChainElements[0], tracks[0].chain.fxChainElements[1]);
    const auto result = magda::engine::insertCrossfades(before, compile(tracks));

    INFO(magda::engine::dumpPlan(result.plan));
    CHECK(result.inserted == 1);
    CHECK(result.unfaded == 2);

    const auto fades = fadesIn(result.plan);
    REQUIRE(fades.size() == 1);
    const auto& fade = result.plan.ops[static_cast<std::size_t>(fades.front())];
    CHECK(magda::engine::crossfadeConsumerRole(fade.key.index) == OpRole::DeviceProcess);
    CHECK(fade.key.deviceId == 7);
    CHECK(fade.inputs[0].op == opWith(result.plan, OpRole::TrackAudioInput, 1));
    CHECK(fade.inputs[1].op == opWith(result.plan, OpRole::DeviceMeter, 1, 8));
}

TEST_CASE("A faded plan keeps the invariants every walk over a plan relies on",
          "[engine][plan][crossfade]") {
    // Inserting ops mid-plan is where dependency order, the baked schedule and
    // key uniqueness would all quietly stop holding.
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    const auto before = compile(tracks);

    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    tracks[1].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(9)));
    const auto result = magda::engine::insertCrossfades(before, compile(tracks));

    INFO(magda::engine::dumpPlan(result.plan));
    for (const auto& problem : magda::engine::validatePlan(result.plan))
        FAIL_CHECK(problem);
    CHECK(magda::engine::carriesSchedule(result.plan));
    CHECK(result.plan.ops.size() ==
          compile(tracks).ops.size() + static_cast<std::size_t>(result.inserted));

    // The outputs still name Output ops after everything moved.
    REQUIRE_FALSE(result.plan.outputOps.empty());
    for (const auto outputOp : result.plan.outputOps)
        CHECK(result.plan.ops[static_cast<std::size_t>(outputOp)].kind == OpKind::Output);
}

TEST_CASE("A second edit mid-fade fades from where the first one was going",
          "[engine][plan][crossfade]") {
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    const auto first = compile(tracks);

    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    const auto second = magda::engine::insertCrossfades(first, compile(tracks));
    REQUIRE(second.inserted == 1);

    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(9)));
    const auto third = magda::engine::insertCrossfades(second.plan, compile(tracks));

    INFO(magda::engine::dumpPlan(third.plan));
    REQUIRE(third.inserted == 1);

    // Device 8, not the first fade's output and not device 7: what the edge
    // carries by the time anyone hears the second edit is where the first fade
    // was heading.
    const auto fades = fadesIn(third.plan);
    REQUIRE(fades.size() == 1);
    const auto& fade = third.plan.ops[static_cast<std::size_t>(fades.front())];
    CHECK(fade.inputs[0].op == opWith(third.plan, OpRole::DeviceMeter, 1, 8));
    CHECK(fade.inputs[1].op == opWith(third.plan, OpRole::DeviceMeter, 1, 9));
}

TEST_CASE("A send added to a track changes a sum's arity, which is not an edge moving",
          "[engine][plan][crossfade]") {
    // The destination's sum gains an input rather than reading somewhere else on
    // one it already had. There is no edge to fade: the old signal is the sum
    // without the send, which would take a second sum to compute. Counted, so
    // that "nothing to do" and "nothing this pass can do" stay distinguishable.
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    const auto before = compile(tracks);

    SendInfo send;
    send.destTrackId = 2;
    send.level = 1.0f;
    tracks[0].sends.push_back(send);
    const auto result = magda::engine::insertCrossfades(before, compile(tracks));

    INFO(magda::engine::dumpPlan(result.plan));
    CHECK(magda::engine::validatePlan(result.plan).empty());
    CHECK(result.inserted == 0);
    CHECK(result.unfaded > 0);
}

// --- the executor ------------------------------------------------------------

TEST_CASE("A fade ramps from the old signal to the new one", "[engine][plan][crossfade]") {
    // Device 8 silences the chain, so what the fade is between is unity and
    // zero, and the ramp reads straight off the output.
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    Swap swap(tracks);
    swap.bindSource(1, std::make_unique<ConstantSource>(1.0f));
    swap.bindDevice(7, std::make_unique<GainDevice>(1.0f));
    swap.bindDevice(8, std::make_unique<GainDevice>(0.0f));
    swap.publish();

    CHECK(swap.render() == Catch::Approx(1.0f));

    swap.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    const auto& faded = swap.publish();
    REQUIRE(faded.inserted == 1);
    REQUIRE(swap.live->activeCrossfades() == 1);

    // The first block starts on the old signal and is on its way down.
    swap.render();
    CHECK(swap.outputSample(0) == Catch::Approx(1.0f));
    for (int sample = 1; sample < kBlockSize; ++sample) {
        CHECK(swap.outputSample(sample) < swap.outputSample(sample - 1));
        CHECK(swap.outputSample(sample) >= 0.0f);
    }

    // And the ramp is the length it says it is: still fading a block short of
    // it, finished a block past it.
    const auto blocksToEnd = (kFadeSamples + kBlockSize - 1) / kBlockSize;
    for (int rendered = 1; rendered < blocksToEnd; ++rendered)
        CHECK(swap.render() > 0.0f);

    swap.render();
    CHECK(swap.render() == Catch::Approx(0.0f));
}

TEST_CASE("A fade resumes rather than restarting when the same plan is prepared again",
          "[engine][plan][crossfade]") {
    // A plugin reporting a new latency re-prepares the plan that is playing.
    // The fade is mid-flight, and starting it again would replay the step it
    // was there to smooth.
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    Swap swap(tracks);
    swap.bindSource(1, std::make_unique<ConstantSource>(1.0f));
    swap.bindDevice(7, std::make_unique<GainDevice>(1.0f));
    swap.bindDevice(8, std::make_unique<GainDevice>(0.0f));
    swap.publish();

    swap.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    swap.publish();
    swap.render();
    const auto partWayDown = swap.outputSample(kBlockSize - 1);
    CHECK(partWayDown < 1.0f);

    // Same model, so the same plan and no new fade: the ramp is adopted.
    swap.publish();
    CHECK(swap.live->carriedCrossfades() == 1);
    CHECK(swap.live->activeCrossfades() == 1);

    swap.render();
    CHECK(swap.outputSample(0) < partWayDown);
}

TEST_CASE("An edit that moves the latency along the edge does not fade it",
          "[engine][plan][crossfade]") {
    // The two sides would be different moments of the timeline, and nothing at
    // this end can fix that: a delay line built now starts flushed, so
    // compensating the old side would fade in from silence. The op stays and
    // passes the new side through.
    std::vector<TrackInfo> tracks{makeTrack(1)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    Swap swap(tracks);
    swap.bindSource(1, std::make_unique<ConstantSource>(1.0f));
    swap.bindDevice(7, std::make_unique<GainDevice>(1.0f));
    swap.bindDevice(8, std::make_unique<GainDevice>(0.5f, 128));
    swap.publish();

    swap.tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    const auto& faded = swap.publish();

    // The op is in the plan: whether it fades is a question about the instances
    // behind the plan, which only prepare can answer.
    CHECK(faded.inserted == 1);
    CHECK(swap.live->activeCrossfades() == 0);

    // And it passes the new side through rather than rendering silence, which
    // is a step and is meant to be: it is what the engine does today.
    swap.render();
    CHECK(swap.outputSample(0) == Catch::Approx(0.5f));
}

TEST_CASE("A fade renders the same on threads as it does on one", "[engine][plan][crossfade]") {
    // The ramp is per-op state mutated during the block, which is the shape of
    // thing that comes out differently once a schedule decides when an op runs.
    // Bit-identical, not close: the two executors share every op body, and this
    // is what says the one they now share is no exception.
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    tracks[1].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    const auto before = compile(tracks);

    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(9)));
    tracks[1].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(10)));
    const auto faded = magda::engine::insertCrossfades(before, compile(tracks));
    REQUIRE(faded.inserted == 2);

    ConstantSource sourceOne(0.75f), sourceTwo(-0.5f);
    GainDevice seven(0.9f), eight(0.4f), nine(0.3f), ten(1.5f);

    const RenderContext context{kSampleRate, kBlockSize, 2};
    PlanBindings bindings;
    bindings.clipAudio[1] = &sourceOne;
    bindings.clipAudio[2] = &sourceTwo;
    bindings.devices[7] = &seven;
    bindings.devices[8] = &eight;
    bindings.devices[9] = &nine;
    bindings.devices[10] = &ten;
    for (auto& [id, device] : bindings.devices)
        device->prepare(context);

    PlanValues values;
    REQUIRE(magda::engine::resolvePlanValues(faded.plan, tracks, makeMaster(), values).empty());

    PlanExecutor reference;
    REQUIRE(reference.prepare(faded.plan, bindings, context).empty());

    magda::engine::RenderThreadPool pool(4, false);
    magda::engine::ParallelPlanExecutor threaded(&pool);
    REQUIRE(threaded.prepare(faded.plan, bindings, context).empty());

    juce::AudioBuffer<float> one(2, kBlockSize), many(2, kBlockSize);

    // Across the whole ramp and past the end of it, because where the two could
    // disagree is in how far each one thinks the fade has got.
    const auto blocks = ((kFadeSamples + kBlockSize - 1) / kBlockSize) + 2;
    for (int rendered = 0; rendered < blocks; ++rendered) {
        reference.process(values, block(kBlockSize), one);
        threaded.process(values, block(kBlockSize), many);

        INFO("block " << rendered);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < kBlockSize; ++sample)
                REQUIRE(one.getSample(channel, sample) == many.getSample(channel, sample));
    }
}
