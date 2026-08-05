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

    /// Stands in for what a real one does: resize the buffers process() reads
    /// and clear whatever it had accumulated.
    void prepare(const RenderContext&) override {
        ++prepareCalls;
        blocksProcessed = 0;
    }

    void process(DeviceBlock& block) override {
        ++blocksProcessed;
        block.audio.multiplyBy(0.5f);
    }

    int latencySamples() const override {
        return latency;
    }

    DeviceId id() const {
        return id_;
    }

    int blocksProcessed = 0;
    int prepareCalls = 0;
    /// What a plugin reports once it is loaded, and may report differently
    /// later. Not const for the same reason the real thing is not.
    int latency = 0;

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
        device->latency = latencyFor.count(id) != 0 ? latencyFor[id] : 0;
        lastDevice = device.get();
        devicesById[id] = device.get();
        return device;
    }

    /// What a device reports once it is loaded, per device ID.
    std::map<DeviceId, int> latencyFor;

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

/// Publishing a plan means publishing the values that belong to it: without
/// them the epoch would render at unity until the next update arrived, which
/// is a fader jumping to 0 dB on every structural edit.
EngineSession::Result publish(EngineSession& session, const std::shared_ptr<const RenderPlan>& plan,
                              const std::vector<TrackInfo>& tracks) {
    return session.publish(plan, context(), modelIds(tracks), resolve(*plan, tracks));
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

/// Records the stretch of timeline it was asked for, so a test can see how a
/// callback was cut up on its way to the ops.
class BlockLog final : public EngineAudioSource {
  public:
    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        out.fill(1.0f);
        blocks.push_back(block);
    }

    std::vector<BlockInfo> blocks;
};

class LoggingFactory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineAudioSource> createClipAudioSource(TrackId) override {
        auto source = std::make_unique<BlockLog>();
        log = source.get();
        return source;
    }

    BlockLog* log = nullptr;
};

/// Playing from a beat, with the metronome off.
magda::engine::TransportSnapshot rolling(double fromBeat) {
    magda::engine::TransportSnapshot transport;
    transport.request = {1, true, fromBeat, 0.0};
    return transport;
}

}  // namespace

TEST_CASE("A session renders nothing until a plan is published", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    juce::AudioBuffer<float> output(2, kBlockSize);
    output.clear();
    session.process(kBlockSize, output);

    CHECK(session.livePlan() == nullptr);
    CHECK(output.getSample(0, 0) == approx(0.0f));
}

TEST_CASE("Publishing a plan puts it on the audio thread", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto tracks = trackWithEffect(7);
    const auto plan = compile(tracks);
    const auto result = publish(session, plan, tracks);
    session.publishValues(resolve(*plan, tracks));

    CHECK(result.published);
    CHECK(result.messages.empty());
    CHECK(session.livePlan() == plan);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);

    CHECK(output.getSample(0, 0) == approx(0.5f));
}

TEST_CASE("A latency change re-prepares the plan rather than recompiling it",
          "[engine][session][pdc]") {
    // Where the compensation goes is topology and lives in the plan; how many
    // samples it holds comes from an instance the compiler has never seen. So a
    // plugin that changes what it reports is this: the same plan, published
    // again, with nothing rebuilt and nothing recompiled.
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    auto tracks = trackWithEffect(7);
    tracks.push_back(makeTrack(2));
    const auto plan = compile(tracks);

    REQUIRE(publish(session, plan, tracks).published);
    session.publishValues(resolve(*plan, tracks));

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(1.5f));

    auto* device = factory.devicesById[7];
    REQUIRE(device != nullptr);
    const auto devicesCreated = ledger.devicesCreated.load();
    device->latency = 16;

    REQUIRE(publish(session, plan, tracks).published);

    CHECK(session.livePlan() == plan);
    CHECK(ledger.devicesCreated.load() == devicesCreated);
    CHECK(factory.devicesById[7] == device);

    // Track 2 now waits for the track the device is on, so the first sixteen
    // samples are the latent track alone and the rest are both.
    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.5f));
    CHECK(output.getSample(0, 15) == approx(0.5f));
    CHECK(output.getSample(0, 16) == approx(1.5f));
}

TEST_CASE("Republishing a plan applies the values it was published with", "[engine][session]") {
    // Two tables with the same fingerprint fit the same structure. That says
    // nothing about which of them is newer, and republishing a plan whose
    // structure did not change is the ordinary case: a plugin reporting new
    // latency re-prepares exactly this plan. Selecting on compatibility alone
    // would keep choosing the table from before and never apply this one.
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    auto tracks = trackWithEffect(7);
    tracks[0].volume = 0.5f;
    const auto plan = compile(tracks);

    publish(session, plan, tracks);
    session.publishValues(resolve(*plan, tracks));

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    REQUIRE(output.getSample(0, 0) == approx(0.25f));

    // The same plan, and nothing published after it.
    tracks[0].volume = 1.0f;
    REQUIRE(publish(session, plan, tracks).published);
    session.process(kBlockSize, output);

    CHECK(output.getSample(0, 0) == approx(0.5f));
}

