#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>

#include "NullDiffGain.hpp"
#include "core/TrackInfo.hpp"
#include "exec/ParallelPlanExecutor.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/RenderThreadPool.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * @file test_render_denormals.cpp
 * @brief Flush-to-zero is on wherever a block is rendered (#2240).
 *
 * A device that feeds its own output back through a filter decays into the
 * denormal range and stays there, where on Intel every multiply is a microcode
 * trap and a reverb tail is where a dropout comes from. The mode is a per-thread
 * CPU flag, so it has to be set on every thread that renders rather than once at
 * startup: the audio callback's, the offline render's, and each of the pool's.
 *
 * Asserted from inside a device, because that is where the arithmetic the mode
 * exists for actually happens.
 */

using namespace magda::engine;

namespace {

constexpr int kBlockSize = 512;

/**
 * @brief A denormal multiplied here and now, and what came back.
 *
 * Through volatile, so the compiler cannot fold it into a constant that never
 * met the CPU's flush-to-zero bit: a folded answer is the same on every thread
 * and would make this file pass without the mode being set anywhere.
 */
float multiplyDenormal() {
    volatile float tiny = std::numeric_limits<float>::denorm_min();
    volatile float one = 1.0f;
    return tiny * one;
}

/// Records, from inside the render, what a denormal multiply came to.
class DenormalProbe final : public EngineDevice {
  public:
    void process(DeviceBlock&) override {
        product = multiplyDenormal();
        rendered = true;
    }

    void setMidiInputBoundBytes(int) override {}
    void setMidiOutputBoundBytes(int) override {}

    bool forwardsMidiInput() const override {
        return false;
    }

    float product = std::numeric_limits<float>::quiet_NaN();
    bool rendered = false;
};

/// One track carrying one device, feeding the master.
RenderPlan planWithDevice(magda::DeviceId device) {
    magda::TrackInfo track;
    track.id = 1;
    track.type = magda::TrackType::Media;
    track.name = "Track";
    track.chain.fxChainElements.emplace_back(magda::nulldiff::gainDevice(device));

    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;
    master.type = magda::TrackType::Master;
    master.name = "Master";

    return compileRenderPlan({track}, master);
}

PlanBindings bindingsFor(const RenderPlan& plan, EngineDevice& probe) {
    PlanBindings bindings;
    for (const auto& op : plan.ops)
        if (op.kind == OpKind::Device)
            bindings.devices[op.key.deviceKey()] = &probe;

    return bindings;
}

PlanValues valuesFor(const RenderPlan& plan) {
    PlanValues values;
    values.planFingerprint = planFingerprint(plan);
    values.ops.assign(plan.ops.size(), kUnityValue);
    return values;
}

/// Prepare, reporting whatever it says. A plan with no clip source bound
/// renders silence and says so, which is what this file wants: the device is
/// the point and there is nothing for it to be fed.
template <typename Executor>
void prepared(Executor& executor, const RenderPlan& plan, PlanBindings& bindings) {
    for (const auto& message :
         executor.prepare(plan, bindings, RenderContext{44100.0, kBlockSize, 2}))
        UNSCOPED_INFO("prepare: " << message);

    REQUIRE(executor.isPrepared());
}

BlockInfo oneBlock() {
    BlockInfo block;
    block.numSamples = kBlockSize;
    block.playing = true;
    block.continuous = true;
    return block;
}

}  // namespace

TEST_CASE("A device renders with flush-to-zero on", "[engine][render][denormals][2240]") {
    // Without the mode this is denorm_min, which is what a filter tail decays
    // into and where the multiplies stop being free.
    REQUIRE(multiplyDenormal() > 0.0f);

    const auto plan = planWithDevice(10);

    DenormalProbe probe;
    auto bindings = bindingsFor(plan, probe);
    auto values = valuesFor(plan);

    PlanExecutor executor;
    prepared(executor, plan, bindings);

    juce::AudioBuffer<float> output(2, kBlockSize);
    output.clear();
    executor.process(values, oneBlock(), output);

    REQUIRE(probe.rendered);
    CHECK(probe.product == 0.0f);
}

TEST_CASE("A device rendered on a pool thread has it too", "[engine][render][denormals][2240]") {
    const auto plan = planWithDevice(11);

    DenormalProbe probe;
    auto bindings = bindingsFor(plan, probe);
    auto values = valuesFor(plan);

    // The workers set their own, since the flag belongs to the thread and the
    // executor's caller is not the one running the op.
    RenderThreadPool pool(2, false);
    ParallelPlanExecutor executor(&pool);
    prepared(executor, plan, bindings);

    juce::AudioBuffer<float> output(2, kBlockSize);
    output.clear();
    executor.process(values, oneBlock(), output);

    REQUIRE(probe.rendered);
    CHECK(probe.product == 0.0f);
}

TEST_CASE("The mode does not outlive the block it was set for",
          "[engine][render][denormals][2240]") {
    const auto plan = planWithDevice(12);

    DenormalProbe probe;
    auto bindings = bindingsFor(plan, probe);
    auto values = valuesFor(plan);

    PlanExecutor executor;
    prepared(executor, plan, bindings);

    juce::AudioBuffer<float> output(2, kBlockSize);
    output.clear();
    executor.process(values, oneBlock(), output);

    // Scoped rather than set once: the host's thread is not the engine's to
    // leave in a mode it did not ask for, and an offline render runs on a
    // thread that goes on to do other things.
    CHECK(multiplyDenormal() > 0.0f);
}
