#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <thread>
#include <vector>

#include "clip/ClipVoicePool.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/RenderPlan.hpp"

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
using magda::engine::RuntimeStateIds;

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

    std::unique_ptr<EngineDevice> createDevice(DeviceKey key) override {
        auto device = std::make_unique<LedgerDevice>(ledger_, key.deviceId);
        device->latency = latencyFor.count(key) != 0 ? latencyFor[key] : 0;
        lastDevice = device.get();
        devicesById[key] = device.get();
        return device;
    }

    /// What a device reports once it is loaded, per section-aware identity.
    std::map<DeviceKey, int> latencyFor;

    std::unique_ptr<EngineAudioSource> createClipAudioSource(TrackId) override {
        ++ledger_.sourcesCreated;
        return std::make_unique<ConstantSource>(1.0f);
    }

    /// A host with a mixer on screen: every meter the plan has gets one. A host
    /// without would return nullptr here and nothing else would change.
    std::unique_ptr<magda::engine::LevelTap> createMeter(const magda::engine::OpKey& key) override {
        if (!makesMeters)
            return nullptr;

        ++metersCreated;
        auto tap = std::make_unique<magda::engine::LevelTap>();
        metersByKey[key] = tap.get();
        return tap;
    }

    bool makesMeters = true;
    int metersCreated = 0;
    std::map<magda::engine::OpKey, magda::engine::LevelTap*> metersByKey;

    LedgerDevice* lastDevice = nullptr;
    std::map<DeviceKey, LedgerDevice*> devicesById;

  private:
    Ledger& ledger_;
};

/// The one meter with this role at this location, or null.
magda::engine::LevelTap* meterAt(const TestFactory& factory, const RenderPlan& plan,
                                 magda::engine::OpRole role, TrackId trackId,
                                 DeviceId deviceId = INVALID_DEVICE_ID) {
    for (const auto& op : plan.ops) {
        if (op.kind != magda::engine::OpKind::Meter || op.key.role != role)
            continue;
        if (op.key.trackId != trackId || op.key.deviceId != deviceId)
            continue;
        const auto found = factory.metersByKey.find(op.key);
        return found == factory.metersByKey.end() ? nullptr : found->second;
    }
    return nullptr;
}

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

/// Enough of a file for a stream to be opened over it. What it holds does not
/// matter here: whether a reader exists is the question, not what it plays.
class SilentReader final : public magda::engine::AudioFileReader {
  public:
    std::int64_t lengthInSamples() const override {
        return 1000000;
    }
    double sampleRate() const override {
        return 44100.0;
    }
    int numChannels() const override {
        return 2;
    }
    int read(juce::AudioBuffer<float>& destination, int destinationOffset, std::int64_t,
             int numSamples) override {
        destination.clear(destinationOffset, numSamples);
        return numSamples;
    }
};

class OpenEverything final : public magda::engine::AudioFileReaderFactory {
  public:
    std::unique_ptr<magda::engine::AudioFileReader> open(const std::string&) override {
        return std::make_unique<SilentReader>();
    }
};

/// One track, one clip, over the seconds given.
std::shared_ptr<const magda::engine::ClipSnapshot> clipsOver(TrackId trackId, double startSeconds,
                                                             double endSeconds) {
    magda::engine::SnapshotSpan span;
    span.seconds = {startSeconds, endSeconds};
    span.beats = {startSeconds * 2.0, endSeconds * 2.0};

    magda::engine::AudioEventPlayback event;
    event.eventId = 1;
    event.sourceId = 1;
    event.filePath = "take.wav";
    event.sourceSampleRate = 44100.0;
    event.span = span;

    magda::engine::AudioClipPlayback clip;
    clip.clipId = 1;
    clip.span = span;
    clip.events.push_back(std::move(event));

    magda::engine::TrackClipPlayback track;
    track.trackId = trackId;
    track.audio.push_back(std::move(clip));

    auto snapshot = std::make_shared<magda::engine::ClipSnapshot>();
    snapshot->tracks.push_back(std::move(track));
    return snapshot;
}

/// A session source that sounds while its slot's handle says it is playing.
/// What it asks is whether a launch reaches the audio thread at all: the handle
/// is made and published by publishClips and advanced by process().
class LaunchGatedSource final : public EngineAudioSource {
  public:
    LaunchGatedSource(TrackId trackId, magda::engine::LaunchHandleFeed& handles)
        : trackId_(trackId), handles_(handles) {}