TEST_CASE("A plan published with another plan's values is refused", "[engine][session]") {
    // The executor would decline to apply them and render at unity, which is
    // the thing bundling values with an epoch exists to prevent. Better to
    // refuse the publish and keep playing what is already right.
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto tracks = trackWithEffect(7);
    const auto plan = compile(tracks);
    REQUIRE(publish(session, plan, tracks).published);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    REQUIRE(output.getSample(0, 0) == approx(0.5f));

    // A structurally different plan, and this plan's values.
    auto edited = tracks;
    edited[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    const auto other = compile(edited);

    SECTION("resolved against another plan") {
        const auto refused =
            session.publish(other, context(), modelIds(edited), resolve(*plan, tracks));

        CHECK_FALSE(refused.published);
        REQUIRE_FALSE(refused.messages.empty());
        CHECK(refused.messages.back().find("not values it can render") != std::string::npos);
    }

    SECTION("resolved against this plan, but not all of it") {
        // The right fingerprint and one op short of the plan it names, which
        // is what a table caught halfway through being assembled looks like.
        // The executor would decline that too, so the fingerprint alone is not
        // what makes a table applicable.
        auto truncated = resolve(*other, edited);
        REQUIRE(truncated.ops.size() > 1);
        truncated.ops.pop_back();

        const auto refused = session.publish(other, context(), modelIds(edited), truncated);

        CHECK_FALSE(refused.published);
        REQUIRE_FALSE(refused.messages.empty());
        CHECK(refused.messages.back().find("not values it can render") != std::string::npos);
    }

    // Still the plan that was right, still rendering what it was.
    CHECK(session.livePlan() == plan);
    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.5f));
}

TEST_CASE("An object that is already playing is never prepared again", "[engine][session][diff]") {
    // prepare() on a real plugin resizes the buffers process() reads and clears
    // what it has accumulated, and the instance a publish reuses is the one the
    // audio thread may be inside this instant. So it happens once, when the
    // object is made and nothing can reach it, and never again on a path that
    // runs while something is playing.
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    auto tracks = trackWithEffect(7);
    publish(session, compile(tracks), tracks);

    auto* device = factory.devicesById[7];
    REQUIRE(device != nullptr);
    CHECK(device->prepareCalls == 1);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    REQUIRE(device->blocksProcessed == 1);

    // A second device on the track: a structural edit that keeps the first one.
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    publish(session, compile(tracks), tracks);

    CHECK(device->prepareCalls == 1);
    CHECK(device->blocksProcessed == 1);

    // The one that is new is prepared, exactly once, before it plays anything.
    auto* added = factory.devicesById[8];
    REQUIRE(added != nullptr);
    CHECK(added->prepareCalls == 1);
    CHECK(added->blocksProcessed == 0);
}

TEST_CASE("A structural edit does not cut what is in flight", "[engine][session][diff]") {
    // Track 2 is compensated for track 1's latency, so sixteen samples of it
    // are always inside a delay line. An edit on track 1 has nothing to do with
    // that line, and the differ is what says so: the epoch being replaced hands
    // it over rather than the new one starting it empty.
    Ledger ledger;
    TestFactory factory(ledger);
    factory.latencyFor[7] = 16;
    EngineSession session(factory);

    auto tracks = trackWithEffect(7);
    tracks.push_back(makeTrack(2));
    const auto first = compile(tracks);

    REQUIRE(publish(session, first, tracks).published);
    session.publishValues(resolve(*first, tracks));

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);

    // Track 1 through its device, and track 2 arriving sixteen samples in.
    CHECK(output.getSample(0, 0) == approx(0.5f));
    CHECK(output.getSample(0, 16) == approx(1.5f));

    // A second device on track 1. Track 2's path is untouched by it.
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    const auto second = compile(tracks);
    REQUIRE(publish(session, second, tracks).published);
    session.publishValues(resolve(*second, tracks));

    session.process(kBlockSize, output);

    // Track 1 through both devices now, and track 2 straight away rather than
    // sixteen samples of silence and then a step.
    CHECK(output.getSample(0, 0) == approx(1.25f));
    CHECK(output.getSample(0, 16) == approx(1.25f));
}

TEST_CASE("A swap carries runtime state that the new plan still names", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto first = trackWithEffect(7);
    const auto firstPlan = compile(first);
    publish(session, firstPlan, first);
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
    publish(session, secondPlan, tracks);
    session.publishValues(resolve(*secondPlan, tracks));

    CHECK(ledger.devicesCreated == 2);
    CHECK(ledger.devicesDestroyed == 0);
    CHECK(factory.devicesById[7] == device);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.25f));
}

