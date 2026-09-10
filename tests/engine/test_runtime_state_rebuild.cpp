#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "exec/RuntimeStateStore.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file test_runtime_state_rebuild.cpp
 * @brief A key that has come to mean a different device (#2572).
 *
 * Two halves: the factory is asked again for a key it names for rebuild, and
 * the instance it replaces outlives the plan that named it.
 */

using namespace magda;
using namespace magda::engine;

namespace {

constexpr TrackId kTrack = 1;
constexpr DeviceId kDevice = 1;

const DeviceKey kKey{ChainSegment::Fx, kDevice};

const RenderContext kContext{.sampleRate = 48000.0, .maxBlockSize = 64, .numChannels = 2};

/// Says which of the factory's devices it is, and reports its own destruction.
class ProbeDevice final : public EngineDevice {
  public:
    ProbeDevice(int which, int& alive) : which_(which), alive_(alive) {
        ++alive_;
    }

    ~ProbeDevice() override {
        --alive_;
    }

    void process(DeviceBlock&) override {}

    int which() const {
        return which_;
    }

  private:
    int which_ = 0;
    int& alive_;
};

/// Numbers every device it makes, and names for rebuild whatever a case asked.
class ProbeFactory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineDevice> createDevice(DeviceKey) override {
        ++made;
        return std::make_unique<ProbeDevice>(made, alive);
    }

    std::set<DeviceKey> devicesToRebuild() override {
        return std::exchange(rebuild, {});
    }

    int made = 0;
    int alive = 0;
    std::set<DeviceKey> rebuild;
};

/// One Device op at kKey.
RenderPlan planWithDevice() {
    RenderPlan plan;
    PlanOp op;
    op.kind = OpKind::Device;
    op.key.trackId = kTrack;
    op.key.deviceId = kDevice;
    op.key.segment = ChainSegment::Fx;
    plan.ops.push_back(op);
    return plan;
}

/// The device bound to kKey by @p bindings, or nullptr.
const ProbeDevice* bound(const PlanBindings& bindings) {
    const auto found = bindings.devices.find(kKey);
    if (found == bindings.devices.end())
        return nullptr;
    return static_cast<const ProbeDevice*>(found->second);
}

/// Everything the model holds.
RuntimeStateIds namedIds() {
    RuntimeStateIds ids;
    ids.devices.insert(kKey);
    ids.tracks.insert(kTrack);
    return ids;
}

}  // namespace

TEST_CASE("A key nothing names for rebuild keeps its device", "[engine][store][rebuild]") {
    ProbeFactory factory;
    RuntimeStateStore store(factory);

    const auto plan = planWithDevice();

    REQUIRE(bound(store.realise(plan, kContext))->which() == 1);
    store.releaseDeleted(plan, namedIds(), nullptr);

    // The retention contract, unchanged.
    CHECK(bound(store.realise(plan, kContext))->which() == 1);
    CHECK(factory.made == 1);
}

TEST_CASE("A key named for rebuild is asked for again", "[engine][store][rebuild]") {
    ProbeFactory factory;
    RuntimeStateStore store(factory);

    const auto plan = planWithDevice();

    REQUIRE(bound(store.realise(plan, kContext))->which() == 1);
    store.releaseDeleted(plan, namedIds(), nullptr);

    factory.rebuild.insert(kKey);

    CHECK(bound(store.realise(plan, kContext))->which() == 2);
    CHECK(factory.made == 2);
}

TEST_CASE("A rebuilt device outlives the plan that named it", "[engine][store][rebuild]") {
    ProbeFactory factory;
    RuntimeStateStore store(factory);

    const auto plan = planWithDevice();

    REQUIRE(bound(store.realise(plan, kContext))->which() == 1);
    store.releaseDeleted(plan, namedIds(), nullptr);

    factory.rebuild.insert(kKey);
    store.realise(plan, kContext);

    // Both: realise() prepares a plan that has not been published yet.
    CHECK(factory.alive == 2);

    // The swap has happened by now, so the one it replaced goes here.
    store.releaseDeleted(plan, namedIds(), nullptr);
    CHECK(factory.alive == 1);
}

TEST_CASE("A rebuild the plan does not name still replaces the device",
          "[engine][store][rebuild]") {
    ProbeFactory factory;
    RuntimeStateStore store(factory);

    const auto plan = planWithDevice();

    REQUIRE(bound(store.realise(plan, kContext))->which() == 1);
    store.releaseDeleted(plan, namedIds(), nullptr);

    // A plan with no Device op -- bypassed, chain powered off -- retires the
    // instance without building one.
    factory.rebuild.insert(kKey);
    store.realise(RenderPlan{}, kContext);
    store.releaseDeleted(RenderPlan{}, namedIds(), nullptr);
    CHECK(factory.alive == 0);

    CHECK(bound(store.realise(plan, kContext))->which() == 2);
}
