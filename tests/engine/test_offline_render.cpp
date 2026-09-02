#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/OfflineRender.hpp"
#include "exec/PlanValues.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/RenderPlan.hpp"
#include "tap/LevelTap.hpp"

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::DeviceBlock;
using magda::engine::EngineAudioSource;
using magda::engine::EngineDevice;
using magda::engine::OfflineRenderRequest;
using magda::engine::OfflineRenderSink;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanValues;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;
using magda::engine::TempoMap;

namespace {

constexpr double kSampleRate = 44100.0;

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

/// A tempo map with one tempo, so what a beat is worth in seconds is arithmetic
/// a test can do in its head.
TempoMap tempoAt(double bpm) {
    return TempoMap({{0.0, bpm, 0.0f}}, {});
}

/// A value derived from where the block is on the timeline rather than from how
/// many blocks have gone by. That is the whole of what block-size independence
/// asks of a source: two short blocks must render what one long one would.
class TimelineRamp final : public EngineAudioSource {
  public:
    void prepare(const RenderContext& context) override {
        sampleRate_ = context.sampleRate;
    }

    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        out.clear();
        if (!block.playing)
            return;

        const auto start = std::llround(block.seconds.start * sampleRate_);
        for (auto sample = 0; sample < block.numSamples; ++sample) {
            const auto value = static_cast<float>((start + sample) % 251) * (1.0f / 251.0f);
            for (std::size_t channel = 0; channel < out.getNumChannels(); ++channel)
                out.setSample(static_cast<int>(channel), sample, value);
        }
    }

  private:
    double sampleRate_ = kSampleRate;
};

/// Rings on after its input stops, which is what a tail is. Counts the blocks
/// it saw, so a stopped block reaching the graph at all is checkable.
class DecayDevice final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        ++blocksProcessed;
        startBeats.push_back(block.block.beats.start);
        for (auto sample = 0; sample < block.block.numSamples; ++sample) {
            for (std::size_t channel = 0; channel < block.audio.getNumChannels(); ++channel) {
                level_ = std::max(
                    level_, std::abs(block.audio.getSample(static_cast<int>(channel), sample)));
            }
            for (std::size_t channel = 0; channel < block.audio.getNumChannels(); ++channel)
                block.audio.setSample(static_cast<int>(channel), sample, level_);
            level_ *= 0.9999f;
        }
    }

    int blocksProcessed = 0;

    /// Where on the timeline each block sat. A device sees every block, tail
    /// included, which is what makes it the one thing that can say where a
    /// stopped block thinks it is.
    std::vector<double> startBeats;

  private:
    float level_ = 0.0f;
};

/// A pure delay that reports exactly what it delays by (#2246).
///
/// The one device shape that can say whether a render was compensated: what
/// comes out is what went in, moved, so a render that took the latency back out
/// is sample for sample the render of the same project without it, and one that
/// did not is that render late.
class LatentDevice final : public EngineDevice {
  public:
    explicit LatentDevice(int latencySamples) : latency_(latencySamples) {}

    int latencySamples() const override {
        return latency_;
    }

    void prepare(const RenderContext& context) override {
        delay_.assign(static_cast<std::size_t>(std::max(1, latency_)) *
                          static_cast<std::size_t>(std::max(1, context.numChannels)),
                      0.0f);
        channels_ = std::max(1, context.numChannels);
        position_ = 0;
    }

    void process(DeviceBlock& block) override {
        ++blocksProcessed;

        const auto channels = std::min(static_cast<int>(block.audio.getNumChannels()), channels_);

        for (auto sample = 0; sample < block.block.numSamples; ++sample) {
            for (auto channel = 0; channel < channels; ++channel) {
                auto& stored = delay_[(static_cast<std::size_t>(position_) *
                                       static_cast<std::size_t>(channels_)) +
                                      static_cast<std::size_t>(channel)];
                const auto input = block.audio.getSample(channel, sample);
                block.audio.setSample(channel, sample, stored);
                stored = input;
            }

            position_ = (position_ + 1) % std::max(1, latency_);
        }
    }

    /// Blocks it was handed, which is how a render that produced nothing for the
    /// sink can still be caught having run the graph.
    int blocksProcessed = 0;

  private:
    int latency_ = 0;
    int channels_ = 2;
    int position_ = 0;
    std::vector<float> delay_;
};