TEST_CASE("What a swap drops is destroyed on the publishing thread", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto withDevice = trackWithEffect(7);
    const auto firstPlan = compile(withDevice);
    publish(session, firstPlan, withDevice);
    session.publishValues(resolve(*firstPlan, withDevice));

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    REQUIRE(ledger.devicesDestroyed == 0);

    const std::vector<TrackInfo> withoutDevice{makeTrack(1)};
    const auto secondPlan = compile(withoutDevice);
    publish(session, secondPlan, withoutDevice);

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
    publish(session, first, tracks);
    auto* device = factory.devicesById[7];
    REQUIRE(device != nullptr);

    SECTION("bypassed") {
        auto bypassed = tracks;
        getDevice(bypassed[0].chain.fxChainElements[0]).bypassed = true;
        publish(session, compile(bypassed), bypassed);

        CHECK(ledger.devicesDestroyed == 0);
        CHECK(factory.devicesById[7] == device);

        publish(session, compile(tracks), tracks);
        CHECK(ledger.devicesCreated == 1);
        CHECK(factory.devicesById[7] == device);
    }

    SECTION("chain power off") {
        auto powered = tracks;
        powered[0].chain.enabled = false;
        publish(session, compile(powered), powered);

        CHECK(ledger.devicesDestroyed == 0);

        powered[0].chain.enabled = true;
        publish(session, compile(powered), powered);
        CHECK(ledger.devicesCreated == 1);
        CHECK(factory.devicesById[7] == device);
    }

    SECTION("deleted from the model") {
        const std::vector<TrackInfo> without{makeTrack(1)};
        publish(session, compile(without), without);

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
    publish(session, plan, without);
    session.publishValues(resolve(*plan, tracks));

    CHECK(ledger.devicesDestroyed == 0);
    REQUIRE(factory.devicesById[7] != nullptr);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.5f));

    // It goes when the plan stops naming it too, which is the first moment
    // nothing can reach it.
    publish(session, compile(without), without);
    CHECK(ledger.devicesDestroyed == 1);
    CHECK(ledger.lastDestroyingThread.load() == std::this_thread::get_id());
}

TEST_CASE("A plan that does not prepare leaves the live one playing", "[engine][session]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto tracks = trackWithEffect(7);
    const auto good = compile(tracks);
    publish(session, good, tracks);
    session.publishValues(resolve(*good, tracks));

    auto broken = std::make_shared<RenderPlan>();
    magda::engine::PlanOp op;
    op.kind = magda::engine::OpKind::Gain;
    op.inputs = {magda::engine::PortRef{4, 0}};  // reads an op that does not exist
    op.outputs = {magda::engine::SignalKind::Audio};
    broken->ops.push_back(op);

    const auto refused = publish(session, broken, tracks);

    CHECK_FALSE(refused.published);
    REQUIRE_FALSE(refused.messages.empty());
    CHECK(session.livePlan() == good);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
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
    const auto refused = publish(session, broken, tracks);

    CHECK_FALSE(refused.published);
    CHECK(session.livePlan() == nullptr);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.0f));

    // This one named nothing, so nothing was created for it. What a refused
    // plan does create stays for the retry, and leaves when the model stops
    // naming it rather than when a plan stops using it.
    CHECK(session.runtimeObjectCount() == 0);

    const auto good = compile(tracks);
    CHECK(publish(session, good, tracks).published);
    CHECK(session.livePlan() == good);
}

