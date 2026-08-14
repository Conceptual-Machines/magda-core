#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/ParallelPlanExecutor.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/RenderThreadPool.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file test_parallel_executor.cpp
 * @brief What the parallel executor owes the reference one: the same samples.
 *
 * Not nearly the same. Every test here compares floats with ==, because the
 * claim being made is bit-identity at every thread count, and a tolerance would
 * hide exactly the thing that would go wrong: a sum that took its inputs in the
 * order they finished rather than the order they were compiled in.
 *
 * Every comparison renders the model twice from scratch. A device carries state
 * from one block to the next, so two renders means two of everything behind the
 * plan, and a scene is therefore a recipe rather than a fixture.
 */

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::DeviceBlock;
using magda::engine::DeviceKey;
using magda::engine::EngineAudioSource;
using magda::engine::EngineDevice;
using magda::engine::EngineMidiSource;
using magda::engine::LevelTap;
using magda::engine::OpId;
using magda::engine::OpKey;
using magda::engine::ParallelPlanExecutor;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanValues;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::RenderThreadPool;

namespace {

constexpr int kBlockSize = 64;
constexpr double kSamplesPerBeat = 64.0;
constexpr int kNumBlocks = 24;

BlockInfo blockAt(std::int64_t timelineSample, int numSamples) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.playing = true;
    block.startBeat = static_cast<double>(timelineSample) / kSamplesPerBeat;
    block.endBeat = static_cast<double>(timelineSample + numSamples) / kSamplesPerBeat;
    block.startSeconds = static_cast<double>(timelineSample) / 44100.0;
    block.endSeconds = static_cast<double>(timelineSample + numSamples) / 44100.0;
    block.continuous = timelineSample > 0;
    return block;
}

std::int64_t timelineSampleOf(const BlockInfo& block) {
    return static_cast<std::int64_t>(block.startBeat * kSamplesPerBeat + 0.5);
}

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

DeviceInfo makeInstrument(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Instrument " + juce::String(id);
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    return device;
}

/// A different sample at every timeline position, and a different one per
/// source, so a buffer read before its producer wrote it comes out wrong rather
/// than merely early.
class RampSource final : public EngineAudioSource {
  public:
    explicit RampSource(int seed) : seed_(seed) {}

    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        const auto start = timelineSampleOf(block);
        for (int sample = 0; sample < block.numSamples; ++sample) {
            const auto value =
                static_cast<float>((start + sample + seed_) % 91) * (1.0f / 91.0f) + 0.001f;
            for (std::size_t channel = 0; channel < out.getNumChannels(); ++channel)
                out.setSample(static_cast<int>(channel), sample,
                              value * (channel == 0 ? 1.0f : -0.5f));
        }
    }

  private:
    int seed_;
};

/// A note-on every @p period samples of the timeline, so what a device receives
/// does not depend on how the blocks were cut.
class TimelineNoteSource final : public EngineMidiSource {
  public:
    TimelineNoteSource(int noteNumber, int period) : noteNumber_(noteNumber), period_(period) {}

    void render(const BlockInfo& block, juce::MidiBuffer& out) override {
        const auto start = timelineSampleOf(block);
        const auto first = (start + period_ - 1) / period_ * period_;
        for (auto position = first; position < start + block.numSamples; position += period_)
            out.addEvent(juce::MidiMessage::noteOn(1, noteNumber_, 1.0f),
                         static_cast<int>(position - start));
    }

  private:
    int noteNumber_, period_;
};

/// Scales its input, and counts the blocks it saw. The count is what says an op
/// ran once per block rather than twice or not at all, which no amount of
/// comparing output can distinguish from a gain that happened to agree.
class GainDevice final : public EngineDevice {
  public:
    explicit GainDevice(float gain) : gain_(gain) {}

    void process(DeviceBlock& block) override {
        ++processedBlocks;
        block.audio.multiplyBy(gain_);
    }

    int processedBlocks = 0;

  private:
    float gain_;
};

/// Scales by which block it is on, so a block rendered twice or skipped shows
/// up in the samples themselves and not only in a counter.
class TallyDevice final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        ++blocks_;
        block.audio.multiplyBy(1.0f / static_cast<float>(blocks_));
    }

  private:
    int blocks_ = 0;
};

/// Delays what it is given, and reports the latency it really has, so the plan
/// compiles delays around it and the schedule has to keep them fed in order.
class LatentDevice final : public EngineDevice {
  public:
    explicit LatentDevice(int latency) : latency_(latency) {}

