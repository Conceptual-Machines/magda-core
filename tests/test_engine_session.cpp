#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <thread>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/RenderPlan.hpp"

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::DeviceBlock;
using magda::engine::EngineAudioSource;
using magda::engine::EngineDevice;
using magda::engine::EngineSession;
using magda::engine::PlanValues;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::RuntimeStateFactory;
using magda::engine::RuntimeStateIds;

namespace {

constexpr int kBlockSize = 64;

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

/// Where an object was destroyed, which is the whole question this slice
/// answers: never on the thread that renders.
struct Ledger {
    std::atomic<int> devicesCreated{0};
    std::atomic<int> devicesDestroyed{0};
    std::atomic<int> sourcesCreated{0};
    std::atomic<std::thread::id> lastDestroyingThread{};
};

class LedgerDevice final : public EngineDevice {
  public:
    LedgerDevice(Ledger& ledger, DeviceId id) : ledger_(ledger), id_(id) {
        ++ledger_.devicesCreated;
    }

    ~LedgerDevice() override {
        ledger_.lastDestroyingThread.store(std::this_thread::get_id());
        ++ledger_.devicesDestroyed;
    }

    void process(DeviceBlock& block) override {
        ++blocksProcessed;
        block.audio.multiplyBy(0.5f);
    }

    DeviceId id() const {
        return id_;
    }

    int blocksProcessed = 0;

  private:
    Ledger& ledger_;
    DeviceId id_;
};

class ConstantSource final : public EngineAudioSource {
  public:
    explicit ConstantSource(float level) : level_(level) {}

    void render(const BlockInfo&, juce::dsp::AudioBlock<float> out) override {
        out.fill(level_);
    }

  private:
    float level_;
};

/// Hands out one object per model ID and remembers what it made, so a test can
/// tell a carried-over instance from a rebuilt one.
class TestFactory final : public RuntimeStateFactory {
  public:
    explicit TestFactory(Ledger& ledger) : ledger_(ledger) {}

    std::unique_ptr<EngineDevice> createDevice(DeviceId id) override {
        auto device = std::make_unique<LedgerDevice>(ledger_, id);
        lastDevice = device.get();
        devicesById[id] = device.get();
        return device;
    }

    std::unique_ptr<EngineAudioSource> createClipAudioSource(TrackId) override {
        ++ledger_.sourcesCreated;
        return std::make_unique<ConstantSource>(1.0f);
    }

    LedgerDevice* lastDevice = nullptr;
    std::map<DeviceId, LedgerDevice*> devicesById;

  private:
    Ledger& ledger_;
};

std::shared_ptr<const RenderPlan> compile(const std::vector<TrackInfo>& tracks) {
    return std::make_shared<const RenderPlan>(
        magda::engine::compileRenderPlan(tracks, makeMaster()));
}

PlanValues resolve(const RenderPlan& plan, const std::vector<TrackInfo>& tracks) {
    PlanValues values;
    magda::engine::resolvePlanValues(plan, tracks, makeMaster(), values);
    return values;
}

RenderContext context() {
    return RenderContext{44100.0, kBlockSize, 2};
}

/// What the model holds, which is what decides when runtime state is destroyed.
RuntimeStateIds modelIds(const std::vector<TrackInfo>& tracks) {
    return magda::engine::collectRuntimeStateIds(tracks, makeMaster());
}

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-5);
}

/// A track with one effect on it, which is enough shape for every question
/// here: one device to carry or retire, one source, one output.
std::vector<TrackInfo> trackWithEffect(DeviceId deviceId) {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(deviceId)));
    return {track};
}

}  // namespace

TEST_CASE("A session renders nothing until a plan is published", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    juce::AudioBuffer<float> output(2, kBlockSize);
    output.clear();
    session.process(BlockInfo{kBlockSize, 0, true}, output);

    CHECK(session.livePlan() == nullptr);
    CHECK(output.getSample(0, 0) == approx(0.0f));
}

TEST_CASE("Publishing a plan puts it on the audio thread", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto tracks = trackWithEffect(7);
    const auto plan = compile(tracks);
    const auto result = session.publish(plan, context(), modelIds(tracks));
    session.publishValues(resolve(*plan, tracks));

    CHECK(result.published);
    CHECK(result.messages.empty());
    CHECK(session.livePlan() == plan);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(BlockInfo{kBlockSize, 0, true}, output);

    CHECK(output.getSample(0, 0) == approx(0.5f));
}

TEST_CASE("A swap carries runtime state that the new plan still names", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto first = trackWithEffect(7);
    const auto firstPlan = compile(first);
    session.publish(firstPlan, context(), modelIds(first));
    session.publishValues(resolve(*firstPlan, first));

    auto* device = factory.devicesById[7];
    REQUIRE(device != nullptr);
    CHECK(ledger.devicesCreated == 1);

    // A second device arrives ahead of the first. The plan is new, every op
    // index has shifted, and the instrument that was already there must not
    // notice: rebuilding it is the click this design exists to prevent.
    auto tracks = first;
    tracks[0].chain.fxChainElements.insert(tracks[0].chain.fxChainElements.begin(),
                                           makeDeviceElement(makeEffect(8)));
    const auto secondPlan = compile(tracks);
    session.publish(secondPlan, context(), modelIds(tracks));
    session.publishValues(resolve(*secondPlan, tracks));

    CHECK(ledger.devicesCreated == 2);
    CHECK(ledger.devicesDestroyed == 0);
    CHECK(factory.devicesById[7] == device);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(BlockInfo{kBlockSize, 0, true}, output);
    CHECK(output.getSample(0, 0) == approx(0.25f));
}