/// Everything a render produced, as one stream per channel, so two renders are
/// comparable sample for sample however they were cut into blocks.
class CollectingSink final : public OfflineRenderSink {
  public:
    void write(const juce::AudioBuffer<float>& block, int numSamples) override {
        for (auto sample = 0; sample < numSamples; ++sample)
            samples.push_back(block.getSample(0, sample));
        ++blocks;
    }

    std::vector<float> samples;
    int blocks = 0;
};

/// Compiles a one-track plan and prepares an executor for it, so a test says
/// what it renders and nothing about how a plan is built.
struct Harness {
    explicit Harness(bool withDecay = false) {
        auto track = makeTrack(1);
        if (withDecay)
            track.chain.fxChainElements.push_back(makeEffect(10));

        tracks = {track};
        master = makeMaster();
        plan = magda::engine::compileRenderPlan(tracks, master);
    }

    /// Prepared for @p maxBlockSize, which is the only thing an offline render
    /// is allowed to have an opinion about.
    std::vector<std::string> prepare(int maxBlockSize) {
        context = RenderContext{kSampleRate, maxBlockSize, 2};

        source = std::make_unique<TimelineRamp>();
        source->prepare(context);
        bindings.clipAudio[1] = source.get();

        for (const auto& op : plan.ops) {
            if (op.kind != magda::engine::OpKind::Device)
                continue;
            auto device = std::make_unique<DecayDevice>();
            device->prepare(context);
            decay = device.get();
            devices.push_back(std::move(device));
            bindings.devices[op.key.deviceKey()] = decay;
        }

        valueMessages = magda::engine::resolvePlanValues(plan, tracks, master, values);
        return executor.prepare(plan, bindings, context);
    }

    magda::engine::OfflineRenderResult render(const OfflineRenderRequest& request,
                                              CollectingSink& sink,
                                              const std::function<bool()>& shouldContinue = {}) {
        return magda::engine::renderOffline(executor, values, context, tempo, request, sink,
                                            nullptr, shouldContinue);
    }

    std::vector<TrackInfo> tracks;
    TrackInfo master;
    RenderPlan plan;
    PlanBindings bindings;
    PlanValues values;
    std::vector<std::string> valueMessages;
    PlanExecutor executor;
    RenderContext context;
    TempoMap tempo = tempoAt(120.0);

    std::unique_ptr<TimelineRamp> source;
    std::vector<std::unique_ptr<DecayDevice>> devices;
    DecayDevice* decay = nullptr;
};

/// A harness whose one device is @p latent, prepared and ready to render.
///
/// The plain Harness binds its own decaying device to every Device op, which is
/// the wrong shape for a latency question: what these tests need is a device
/// that reports one and delays by exactly it.
void bindLatentDevice(Harness& harness, LatentDevice& latent) {
    harness.context = RenderContext{kSampleRate, 512, 2};

    harness.source = std::make_unique<TimelineRamp>();
    harness.source->prepare(harness.context);
    harness.bindings.clipAudio[1] = harness.source.get();

    latent.prepare(harness.context);

    for (const auto& op : harness.plan.ops)
        if (op.kind == magda::engine::OpKind::Device)
            harness.bindings.devices[op.key.deviceKey()] = &latent;

    harness.valueMessages = magda::engine::resolvePlanValues(harness.plan, harness.tracks,
                                                             harness.master, harness.values);
}

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-6);
}

}  // namespace

TEST_CASE("An offline render covers exactly the range it was asked for", "[engine][offline]") {
    Harness harness;
    REQUIRE(harness.prepare(512).empty());

    // Four beats at 120 bpm is two seconds.
    CollectingSink sink;
    const auto result = harness.render({0.0, 4.0, 0.0, 512}, sink);

    CHECK_FALSE(result.refused);
    CHECK_FALSE(result.cancelled);
    CHECK(result.samplesRendered == static_cast<std::int64_t>(std::llround(2.0 * kSampleRate)));
    CHECK(sink.samples.size() == static_cast<std::size_t>(result.samplesRendered));
}