    void prepare(const RenderContext& context) override {
        history_.assign(static_cast<std::size_t>(context.numChannels),
                        std::deque<float>(static_cast<std::size_t>(latency_), 0.0f));
    }

    void process(DeviceBlock& block) override {
        for (std::size_t channel = 0; channel < block.audio.getNumChannels(); ++channel) {
            auto* samples = block.audio.getChannelPointer(channel);
            auto& history = history_[channel];
            for (int sample = 0; sample < block.block.numSamples; ++sample) {
                history.push_back(samples[sample]);
                samples[sample] = history.front();
                history.pop_front();
            }
        }
    }

    int latencySamples() const override {
        return latency_;
    }

  private:
    int latency_;
    std::vector<std::deque<float>> history_;
};

/// Turns the notes it receives into DC, so MIDI arriving at the wrong moment is
/// audible in the output rather than invisible.
class NoteToDcInstrument final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        for (const auto metadata : *block.midiIn)
            if (const auto message = metadata.getMessage(); message.isNoteOn())
                level_ = static_cast<float>(message.getNoteNumber()) / 127.0f;

        block.audio.fill(level_);
    }

  private:
    float level_ = 0.0f;
};

/// Emits a note of its own, which the chain merges with what it was given.
class ArpDevice final : public EngineDevice {
  public:
    explicit ArpDevice(int noteNumber) : noteNumber_(noteNumber) {}

    void process(DeviceBlock& block) override {
        block.audio.clear();
        if (block.midiOut != nullptr)
            block.midiOut->addEvent(juce::MidiMessage::noteOn(1, noteNumber_, 1.0f), 0);
    }

  private:
    int noteNumber_;
};

/**
 * @brief Two devices that will not finish until both have started.
 *
 * Everything else here would pass on a pool whose workers never woke: the
 * output would be right and only the wall clock would say otherwise. This is
 * the one test that cannot, because it counts how many devices were inside
 * process() at once, and one thread can never make that two.
 *
 * Blocking inside process() is a thing no real device may do. It is safe here
 * because the branches are independent, so the ops can genuinely run at the
 * same time, and because the wait times out rather than hanging: a pool that
 * does not deliver fails this in two seconds instead of never returning.
 */
struct Meeting {
    std::mutex mutex;
    std::condition_variable arrived;
    int inside = 0;
    int mostAtOnce = 0;
    bool met = false;
};

class RendezvousDevice final : public EngineDevice {
  public:
    RendezvousDevice(Meeting& meeting, int expected) : meeting_(meeting), expected_(expected) {}

    void process(DeviceBlock& block) override {
        block.audio.clear();

        std::unique_lock<std::mutex> lock(meeting_.mutex);
        ++meeting_.inside;
        meeting_.mostAtOnce = std::max(meeting_.mostAtOnce, meeting_.inside);

        if (meeting_.inside >= expected_) {
            meeting_.met = true;
            meeting_.arrived.notify_all();
        } else {
            meeting_.arrived.wait_for(lock, std::chrono::seconds(2),
                                      [this] { return meeting_.met; });
        }

        --meeting_.inside;
    }

  private:
    Meeting& meeting_;
    int expected_;
};

/**
 * @brief A model and the objects behind it, built fresh for one render.
 *
 * Owns everything the bindings point at: comparing two executors means running
 * the same recipe twice, because a device that has already seen twelve blocks
 * is not the device the other run started with.
 */
struct Scene {
    std::vector<TrackInfo> tracks;
    TrackInfo master = makeMaster();
    PlanBindings bindings;

    std::vector<std::unique_ptr<EngineAudioSource>> audioSources;
    std::vector<std::unique_ptr<EngineMidiSource>> midiSources;
    std::vector<std::unique_ptr<EngineDevice>> devices;

    EngineAudioSource* own(std::unique_ptr<EngineAudioSource> source) {
        audioSources.push_back(std::move(source));
        return audioSources.back().get();
    }
    EngineMidiSource* own(std::unique_ptr<EngineMidiSource> source) {
        midiSources.push_back(std::move(source));
        return midiSources.back().get();
    }
    EngineDevice* own(std::unique_ptr<EngineDevice> device) {
        devices.push_back(std::move(device));
        return devices.back().get();
    }
};

/// What the runtime store does when it makes an object: the executor does not
/// prepare what it does not own.
void prepareBindings(PlanBindings& bindings, const RenderContext& context) {
    for (auto& [id, device] : bindings.devices)
        device->prepare(context);
    for (auto& [id, source] : bindings.clipAudio)
        source->prepare(context);
    for (auto& [id, source] : bindings.clipMidi)
        source->prepare(context);
    for (auto& [id, source] : bindings.audioInputs)
        source->prepare(context);
    for (auto& [id, source] : bindings.midiInputs)
        source->prepare(context);
}