TEST_CASE("A plan renders with values that belong to it, always", "[engine][session]") {
    // Values published for a plan that has since been replaced are not applied
    // to its successor: they were resolved against ops that may not be there.
    // What the successor falls back to is what it was published with, not
    // unity. Unity was a defensible reading while a swap flushed everything
    // anyway; now that one is otherwise seamless, a fader at -6 dB jumping to
    // 0 dB for a block is the loudest thing left in it.
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    auto tracks = trackWithEffect(7);
    tracks[0].volume = 0.5f;
    const auto firstPlan = compile(tracks);
    publish(session, firstPlan, tracks);
    session.publishValues(resolve(*firstPlan, tracks));

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    REQUIRE(output.getSample(0, 0) == approx(0.25f));

    // Swap the device out and publish nothing new: the values in flight belong
    // to a plan that no longer exists.
    auto replaced = tracks;
    replaced[0].chain.fxChainElements[0] = makeDeviceElement(makeEffect(8));
    const auto secondPlan = compile(replaced);
    publish(session, secondPlan, replaced);

    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.25f));

    // And the mixer-speed path still lands, on top of the same reading.
    replaced[0].volume = 1.0f;
    session.publishValues(resolve(*secondPlan, replaced));
    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.5f));
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
    publish(session, firstPlan, tracks);
    session.publishValues(resolve(*firstPlan, tracks));

    std::atomic<bool> running{true};
    std::atomic<int> blocks{0};
    std::atomic<int> badBlocks{0};

    // Stands in for the callback: one thread, rendering back to back, never
    // told that anything is being republished underneath it.
    std::thread audio([&] {
        juce::AudioBuffer<float> output(2, kBlockSize);
        while (running.load(std::memory_order_relaxed)) {
            session.process(kBlockSize, output);
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
        publish(session, wide, twoDevices);
        session.publishValues(resolve(*wide, twoDevices));

        // Device 8 leaves the plan and the model together, so it is destroyed
        // rather than kept dormant.
        const auto narrow = compile(tracks);
        publish(session, narrow, tracks);
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

// A plan renders one stretch of timeline at a time. The transport is what
// decides which stretch that is, and what happens when one callback covers two.
TEST_CASE("A callback the loop wraps inside renders as two blocks",
          "[engine][session][transport]") {
    LoggingFactory factory;
    EngineSession session(factory);

    const std::vector<TrackInfo> tracks{makeTrack(1)};
    const auto plan = compile(tracks);
    REQUIRE(publish(session, plan, tracks).published);
    REQUIRE(factory.log != nullptr);

    // A loop one beat long at 120 bpm: 22050 samples, which a callback lands
    // inside of after 344 blocks of 64.
    auto transport = rolling(0.0);
    transport.loop = {true, 0.0, 1.0};
    session.publishTransport(transport);

    juce::AudioBuffer<float> output(2, kBlockSize);
    for (auto block = 0; block < 345; ++block)
        session.process(kBlockSize, output);

    REQUIRE(factory.log->blocks.size() == 346);

    const auto& before = factory.log->blocks[344];
    const auto& after = factory.log->blocks[345];

    // The two pieces of the callback that straddled the loop end. Neither of
    // them covers the wrap, which is the point: an op is never handed a block
    // whose timeline jumps in the middle.
    CHECK(before.numSamples + after.numSamples == kBlockSize);
    CHECK(before.endBeat == Catch::Approx(1.0).margin(1e-9));
    CHECK(after.startBeat == Catch::Approx(0.0).margin(1e-9));
    CHECK(before.continuous);
    CHECK(!after.continuous);
}

TEST_CASE("The transport, not the caller, says where the timeline is",
          "[engine][session][transport]") {
    LoggingFactory factory;
    EngineSession session(factory);

    const std::vector<TrackInfo> tracks{makeTrack(1)};
    const auto plan = compile(tracks);
    REQUIRE(publish(session, plan, tracks).published);

    juce::AudioBuffer<float> output(2, kBlockSize);

    SECTION("stopped until it is asked to play") {
        session.process(kBlockSize, output);

        REQUIRE(factory.log->blocks.size() == 1);
        CHECK(!factory.log->blocks[0].playing);
        CHECK(session.positionBeats() == Catch::Approx(0.0));
    }

    SECTION("and moves at the rate the tempo map says") {
        session.publishTransport(rolling(0.0));

        // 22050 samples to the beat at 120 bpm, which is not a whole number of
        // blocks: the last one is short, and the cursor lands on the beat
        // either way because it counts samples rather than callbacks.
        for (auto block = 0; block < 22050 / kBlockSize; ++block)
            session.process(kBlockSize, output);
        session.process(22050 % kBlockSize, output);

        CHECK(session.positionBeats() == Catch::Approx(1.0).margin(1e-9));
        CHECK(factory.log->blocks.back().playing);
    }
}

TEST_CASE("The metronome is added after the plan, not through it",
          "[engine][session][transport][click]") {
    LoggingFactory factory;
    EngineSession session(factory);

    // A muted master: the plan renders silence, so anything in the output came
    // from outside it.
    auto master = makeMaster();
    master.muted = true;

    const std::vector<TrackInfo> tracks{makeTrack(1)};
    const auto plan =
        std::make_shared<const RenderPlan>(magda::engine::compileRenderPlan(tracks, master));

    PlanValues values;
    magda::engine::resolvePlanValues(*plan, tracks, master, values);
    REQUIRE(
        session
            .publish(plan, context(), magda::engine::collectRuntimeStateIds(tracks, master), values)
            .published);

    juce::AudioBuffer<float> output(2, kBlockSize);

    SECTION("silent with the metronome off") {
        session.publishTransport(rolling(0.0));
        session.process(kBlockSize, output);

        CHECK(output.getMagnitude(0, kBlockSize) == approx(0.0f));
    }

    SECTION("sounding with it on, through a master that is muted") {
        auto transport = rolling(0.0);
        transport.click = {true, true, 1.0f};
        session.publishTransport(transport);
        session.process(kBlockSize, output);

        CHECK(output.getMagnitude(0, kBlockSize) > 0.1f);
    }
}
