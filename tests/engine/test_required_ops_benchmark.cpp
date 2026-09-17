#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "plan/RenderPlan.hpp"

using namespace magda;
using namespace magda::engine;

namespace {

constexpr int kBlockSize = 512;
constexpr int kDeadInputs = 64;

class CountingSource final : public EngineAudioSource {
  public:
    explicit CountingSource(float value) : value_(value) {}

    void render(const BlockInfo&, juce::dsp::AudioBlock<float> output) override {
        ++renders;
        output.fill(value_);
    }

    int renders = 0;

  private:
    float value_ = 0.0f;
};

PlanOp audioSource(OpKind kind, TrackId track, OpRole role) {
    PlanOp op;
    op.kind = kind;
    op.key.trackId = track;
    op.key.role = role;
    if (kind == OpKind::AudioInput)
        op.liveness = LivenessDomain::Live;
    op.outputs = {SignalKind::Audio};
    return op;
}

struct Fixture {
    Fixture() {
        std::vector<PortRef> deadInputs;
        deadInputs.reserve(kDeadInputs);
        deadSources.reserve(kDeadInputs);

        for (int input = 0; input < kDeadInputs; ++input) {
            const auto track = static_cast<TrackId>(input + 1);
            const auto sourceOp = static_cast<OpId>(plan.ops.size());
            plan.ops.push_back(audioSource(OpKind::ClipAudio, track, OpRole::ClipAudio));
            deadInputs.push_back(PortRef{sourceOp, 0});

            auto source = std::make_unique<CountingSource>(0.01f * static_cast<float>(input + 1));
            bindings.clipAudio[track] = source.get();
            deadSources.push_back(std::move(source));
        }

        PlanOp mix;
        mix.kind = OpKind::MixAudio;
        mix.key.trackId = 100;
        mix.key.role = OpRole::TrackAudioInput;
        mix.inputs = std::move(deadInputs);
        mix.outputs = {SignalKind::Audio};
        const auto mixOp = static_cast<OpId>(plan.ops.size());
        plan.ops.push_back(std::move(mix));

        PlanOp mutedGain;
        mutedGain.kind = OpKind::Gain;
        mutedGain.key.trackId = 100;
        mutedGain.key.deviceId = 1;
        mutedGain.key.role = OpRole::DeviceGain;
        mutedGain.inputs = {PortRef{mixOp, 0}};
        mutedGain.outputs = {SignalKind::Audio};
        const auto gainOp = plan.ops.size();
        plan.ops.push_back(std::move(mutedGain));

        const auto liveOp = static_cast<OpId>(plan.ops.size());
        plan.ops.push_back(audioSource(OpKind::AudioInput, 200, OpRole::LiveAudioInput));
        bindings.audioInputs[200] = &liveSource;

        PlanOp outputMix;
        outputMix.kind = OpKind::MixAudio;
        outputMix.key.trackId = MASTER_TRACK_ID;
        outputMix.key.role = OpRole::TrackAudioInput;
        outputMix.liveness = LivenessDomain::Live;
        outputMix.inputs = {PortRef{static_cast<OpId>(gainOp), 0}, PortRef{liveOp, 0}};
        outputMix.outputs = {SignalKind::Audio};
        const auto outputMixOp = static_cast<OpId>(plan.ops.size());
        plan.ops.push_back(std::move(outputMix));

        PlanOp output;
        output.kind = OpKind::Output;
        output.key.trackId = MASTER_TRACK_ID;
        output.key.role = OpRole::HardwareOutput;
        output.liveness = LivenessDomain::Live;
        output.inputs = {PortRef{outputMixOp, 0}};
        plan.outputOps.push_back(static_cast<OpId>(plan.ops.size()));
        plan.ops.push_back(std::move(output));
        bakeScheduling(plan);
        REQUIRE(validatePlan(plan).empty());

        pruned.planFingerprint = planFingerprint(plan);
        pruned.ops.resize(plan.ops.size());
        pruned.ops[gainOp].silent = true;
        resolveRequiredOps(plan, pruned);
        REQUIRE_FALSE(pruned.ops[static_cast<std::size_t>(mixOp)].required);
        REQUIRE(pruned.ops[gainOp].required);
        allRequired = pruned;
        for (auto& value : allRequired.ops)
            value.required = true;

        const RenderContext context{48000.0, kBlockSize, 2};
        for (auto& source : deadSources)
            source->prepare(context);
        liveSource.prepare(context);
        REQUIRE(prunedExecutor.prepare(plan, bindings, context, nullptr, &pruned).empty());
        REQUIRE(allExecutor.prepare(plan, bindings, context, nullptr, &allRequired).empty());
    }

    BlockInfo block() const {
        BlockInfo result;
        result.numSamples = kBlockSize;
        result.playing = true;
        result.beats.end = 1.0;
        return result;
    }

    RenderPlan plan;
    PlanBindings bindings;
    std::vector<std::unique_ptr<CountingSource>> deadSources;
    CountingSource liveSource{0.25f};
    PlanValues pruned;
    PlanValues allRequired;
    PlanExecutor prunedExecutor;
    PlanExecutor allExecutor;
    juce::AudioBuffer<float> prunedOutput{2, kBlockSize};
    juce::AudioBuffer<float> allOutput{2, kBlockSize};
};

}  // namespace

/// Hidden: a measurement rather than a timing requirement. Run with
/// `magda_tests "[required-ops-bench]"`. Both cases retain the 64 clip sources;
/// pruning removes the wide sum upstream of a muted gain while the live input
/// continues to reach the output.
TEST_CASE("Required-op pruning avoids a wide muted mix", "[.][bench][required-ops-bench]") {
    Fixture fixture;

    fixture.prunedExecutor.process(fixture.pruned, fixture.block(), fixture.prunedOutput);
    fixture.allExecutor.process(fixture.allRequired, fixture.block(), fixture.allOutput);

    REQUIRE(fixture.liveSource.renders == 2);
    for (const auto& source : fixture.deadSources)
        REQUIRE(source->renders == 2);
    for (int channel = 0; channel < fixture.prunedOutput.getNumChannels(); ++channel)
        for (int sample = 0; sample < kBlockSize; ++sample)
            REQUIRE(fixture.prunedOutput.getSample(channel, sample) ==
                    fixture.allOutput.getSample(channel, sample));

    BENCHMARK("pruned values") {
        fixture.prunedExecutor.process(fixture.pruned, fixture.block(), fixture.prunedOutput);
        return fixture.prunedOutput.getSample(0, 0);
    };

    BENCHMARK("all ops required") {
        fixture.allExecutor.process(fixture.allRequired, fixture.block(), fixture.allOutput);
        return fixture.allOutput.getSample(0, 0);
    };
}
