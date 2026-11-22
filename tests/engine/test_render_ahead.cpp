// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RenderLoad.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanHandoff.hpp"
#include "plan/RenderPlan.hpp"

// A session rendering its deterministic side ahead of the callback (#1898).

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::DeviceBlock;
using magda::engine::DeviceKey;
using magda::engine::EngineAudioSource;
using magda::engine::EngineDevice;
using magda::engine::EngineSession;
using magda::engine::PlanValues;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::RuntimeStateFactory;

namespace {

constexpr int kBlockSize = 64;

TrackInfo makeTrack(TrackId id, TrackType type = TrackType::Media) {
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

/// A different value every sample, so a block rendered out of turn cannot pass for another.
class CountingSource final : public EngineAudioSource {
  public:
    explicit CountingSource(float step) : step_(step) {}

    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        for (int i = 0; i < block.numSamples; ++i, value_ += step_)
            for (std::size_t channel = 0; channel < out.getNumChannels(); ++channel)
                out.setSample(static_cast<int>(channel), i, value_);
    }

  private:
    float step_;
    float value_ = 0.0f;
};

/// Carries state across blocks, so the order the blocks reach it in shows.
class Smoother final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        for (std::size_t channel = 0; channel < block.audio.getNumChannels(); ++channel)
            for (std::size_t i = 0; i < block.audio.getNumSamples(); ++i) {
                auto& state = last_[channel % 2];
                state = 0.5f * (state + block.audio.getSample(static_cast<int>(channel),
                                                              static_cast<int>(i)));
                block.audio.setSample(static_cast<int>(channel), static_cast<int>(i), state);
            }
    }

  private:
    float last_[2]{};
};

class Factory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineDevice> createDevice(DeviceKey) override {
        return std::make_unique<Smoother>();
    }
    std::unique_ptr<EngineAudioSource> createClipAudioSource(TrackId trackId) override {
        return std::make_unique<CountingSource>(0.001f * static_cast<float>(trackId));
    }
};

std::vector<TrackInfo> project() {
    std::vector<TrackInfo> tracks;
    for (TrackId id : {1, 2}) {
        auto track = makeTrack(id);
        DeviceInfo device;
        device.id = 10 + id;
        device.name = "Smoother";
        device.deviceType = DeviceType::Effect;
        track.chain.fxChainElements.push_back(makeDeviceElement(device));
        tracks.push_back(track);
    }
    return tracks;
}

struct Rig {
    Factory factory;
    EngineSession session{factory};
    int renderedAhead = 0;

    explicit Rig(int depth, bool inBackground = false, magda::engine::RenderLoad* load = nullptr)
        : depth(depth), inBackground(inBackground) {
        session.setRenderLoad(load);
        if (depth > 0)
            session.setRenderAhead(depth, inBackground);
        publish();
    }

    void publish() {
        const auto tracks = project();
        const auto plan = std::make_shared<const RenderPlan>(
            magda::engine::insertHandoffs(magda::engine::compileRenderPlan(tracks, makeMaster())));
        PlanValues values;
        magda::engine::resolvePlanValues(*plan, tracks, makeMaster(), values);
        REQUIRE(session
                    .publish(plan, RenderContext{44100.0, kBlockSize, 2},
                             magda::engine::collectRuntimeStateIds(tracks, makeMaster()),
                             std::move(values))
                    .published);
    }

    void callback(juce::AudioBuffer<float>& output) {
        session.process(kBlockSize, output);
        if (!inBackground) {
            renderedAhead += session.renderAheadOnce();
            return;
        }
        if (!paced)
            return;
        // The thread finishing the round this callback woke it for is the signal: a callback that
        // came sooner would be faster than real time and could find it inside the next block.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!session.renderAheadCaughtUp()) {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
    }

    int depth;
    bool inBackground;

    /// Whether a callback waits for the thread to catch up, as real time would let it.
    bool paced = true;
};