TEST_CASE("An offline render is the same audio at any block size", "[engine][offline]") {
    // The property the null-diff harness rests on. If this can fail, a
    // difference between the two engines could be a difference in how the
    // comparison was pulled rather than in what they render.
    std::vector<float> reference;

    for (const auto blockSize : {17, 64, 512, 4096}) {
        Harness harness;
        REQUIRE(harness.prepare(4096).empty());

        CollectingSink sink;
        const auto result = harness.render({1.0, 5.0, 0.0, blockSize}, sink);
        REQUIRE_FALSE(result.refused);

        INFO("block size " << blockSize);
        if (reference.empty()) {
            reference = sink.samples;
            REQUIRE_FALSE(reference.empty());

            // Comparing two renders of silence would pass at every block size
            // and mean nothing, so the material has to be material.
            const auto distinct = std::set<float>(reference.begin(), reference.end()).size();
            REQUIRE(distinct > 100);
            continue;
        }

        REQUIRE(sink.samples.size() == reference.size());
        for (std::size_t sample = 0; sample < reference.size(); ++sample) {
            INFO("sample " << sample);
            REQUIRE(sink.samples[sample] == approx(reference[sample]));
        }
    }
}

TEST_CASE("An offline render starts where the range starts", "[engine][offline]") {
    // Two renders of the same beat, reached from different places. What comes
    // out has to be the same audio: the cursor is a position on the timeline,
    // not a distance from wherever the render happened to begin.
    Harness fromZero;
    REQUIRE(fromZero.prepare(512).empty());
    CollectingSink wholeRange;
    fromZero.render({0.0, 8.0, 0.0, 512}, wholeRange);

    Harness fromFour;
    REQUIRE(fromFour.prepare(512).empty());
    CollectingSink secondHalf;
    fromFour.render({4.0, 8.0, 0.0, 512}, secondHalf);

    const auto offset = wholeRange.samples.size() - secondHalf.samples.size();
    REQUIRE(secondHalf.samples.size() > 0);
    for (std::size_t sample = 0; sample < secondHalf.samples.size(); ++sample) {
        INFO("sample " << sample);
        REQUIRE(secondHalf.samples[sample] == approx(wholeRange.samples[offset + sample]));
    }
}

TEST_CASE("An offline render keeps rendering for the tail it was given", "[engine][offline]") {
    Harness harness(true);
    REQUIRE(harness.prepare(512).empty());
    REQUIRE(harness.decay != nullptr);

    CollectingSink sink;
    const auto result = harness.render({0.0, 2.0, 0.5, 512}, sink);

    const auto rangeSamples = static_cast<std::int64_t>(std::llround(1.0 * kSampleRate));
    const auto tailSamples = static_cast<std::int64_t>(std::llround(0.5 * kSampleRate));
    CHECK(result.samplesRendered == rangeSamples + tailSamples);

    // The tail is audible, not silence appended to the file: the graph kept
    // running through blocks the timeline did not move across.
    CHECK(sink.samples.back() != approx(0.0f));
}

TEST_CASE("A tail is exactly as long as it was asked for", "[engine][offline]") {
    // Not "until it goes quiet". A gated or tremolo'd tail is silent in the
    // middle, and a render that stopped at the first silent block would cut it
    // there; trimming what the caller does not want is the caller's, because it
    // has the samples and this does not have the intent.
    Harness harness;
    REQUIRE(harness.prepare(512).empty());

    CollectingSink sink;
    const auto result = harness.render({0.0, 1.0, 0.25, 512}, sink);

    const auto rangeSamples = static_cast<std::int64_t>(std::llround(0.5 * kSampleRate));
    const auto tailSamples = static_cast<std::int64_t>(std::llround(0.25 * kSampleRate));
    CHECK(result.samplesRendered == rangeSamples + tailSamples);

    // Nothing is placed after the range, so the tail of a plan with no tail in
    // it is silence, and it is still exactly the length asked for.
    CHECK(sink.samples.back() == approx(0.0f));
}

TEST_CASE("Nothing plays during an offline render's tail", "[engine][offline]") {
    // The cursor stands still, so material after the range is not printed into
    // the bounce. A source that rendered on through the tail would be the
    // clearest possible sign the transport kept moving.
    Harness harness;
    REQUIRE(harness.prepare(512).empty());

    CollectingSink sink;
    harness.render({0.0, 2.0, 0.5, 512}, sink);

    const auto rangeSamples = static_cast<std::size_t>(std::llround(1.0 * kSampleRate));
    for (auto sample = rangeSamples; sample < sink.samples.size(); ++sample) {
        INFO("sample " << sample);
        REQUIRE(sink.samples[sample] == approx(0.0f));
    }
}