/// Everything one render needs: the compiled plan, the values, the taps, and
/// somewhere to render into.
struct Rig {
    explicit Rig(Scene sceneIn) : scene(std::move(sceneIn)) {
        plan = magda::engine::compileRenderPlan(scene.tracks, scene.master);
        valueMessages = magda::engine::resolvePlanValues(plan, scene.tracks, scene.master, values);

        for (const auto& op : plan.ops) {
            if (op.kind != magda::engine::OpKind::Meter)
                continue;
            auto tap = std::make_unique<LevelTap>();
            scene.bindings.meters[op.key] = tap.get();
            meters.emplace(op.key, std::move(tap));
        }

        prepareBindings(scene.bindings, context);
        output.setSize(context.numChannels, kBlockSize);
    }

    /// Every tap, in key order, taken once. Order matters: a read is
    /// destructive, so this has to be the same walk for both executors.
    std::vector<float> readMeters() {
        std::vector<float> levels;
        for (auto& [key, tap] : meters)
            levels.push_back(tap->read().loudest());
        return levels;
    }

    Scene scene;
    RenderPlan plan;
    PlanValues values;
    std::vector<std::string> valueMessages;
    std::map<OpKey, std::unique_ptr<LevelTap>> meters;
    RenderContext context{44100.0, kBlockSize, 2};
    juce::AudioBuffer<float> output;
};

/// Every sample of every block, and what the meters read at the end of them.
struct Render {
    std::vector<float> samples;
    std::vector<float> meters;
};

/// How long each block of a render is. Blocks shorter than the prepared one are
/// legal and a scheduler that assumed otherwise would only be caught by them.
std::vector<int> blockLengths(int numBlocks, bool ragged) {
    std::vector<int> lengths;
    for (int block = 0; block < numBlocks; ++block)
        lengths.push_back(ragged ? 1 + (block * 17) % kBlockSize : kBlockSize);
    return lengths;
}

template <typename Executor>
Render renderThrough(Rig& rig, Executor& executor, const std::vector<int>& lengths) {
    INFO(magda::engine::dumpPlan(rig.plan));
    for (const auto& message : rig.valueMessages)
        FAIL_CHECK(message);
    for (const auto& message : executor.prepare(rig.plan, rig.scene.bindings, rig.context))
        FAIL_CHECK(message);

    Render render;
    std::int64_t position = 0;
    for (const auto length : lengths) {
        executor.process(rig.values, blockAt(position, length), rig.output);
        for (int channel = 0; channel < rig.output.getNumChannels(); ++channel)
            for (int sample = 0; sample < length; ++sample)
                render.samples.push_back(rig.output.getSample(channel, sample));
        position += length;
    }

    render.meters = rig.readMeters();
    return render;
}

Render renderReference(Scene scene, const std::vector<int>& lengths) {
    Rig rig(std::move(scene));
    PlanExecutor executor;
    return renderThrough(rig, executor, lengths);
}

Render renderParallel(Scene scene, RenderThreadPool& pool, const std::vector<int>& lengths) {
    Rig rig(std::move(scene));
    ParallelPlanExecutor executor(&pool);
    return renderThrough(rig, executor, lengths);
}

/// Where two renders first disagree, or -1. A sample index rather than a bare
/// failure, because the index says which op got it wrong.
int firstDifference(const std::vector<float>& expected, const std::vector<float>& actual) {
    if (expected.size() != actual.size())
        return 0;
    for (std::size_t sample = 0; sample < expected.size(); ++sample)
        if (expected[sample] != actual[sample])
            return static_cast<int>(sample);
    return -1;
}

// --- the scenes ------------------------------------------------------------

/// One track, one chain. The degenerate case: nothing to run in parallel, and
/// every op waiting on the one before it.
Scene chainScene() {
    Scene scene;
    auto track = makeTrack(1);
    track.volume = 0.7f;
    track.pan = -0.25f;
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));
    scene.tracks.push_back(track);

    scene.bindings.clipAudio[1] = scene.own(std::make_unique<RampSource>(3));
    scene.bindings.devices[DeviceKey{7}] = scene.own(std::make_unique<GainDevice>(0.5f));
    scene.bindings.devices[DeviceKey{8}] = scene.own(std::make_unique<TallyDevice>());
    return scene;
}

