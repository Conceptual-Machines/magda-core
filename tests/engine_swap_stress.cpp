// The plan swap under load: one thread renders continuously while another
// republishes, for as long as it takes to be convincing.
//
// Separate from the test suite because its point is to be run under
// ThreadSanitizer, which the suite is not built with:
//
//   cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMAGDA_BUILD_TESTS=ON \
//       -DCMAKE_CXX_FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=thread" \
//       -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
//   cmake --build build-tsan --target magda_engine_swap_stress
//   ./build-tsan/tests/magda_engine_swap_stress
//
// A clean run means nothing unless the harness can fail: adding a plain
// non-atomic counter incremented from both threads is enough to check that
// the sanitizer is really watching.
#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include "exec/EngineSession.hpp"

using namespace magda;
using namespace magda::engine;

namespace {

class Tone final : public EngineAudioSource {
  public:
    void render(const BlockInfo&, juce::dsp::AudioBlock<float> out) override {
        out.fill(0.5f);
    }
};

class Half final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        block.audio.multiplyBy(0.5f);
    }
};

class Factory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineDevice> createDevice(DeviceId) override {
        return std::make_unique<Half>();
    }
    std::unique_ptr<EngineAudioSource> createClipAudioSource(TrackId) override {
        return std::make_unique<Tone>();
    }
};

/// source -> [device] -> output, built by hand so the harness needs no model.
std::shared_ptr<const RenderPlan> makePlan(int numDevices) {
    auto plan = std::make_shared<RenderPlan>();
    PlanOp source;
    source.kind = OpKind::ClipAudio;
    source.key =
        OpKey{1, INVALID_RACK_ID, INVALID_CHAIN_ID, INVALID_DEVICE_ID, OpRole::ClipAudio, 0};
    source.outputs = {SignalKind::Audio};
    plan->ops.push_back(source);

    PortRef signal{0, 0};
    for (int i = 0; i < numDevices; ++i) {
        PlanOp device;
        device.kind = OpKind::Device;
        device.key = OpKey{1, INVALID_RACK_ID, INVALID_CHAIN_ID, 100 + i, OpRole::DeviceProcess, 0};
        device.inputs = {signal, noInput(), noInput()};
        device.outputs = {SignalKind::Audio};
        plan->ops.push_back(device);
        signal = PortRef{static_cast<OpId>(plan->ops.size()) - 1, 0};
    }

    PlanOp output;
    output.kind = OpKind::Output;
    output.key = OpKey{MASTER_TRACK_ID,   INVALID_RACK_ID,        INVALID_CHAIN_ID,
                       INVALID_DEVICE_ID, OpRole::HardwareOutput, 0};
    output.inputs = {signal};
    plan->ops.push_back(output);
    plan->outputOps.push_back(static_cast<OpId>(plan->ops.size()) - 1);

    bakeScheduling(*plan);
    return plan;
}

PlanValues valuesFor(const RenderPlan& plan) {
    PlanValues values;
    values.planFingerprint = planFingerprint(plan);
    values.ops.assign(plan.ops.size(), OpValue{});
    return values;
}

}  // namespace

int main() {
    Factory factory;
    EngineSession session(factory);

    // The harness builds plans rather than models, so it states directly what
    // the model would have held: every device it ever names, and the one track.
    RuntimeStateIds modelIds;
    modelIds.tracks = {1};
    modelIds.devices = {100, 101, 102};

    const RenderContext context{44100.0, 64, 2};
    auto plan = makePlan(1);
    session.publish(plan, context, modelIds);
    session.publishValues(valuesFor(*plan));

    std::atomic<bool> running{true};
    std::atomic<long> blocks{0};

    std::thread audio([&] {
        juce::AudioBuffer<float> out(2, 64);
        while (running.load(std::memory_order_relaxed)) {
            session.process(BlockInfo{64, 0, true}, out);
            ++blocks;
        }
    });

    for (int round = 0; round < 500; ++round) {
        auto next = makePlan(1 + (round % 3));
        session.publish(next, context, modelIds);
        session.publishValues(valuesFor(*next));
    }

    running.store(false);
    audio.join();
    std::printf("blocks rendered: %ld\n", blocks.load());
    return 0;
}