magda::engine::TransportSnapshot rolling(double fromBeat) {
    magda::engine::TransportSnapshot transport;
    transport.request.generation = 1;
    transport.request.playing = true;
    transport.request.positionBeat = fromBeat;
    return transport;
}

}  // namespace

TEST_CASE("A session rendering ahead plays what it plays at the callback",
          "[engine][session][1898]") {
    // Through a loop a beat long, so the prediction crosses wraps the callback cuts.
    auto transport = rolling(0.0);
    transport.loop = {true, 0.0, 1.0};

    Rig whole(0), ahead(3);
    for (auto* rig : {&whole, &ahead})
        rig->session.publishTransport(transport);

    juce::AudioBuffer<float> a(2, kBlockSize), b(2, kBlockSize);
    for (int callback = 0; callback < 800; ++callback) {
        whole.callback(a);
        ahead.callback(b);
        INFO("callback " << callback);
        for (int channel = 0; channel < 2; ++channel)
            REQUIRE(std::memcmp(a.getReadPointer(channel), b.getReadPointer(channel),
                                kBlockSize * sizeof(float)) == 0);
    }

    CHECK(a.getMagnitude(0, 0, kBlockSize) > 0.0f);
    CHECK(ahead.renderedAhead > 700);
    // Only the first callback, before anything was rendered ahead, renders whole.
    CHECK(ahead.session.renderAheadMisses() == 0);
}

TEST_CASE("The ahead thread renders what the callback plays", "[engine][session][1898]") {
    auto transport = rolling(0.0);
    transport.loop = {true, 0.0, 1.0};

    Rig whole(0), ahead(3, true);
    for (auto* rig : {&whole, &ahead})
        rig->session.publishTransport(transport);

    juce::AudioBuffer<float> a(2, kBlockSize), b(2, kBlockSize);
    for (int callback = 0; callback < 800; ++callback) {
        whole.callback(a);
        ahead.callback(b);
        INFO("callback " << callback);
        for (int channel = 0; channel < 2; ++channel)
            REQUIRE(std::memcmp(a.getReadPointer(channel), b.getReadPointer(channel),
                                kBlockSize * sizeof(float)) == 0);
    }
    CHECK(ahead.session.renderAheadMisses() == 0);
    CHECK(ahead.session.renderedAheadThrough() > 700);
}

TEST_CASE("A republish plays out what was rendered ahead first", "[engine][session][1898]") {
    // The new plan carries the old one's state, which the ahead thread has already moved past
    // the callback; the callback hears those blocks before the swap.
    auto transport = rolling(0.0);
    Rig whole(0), ahead(3, true);
    for (auto* rig : {&whole, &ahead})
        rig->session.publishTransport(transport);

    constexpr int kCallbacks = 600;
    std::vector<float> heard, expected;
    juce::AudioBuffer<float> a(2, kBlockSize), b(2, kBlockSize);
    for (int callback = 0; callback < kCallbacks; ++callback) {
        whole.callback(a);
        expected.insert(expected.end(), a.getReadPointer(0), a.getReadPointer(0) + kBlockSize);
    }

    std::atomic<bool> done{false};
    std::thread audio([&] {
        for (int callback = 0; callback < kCallbacks; ++callback) {
            ahead.callback(b);
            heard.insert(heard.end(), b.getReadPointer(0), b.getReadPointer(0) + kBlockSize);
        }
        done.store(true);
    });
    int republished = 0;
    while (!done.load() && republished < 20) {
        ahead.publish();
        ++republished;
        std::this_thread::yield();
    }
    audio.join();

    CHECK(republished > 0);
    REQUIRE(heard.size() == expected.size());
    const auto differs = std::ranges::mismatch(heard, expected);
    if (differs.in1 != heard.end()) {
        const auto at = differs.in1 - heard.begin();
        UNSCOPED_INFO("first difference at block " << at / kBlockSize << " sample "
                                                   << at % kBlockSize << ": heard " << *differs.in1
                                                   << ", expected " << *differs.in2 << ", misses "
                                                   << ahead.session.renderAheadMisses());
    }
    CHECK(std::memcmp(heard.data(), expected.data(), heard.size() * sizeof(float)) == 0);
}