/// Eight tracks into the master. The widest fan-in the model produces, and the
/// one place where summing in the order things finished would be visible.
Scene wideScene() {
    Scene scene;
    for (TrackId id = 1; id <= 8; ++id) {
        auto track = makeTrack(id);
        track.volume = 0.4f + 0.05f * static_cast<float>(id);
        track.pan = -1.0f + 0.25f * static_cast<float>(id);
        track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(100 + id)));
        scene.tracks.push_back(track);

        scene.bindings.clipAudio[id] = scene.own(std::make_unique<RampSource>(id * 7));
        scene.bindings.devices[DeviceKey{100 + id}] =
            scene.own(std::make_unique<GainDevice>(0.9f - 0.05f * static_cast<float>(id)));
    }
    return scene;
}

/// Sends into an aux, which is the plan's other fan-in and the one that crosses
/// tracks: the aux return waits on every track that feeds it.
Scene sendScene() {
    Scene scene;
    auto aux = makeTrack(9, TrackType::Aux);
    aux.auxBusIndex = 0;
    aux.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(200)));

    for (TrackId id = 1; id <= 4; ++id) {
        auto track = makeTrack(id);
        track.sends.push_back(SendInfo{0, 0.3f + 0.1f * static_cast<float>(id), id % 2 == 0, 9});
        scene.tracks.push_back(track);
        scene.bindings.clipAudio[id] = scene.own(std::make_unique<RampSource>(id * 13));
    }

    scene.tracks.push_back(aux);
    scene.bindings.devices[DeviceKey{200}] = scene.own(std::make_unique<GainDevice>(0.6f));
    return scene;
}

/// A rack's parallel chains: fan-out and fan-in inside one track.
Scene rackScene() {
    Scene scene;
    auto rack = std::make_unique<RackInfo>();
    rack->id = 5;
    rack->volume = -3.0f;

    for (int index = 0; index < 3; ++index) {
        ChainInfo chain;
        chain.id = 10 + index;
        chain.volume = -2.0f * static_cast<float>(index);
        chain.pan = 0.5f - 0.5f * static_cast<float>(index);
        chain.elements.push_back(makeDeviceElement(makeEffect(300 + index)));
        rack->chains.push_back(std::move(chain));

        scene.bindings.devices[DeviceKey{300 + index}] =
            scene.own(std::make_unique<GainDevice>(0.3f + 0.2f * static_cast<float>(index)));
    }

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(ChainElement{std::move(rack)});
    scene.tracks.push_back(track);
    scene.bindings.clipAudio[1] = scene.own(std::make_unique<RampSource>(5));
    return scene;
}

/// Devices that really are late, so the plan carries delays and the paths
/// meeting at the master have to be aligned by them rather than by luck.
Scene latencyScene() {
    Scene scene;
    auto aux = makeTrack(9, TrackType::Aux);
    aux.auxBusIndex = 0;

    auto latent = makeTrack(1);
    latent.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(400)));
    latent.sends.push_back(SendInfo{0, 0.8f, false, 9});
    scene.tracks.push_back(latent);

    auto slower = makeTrack(2);
    slower.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(401)));
    scene.tracks.push_back(slower);

    scene.tracks.push_back(makeTrack(3));
    scene.tracks.push_back(aux);

    scene.bindings.clipAudio[1] = scene.own(std::make_unique<RampSource>(11));
    scene.bindings.clipAudio[2] = scene.own(std::make_unique<RampSource>(23));
    scene.bindings.clipAudio[3] = scene.own(std::make_unique<RampSource>(37));
    scene.bindings.devices[DeviceKey{400}] = scene.own(std::make_unique<LatentDevice>(37));
    scene.bindings.devices[DeviceKey{401}] = scene.own(std::make_unique<LatentDevice>(96));
    return scene;
}

/// MIDI through instruments, and a device that adds MIDI of its own so the
/// chain merges two streams.
Scene midiScene() {
    Scene scene;
    for (TrackId id = 1; id <= 3; ++id) {
        auto track = makeTrack(id);
        track.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(500 + id)));
        track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(600 + id)));
        scene.tracks.push_back(track);

        scene.bindings.clipAudio[id] = scene.own(std::make_unique<RampSource>(id * 3));
        scene.bindings.clipMidi[id] =
            scene.own(std::make_unique<TimelineNoteSource>(48 + 5 * id, 37 + id));
        scene.bindings.devices[DeviceKey{500 + id}] =
            scene.own(std::make_unique<NoteToDcInstrument>());
        scene.bindings.devices[DeviceKey{600 + id}] =
            scene.own(std::make_unique<GainDevice>(0.75f));
    }

    // MIDI out and the raw input passed on with it, so the chain merges two
    // streams rather than replacing one.
    auto arpTrack = makeTrack(4);
    auto arp = makeInstrument(700);
    arp.producesMidi = true;
    arp.midiInThru = true;
    arpTrack.chain.fxChainElements.push_back(makeDeviceElement(arp));
    arpTrack.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(701)));
    scene.tracks.push_back(arpTrack);

    scene.bindings.clipAudio[4] = scene.own(std::make_unique<RampSource>(17));
    scene.bindings.clipMidi[4] = scene.own(std::make_unique<TimelineNoteSource>(72, 29));
    scene.bindings.devices[DeviceKey{700}] = scene.own(std::make_unique<ArpDevice>(60));
    scene.bindings.devices[DeviceKey{701}] = scene.own(std::make_unique<NoteToDcInstrument>());
    return scene;
}

