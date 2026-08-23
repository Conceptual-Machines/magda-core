#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/PlanLayout.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDump.hpp"

using namespace magda;
using magda::engine::BufferLayout;
using magda::engine::OpId;
using magda::engine::OpKind;
using magda::engine::OpRole;
using magda::engine::PortRef;
using magda::engine::RenderPlan;
using magda::engine::SignalKind;

namespace {

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

/// A plan with enough shapes in it to be worth checking: a rack of two chains,
/// a send, a sidechain, a group, and an instrument carrying MIDI.
std::vector<TrackInfo> busyFixture() {
    auto rack = std::make_unique<RackInfo>();
    rack->id = 5;
    for (const ChainId chainId : {ChainId{10}, ChainId{11}}) {
        ChainInfo chain;
        chain.id = chainId;
        chain.elements.push_back(makeDeviceElement(makeEffect(static_cast<DeviceId>(chainId))));
        rack->chains.push_back(std::move(chain));
    }

    auto keyed = makeTrack(1);
    auto compressor = makeEffect(7);
    compressor.sidechain.type = SidechainConfig::Type::Audio;
    compressor.sidechain.sourceTrackId = 2;
    keyed.chain.fxChainElements.push_back(ChainElement{std::move(rack)});
    keyed.chain.fxChainElements.push_back(makeDeviceElement(compressor));
    keyed.audioOutputDevice = "track:4";
    keyed.sends.push_back(SendInfo{0, 0.5f, false, 3});

    auto key = makeTrack(2);
    key.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));

    auto bus = makeTrack(3);
    bus.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(9)));

    return {std::move(keyed), std::move(key), std::move(bus), makeTrack(4, TrackType::Group)};
}

/// Ops that must have finished before each op can start, worked out here
/// rather than borrowed from the pass under test.
std::vector<std::set<OpId>> ancestorsOf(const RenderPlan& plan) {
    std::vector<std::set<OpId>> ancestors(plan.ops.size());
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        for (const auto& input : plan.ops[i].inputs) {
            if (!input.valid())
                continue;
            ancestors[i].insert(input.op);
            const auto& theirs = ancestors[static_cast<std::size_t>(input.op)];
            ancestors[i].insert(theirs.begin(), theirs.end());
        }
    }
    return ancestors;
}

struct Layout {
    RenderPlan plan;
    std::vector<int> portOffsets;
    std::vector<int> delaySamples;
    BufferLayout buffers;

    std::size_t flat(OpId op, int port) const {
        return static_cast<std::size_t>(portOffsets[static_cast<std::size_t>(op)] + port);
    }
    int slot(OpId op, int port) const {
        return buffers.portSlots[flat(op, port)];
    }
};

Layout layoutOf(const std::vector<TrackInfo>& tracks, const TrackInfo& master,
                const std::map<DeviceId, int>& latencies = {}) {
    Layout out;
    out.plan = magda::engine::compileRenderPlan(tracks, master);
    out.portOffsets = magda::engine::portOffsetsOf(out.plan);

    std::vector<int> deviceLatency(out.plan.ops.size(), 0);
    for (std::size_t i = 0; i < out.plan.ops.size(); ++i)
        if (out.plan.ops[i].kind == OpKind::Device)
            if (const auto found = latencies.find(out.plan.ops[i].key.deviceId);
                found != latencies.end())
                deviceLatency[i] = found->second;

    out.delaySamples =
        magda::engine::resolvePlanLatency(out.plan, out.portOffsets, deviceLatency).delaySamples;
    out.buffers = magda::engine::assignBuffers(out.plan, out.portOffsets, out.delaySamples);
    return out;
}