TEST_CASE("A jump forgets what was rendered ahead for where the playhead was",
          "[engine][session][1898]") {
    // The blocks rendered for the old position were never going to be heard, so the callback
    // renders the first one after the jump itself and the thread starts again from there.
    Rig ahead(3, true);
    auto transport = rolling(0.0);
    ahead.session.publishTransport(transport);

    juce::AudioBuffer<float> out(2, kBlockSize);
    for (int callback = 0; callback < 400; ++callback) {
        if (callback == 200) {
            transport.request.generation = 2;
            transport.request.locate = true;
            transport.request.locateId = 1;
            transport.request.positionBeat = 32.0;
            ahead.session.publishTransport(transport);
        }
        ahead.callback(out);
        INFO("callback " << callback);
        REQUIRE(out.getMagnitude(0, 0, kBlockSize) > 0.0f);
    }

    CHECK(ahead.session.renderAheadDiscards() >= 1);
    CHECK(ahead.session.renderAheadMisses() == 0);
    CHECK(ahead.session.renderedAheadThrough() > ahead.session.nextBlock());
}

TEST_CASE("A callback says how it rendered, for the xrun record", "[engine][session][3401]") {
    Rig ahead(3);
    auto transport = rolling(0.0);
    ahead.session.publishTransport(transport);

    juce::AudioBuffer<float> out(2, kBlockSize);
    ahead.callback(out);
    const auto first = ahead.session.lastCallback();
    CHECK(first.blocks == 1);
    CHECK(first.started);
    CHECK(first.published);
    CHECK(first.wholeBlocks == 1);

    for (int callback = 0; callback < 10; ++callback)
        ahead.callback(out);
    const auto steady = ahead.session.lastCallback();
    CHECK_FALSE(steady.started);
    CHECK_FALSE(steady.published);
    CHECK_FALSE(steady.jumped);
    CHECK(steady.wholeBlocks == 0);
    CHECK(steady.missedBlocks == 0);

    transport.request.generation = 2;
    transport.request.locate = true;
    transport.request.locateId = 1;
    transport.request.positionBeat = 32.0;
    ahead.session.publishTransport(transport);
    ahead.callback(out);
    CHECK(ahead.session.lastCallback().jumped);
    CHECK(ahead.session.lastCallback().wholeBlocks == 1);

    ahead.publish();
    ahead.callback(out);
    CHECK(ahead.session.lastCallback().published);
}

TEST_CASE("A track holding session clips renders at the callback", "[engine][plan][1898]") {
    // A launch has to be heard when it fires, so its clips are never rendered ahead; a track
    // playing only its arrangement still is.
    magda::engine::CompileOptions options;
    options.sessionTracks = {2};
    const auto tracks = project();
    auto plan = magda::engine::compileRenderPlan(tracks, makeMaster(), options);
    for (const auto& problem : magda::engine::validatePlan(plan))
        UNSCOPED_INFO(problem);
    REQUIRE(magda::engine::validatePlan(plan).empty());

    for (const auto& op : plan.ops) {
        if (op.kind != magda::engine::OpKind::ClipAudio)
            continue;
        INFO("track " << op.key.trackId);
        const bool session = op.key.trackId == 2;
        CHECK((op.liveness == magda::engine::LivenessDomain::Live) == session);
        CHECK(op.liveByPlayback == session);
    }

    const auto guarded = magda::engine::insertHandoffs(plan);
    REQUIRE(magda::engine::validatePlan(guarded).empty());
    PlanValues values;
    magda::engine::resolvePlanValues(guarded, tracks, makeMaster(), values);
    Factory factory;
    magda::engine::RuntimeStateStore store{factory};
    const RenderContext context{44100.0, kBlockSize, 2};
    magda::engine::PlanExecutor executor;
    executor.prepare(guarded, store.realise(guarded, context), context, nullptr, &values);
    REQUIRE(executor.isPrepared());
    CHECK(executor.aheadTracks() == std::vector<TrackId>{1});
}