/// Tracks feeding another track, which is the deepest the graph gets: the group
/// cannot start until everything routed into it has finished.
Scene groupScene() {
    Scene scene;
    auto group = makeTrack(5);
    group.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(800)));
    scene.tracks.push_back(group);

    for (TrackId id = 1; id <= 3; ++id) {
        auto track = makeTrack(id);
        track.audioOutputDevice = "track:5";
        track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(810 + id)));
        scene.tracks.push_back(track);

        scene.bindings.clipAudio[id] = scene.own(std::make_unique<RampSource>(id * 29));
        scene.bindings.devices[DeviceKey{810 + id}] = scene.own(std::make_unique<TallyDevice>());
    }

    scene.bindings.clipAudio[5] = scene.own(std::make_unique<RampSource>(53));
    scene.bindings.devices[DeviceKey{800}] = scene.own(std::make_unique<GainDevice>(0.8f));
    return scene;
}

struct NamedScene {
    const char* name;
    Scene (*build)();
};

const std::vector<NamedScene> kScenes = {
    {"one chain", chainScene},      {"eight tracks", wideScene}, {"sends into an aux", sendScene},
    {"a rack's chains", rackScene}, {"latency", latencyScene},   {"MIDI", midiScene},
    {"a group track", groupScene},
};

/// The thread counts every scene is rendered at. Zero workers is the parallel
/// executor with nothing to be parallel with, which has to agree too: it is the
/// same code, and a schedule that only works when it is spread out is a
/// schedule that works by accident.
const std::vector<int> kWorkerCounts = {0, 1, 2, 3, 7};

}  // namespace

TEST_CASE("The parallel executor renders what the reference executor renders, sample for sample",
          "[engine][exec][parallel]") {
    const auto lengths = blockLengths(kNumBlocks, false);

    // Made once and rendered through by every scene: a pool outlives plans, and
    // reusing one across epochs is what a session does.
    std::vector<std::unique_ptr<RenderThreadPool>> pools;
    for (const auto workers : kWorkerCounts)
        pools.push_back(std::make_unique<RenderThreadPool>(workers, false));

    for (const auto& scene : kScenes) {
        INFO("scene: " << scene.name);
        const auto expected = renderReference(scene.build(), lengths);
        REQUIRE_FALSE(expected.samples.empty());

        for (std::size_t index = 0; index < pools.size(); ++index) {
            INFO("threads: " << pools[index]->numThreads());
            const auto actual = renderParallel(scene.build(), *pools[index], lengths);
            CHECK(firstDifference(expected.samples, actual.samples) == -1);
            CHECK(actual.samples.size() == expected.samples.size());
            CHECK(actual.meters == expected.meters);
        }
    }
}

TEST_CASE("Blocks shorter than the prepared one are scheduled the same way",
          "[engine][exec][parallel]") {
    const auto lengths = blockLengths(kNumBlocks, true);
    RenderThreadPool pool(4, false);

    for (const auto& scene : kScenes) {
        INFO("scene: " << scene.name);
        const auto expected = renderReference(scene.build(), lengths);
        const auto actual = renderParallel(scene.build(), pool, lengths);
        CHECK(firstDifference(expected.samples, actual.samples) == -1);
        CHECK(actual.meters == expected.meters);
    }
}

TEST_CASE("The same scene renders identically however many times it is run",
          "[engine][exec][parallel]") {
    // Determinism across runs, not only across executors: a race that changes
    // the output would otherwise have to be caught by the one comparison it
    // happened to lose.
    const auto lengths = blockLengths(kNumBlocks, false);
    RenderThreadPool pool(7, false);

    const auto first = renderParallel(wideScene(), pool, lengths);
    for (int attempt = 0; attempt < 8; ++attempt) {
        INFO("attempt " << attempt);
        const auto again = renderParallel(wideScene(), pool, lengths);
        CHECK(firstDifference(first.samples, again.samples) == -1);
    }
}