/// The one thing sharing a buffer promises: no schedule can want both of the
/// ports in it at once.
///
/// Checked against transitive dependency rather than against the order the ops
/// happen to sit in. Two ops with no path between them may run in either order
/// or at the same time, whatever the reference executor's straight walk does
/// with them, so a slot they both hold is a race the parallel executor would
/// find and this one never would.
void requireNoLiveOverlap(const Layout& layout) {
    const auto& plan = layout.plan;
    const auto ancestors = ancestorsOf(plan);

    // Where each port really lives: an elided delay does not run, so its output
    // is the port behind it.
    std::vector<std::size_t> canonical(layout.buffers.portSlots.size());
    for (std::size_t i = 0; i < canonical.size(); ++i)
        canonical[i] = i;
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (layout.buffers.elided[i] != 0)
            canonical[layout.flat(static_cast<OpId>(i), 0)] = canonical[layout.flat(
                plan.ops[i].inputs.front().op, plan.ops[i].inputs.front().port)];

    std::vector<OpId> producer(canonical.size(), magda::engine::INVALID_OP_ID);
    std::vector<std::vector<OpId>> readers(canonical.size());
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        if (layout.buffers.elided[i] != 0)
            continue;
        for (std::size_t port = 0; port < plan.ops[i].outputs.size(); ++port)
            producer[canonical[layout.flat(static_cast<OpId>(i), static_cast<int>(port))]] =
                static_cast<OpId>(i);
        for (const auto& input : plan.ops[i].inputs)
            if (input.valid())
                readers[canonical[layout.flat(input.op, input.port)]].push_back(
                    static_cast<OpId>(i));
    }

    // Everything that has to have happened before the port's buffer is free.
    const auto keepAlive = [&](std::size_t port) {
        auto ops = readers[port];
        if (ops.empty())
            ops.push_back(producer[port]);
        return ops;
    };

    const auto isFinishedBefore = [&](std::size_t port, OpId op) {
        for (const auto keeper : keepAlive(port))
            if (!ancestors[static_cast<std::size_t>(op)].contains(keeper))
                return false;
        return true;
    };

    // Writing over an input is allowed where the op doing the writing is the
    // only thing that ever reads it: the read and the write are the same op,
    // so no order between them exists to get wrong.
    const auto isWrittenOverBy = [&](std::size_t port, std::size_t other) {
        const auto writer = producer[other];
        return layout.buffers.writesInPlace[static_cast<std::size_t>(writer)] != 0 &&
               readers[port].size() == 1 && readers[port].front() == writer;
    };

    std::map<std::pair<SignalKind, int>, std::vector<std::size_t>> ports;
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        for (std::size_t port = 0; port < plan.ops[i].outputs.size(); ++port) {
            const auto flat = layout.flat(static_cast<OpId>(i), static_cast<int>(port));
            if (canonical[flat] != flat)
                continue;
            ports[{plan.ops[i].outputs[port].kind, layout.buffers.portSlots[flat]}].push_back(flat);
        }

    INFO(magda::engine::dumpPlan(plan));
    for (const auto& [slot, sharing] : ports) {
        for (std::size_t a = 0; a < sharing.size(); ++a) {
            for (std::size_t b = a + 1; b < sharing.size(); ++b) {
                const auto first = sharing[a];
                const auto second = sharing[b];
                const auto safe = isFinishedBefore(first, producer[second]) ||
                                  isFinishedBefore(second, producer[first]) ||
                                  isWrittenOverBy(first, second) || isWrittenOverBy(second, first);
                INFO("ports " << first << " and " << second << " share slot " << slot.second);
                CHECK(safe);
            }
        }
    }
}

int countPorts(const Layout& layout, SignalKind kind) {
    int ports = 0;
    for (const auto& op : layout.plan.ops)
        for (const auto output : op.outputs)
            if (output == kind)
                ++ports;
    return ports;
}

}  // namespace

TEST_CASE("Buffers are shared only where no schedule can want both",
          "[engine][exec][layout][pdc]") {
    const auto tracks = busyFixture();

    SECTION("with nothing latent in the plan") {
        const auto layout = layoutOf(tracks, makeMaster());
        requireNoLiveOverlap(layout);
        CHECK(layout.buffers.numAudioSlots < countPorts(layout, SignalKind::Audio));
    }

    SECTION("with latency spread through it") {
        const auto layout = layoutOf(tracks, makeMaster(), {{7, 64}, {10, 32}, {9, 128}});
        requireNoLiveOverlap(layout);
        CHECK(layout.buffers.numAudioSlots < countPorts(layout, SignalKind::Audio));
    }
}

TEST_CASE("A delay holding no samples is not a buffer and not an op",
          "[engine][exec][layout][pdc]") {
    const std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};

    SECTION("with no latency anywhere, every delay disappears") {
        const auto layout = layoutOf(tracks, makeMaster());

        int delays = 0;
        for (std::size_t i = 0; i < layout.plan.ops.size(); ++i) {
            if (layout.plan.ops[i].kind != OpKind::Delay)
                continue;
            ++delays;
            CHECK(layout.delaySamples[i] == 0);
            CHECK(layout.buffers.elided[i] != 0);

            // Its port is the one behind it rather than a copy of it.
            const auto& input = layout.plan.ops[i].inputs.front();
            CHECK(layout.slot(static_cast<OpId>(i), 0) == layout.slot(input.op, input.port));
        }
        REQUIRE(delays == 2);
    }

    SECTION("a delay that has samples to hold is not elided") {
        const auto layout = layoutOf(tracks, makeMaster(), {{7, 96}});
        auto latent = tracks;
        latent[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
        const auto compensated = layoutOf(latent, makeMaster(), {{7, 96}});

        int held = 0;
        for (std::size_t i = 0; i < compensated.plan.ops.size(); ++i) {
            if (compensated.plan.ops[i].kind != OpKind::Delay)
                continue;
            if (compensated.delaySamples[i] == 0)
                CHECK(compensated.buffers.elided[i] != 0);
            else
                ++held;
        }

        // Two: track 2's side of the master's sum, which waits for track 1's
        // device, and the dry edge of that device's own delta, which waits for
        // the device itself.
        CHECK(held == 2);
    }
}