namespace {

/// Sounds while its slot's handle plays, read through the block the way a clip source reads it.
class LaunchGatedSource final : public EngineAudioSource {
  public:
    LaunchGatedSource(TrackId trackId, magda::engine::LaunchHandleFeed& handles)
        : trackId_(trackId), handles_(handles) {}

    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        out.clear();
        const magda::engine::LaunchHandleFeed::Reader table(handles_, block);
        if (!table)
            return;
        const auto [first, last] = table->rangeFor(trackId_);
        for (const auto* entry = first; entry != last; ++entry)
            if (entry->handle != nullptr && entry->handle->blockStatus().beforeEvent.playing())
                out.fill(1.0f);
    }

  private:
    TrackId trackId_;
    magda::engine::LaunchHandleFeed& handles_;
};

class LauncherFactory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineAudioSource> createClipAudioSource(TrackId) override {
        return std::make_unique<CountingSource>(0.0f);
    }
    std::unique_ptr<EngineAudioSource> createSessionAudioSource(TrackId trackId) override {
        return std::make_unique<LaunchGatedSource>(trackId, *handles);
    }
    magda::engine::LaunchHandleFeed* handles = nullptr;
};

std::shared_ptr<const magda::engine::ClipSnapshot> snapshotWithSlot(TrackId trackId) {
    auto snapshot = std::make_shared<magda::engine::ClipSnapshot>();
    magda::engine::TrackClipPlayback track;
    track.trackId = trackId;
    magda::engine::SessionSlotPlayback slot;
    slot.sceneIndex = 0;
    slot.lengthBeats = 4.0;
    slot.audio.emplace_back();
    track.session.push_back(std::move(slot));
    snapshot->tracks.push_back(std::move(track));
    return snapshot;
}

}  // namespace

TEST_CASE("A slot added to a track rendered ahead launches from the ahead side",
          "[engine][session][1898][3316]") {
    // Its first slot arrives after the plan, and the ahead side's copies take it on (#3316).
    LauncherFactory factory;
    EngineSession session(factory);
    factory.handles = &session.launchHandleFeed();
    session.setRenderAhead(3, false);

    const std::vector<TrackInfo> tracks{makeTrack(1)};
    const auto plan = std::make_shared<const RenderPlan>(
        magda::engine::insertHandoffs(magda::engine::compileRenderPlan(tracks, makeMaster())));
    PlanValues values;
    magda::engine::resolvePlanValues(*plan, tracks, makeMaster(), values);
    REQUIRE(session
                .publish(plan, RenderContext{44100.0, kBlockSize, 2},
                         magda::engine::collectRuntimeStateIds(tracks, makeMaster()),
                         std::move(values))
                .published);
    session.publishTransport(rolling(0.0));

    juce::AudioBuffer<float> output(2, kBlockSize);
    for (int callback = 0; callback < 8; ++callback) {
        session.process(kBlockSize, output);
        session.renderAheadOnce();
        CHECK(output.getSample(0, 0) == 0.0f);
    }
    REQUIRE(session.renderedAheadThrough() > session.nextBlock());
    session.publishClips(snapshotWithSlot(1));

    // As soon as possible is the end of what was already rendered ahead.
    {
        magda::engine::LaunchRequestQueue::Gesture gesture(session.launchRequests());
        gesture.play(magda::engine::SlotKey{1, 0});
    }
    int firstHeard = -1;
    for (int callback = 0; callback < 16; ++callback) {
        session.process(kBlockSize, output);
        CHECK(session.renderAheadOnce() > 0);
        INFO("callback " << callback);
        if (firstHeard >= 0)
            CHECK(output.getSample(0, 0) == 1.0f);
        else if (output.getSample(0, 0) == 1.0f)
            firstHeard = callback;
    }
    CHECK(firstHeard >= 3);
    CHECK(firstHeard <= 5);
    CHECK(session.renderAheadDiscards() == 0);
}

