#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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

/**
 * @brief Two devices that stay inside process() until both are.
 *
 * The pool's caller takes work as well as waking the workers
 * (RenderThreadPool::render), so a plan with one device is usually rendered by
 * the thread that asked for it, under the executor's own guard. One op cannot
 * prove a worker set anything. Two that have to overlap can, because one thread
 * cannot be inside both.
 *
 * Blocking inside process() is a thing no real device may do. It is safe here
 * for the reason it is safe in test_parallel_executor.cpp: the branches are
 * independent, and the wait times out rather than hanging.
 */
struct Meeting {
    std::mutex mutex;
    std::condition_variable arrived;
    int inside = 0;
    bool met = false;
};

/// Records the thread it ran on and what a denormal came to there.
class RendezvousProbe final : public EngineDevice {
  public:
    explicit RendezvousProbe(Meeting& meeting) : meeting_(meeting) {}

    void process(DeviceBlock&) override {
        product = multiplyDenormal();
        thread = std::this_thread::get_id();

        std::unique_lock<std::mutex> lock(meeting_.mutex);
        if (++meeting_.inside >= 2) {
            meeting_.met = true;
            meeting_.arrived.notify_all();
        } else {
            meeting_.arrived.wait_for(lock, std::chrono::seconds(2),
                                      [this] { return meeting_.met; });
        }
        --meeting_.inside;
    }

    void setMidiInputBoundBytes(int) override {}
    void setMidiOutputBoundBytes(int) override {}

    bool forwardsMidiInput() const override {
        return false;
    }

    float product = std::numeric_limits<float>::quiet_NaN();
    std::thread::id thread;

  private:
    Meeting& meeting_;
};

/// One track per device, each feeding the master.
RenderPlan planWithDevices(const std::vector<magda::DeviceId>& devices) {
    std::vector<magda::TrackInfo> tracks;
    for (std::size_t at = 0; at < devices.size(); ++at) {
        magda::TrackInfo track;
        track.id = static_cast<magda::TrackId>(at + 1);
        track.type = magda::TrackType::Media;
        track.name = "Track " + juce::String(track.id);
        track.chain.fxChainElements.emplace_back(magda::nulldiff::gainDevice(devices[at]));
        tracks.push_back(std::move(track));
    }

    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;
    master.type = magda::TrackType::Master;
    master.name = "Master";

    return compileRenderPlan(tracks, master);
}

RenderPlan planWithDevice(magda::DeviceId device) {
    return planWithDevices({device});
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
    const auto plan = planWithDevices({11, 12});

    Meeting meeting;
    RendezvousProbe first(meeting);
    RendezvousProbe second(meeting);

    PlanBindings bindings;
    for (const auto& op : plan.ops)
        if (op.kind == OpKind::Device)
            bindings.devices[op.key.deviceKey()] = op.key.deviceId == 11
                                                       ? static_cast<EngineDevice*>(&first)
                                                       : static_cast<EngineDevice*>(&second);

    auto values = valuesFor(plan);

    RenderThreadPool pool(3, false);
    ParallelPlanExecutor executor(&pool);
    prepared(executor, plan, bindings);

    juce::AudioBuffer<float> output(2, kBlockSize);
    output.clear();
    executor.process(values, oneBlock(), output);

    // Both were inside process() at once, so one thread cannot have run them:
    // at least one is a worker, and it set its own mode rather than inheriting
    // the caller's.
    REQUIRE(meeting.met);
    REQUIRE(first.thread != second.thread);
    CHECK((first.thread != std::this_thread::get_id() ||
           second.thread != std::this_thread::get_id()));

    CHECK(first.product == 0.0f);
    CHECK(second.product == 0.0f);
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