TEST_CASE("Every op runs exactly once a block", "[engine][exec][parallel]") {
    Rig rig(wideScene());
    RenderThreadPool pool(7, false);
    ParallelPlanExecutor executor(&pool);

    REQUIRE(executor.prepare(rig.plan, rig.scene.bindings, rig.context).empty());

    for (int block = 0; block < kNumBlocks; ++block)
        executor.process(rig.values, blockAt(block * kBlockSize, kBlockSize), rig.output);

    for (auto& [id, device] : rig.scene.bindings.devices) {
        INFO("device " << id);
        auto* gain = dynamic_cast<GainDevice*>(device);
        REQUIRE(gain != nullptr);
        CHECK(gain->processedBlocks == kNumBlocks);
    }
}

TEST_CASE("Ops on independent branches really do run at the same time",
          "[engine][exec][parallel]") {
    Meeting meeting;

    Scene scene;
    for (TrackId id = 1; id <= 2; ++id) {
        auto track = makeTrack(id);
        track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(900 + id)));
        scene.tracks.push_back(track);

        scene.bindings.clipAudio[id] = scene.own(std::make_unique<RampSource>(id));
        scene.bindings.devices[DeviceKey{900 + id}] =
            scene.own(std::make_unique<RendezvousDevice>(meeting, 2));
    }

    Rig rig(std::move(scene));
    RenderThreadPool pool(3, false);
    ParallelPlanExecutor executor(&pool);
    REQUIRE(executor.prepare(rig.plan, rig.scene.bindings, rig.context).empty());

    executor.process(rig.values, blockAt(0, kBlockSize), rig.output);

    CHECK(meeting.mostAtOnce == 2);
    CHECK(meeting.met);
}

TEST_CASE("The two executors prepare to the same layout", "[engine][exec][parallel]") {
    // Everything below the schedule is shared rather than mirrored, and this is
    // what says so: a second answer to any of these would be a second
    // implementation of prepare.
    for (const auto& scene : kScenes) {
        INFO("scene: " << scene.name);
        Rig reference(scene.build());
        Rig parallel(scene.build());

        PlanExecutor referenceExecutor;
        RenderThreadPool pool(2, false);
        ParallelPlanExecutor parallelExecutor(&pool);

        REQUIRE(
            referenceExecutor.prepare(reference.plan, reference.scene.bindings, reference.context)
                .empty());
        REQUIRE(parallelExecutor.prepare(parallel.plan, parallel.scene.bindings, parallel.context)
                    .empty());

        CHECK(parallelExecutor.latencySamples() == referenceExecutor.latencySamples());
        CHECK(parallelExecutor.audioBufferCount() == referenceExecutor.audioBufferCount());
        CHECK(parallelExecutor.midiBufferCount() == referenceExecutor.midiBufferCount());
        CHECK(parallelExecutor.boundMeterCount() == referenceExecutor.boundMeterCount());
        CHECK(parallelExecutor.midiDelayOverflows() == 0);
    }
}