TEST_CASE("A tail stands where the range ended", "[engine][offline]") {
    Harness harness(true);
    REQUIRE(harness.prepare(512).empty());
    REQUIRE(harness.decay != nullptr);

    CollectingSink sink;
    harness.render({4.0, 8.0, 0.25, 512}, sink);

    REQUIRE_FALSE(harness.decay->startBeats.empty());
    CHECK(harness.decay->startBeats.front() == approx(4.0f));
    CHECK(harness.decay->startBeats.back() == approx(8.0f));
}

TEST_CASE("An empty range's tail stands where the range was, not at the beginning",
          "[engine][offline]") {
    // A range that rounds to no samples renders no blocks, so nothing had
    // applied the locate. The tail would then run at beat zero, where the clock
    // starts, and a transport-aware device would be handed a tail from the
    // wrong end of the song.
    Harness harness(true);
    REQUIRE(harness.prepare(512).empty());
    REQUIRE(harness.decay != nullptr);

    CollectingSink sink;
    const auto result = harness.render({4.0, 4.0, 0.25, 512}, sink);

    CHECK(result.samplesRendered == static_cast<std::int64_t>(std::llround(0.25 * kSampleRate)));
    REQUIRE_FALSE(harness.decay->startBeats.empty());
    for (const auto beat : harness.decay->startBeats) {
        INFO("block at beat " << beat);
        REQUIRE(beat == approx(4.0f));
    }
}

TEST_CASE("An offline render stops when the caller says to", "[engine][offline]") {
    Harness harness;
    REQUIRE(harness.prepare(512).empty());

    CollectingSink sink;
    auto blocksLeft = 3;
    const auto result =
        harness.render({0.0, 64.0, 0.0, 512}, sink, [&blocksLeft] { return blocksLeft-- > 0; });

    CHECK(result.cancelled);
    CHECK(result.blocksRendered == 3);

    // What was rendered stays rendered. An abandoned bounce is a short file
    // rather than a corrupt one, and which of those to keep is the caller's.
    CHECK(sink.samples.size() == static_cast<std::size_t>(3 * 512));
}

TEST_CASE("An empty range renders nothing and is not a failure", "[engine][offline]") {
    Harness harness;
    REQUIRE(harness.prepare(512).empty());

    CollectingSink sink;
    const auto result = harness.render({4.0, 4.0, 0.0, 512}, sink);

    CHECK_FALSE(result.refused);
    CHECK(result.samplesRendered == 0);
    CHECK(sink.blocks == 0);
}

TEST_CASE("An offline render refuses values that belong to another plan", "[engine][offline]") {
    Harness harness;
    REQUIRE(harness.prepare(512).empty());

    // What the executor would do with these is render at unity, which as a
    // bounce is a file with every fader at 0 dB and nothing saying why.
    harness.values.planFingerprint ^= 1;

    CollectingSink sink;
    const auto result = harness.render({0.0, 4.0, 0.0, 512}, sink);

    CHECK(result.refused);
    CHECK(result.samplesRendered == 0);
    CHECK(sink.blocks == 0);
}

TEST_CASE("An offline render refuses a context the plan was not prepared for",
          "[engine][offline]") {
    // Every field, because each one goes wrong differently and all of them go
    // wrong quietly. A longer maxBlockSize gets the head of every block and
    // silence for the rest, since the executor renders only what it prepared
    // for and says so with an assert that is not there in release. A different
    // rate runs the whole bounce at the wrong speed, and a different channel
    // count renders it at the wrong width.
    Harness harness;
    REQUIRE(harness.prepare(512).empty());

    auto mismatched = harness.context;
    SECTION("a longer block than it prepared for") {
        mismatched.maxBlockSize = 4096;
    }
    SECTION("another sample rate") {
        mismatched.sampleRate = 96000.0;
    }
    SECTION("another channel count") {
        mismatched.numChannels = 1;
    }

    CollectingSink sink;
    const auto result = magda::engine::renderOffline(harness.executor, harness.values, mismatched,
                                                     harness.tempo, {0.0, 4.0, 0.0, 512}, sink);

    CHECK(result.refused);
    CHECK(result.samplesRendered == 0);
    CHECK(sink.blocks == 0);
}