    void render(const BlockInfo&, juce::dsp::AudioBlock<float> out) override {
        out.clear();

        const magda::engine::LaunchHandleFeed::Reader table(handles_);
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

/// A host with a launcher: it binds a session source for every track.
class LauncherFactory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineAudioSource> createClipAudioSource(TrackId) override {
        return std::make_unique<ConstantSource>(0.0f);
    }

    std::unique_ptr<EngineAudioSource> createSessionAudioSource(TrackId trackId) override {
        REQUIRE(handles != nullptr);
        return std::make_unique<LaunchGatedSource>(trackId, *handles);
    }

    magda::engine::LaunchHandleFeed* handles = nullptr;
};

/// A snapshot with one slot in one scene of one track, and nothing else.
std::shared_ptr<const magda::engine::ClipSnapshot> snapshotWithSlot(TrackId trackId, int scene) {
    auto snapshot = std::make_shared<magda::engine::ClipSnapshot>();

    magda::engine::TrackClipPlayback track;
    track.trackId = trackId;

    magda::engine::SessionSlotPlayback slot;
    slot.sceneIndex = scene;
    slot.lengthBeats = 4.0;
    slot.audio.emplace_back();
    track.session.push_back(std::move(slot));

    snapshot->tracks.push_back(std::move(track));
    return snapshot;
}

/// Playing from a beat, with the metronome off.
magda::engine::TransportSnapshot rolling(double fromBeat) {
    magda::engine::TransportSnapshot transport;
    transport.request.generation = 1;
    transport.request.playing = true;
    transport.request.positionBeat = fromBeat;
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

    auto* device = factory.devicesById[DeviceKey{7}];
    REQUIRE(device != nullptr);
    const auto devicesCreated = ledger.devicesCreated.load();
    device->latency = 16;

    REQUIRE(publish(session, plan, tracks).published);

    CHECK(session.livePlan() == plan);
    CHECK(ledger.devicesCreated.load() == devicesCreated);
    CHECK(factory.devicesById[DeviceKey{7}] == device);

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

    auto* device = factory.devicesById[DeviceKey{7}];
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
    auto* added = factory.devicesById[DeviceKey{8}];
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
    factory.latencyFor[DeviceKey{7}] = 16;
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

    auto* device = factory.devicesById[DeviceKey{7}];
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
    CHECK(factory.devicesById[DeviceKey{7}] == device);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.25f));
}

