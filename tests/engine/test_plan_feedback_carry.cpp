#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "plan/RenderPlan.hpp"

// The block of audio a cut routing loop is rejoined through (#2612). An input
// route that closes a cycle cannot be an ordering edge, so the edge is cut: a
// FeedbackSend fills the carry and a FeedbackReturn reads what was in it before
// this block started. Audio only; the compiler refuses a MIDI loop.

using magda::engine::BlockInfo;
using magda::engine::EngineAudioSource;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanValues;
using magda::engine::PortRef;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::SignalKind;

namespace {

constexpr int kMaxBlock = 512;

/// Writes one constant to every sample, so what comes back out of a carry says
/// which block it was written in.
class ConstantAudio final : public EngineAudioSource {
  public:
    void render(const BlockInfo& /*block*/, juce::dsp::AudioBlock<float> out) override {
        out.fill(value);
    }

    float value = 0.0f;
};

/// ClipAudio -> FeedbackSend, and FeedbackReturn -> Output: one carry, with
/// what went in on one side and what comes back on the other.
struct AudioCarryHarness {
    RenderPlan plan;
    PlanExecutor executor;
    PlanBindings bindings;
    ConstantAudio source;
    PlanValues values;
    juce::AudioBuffer<float> output{2, kMaxBlock};

    AudioCarryHarness() {
        magda::engine::PlanOp ret;
        ret.kind = OpKind::FeedbackReturn;
        ret.key.trackId = 2;
        ret.key.role = OpRole::FeedbackReturn;
        ret.outputs = {SignalKind::Audio};
        plan.ops.push_back(ret);

        magda::engine::PlanOp out;
        out.kind = OpKind::Output;
        out.key.trackId = 2;
        out.key.role = OpRole::HardwareOutput;
        out.inputs = {PortRef{0, 0}};
        plan.ops.push_back(out);

        magda::engine::PlanOp clip;
        clip.kind = OpKind::ClipAudio;
        clip.key.trackId = 1;
        clip.key.role = OpRole::ClipAudio;
        clip.outputs = {SignalKind::Audio};
        plan.ops.push_back(clip);

        magda::engine::PlanOp send;
        send.kind = OpKind::FeedbackSend;
        send.key.trackId = 2;
        send.key.role = OpRole::FeedbackSend;
        send.inputs = {PortRef{2, 0}, PortRef{0, 0}};
        plan.ops.push_back(send);

        plan.outputOps = {1};
        magda::engine::bakeScheduling(plan);

        bindings.clipAudio[1] = &source;

        values.planFingerprint = magda::engine::planFingerprint(plan);
        values.ops.assign(plan.ops.size(), magda::engine::kUnityValue);
    }

    void prepare() {
        const auto messages =
            executor.prepare(plan, bindings, RenderContext{44100.0, kMaxBlock, 2});
        for (const auto& message : messages)
            UNSCOPED_INFO("prepare: " << message);
        REQUIRE(messages.empty());
    }

    /// Renders @p numSamples with the source writing @p value, and hands back
    /// what the carry returned this block.
    std::vector<float> render(int numSamples, float value) {
        source.value = value;
        output.clear();

        BlockInfo block;
        block.numSamples = numSamples;
        block.playing = true;
        block.continuous = true;
        executor.process(values, block, output);

        return {output.getReadPointer(0), output.getReadPointer(0) + numSamples};
    }
};

bool allOf(const std::vector<float>& samples, float value) {
    return std::ranges::all_of(samples, [value](float sample) { return sample == value; });
}

}  // namespace

TEST_CASE("a carry hands back the block before it", "[engine][exec][2612]") {
    AudioCarryHarness harness;
    harness.prepare();

    // Nothing has been written yet, so the first block is silence rather than
    // whatever the buffer happened to hold.
    CHECK(allOf(harness.render(kMaxBlock, 1.0f), 0.0f));
    CHECK(allOf(harness.render(kMaxBlock, 2.0f), 1.0f));
    CHECK(allOf(harness.render(kMaxBlock, 3.0f), 2.0f));
}

TEST_CASE("a short block does not leave the tail of a long one in the carry",
          "[engine][exec][2612]") {
    // Callback sizes vary, and the carry is sized for the largest. A block
    // that fills only part of it must not leave the rest to come back as the
    // end of the next read.
    AudioCarryHarness harness;
    harness.prepare();

    harness.render(kMaxBlock, 1.0f);

    // A quarter-length block: what it wrote is a quarter of the carry.
    const auto afterLong = harness.render(kMaxBlock / 4, 2.0f);
    CHECK(allOf(afterLong, 1.0f));

    // Back to full length. The first quarter is the short block's, and the
    // rest is silence, not the 1.0f from two callbacks ago.
    const auto afterShort = harness.render(kMaxBlock, 3.0f);
    REQUIRE(afterShort.size() == static_cast<std::size_t>(kMaxBlock));
    CHECK(allOf({afterShort.begin(), afterShort.begin() + kMaxBlock / 4}, 2.0f));
    CHECK(allOf({afterShort.begin() + kMaxBlock / 4, afterShort.end()}, 0.0f));
}

TEST_CASE("A carry and its return must agree about liveness", "[engine][plan][2612]") {
    // The compiler settles the two together, so this is the validator's job
    // alone: a return that says deterministic while its send fills it with a
    // hardware input would let every op downstream claim it may be rendered
    // ahead of the outside world.
    AudioCarryHarness harness;
    auto& plan = harness.plan;

    // Nothing is live yet, so both agree and the plan is well formed.
    CHECK(magda::engine::validatePlan(plan).empty());

    // Make what fills the carry live, and leave the return as it was.
    plan.ops[2].kind = OpKind::AudioInput;
    plan.ops[2].key.role = OpRole::LiveAudioInput;
    plan.ops[2].liveness = magda::engine::LivenessDomain::Live;

    const auto problems = magda::engine::validatePlan(plan);
    CHECK(std::ranges::any_of(problems, [](const std::string& problem) {
        return problem.find("its send fills it with live audio") != std::string::npos;
    }));

    // And it is well formed again once the return says the same, and the ops
    // reading it follow: that is the propagation this rule exists to protect.
    for (auto& op : plan.ops)
        op.liveness = magda::engine::LivenessDomain::Live;
    CHECK(magda::engine::validatePlan(plan).empty());
}