TEST_CASE("A callback that finds the ahead thread mid-block takes the block over",
          "[engine][session][1898]") {
    // Callbacks as fast as they come, so the thread is often inside its ops when one arrives:
    // it gives the block up between ops and the callback renders it, never silence.
    Rig ahead(3, true);
    ahead.paced = false;
    ahead.session.publishTransport(rolling(0.0));

    juce::AudioBuffer<float> out(2, kBlockSize);
    int silent = 0;
    for (int callback = 0; callback < 3000; ++callback) {
        ahead.callback(out);
        silent += out.getMagnitude(0, 0, kBlockSize) > 0.0f ? 0 : 1;
    }
    // A miss is the thread not letting go within the callback's patience, which a busy machine
    // can cause; without the hand-over, dozens of these blocks go silent.
    const auto misses = ahead.session.renderAheadMisses();
    CHECK(silent <= misses + 1);
    CHECK(misses <= 3);
}

namespace {

namespace {

/// Each device's time on @p thread, by device id.
std::map<DeviceId, std::uint64_t> deviceNanos(const magda::engine::RenderLoad& load,
                                              magda::engine::RenderLoad::Thread thread) {
    std::map<DeviceId, std::uint64_t> nanos;
    load.forEach([&](const magda::engine::RenderLoad::Entry& entry) {
        if (entry.device != INVALID_DEVICE_ID)
            nanos[entry.device] += entry.nanos[static_cast<std::size_t>(thread)];
    });
    return nanos;
}

}  // namespace

TEST_CASE("Render time is tallied to the thread that renders each device", "[engine][cpu][3357]") {
    using magda::engine::RenderLoad;
    constexpr int kCallbacks = 200;
    RenderLoad aheadLoad, wholeLoad;
    Rig ahead(3, false, &aheadLoad), whole(0, false, &wholeLoad);
    for (auto* rig : {&ahead, &whole})
        rig->session.publishTransport(rolling(0.0));

    juce::AudioBuffer<float> output(2, kBlockSize);
    const auto run = [&](int callbacks) {
        for (int callback = 0; callback < callbacks; ++callback) {
            ahead.callback(output);
            whole.callback(output);
        }
    };
    // The first block renders whole at the callback, and cold.
    run(10);
    const auto callbackBefore = deviceNanos(aheadLoad, RenderLoad::Callback);
    run(kCallbacks - 10);

    const auto expectedBudget = kCallbacks * kBlockSize * 1.0e9 / 44100.0;
    CHECK(static_cast<double>(aheadLoad.budgetNanos()) ==
          Catch::Approx(expectedBudget).epsilon(1e-6));

    // Once rendering ahead, the two Smoothers are the ahead thread's alone.
    REQUIRE(ahead.session.renderAheadMisses() == 0);
    const auto aheadNanos = deviceNanos(aheadLoad, RenderLoad::Ahead);
    const auto callbackNanos = deviceNanos(aheadLoad, RenderLoad::Callback);
    for (DeviceId device : {11, 12}) {
        INFO("device " << device);
        CHECK(aheadNanos.at(device) > 0);
        CHECK(callbackNanos.at(device) == callbackBefore.at(device));
    }
    CHECK(aheadLoad.takeAhead().busyMs > 0.0);
    CHECK(aheadLoad.takeAhead().busyMs == 0.0);

    // Without render-ahead, nothing is the ahead thread's.
    for (const auto& [device, nanos] : deviceNanos(wholeLoad, RenderLoad::Ahead))
        CHECK(nanos == 0);
    CHECK(deviceNanos(wholeLoad, RenderLoad::Callback).at(11) > 0);
}

TrackInfo makeSynth(TrackId id) {
    auto synth = makeTrack(id);
    DeviceInfo instrument;
    instrument.id = id * 10;
    instrument.name = "Synth";
    instrument.deviceType = DeviceType::Instrument;
    instrument.isInstrument = true;
    synth.chain.fxChainElements.push_back(makeDeviceElement(instrument));
    return synth;
}

/// Tracks whose ops of @p kind are in @p plan, live or not.
std::set<TrackId> tracksWith(const magda::engine::RenderPlan& plan, magda::engine::OpKind kind,
                             bool live) {
    std::set<TrackId> tracks;
    for (const auto& op : plan.ops)
        if (op.kind == kind && (op.liveness == magda::engine::LivenessDomain::Live) == live)
            tracks.insert(op.key.trackId);
    return tracks;
}

}  // namespace