TEST_CASE("Section-local device ids keep distinct values, instances and lifetimes",
          "[engine][session][device-identity]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    auto track = makeTrack(1);
    auto fx = makeEffect(3);
    fx.gainValue = 0.5f;
    track.chain.fxChainElements.push_back(makeDeviceElement(fx));

    auto postFx = makeEffect(3);
    postFx.gainValue = 0.25f;
    track.chain.postFxChainElements.push_back(PostFxChainElement{postFx});

    auto analysis = makeEffect(3);
    analysis.deviceType = DeviceType::Analysis;
    track.chain.mixerAnalysisElements.push_back(PostFxChainElement{analysis});

    std::vector<TrackInfo> tracks{track};
    const auto plan = compile(tracks);
    const auto values = resolve(*plan, tracks);

    // Value lookup follows the same section discriminator as compilation.
    std::map<ChainSegment, float> gains;
    for (std::size_t i = 0; i < plan->ops.size(); ++i)
        if (plan->ops[i].key.role == magda::engine::OpRole::DeviceGain)
            gains[plan->ops[i].key.segment] = values.ops[i].gainLeft;
    CHECK(gains[ChainSegment::Fx] == approx(0.5f));
    CHECK(gains[ChainSegment::PostFx] == approx(0.25f));
    CHECK_FALSE(gains.contains(ChainSegment::MixerAnalysis));

    REQUIRE(publish(session, plan, tracks).published);
    CHECK(ledger.devicesCreated == 3);

    const DeviceKey fxKey{ChainSegment::Fx, 3};
    const DeviceKey postFxKey{ChainSegment::PostFx, 3};
    const DeviceKey analysisKey{ChainSegment::MixerAnalysis, 3};
    auto* const fxInstance = factory.devicesById[fxKey];
    auto* const postFxInstance = factory.devicesById[postFxKey];
    auto* const analysisInstance = factory.devicesById[analysisKey];
    REQUIRE(fxInstance != nullptr);
    REQUIRE(postFxInstance != nullptr);
    REQUIRE(analysisInstance != nullptr);
    CHECK(fxInstance != postFxInstance);
    CHECK(fxInstance != analysisInstance);
    CHECK(postFxInstance != analysisInstance);

    session.publishValues(values);
    juce::AudioBuffer<float> output(2, kBlockSize);
    session.process(kBlockSize, output);
    // Each test device halves the signal, then the independently resolved FX
    // and post-FX slot gains apply. Analysis devices have no slot gain.
    CHECK(output.getSample(0, 0) == approx(1.0f / 64.0f));

    tracks[0].chain.postFxChainElements.clear();
    REQUIRE(publish(session, compile(tracks), tracks).published);
    CHECK(ledger.devicesDestroyed == 1);
    CHECK(factory.devicesById[fxKey] == fxInstance);
    CHECK(factory.devicesById[analysisKey] == analysisInstance);

    tracks[0].chain.fxChainElements.clear();
    tracks[0].chain.mixerAnalysisElements.clear();
    REQUIRE(publish(session, compile(tracks), tracks).published);
    CHECK(ledger.devicesDestroyed == 3);
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

    // What the model still names: the track's clip source, the track's meter
    // and the master's. The device's own meter went with the device, keyed on
    // the DeviceId that is no longer there.
    CHECK(session.runtimeObjectCount() == 3);
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
    auto* device = factory.devicesById[DeviceKey{7}];
    REQUIRE(device != nullptr);

    SECTION("bypassed") {
        auto bypassed = tracks;
        getDevice(bypassed[0].chain.fxChainElements[0]).bypassed = true;
        publish(session, compile(bypassed), bypassed);

        CHECK(ledger.devicesDestroyed == 0);
        CHECK(factory.devicesById[DeviceKey{7}] == device);

        publish(session, compile(tracks), tracks);
        CHECK(ledger.devicesCreated == 1);
        CHECK(factory.devicesById[DeviceKey{7}] == device);
    }

    SECTION("chain power off") {
        auto powered = tracks;
        powered[0].chain.enabled = false;
        publish(session, compile(powered), powered);

        CHECK(ledger.devicesDestroyed == 0);

        powered[0].chain.enabled = true;
        publish(session, compile(powered), powered);
        CHECK(ledger.devicesCreated == 1);
        CHECK(factory.devicesById[DeviceKey{7}] == device);
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
    REQUIRE(factory.devicesById[DeviceKey{7}] != nullptr);

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
    CHECK(factory.devicesById[DeviceKey{7}] != nullptr);
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
    CHECK(before.beats.end == Catch::Approx(1.0).margin(1e-9));
    CHECK(after.beats.start == Catch::Approx(0.0).margin(1e-9));
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

TEST_CASE("A meter is bound at the master, at a track and at a device slot",
          "[engine][session][tap]") {
    // The three places the issue names, and the three the compiler emits a
    // Meter op at. A tap that only reached one of them would leave a mixer with
    // meters on some strips and not others.
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto tracks = trackWithEffect(7);
    const auto plan = compile(tracks);
    REQUIRE(publish(session, plan, tracks).published);

    CHECK(meterAt(factory, *plan, magda::engine::OpRole::TrackMeter, MASTER_TRACK_ID) != nullptr);
    CHECK(meterAt(factory, *plan, magda::engine::OpRole::TrackMeter, 1) != nullptr);
    CHECK(meterAt(factory, *plan, magda::engine::OpRole::DeviceMeter, 1, 7) != nullptr);
}

TEST_CASE("A meter reads the level the track is rendering", "[engine][session][tap]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const std::vector<TrackInfo> tracks{makeTrack(1)};
    const auto plan = compile(tracks);
    REQUIRE(publish(session, plan, tracks).published);

    auto* tap = meterAt(factory, *plan, magda::engine::OpRole::TrackMeter, 1);
    REQUIRE(tap != nullptr);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.publishTransport(rolling(0.0));
    session.process(kBlockSize, output);

    // The source renders at unity and nothing between it and the meter
    // attenuates, so this is the whole of the path from a block to a display.
    CHECK(tap->read().loudest() == approx(1.0f));
}

TEST_CASE("A meter is the same tap across a plan swap", "[engine][session][tap]") {
    // The differ's promise, applied to taps: a structural edit elsewhere in the
    // project must not reset a meter that was reading. Rebuilding it would drop
    // whatever it had accumulated since the last poll, which is a meter that
    // flickers to zero every time a device is added anywhere.
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const std::vector<TrackInfo> before{makeTrack(1)};
    const auto firstPlan = compile(before);
    REQUIRE(publish(session, firstPlan, before).published);

    auto* firstTap = meterAt(factory, *firstPlan, magda::engine::OpRole::TrackMeter, 1);
    REQUIRE(firstTap != nullptr);
    const auto metersAfterFirst = factory.metersCreated;

    const auto after = trackWithEffect(7);
    const auto secondPlan = compile(after);
    REQUIRE(publish(session, secondPlan, after).published);

    CHECK(meterAt(factory, *secondPlan, magda::engine::OpRole::TrackMeter, 1) == firstTap);

    // Only the new device's meter was made. The track's and the master's were
    // already there and were handed back.
    CHECK(factory.metersCreated == metersAfterFirst + 1);
}

TEST_CASE("A meter goes when the device it reads is deleted", "[engine][session][tap]") {
    Ledger ledger;
    TestFactory factory(ledger);
    EngineSession session(factory);

    const auto withEffect = trackWithEffect(7);
    const auto firstPlan = compile(withEffect);
    REQUIRE(publish(session, firstPlan, withEffect).published);
    const auto objectsWithEffect = session.runtimeObjectCount();

    const std::vector<TrackInfo> withoutEffect{makeTrack(1)};
    const auto secondPlan = compile(withoutEffect);
    REQUIRE(publish(session, secondPlan, withoutEffect).published);

    // The device and its meter, both keyed on the DeviceId the model no longer
    // holds. A tap that outlived its device would be a row in the mixer for
    // something that is not there.
    CHECK(session.runtimeObjectCount() == objectsWithEffect - 2);
}

TEST_CASE("A host that wants no meters binds none and renders the same", "[engine][session][tap]") {
    Ledger ledger;
    TestFactory factory(ledger);
    factory.makesMeters = false;
    EngineSession session(factory);

    const auto tracks = trackWithEffect(7);
    const auto plan = compile(tracks);
    const auto result = publish(session, plan, tracks);

    // Not a diagnostic. A plan has a meter at every track and device slot, and
    // an export reporting one line per meter would bury whatever it had to say.
    CHECK(result.published);
    CHECK(result.messages.empty());
    CHECK(factory.metersCreated == 0);

    juce::AudioBuffer<float> output(2, kBlockSize);
    session.publishTransport(rolling(0.0));
    session.process(kBlockSize, output);

    CHECK(output.getMagnitude(0, kBlockSize) > 0.0f);
}

TEST_CASE("A session on a thread pool renders what a session without one renders",
          "[engine][session][parallel]") {
    // The session drives the parallel executor whether or not it was given
    // threads, so this is not two engines being compared: it is one, with the
    // block spread out and not. Bit for bit, because that is the executor's
    // whole claim and the session is where it has to survive plan swaps,
    // published values and a callback cut by the transport.
    const auto render = [](magda::engine::RenderThreadPool* pool) {
        Ledger ledger;
        TestFactory factory(ledger);
        factory.latencyFor[DeviceKey{7}] = 53;
        EngineSession session(factory, pool);

        auto tracks = trackWithEffect(7);
        tracks.push_back(makeTrack(2));
        const auto plan = compile(tracks);
        REQUIRE(publish(session, plan, tracks).published);
        session.publishValues(resolve(*plan, tracks));
        session.publishTransport(rolling(0.0));

        std::vector<float> samples;
        juce::AudioBuffer<float> output(2, kBlockSize);
        for (int block = 0; block < 16; ++block) {
            // A swap partway through, so what the differ carried across it is
            // part of what is being compared.
            if (block == 8) {
                tracks.push_back(makeTrack(3));
                const auto grown = compile(tracks);
                REQUIRE(publish(session, grown, tracks).published);
                session.publishValues(resolve(*grown, tracks));
            }

            output.clear();
            session.process(kBlockSize, output);
            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                for (int sample = 0; sample < kBlockSize; ++sample)
                    samples.push_back(output.getSample(channel, sample));
        }
        return samples;
    };

    const auto alone = render(nullptr);
    REQUIRE_FALSE(alone.empty());

    magda::engine::RenderThreadPool pool(4, false);
    CHECK(render(&pool) == alone);
}

TEST_CASE("Clips reach the voice pool with the transport that plays them",
          "[engine][session][clip]") {
    // Two things the session owes the clip layer (#2035), and both of them are
    // silence when they are missing: a pool that never sees a snapshot opens no
    // readers at all, and one that never hears where the transport is opens
    // them for the top of the timeline while playback is somewhere else.
    Ledger ledger;
    TestFactory factory(ledger);
    OpenEverything files;
    magda::engine::PrefetchThread reader;
    magda::engine::ClipVoicePool voices(files, reader, context());

    EngineSession session(factory, nullptr, &voices);

    const auto tracks = trackWithEffect(7);
    REQUIRE(publish(session, compile(tracks), tracks).published);
    session.publishTransport(rolling(0.0));

    // Far enough down the timeline that only a transport which has got near it
    // provisions a reader.
    session.publishClips(clipsOver(1, 2.0, 3.0));

    voices.service();
    REQUIRE(voices.streamCount() == 0);

    juce::AudioBuffer<float> output(2, kBlockSize);
    const auto blocks = static_cast<int>(1.2 * 44100.0 / kBlockSize);
    for (auto block = 0; block < blocks; ++block) {
        output.clear();
        session.process(kBlockSize, output);
    }

    voices.service();
    CHECK(voices.streamCount() == 1);
}

TEST_CASE("A launch published with the clips reaches the audio thread", "[engine][session]") {
    // The whole path once: publishClips makes and publishes the handle,
    // process() advances it, and the session op is bound to a source reading it.
    LauncherFactory factory;
    EngineSession session(factory);
    factory.handles = &session.launchHandleFeed();

    const std::vector<TrackInfo> tracks{makeTrack(1)};
    const auto plan = compile(tracks);
    REQUIRE(publish(session, plan, tracks).published);

    session.publishTransport(rolling(0.0));
    session.publishClips(snapshotWithSlot(1, 0));

    juce::AudioBuffer<float> output(2, kBlockSize);

    // Nothing launched, so the session is silent.
    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.0f));

    const auto slot = magda::engine::SlotKey{1, 0};
    REQUIRE(session.launchHandle(slot) != nullptr);
    CHECK(session.launchHandle(magda::engine::SlotKey{1, 3}) == nullptr);

    // Asked for from off the audio thread and applied by the block it reaches:
    // the lane rather than a pointer into the handle (#2305).
    {
        magda::engine::LaunchRequestQueue::Gesture gesture(session.launchRequests());
        gesture.play(slot);
    }

    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(1.0f));

    {
        magda::engine::LaunchRequestQueue::Gesture gesture(session.launchRequests());
        gesture.stop(slot);
    }

    session.process(kBlockSize, output);
    CHECK(output.getSample(0, 0) == approx(0.0f));
}