TEST_CASE("A plan whose schedule is not the one its ops imply is refused",
          "[engine][exec][parallel]") {
    // Every one of these is expensive rather than merely wrong, which is why
    // the check is a recompute and not a look at the array lengths. An op that
    // nothing will ever release leaves the block unfinished, and a block that
    // never finishes is an audio callback that never returns; an op released
    // early runs while another op is still writing the buffer it reads. Both
    // are invisible to validatePlan, which reads the topology, and to the
    // fingerprint, which is computed from the topology too.
    Rig rig(wideScene());
    RenderThreadPool pool(2, false);

    auto plan = rig.plan;
    REQUIRE(plan.ops.size() > 4);
    REQUIRE_FALSE(plan.initialReadyOps.empty());

    bool broken = true;

    SECTION("nothing wrong with it") {
        broken = false;
    }

    SECTION("no counts at all") {
        plan.dependencyCounts.clear();
    }

    SECTION("a count that is one too high") {
        // The op waits for a producer that does not exist, so nothing ever
        // takes it to zero and the block cannot end.
        ++plan.dependencyCounts.back();
    }

    SECTION("a count that is one too low") {
        for (auto& count : plan.dependencyCounts)
            if (count > 0) {
                --count;
                break;
            }
    }

    SECTION("a source missing from the ready set") {
        // The one the size checks could never catch: everything is the right
        // length, and an op with no producers is simply never seeded.
        plan.initialReadyOps.pop_back();
    }

    SECTION("a ready op that is not an op") {
        plan.initialReadyOps.back() = static_cast<OpId>(plan.ops.size() + 3);
    }

    SECTION("offsets that do not describe these edges") {
        plan.consumerOffsets[1] += 5;
    }

    SECTION("an edge pointing somewhere else") {
        REQUIRE_FALSE(plan.consumerEdges.empty());
        plan.consumerEdges.front() = static_cast<OpId>(plan.ops.size() + 1);
    }

    ParallelPlanExecutor executor(&pool);

    if (!broken) {
        CHECK(executor.prepare(plan, rig.scene.bindings, rig.context).empty());
        CHECK(executor.isPrepared());
        return;
    }

    // Checked here as well as through prepare, so a section that meant to break
    // something and did not is a failure rather than a test that quietly agrees.
    REQUIRE_FALSE(magda::engine::carriesSchedule(plan));

    const auto messages = executor.prepare(plan, rig.scene.bindings, rig.context);
    CHECK_FALSE(messages.empty());
    CHECK_FALSE(executor.isPrepared());

    // Refused means refused: nothing prepared, and a block that renders silence
    // rather than half a graph.
    rig.output.clear();
    executor.process(rig.values, blockAt(0, kBlockSize), rig.output);
    CHECK(rig.output.getMagnitude(0, kBlockSize) == 0.0f);
}

TEST_CASE("Retiring an epoch waits for its own workers and not for the ones after it",
          "[engine][exec][parallel]") {
    // The shape a session makes, with both threads doing their real jobs: one
    // renders block after block on the pool, the other publishes epochs and
    // retires the ones they replace. What is being asked of the retirement is
    // that it ends, and ends while the pool is busy. A wait on how many workers
    // are in the pool rather than on how many are in this job would be pushed
    // back up by every block the render thread starts next.
    struct Epoch {
        Epoch(Scene scene, RenderThreadPool& pool) : rig(std::move(scene)), executor(&pool) {}

        Rig rig;
        ParallelPlanExecutor executor;
    };

    RenderThreadPool pool(4, false);

    std::mutex swapMutex;
    std::shared_ptr<Epoch> current;
    std::atomic<int> published{0};
    std::atomic<int> rendered{-1};
    std::atomic<bool> stop{false};
    std::atomic<int> blocks{0};

    std::thread audioThread([&] {
        for (std::int64_t block = 0; !stop.load(); ++block) {
            std::shared_ptr<Epoch> epoch;
            int generation = 0;
            {
                const std::lock_guard<std::mutex> lock(swapMutex);
                epoch = current;
                generation = published.load();
            }
            if (epoch == nullptr) {
                juce::Thread::yield();
                continue;
            }

            epoch->executor.process(epoch->rig.values, blockAt(block * kBlockSize, kBlockSize),
                                    epoch->rig.output);
            rendered.store(generation);
            ++blocks;
        }
    });

    const auto started = std::chrono::steady_clock::now();
    for (int swap = 0; swap < 20; ++swap) {
        auto next = std::make_shared<Epoch>(wideScene(), pool);
        REQUIRE(next->executor.prepare(next->rig.plan, next->rig.scene.bindings, next->rig.context)
                    .empty());

        std::shared_ptr<Epoch> retiring;
        {
            const std::lock_guard<std::mutex> lock(swapMutex);
            retiring = std::move(current);
            current = std::move(next);
            published.fetch_add(1);
        }

        // Let the render thread get into the new epoch, so the one being
        // retired is let go of while the pool is busy rather than idle.
        while (rendered.load() < published.load())
            juce::Thread::yield();

        // The last reference, so the destructor and its wait run here.
        retiring.reset();
    }
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    stop.store(true);
    audioThread.join();

    CHECK(blocks.load() > 0);
    CHECK(elapsed < 10.0);
}

TEST_CASE("An executor with no plan renders silence", "[engine][exec][parallel]") {
    RenderThreadPool pool(2, false);
    ParallelPlanExecutor executor(&pool);
    juce::AudioBuffer<float> output(2, kBlockSize);
    output.clear();

    CHECK_FALSE(executor.isPrepared());
    executor.process(PlanValues{}, blockAt(0, kBlockSize), output);
    CHECK(output.getMagnitude(0, kBlockSize) == 0.0f);
}