TEST_CASE("An instrument renders ahead unless it is being played", "[engine][plan][1898]") {
    // The one played keeps its live input and the callback; the other is heard from its clips.
    auto tracks = project();
    tracks.push_back(makeSynth(3));
    tracks.push_back(makeSynth(4));
    REQUIRE(magda::engine::chainConsumesMidi(tracks.back()));

    magda::engine::CompileOptions options;
    options.auditionMidi = true;
    options.playedInstruments = std::set<TrackId>{3};
    const auto plan = magda::engine::insertHandoffs(
        magda::engine::compileRenderPlan(tracks, makeMaster(), options));
    REQUIRE(magda::engine::validatePlan(plan).empty());

    using magda::engine::OpKind;
    CHECK(tracksWith(plan, OpKind::ClipMidi, true) == std::set<TrackId>{3});
    CHECK(tracksWith(plan, OpKind::ClipMidi, false) == std::set<TrackId>{4});
    CHECK(tracksWith(plan, OpKind::MidiInput, true) == std::set<TrackId>{3});
    CHECK(tracksWith(plan, OpKind::ClipAudio, true) == std::set<TrackId>{3});

    PlanValues values;
    magda::engine::resolvePlanValues(plan, tracks, makeMaster(), values);
    Factory factory;
    magda::engine::RuntimeStateStore store{factory};
    const RenderContext context{44100.0, kBlockSize, 2};
    magda::engine::PlanExecutor executor;
    executor.prepare(plan, store.realise(plan, context), context, nullptr, &values);
    REQUIRE(executor.isPrepared());
    CHECK(executor.aheadTracks() == std::vector<TrackId>{1, 2, 4});
}

TEST_CASE("A track sending MIDI to hardware renders at the callback unplayed",
          "[engine][plan][1898]") {
    // Rendered ahead, its notes would leave for the hardware a look-ahead early.
    auto tracks = project();
    auto external = makeSynth(3);
    external.midiOutputDevice = "Hardware Synth";
    tracks.push_back(external);

    magda::engine::CompileOptions options;
    options.auditionMidi = true;
    options.playedInstruments = std::set<TrackId>{};
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster(), options);

    using magda::engine::OpKind;
    CHECK(tracksWith(plan, OpKind::ClipMidi, true) == std::set<TrackId>{3});
    CHECK(tracksWith(plan, OpKind::MidiInput, true) == std::set<TrackId>{3});
}

TEST_CASE("Without render-ahead every instrument keeps its live input", "[engine][plan][1898]") {
    auto tracks = project();
    tracks.push_back(makeSynth(3));
    tracks.push_back(makeSynth(4));

    magda::engine::CompileOptions options;
    options.auditionMidi = true;
    const auto plan = magda::engine::compileRenderPlan(tracks, makeMaster(), options);

    CHECK(tracksWith(plan, magda::engine::OpKind::MidiInput, true) == std::set<TrackId>{3, 4});
}