TEST_CASE("What a swap drops is destroyed on the publishing thread", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto withDevice = trackWithEffect(7);
    const auto firstPlan = compile(withDevice);
    session.publish(firstPlan, context(), modelIds(withDevice));
    session.publishValues(resolve(*firstPlan, withDevice));

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(BlockInfo{kBlockSize, 0, true}, output);
    REQUIRE(ledger.devicesDestroyed == 0);

    const std::vector<TrackInfo> withoutDevice{makeTrack(1)};
    const auto secondPlan = compile(withoutDevice);
    session.publish(secondPlan, context(), modelIds(withoutDevice));

    // Gone, and gone here: the audio thread neither waited for it nor ran its
    // destructor.
    CHECK(ledger.devicesDestroyed == 1);
    CHECK(ledger.lastDestroyingThread.load() == std::this_thread::get_id());
    CHECK(session.runtimeObjectCount() == 1);  // the clip source is still named
}

TEST_CASE("Bypass and chain power keep the device, deletion destroys it", "[engine][session]") {
    // Both are structural: a bypassed device contributes no ops, and neither
    // does anything on a chain with the power off. Keyed on the plan, either
    // gesture would tear the plugin down and rebuild it on the way back,
    // losing its tail and its state for what the user reads as a toggle.
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    auto tracks = trackWithEffect(7);
    const auto first = compile(tracks);
    session.publish(first, context(), modelIds(tracks));
    auto* device = factory.devicesById[7];
    REQUIRE(device != nullptr);

    SECTION("bypassed") {
        auto bypassed = tracks;
        getDevice(bypassed[0].chain.fxChainElements[0]).bypassed = true;
        session.publish(compile(bypassed), context(), modelIds(bypassed));

        CHECK(ledger.devicesDestroyed == 0);
        CHECK(factory.devicesById[7] == device);

        session.publish(compile(tracks), context(), modelIds(tracks));
        CHECK(ledger.devicesCreated == 1);
        CHECK(factory.devicesById[7] == device);
    }

    SECTION("chain power off") {
        auto powered = tracks;
        powered[0].chain.enabled = false;
        session.publish(compile(powered), context(), modelIds(powered));

        CHECK(ledger.devicesDestroyed == 0);

        powered[0].chain.enabled = true;
        session.publish(compile(powered), context(), modelIds(powered));
        CHECK(ledger.devicesCreated == 1);
        CHECK(factory.devicesById[7] == device);
    }

    SECTION("deleted from the model") {
        const std::vector<TrackInfo> without{makeTrack(1)};
        session.publish(compile(without), context(), modelIds(without));

        CHECK(ledger.devicesDestroyed == 1);
        CHECK(ledger.lastDestroyingThread.load() == std::this_thread::get_id());
    }
}

TEST_CASE("Model IDs that have moved on cannot retire what the live plan uses",
          "[engine][session]") {
    // Plans are compiled from one model revision and published against
    // whatever the model has become, so the two arguments to retirement drift
    // apart by design. When they do, the plan wins: what it names is reachable
    // from the audio thread this instant, and freeing it is not an early
    // eviction but a pointer the callback is about to follow.
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto tracks = trackWithEffect(7);
    const auto plan = compile(tracks);

    // The model has already lost the device this plan renders through.
    const std::vector<TrackInfo> without{makeTrack(1)};
    session.publish(plan, context(), modelIds(without));
    session.publishValues(resolve(*plan, tracks));

    CHECK(ledger.devicesDestroyed == 0);
    REQUIRE(factory.devicesById[7] != nullptr);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(BlockInfo{kBlockSize, 0, true}, output);
    CHECK(output.getSample(0, 0) == approx(0.5f));

    // It goes when the plan stops naming it too, which is the first moment
    // nothing can reach it.
    session.publish(compile(without), context(), modelIds(without));
    CHECK(ledger.devicesDestroyed == 1);
    CHECK(ledger.lastDestroyingThread.load() == std::this_thread::get_id());
}

TEST_CASE("A plan that does not prepare leaves the live one playing", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto tracks = trackWithEffect(7);
    const auto good = compile(tracks);
    session.publish(good, context(), modelIds(tracks));
    session.publishValues(resolve(*good, tracks));

    auto broken = std::make_shared<RenderPlan>();
    magda::engine::PlanOp op;
    op.kind = magda::engine::OpKind::Gain;
    op.inputs = {magda::engine::PortRef{4, 0}};  // reads an op that does not exist
    op.outputs = {magda::engine::SignalKind::Audio};
    broken->ops.push_back(op);

    const auto refused = session.publish(broken, context(), modelIds(tracks));

    CHECK_FALSE(refused.published);
    REQUIRE_FALSE(refused.messages.empty());
    CHECK(session.livePlan() == good);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(BlockInfo{kBlockSize, 0, true}, output);
    CHECK(output.getSample(0, 0) == approx(0.5f));
}