TEST_CASE("A plan swap carries what the differ matched, and renders the same either way",
          "[engine][exec][parallel]") {
    // The swap a session makes, run through both executors: four blocks on one
    // epoch, a second epoch prepared against it, the first destroyed while the
    // pool is still live, and the rest of the render on the second. What the
    // delay lines held when the swap happened is part of the output, so this
    // compares bit for bit like everything else here.
    constexpr int kBeforeSwap = 4;
    const auto lengths = blockLengths(16, false);

    struct Swap {
        std::vector<float> samples;
        int carried = 0;
    };

    const auto swapThrough = [&lengths](auto makeExecutor) {
        Rig first(latencyScene());
        Rig second(latencyScene());

        auto retiring = makeExecutor();
        REQUIRE(retiring->prepare(first.plan, first.scene.bindings, first.context).empty());

        Swap swap;
        std::int64_t position = 0;
        const auto collect = [&swap](Rig& rig, int length) {
            for (int channel = 0; channel < rig.output.getNumChannels(); ++channel)
                for (int sample = 0; sample < length; ++sample)
                    swap.samples.push_back(rig.output.getSample(channel, sample));
        };

        for (int block = 0; block < kBeforeSwap; ++block) {
            retiring->process(first.values, blockAt(position, lengths[block]), first.output);
            collect(first, lengths[block]);
            position += lengths[block];
        }

        auto live = makeExecutor();
        REQUIRE(live->prepare(second.plan, second.scene.bindings, second.context, retiring.get())
                    .empty());
        swap.carried = live->carriedDelayLines();
        retiring.reset();

        for (std::size_t block = kBeforeSwap; block < lengths.size(); ++block) {
            live->process(second.values, blockAt(position, lengths[block]), second.output);
            collect(second, lengths[block]);
            position += lengths[block];
        }

        return swap;
    };

    const auto reference = swapThrough([] { return std::make_unique<PlanExecutor>(); });
    REQUIRE(reference.carried > 0);

    RenderThreadPool pool(4, false);
    const auto parallel =
        swapThrough([&pool] { return std::make_unique<ParallelPlanExecutor>(&pool); });

    CHECK(parallel.carried == reference.carried);
    CHECK(firstDifference(reference.samples, parallel.samples) == -1);
}

/// Hidden: a measurement, not a check. Wall clock is a property of the machine
/// it runs on, so this asserts nothing and prints what it saw. Run it with
/// `magda_tests "[bench]"`, and read it as scaling rather than as CPU: the test
/// binary is a debug build, so the ops are slower than they will ever be in
/// release and the schedule looks cheaper by comparison than it is.
TEST_CASE("How a heavy graph scales across threads", "[.][bench]") {
    constexpr int kTracks = 16;
    constexpr int kDevicesPerTrack = 4;
    constexpr int kBlocks = 200;

    /// Something to actually spend time on: a one-pole per sample, repeated, so
    /// the op costs what a small plugin costs rather than what a memcpy does.
    class BusyDevice final : public EngineDevice {
      public:
        void process(DeviceBlock& block) override {
            for (std::size_t channel = 0; channel < block.audio.getNumChannels(); ++channel) {
                auto* samples = block.audio.getChannelPointer(channel);
                for (int pass = 0; pass < 40; ++pass)
                    for (int sample = 0; sample < block.block.numSamples; ++sample) {
                        state_ += 0.05f * (samples[sample] - state_);
                        samples[sample] = state_;
                    }
            }
        }

      private:
        float state_ = 0.0f;
    };

    const auto build = []() {
        Scene scene;
        for (TrackId id = 1; id <= kTracks; ++id) {
            auto track = makeTrack(id);
            for (int slot = 0; slot < kDevicesPerTrack; ++slot) {
                const DeviceId device = id * 100 + slot;
                track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(device)));
                scene.bindings.devices[DeviceKey{device}] =
                    scene.own(std::make_unique<BusyDevice>());
            }
            scene.tracks.push_back(track);
            scene.bindings.clipAudio[id] = scene.own(std::make_unique<RampSource>(id));
        }
        return scene;
    };

    const auto lengths = blockLengths(kBlocks, false);

    for (const auto workers : {0, 1, 3, 7}) {
        RenderThreadPool pool(workers, false);
        Rig rig(build());
        ParallelPlanExecutor executor(&pool);
        REQUIRE(executor.prepare(rig.plan, rig.scene.bindings, rig.context).empty());

        const auto started = std::chrono::steady_clock::now();
        std::int64_t position = 0;
        for (const auto length : lengths) {
            executor.process(rig.values, blockAt(position, length), rig.output);
            position += length;
        }
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();

        WARN("threads " << executor.numThreads() << ": " << elapsed << " ms for " << kBlocks
                        << " blocks");
    }
}