TEST_CASE("An offline render does not need meters bound", "[engine][offline]") {
    // A plan has a meter at every track and device slot. Offline there is
    // nothing watching them, and needing to bind one per meter to render a
    // bounce would be ceremony that every host doing an export would pay.
    Harness harness;
    const auto messages = harness.prepare(512);
    REQUIRE(messages.empty());
    CHECK(harness.executor.boundMeterCount() == 0);

    CollectingSink sink;
    CHECK_FALSE(harness.render({0.0, 4.0, 0.0, 512}, sink).refused);
    CHECK(sink.samples.size() > 0);
}

TEST_CASE("An offline render's block size is held to what the plan was prepared for",
          "[engine][offline]") {
    Harness harness;
    REQUIRE(harness.prepare(128).empty());

    CollectingSink sink;
    const auto result = harness.render({0.0, 4.0, 0.0, 4096}, sink);

    CHECK_FALSE(result.refused);
    CHECK(result.samplesRendered == static_cast<std::int64_t>(std::llround(2.0 * kSampleRate)));
    // Cut to the prepared size rather than asserted against: the caller's block
    // size is a batching hint and the render is the same audio either way.
    CHECK(result.blocksRendered == static_cast<int>((result.samplesRendered + 127) / 128));
}

TEST_CASE("A render takes the plan's own latency back out", "[engine][offline]") {
    // What a bounce owes: its first sample is the range's first sample. A plan
    // whose devices report latency is that far behind the timeline, which is
    // right during playback and wrong in a file -- a bounce that starts late
    // drifts against every other bounce of the same project, and against the
    // project itself the moment it is imported back in.
    //
    // Asserted as an identity rather than as an offset. A pure delay that is
    // compensated renders what the same project renders without it, so the
    // reference here is that project: nothing about the expected samples has to
    // be written down, and a compensation that is off by one fails as loudly as
    // one that is missing.
    constexpr int kLatency = 333;

    std::vector<float> reference;
    {
        Harness harness;
        REQUIRE(harness.prepare(512).empty());

        CollectingSink sink;
        REQUIRE_FALSE(harness.render({0.0, 4.0, 0.0, 512}, sink).refused);
        reference = sink.samples;
    }

    Harness harness(true);
    LatentDevice latent(kLatency);
    bindLatentDevice(harness, latent);

    REQUIRE(harness.executor.prepare(harness.plan, harness.bindings, harness.context).empty());
    REQUIRE(harness.executor.latencySamples() == kLatency);

    CollectingSink sink;
    const auto result = harness.render({0.0, 4.0, 0.0, 512}, sink);

    CHECK_FALSE(result.refused);

    // The same length as the range asked for, so the extra samples the render
    // pulled to flush the delay went to the trim rather than into the file, and
    // what the result reports is what the sink was handed.
    CHECK(sink.samples.size() == reference.size());
    CHECK(result.samplesRendered == static_cast<std::int64_t>(sink.samples.size()));
    CHECK(result.blocksRendered == sink.blocks);

    REQUIRE(sink.samples.size() == reference.size());
    for (std::size_t sample = 0; sample < reference.size(); ++sample) {
        INFO("sample " << sample);
        REQUIRE(sink.samples[sample] == approx(reference[sample]));
    }
}

TEST_CASE("An empty range renders nothing through a plan that reports latency",
          "[engine][offline]") {
    // The flush that takes a plan's latency back out is for pushing the last of
    // a range through the graph, and a range with no samples in it has nothing
    // to push. Rendering it anyway would run every bound device for blocks the
    // sink is never handed, which is a side effect an empty request is not
    // supposed to have: a device left holding something, and a caller that can
    // be asked whether to continue a render that produced nothing.
    Harness harness(true);
    LatentDevice latent(333);
    bindLatentDevice(harness, latent);

    REQUIRE(harness.executor.prepare(harness.plan, harness.bindings, harness.context).empty());
    REQUIRE(harness.executor.latencySamples() == 333);

    CollectingSink sink;
    const auto result = harness.render({4.0, 4.0, 0.0, 512}, sink);

    CHECK_FALSE(result.refused);
    CHECK(result.samplesRendered == 0);
    CHECK(sink.blocks == 0);
    CHECK(latent.blocksProcessed == 0);
}