TEST_CASE("A first plan that is refused leaves a session with nothing live", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    auto broken = std::make_shared<RenderPlan>();
    magda::engine::PlanOp op;
    op.kind = magda::engine::OpKind::Gain;
    op.inputs = {magda::engine::PortRef{4, 0}};
    op.outputs = {magda::engine::SignalKind::Audio};
    broken->ops.push_back(op);

    const auto tracks = trackWithEffect(7);
    const auto refused = session.publish(broken, context(), modelIds(tracks));

    CHECK_FALSE(refused.published);
    CHECK(session.livePlan() == nullptr);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(BlockInfo{kBlockSize, 0, true}, output);
    CHECK(output.getSample(0, 0) == approx(0.0f));

    // This one named nothing, so nothing was created for it. What a refused
    // plan does create stays for the retry, and leaves when the model stops
    // naming it rather than when a plan stops using it.
    CHECK(session.runtimeObjectCount() == 0);

    const auto good = compile(tracks);
    CHECK(session.publish(good, context(), modelIds(tracks)).published);
    CHECK(session.livePlan() == good);
}

TEST_CASE("Values published for the old plan are not applied to the new one", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    auto tracks = trackWithEffect(7);
    tracks[0].volume = 0.5f;
    const auto firstPlan = compile(tracks);
    session.publish(firstPlan, context(), modelIds(tracks));
    session.publishValues(resolve(*firstPlan, tracks));

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(BlockInfo{kBlockSize, 0, true}, output);
    REQUIRE(output.getSample(0, 0) == approx(0.25f));

    // Swap the device out and publish nothing new: the values in flight belong
    // to a plan that no longer exists.
    auto replaced = tracks;
    replaced[0].chain.fxChainElements[0] = makeDeviceElement(makeEffect(8));
    const auto secondPlan = compile(replaced);
    session.publish(secondPlan, context(), modelIds(replaced));

    session.process(BlockInfo{kBlockSize, 0, true}, output);
    CHECK(output.getSample(0, 0) == approx(0.5f));  // unity fader, device gain only

    session.publishValues(resolve(*secondPlan, replaced));
    session.process(BlockInfo{kBlockSize, 0, true}, output);
    CHECK(output.getSample(0, 0) == approx(0.25f));
}

// This is also the sanitizer target. A clean pass here says the swap holds
// under load; whether it is free of races is a question only a sanitizer
// answers, and this suite is not built with one:
//
//   cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMAGDA_BUILD_TESTS=ON \
//       -DCMAKE_CXX_FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=thread" \
//       -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
//   cmake --build build-tsan --target magda_tests
//   ./build-tsan/tests/magda_tests "[engine][session]"
//
// A clean sanitizer run means nothing unless the harness can fail: a plain
// non-atomic counter incremented from both threads below is enough to check
// that the sanitizer is really watching.
TEST_CASE("Swapping under a running audio thread never tears", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    auto tracks = trackWithEffect(7);
    const auto firstPlan = compile(tracks);
    session.publish(firstPlan, context(), modelIds(tracks));
    session.publishValues(resolve(*firstPlan, tracks));

    std::atomic<bool> running{true};
    std::atomic<int> blocks{0};
    std::atomic<int> badBlocks{0};

    // Stands in for the callback: one thread, rendering back to back, never
    // told that anything is being republished underneath it.
    std::thread audio([&] {
        juce::AudioBuffer<float> output(2, kBlockSize);
        while (running.load(std::memory_order_relaxed)) {
            session.process(BlockInfo{kBlockSize, 0, true}, output);
            const auto sample = output.getSample(0, 0);
            // Every plan in the rotation renders one of these, and a torn read
            // would land somewhere else entirely.
            if (sample != Catch::Approx(0.5f).margin(1e-5) &&
                sample != Catch::Approx(0.25f).margin(1e-5))
                ++badBlocks;
            ++blocks;
        }
    });

    for (int round = 0; round < 300; ++round) {
        auto twoDevices = tracks;
        twoDevices[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));

        const auto wide = compile(twoDevices);
        session.publish(wide, context(), modelIds(twoDevices));
        session.publishValues(resolve(*wide, twoDevices));

        // Device 8 leaves the plan and the model together, so it is destroyed
        // rather than kept dormant.
        const auto narrow = compile(tracks);
        session.publish(narrow, context(), modelIds(tracks));
        session.publishValues(resolve(*narrow, tracks));
    }

    running.store(false);
    audio.join();

    CHECK(blocks > 0);
    CHECK(badBlocks == 0);
    // Device 8 came and went a hundred times; device 7 was named throughout.
    CHECK(ledger.devicesDestroyed > 0);
    CHECK(factory.devicesById[7] != nullptr);
    CHECK(ledger.lastDestroyingThread.load() == std::this_thread::get_id());
}