TEST_CASE("Latency accumulates along a chain and meets at a fan-in",
          "[engine][exec][layout][pdc]") {
    std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));

    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster());
    const auto portOffsets = magda::engine::portOffsetsOf(plan);

    std::vector<int> deviceLatency(plan.ops.size(), 0);
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (plan.ops[i].kind == OpKind::Device)
            deviceLatency[i] = plan.ops[i].key.deviceId == 7 ? 100 : 20;

    const auto latency = magda::engine::resolvePlanLatency(plan, portOffsets, deviceLatency);

    INFO(magda::engine::dumpPlan(plan));
    CHECK(latency.outputLatency == 120);

    // The master's two inputs: track 1 arrives 120 samples late and needs
    // nothing, track 2 arrives on time and waits for it.
    std::vector<int> delays;
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        if (plan.ops[i].key.role == OpRole::MixInputDelay)
            delays.push_back(latency.delaySamples[i]);

    REQUIRE(delays.size() == 2);
    CHECK(delays[0] == 0);
    CHECK(delays[1] == 120);
}

TEST_CASE("A delta solo's dry edge waits for the processing it is measured against",
          "[engine][exec][layout][pdc]") {
    // The delays aligning one delta's two sides, by the slot each fills. Every
    // device slot and every rack carries a delta now, so which one is being
    // asked about has to be named: keyed on the op the delays belong to rather
    // than on the role alone, or the section would be reading whichever delta
    // the compiler happened to emit last.
    const auto dryDelays = [](const Layout& layout, DeviceId deviceId, RackId rackId) {
        std::map<int, int> bySlot;
        for (std::size_t i = 0; i < layout.plan.ops.size(); ++i) {
            const auto& key = layout.plan.ops[i].key;
            if (key.role == OpRole::SubtractInputDelay && key.deviceId == deviceId &&
                key.rackId == rackId)
                bySlot[key.index] = layout.delaySamples[i];
        }
        return bySlot;
    };

    SECTION("a device: its own latency, and nothing on the wet side") {
        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

        const auto layout = layoutOf(tracks, makeMaster(), {{7, 100}});
        INFO(magda::engine::dumpPlan(layout.plan));

        const auto delays = dryDelays(layout, 7, INVALID_RACK_ID);
        REQUIRE(delays.size() == 2);
        CHECK(delays.at(0) == 0);
        CHECK(delays.at(1) == 100);
    }

    SECTION("and whatever aligned the device's own input, on top of it") {
        auto compressor = makeEffect(7);
        compressor.sidechain.type = SidechainConfig::Type::Audio;
        compressor.sidechain.sourceTrackId = 2;

        std::vector<TrackInfo> tracks{makeTrack(1), makeTrack(2)};
        tracks[0].chain.fxChainElements.push_back(makeDeviceElement(compressor));
        tracks[1].chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));

        // The key arrives 40 samples late, so the device's own audio input is
        // held back that far before the device adds its 100.
        const auto layout = layoutOf(tracks, makeMaster(), {{7, 100}, {8, 40}});
        INFO(magda::engine::dumpPlan(layout.plan));

        const auto delays = dryDelays(layout, 7, INVALID_RACK_ID);
        REQUIRE(delays.size() == 2);
        CHECK(delays.at(0) == 0);
        CHECK(delays.at(1) == 140);
    }

    SECTION("a rack: everything its chains and its own alignment came to") {
        auto rack = std::make_unique<RackInfo>();
        rack->id = 5;
        for (const ChainId chainId : {ChainId{10}, ChainId{11}}) {
            ChainInfo chain;
            chain.id = chainId;
            chain.elements.push_back(makeDeviceElement(makeEffect(static_cast<DeviceId>(chainId))));
            rack->chains.push_back(std::move(chain));
        }

        std::vector<TrackInfo> tracks{makeTrack(1)};
        tracks[0].chain.fxChainElements.push_back(ChainElement{std::move(rack)});

        // The rack's output is as late as its latest chain, which is what its
        // own mix aligned the other one to.
        const auto layout = layoutOf(tracks, makeMaster(), {{10, 48}, {11, 16}});
        INFO(magda::engine::dumpPlan(layout.plan));

        const auto delays = dryDelays(layout, INVALID_DEVICE_ID, 5);
        REQUIRE(delays.size() == 2);
        CHECK(delays.at(0) == 0);
        CHECK(delays.at(1) == 48);
    }
}