TEST_CASE("A structural edit under a playing slot does not restart it",
          "[engine][session][launch]") {
    // A track added while a scene plays compiles a new plan and swaps it in,
    // and the clip that was sounding goes on from where it was: handles live in
    // the store, so the swap has nothing of theirs to take (#2305).
    LauncherFactory factory;
    EngineSession session(factory);
    factory.handles = &session.launchHandleFeed();

    const std::vector<TrackInfo> before{makeTrack(1)};
    REQUIRE(publish(session, compile(before), before).published);

    session.publishTransport(rolling(0.0));
    session.publishClips(snapshotWithSlot(1, 0));

    const auto slot = magda::engine::SlotKey{1, 0};
    juce::AudioBuffer<float> output(2, kBlockSize);

    {
        magda::engine::LaunchRequestQueue::Gesture gesture(session.launchRequests());
        gesture.play(slot);
    }

    for (auto block = 0; block < 3; ++block)
        session.process(kBlockSize, output);

    REQUIRE(output.getSample(0, 0) == approx(1.0f));

    const auto* handle = session.launchHandle(slot);
    REQUIRE(handle != nullptr);

    const auto running = handle->playedSampleRange();
    REQUIRE(running.has_value());

    // The structural edit: another track, which is a different plan and a new
    // epoch. The clips are republished with it, as a real edit would.
    std::vector<TrackInfo> after{makeTrack(1)};
    after.push_back(makeTrack(2));
    REQUIRE(publish(session, compile(after), after).published);
    session.publishClips(snapshotWithSlot(1, 0));

    for (auto block = 0; block < 3; ++block)
        session.process(kBlockSize, output);

    // Not dropped.
    CHECK(output.getSample(0, 0) == approx(1.0f));

    const auto* kept = session.launchHandle(slot);
    REQUIRE(kept == handle);

    const auto still = kept->playedSampleRange();
    REQUIRE(still.has_value());

    // Not restarted: the run began where it began, and a swap is not an event
    // the launcher has anything to say about.
    CHECK(still->start == running->start);

    // Not doubled: advanced twice over a swap, the run would be six blocks long
    // and the clip half a bar ahead of the transport.
    CHECK(still->end == running->end + magda::engine::SampleDuration{3 * kBlockSize});
}
