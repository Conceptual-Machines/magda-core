#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanValues.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanDump.hpp"
#include "plan/RenderPlan.hpp"

using namespace magda;
using magda::engine::BlockInfo;
using magda::engine::DeviceBlock;
using magda::engine::DeviceKey;
using magda::engine::EngineAudioSource;
using magda::engine::EngineDevice;
using magda::engine::EngineMidiSource;
using magda::engine::OpId;
using magda::engine::OpRole;
using magda::engine::PlanBindings;
using magda::engine::PlanExecutor;
using magda::engine::PlanValues;
using magda::engine::RenderContext;
using magda::engine::RenderPlan;

namespace {

constexpr int kBlockSize = 64;

/// These tests place things on a sample timeline, because that is what makes
/// two short blocks comparable with one long one, sample for sample. The
/// transport hands out beats, so the fixtures convert with one constant chosen
/// so that every sample position is a beat position exactly: what is being
/// tested here is the executor, and a rounding error in the fixture would look
/// like one in the thing under test.
constexpr double kSamplesPerBeat = 64.0;

BlockInfo blockAt(std::int64_t timelineSample, int numSamples, bool continuous = true) {
    BlockInfo block;
    block.numSamples = numSamples;
    block.playing = true;
    block.startBeat = static_cast<double>(timelineSample) / kSamplesPerBeat;
    block.endBeat = static_cast<double>(timelineSample + numSamples) / kSamplesPerBeat;
    block.continuous = continuous;
    return block;
}

/// Where a block starts, back in samples. What a real source does with the
/// clip's own mapping, done here with the fixture's.
std::int64_t timelineSampleOf(const BlockInfo& block) {
    return std::llround(block.startBeat * kSamplesPerBeat);
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

/// A steady level on every channel, so a gain anywhere in the plan reads
/// straight off the output.
class ConstantSource final : public EngineAudioSource {
  public:
    explicit ConstantSource(float level) : level_(level) {}

    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        out.fill(level_);
        juce::ignoreUnused(block);
    }

  private:
    float level_;
};

/// One sample per timeline position, so two short blocks and one long block
/// over the same range are comparable sample for sample.
class RampSource final : public EngineAudioSource {
  public:
    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        for (int sample = 0; sample < block.numSamples; ++sample) {
            const auto value =
                static_cast<float>((timelineSampleOf(block) + sample) % 97) * (1.0f / 97.0f);
            for (std::size_t channel = 0; channel < out.getNumChannels(); ++channel)
                out.setSample(static_cast<int>(channel), sample, value);
        }
    }
};

/// A note-on at fixed positions on the timeline, so how the blocks are cut up
/// does not change what it plays.
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

/// As many notes per sample as the port's budget allows, to run a delay line
/// at the rate its storage was reserved for.
class DenseNoteSource final : public EngineMidiSource {
  public:
    DenseNoteSource(int noteNumber, int perSample)
        : noteNumber_(noteNumber), perSample_(perSample) {}

    void render(const BlockInfo& block, juce::MidiBuffer& out) override {
        for (int sample = 0; sample < block.numSamples; ++sample)
            for (int note = 0; note < perSample_; ++note)
                out.addEvent(juce::MidiMessage::noteOn(1, noteNumber_, 1.0f), sample);
    }

  private:
    int noteNumber_, perSample_;
};

/// A note-on at a fixed offset, every block.
class NoteSource final : public EngineMidiSource {
  public:
    explicit NoteSource(int noteNumber) : noteNumber_(noteNumber) {}

    void render(const BlockInfo&, juce::MidiBuffer& out) override {
        out.addEvent(juce::MidiMessage::noteOn(1, noteNumber_, 1.0f), 0);
    }

  private:
    int noteNumber_;
};

/// Scales its input. Also records that it ran, so tests can tell "silent
/// because the gain was zero" from "silent because it never processed".
class GainDevice final : public EngineDevice {
  public:
    explicit GainDevice(float gain, int latency = 0) : gain_(gain), latency_(latency) {}

    void process(DeviceBlock& block) override {
        ++processedBlocks;
        block.audio.multiplyBy(gain_);
    }

    int latencySamples() const override {
        return latency_;
    }

    int processedBlocks = 0;

  private:
    float gain_;
    int latency_;
};

/// Writes the note number it last saw as DC, so MIDI arriving at a device is
/// visible in the audio output.
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

/// One sample at timeline position zero and silence everywhere else, so where
/// a signal ends up is a sample index rather than a level.
class ImpulseSource final : public EngineAudioSource {
  public:
    explicit ImpulseSource(float level) : level_(level) {}

    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        out.clear();
        const auto start = timelineSampleOf(block);
        if (start <= 0 && start + block.numSamples > 0)
            for (std::size_t channel = 0; channel < out.getNumChannels(); ++channel)
                out.setSample(static_cast<int>(channel), static_cast<int>(-start), level_);
    }

  private:
    float level_;
};

/// Delays its input, and says so.
///
/// Deliberately not the engine's own delay line: a device that reports latency
/// it does not produce would pass a compensation test whether or not anything
/// compensated, and one that borrowed the line under test would pass a broken
/// line twice over.
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

/// Adds its sidechain to its audio, so where the two arrive relative to each
/// other is visible downstream.
class SidechainSumDevice final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        const auto numChannels =
            std::min(block.audio.getNumChannels(), block.sidechain.getNumChannels());
        for (std::size_t channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::add(block.audio.getChannelPointer(channel),
                                             block.sidechain.getChannelPointer(channel),
                                             block.block.numSamples);
    }
};

/// Emits MIDI of its own, to exercise the MIDI ports and merges.
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

/// An arp whose notes really do come out as late as it says they do, at a fixed
/// offset into every block.
class LatentArpDevice final : public EngineDevice {
  public:
    LatentArpDevice(int noteNumber, int latency, int offsetInBlock)
        : noteNumber_(noteNumber), latency_(latency), offset_(offsetInBlock) {}

    void process(DeviceBlock& block) override {
        block.audio.clear();
        if (block.midiOut != nullptr && offset_ < block.block.numSamples)
            block.midiOut->addEvent(juce::MidiMessage::noteOn(1, noteNumber_, 1.0f), offset_);
    }

    int latencySamples() const override {
        return latency_;
    }

  private:
    int noteNumber_, latency_, offset_;
};

/// Where every note-on reached it, on the timeline.
class MidiPositionProbe final : public EngineDevice {
  public:
    void process(DeviceBlock& block) override {
        block.audio.clear();
        for (const auto metadata : *block.midiIn)
            if (const auto message = metadata.getMessage(); message.isNoteOn())
                notes.push_back(
                    {message.getNoteNumber(),
                     static_cast<int>(timelineSampleOf(block.block)) + metadata.samplePosition});
    }

    struct Note {
        int number = 0;
        int position = 0;
    };
    std::vector<Note> notes;

    /// Where the given note landed, once per block it arrived in.
    std::vector<int> positionsOf(int number) const {
        std::vector<int> positions;
        for (const auto& note : notes)
            if (note.number == number)
                positions.push_back(note.position);
        return positions;
    }
};

/// What the runtime store does when it makes an object. The executor does not
/// prepare what it does not own: the instances are shared with the epoch still
/// rendering, and preparing one the audio thread is inside is both a race and
/// the loss of what a swap exists to keep.
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

/// Compile, resolve, prepare and render, so a test says what it is about and
/// nothing else.
struct Harness {
    Harness(std::vector<TrackInfo> tracksIn, TrackInfo masterIn)
        : tracks(std::move(tracksIn)), master(std::move(masterIn)) {
        plan = magda::engine::compileRenderPlan(tracks, master);
        output.setSize(2, kBlockSize);
    }

    std::vector<std::string> prepare() {
        INFO(magda::engine::dumpPlan(plan));
        valueMessages = magda::engine::resolvePlanValues(plan, tracks, master, values);
        const RenderContext context{44100.0, kBlockSize, 2};
        prepareBindings(bindings, context);
        bindMeters();
        return executor.prepare(plan, bindings, context);
    }

    /// A tap on every Meter op, which is what a host with a mixer on screen
    /// binds. Nothing forces it: a Meter op with no tap renders the same and
    /// publishes nothing.
    void bindMeters() {
        for (const auto& op : plan.ops) {
            if (op.kind != magda::engine::OpKind::Meter)
                continue;
            auto& tap = meters[op.key];
            if (tap == nullptr)
                tap = std::make_unique<magda::engine::LevelTap>();
            bindings.meters[op.key] = tap.get();
        }
    }

    /// Prepares, requiring both layers to have nothing to report.
    void prepareCleanly() {
        const auto messages = prepare();
        INFO(magda::engine::dumpPlan(plan));
        for (const auto& message : messages)
            FAIL_CHECK(message);
        for (const auto& message : valueMessages)
            FAIL_CHECK(message);
        REQUIRE(messages.empty());
        REQUIRE(valueMessages.empty());
    }

    void render(int numSamples = kBlockSize, std::int64_t timelineSample = 0) {
        executor.process(values, blockAt(timelineSample, numSamples), output);
    }

    float outputSample(int channel = 0, int sample = 0) const {
        return output.getSample(channel, sample);
    }

    /// The one op with this role, or a failure if the plan has none.
    OpId opWithRole(OpRole role, TrackId trackId) const {
        for (std::size_t i = 0; i < plan.ops.size(); ++i)
            if (plan.ops[i].key.role == role && plan.ops[i].key.trackId == trackId)
                return static_cast<OpId>(i);
        FAIL("plan has no op with the requested role");
        return magda::engine::INVALID_OP_ID;
    }

    /// Takes the tap's level, which is what reading a meter does: it reports
    /// the loudest thing since it was last read and starts again.
    float takeMeterFor(OpRole role, TrackId trackId) {
        const auto op = opWithRole(role, trackId);
        const auto found = meters.find(plan.ops[static_cast<std::size_t>(op)].key);
        REQUIRE(found != meters.end());
        return found->second->read().loudest();
    }

    std::vector<TrackInfo> tracks;
    TrackInfo master;
    RenderPlan plan;
    PlanBindings bindings;
    std::map<magda::engine::OpKey, std::unique_ptr<magda::engine::LevelTap>> meters;
    PlanValues values;
    std::vector<std::string> valueMessages;
    PlanExecutor executor;
    juce::AudioBuffer<float> output;
};

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-5);
}

/// Channel 0 of @p numBlocks consecutive blocks, as one stream, so where a
/// signal came out is an index into it.
std::vector<float> renderStream(Harness& harness, int numBlocks) {
    std::vector<float> stream;
    for (int block = 0; block < numBlocks; ++block) {
        harness.render(kBlockSize, static_cast<std::int64_t>(block) * kBlockSize);
        for (int sample = 0; sample < kBlockSize; ++sample)
            stream.push_back(harness.outputSample(0, sample));
    }
    return stream;
}

/// Every sample that is not silent, as (position, level).
std::vector<std::pair<int, float>> soundingSamples(const std::vector<float>& stream) {
    std::vector<std::pair<int, float>> sounding;
    for (std::size_t sample = 0; sample < stream.size(); ++sample)
        if (std::abs(stream[sample]) > 1e-5f)
            sounding.push_back({static_cast<int>(sample), stream[sample]});
    return sounding;
}

}  // namespace

TEST_CASE("A source renders through a device into the master output", "[engine][exec]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    Harness harness({track}, makeMaster());
    ConstantSource source(0.5f);
    GainDevice device(0.5f);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[DeviceKey{7}] = &device;

    harness.prepareCleanly();
    harness.render();

    CHECK(device.processedBlocks == 1);
    CHECK(harness.outputSample() == approx(0.25f));
    CHECK(harness.outputSample(1) == approx(0.25f));
}

TEST_CASE("The track fader applies volume through the linear pan law", "[engine][exec]") {
    auto track = makeTrack(1);
    track.volume = 0.5f;
    track.pan = 0.5f;

    Harness harness({track}, makeMaster());
    ConstantSource source(1.0f);
    harness.bindings.clipAudio[1] = &source;

    harness.prepareCleanly();
    harness.render();

    // Linear pan boosts the near side rather than attenuating the far one, so
    // the pair is (gain - pan * gain, gain + pan * gain) and centre is unity.
    CHECK(harness.outputSample(0) == approx(0.25f));
    CHECK(harness.outputSample(1) == approx(0.75f));
}

TEST_CASE("The fader tops out at +6 dB, where the slider does", "[engine][exec]") {
    auto track = makeTrack(1);
    track.volume = 8.0f;

    Harness harness({track}, makeMaster());
    ConstantSource source(0.1f);
    harness.bindings.clipAudio[1] = &source;

    harness.prepareCleanly();
    harness.render();

    CHECK(harness.outputSample() == approx(0.1f * juce::Decibels::decibelsToGain(6.0f)));
}

TEST_CASE("A device's gain trim scales its output", "[engine][exec]") {
    auto effect = makeEffect(7);
    effect.gainValue = 0.25f;

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(effect));

    Harness harness({track}, makeMaster());
    ConstantSource source(1.0f);
    GainDevice device(1.0f);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[DeviceKey{7}] = &device;

    harness.prepareCleanly();
    harness.render();

    CHECK(harness.outputSample() == approx(0.25f));
}

TEST_CASE("Mute silences the track after its meter has read it", "[engine][exec]") {
    auto track = makeTrack(1);
    track.muted = true;

    Harness harness({track}, makeMaster());
    ConstantSource source(0.8f);
    harness.bindings.clipAudio[1] = &source;

    harness.prepareCleanly();
    harness.render();

    CHECK(harness.outputSample() == approx(0.0f));
    // Mute is its own stage after the meter, so a muted track still shows a
    // level: folding it into the fader would make the meter read silence.
    CHECK(harness.takeMeterFor(OpRole::TrackMeter, 1) == approx(0.8f));
}

TEST_CASE("Solo silences the tracks that are not soloed", "[engine][exec]") {
    auto first = makeTrack(1);
    auto second = makeTrack(2);
    second.soloed = true;

    Harness harness({first, second}, makeMaster());
    ConstantSource quiet(0.25f);
    ConstantSource loud(0.5f);
    harness.bindings.clipAudio[1] = &quiet;
    harness.bindings.clipAudio[2] = &loud;

    harness.prepareCleanly();
    harness.render();

    // Only the soloed track reaches the master, which is never solo-gated.
    CHECK(harness.outputSample() == approx(0.5f));
}

TEST_CASE("Soloing a group keeps its children audible", "[engine][exec]") {
    // Nothing solos a child directly. It is soloed by way of the group it
    // feeds, the same edge that makes it inherit the group's mute, so solo has
    // to follow the routing graph or a soloed group renders silence.
    auto group = makeTrack(2, TrackType::Group);
    group.soloed = true;
    group.childIds.push_back(1);

    auto child = makeTrack(1);
    child.parentId = 2;
    child.audioOutputDevice = "track:2";

    auto other = makeTrack(3);

    Harness harness({child, other, group}, makeMaster());
    ConstantSource inGroup(0.5f);
    ConstantSource outside(0.25f);
    harness.bindings.clipAudio[1] = &inGroup;
    harness.bindings.clipAudio[3] = &outside;

    harness.prepareCleanly();
    harness.render();

    CHECK(harness.outputSample() == approx(0.5f));
}

TEST_CASE("Solo follows a chain of routes", "[engine][exec]") {
    auto destination = makeTrack(3, TrackType::Aux);
    destination.auxBusIndex = 0;
    destination.soloed = true;

    auto middle = makeTrack(2, TrackType::Aux);
    middle.auxBusIndex = 1;
    middle.audioOutputDevice = "track:3";

    auto source = makeTrack(1);
    source.audioOutputDevice = "track:2";

    Harness harness({source, middle, destination}, makeMaster());
    ConstantSource level(0.5f);
    harness.bindings.clipAudio[1] = &level;

    harness.prepareCleanly();
    harness.render();

    CHECK(harness.outputSample() == approx(0.5f));
}

TEST_CASE("Mute is inherited from a group parent and from the destination track",
          "[engine][exec]") {
    SECTION("group parent") {
        auto group = makeTrack(2, TrackType::Group);
        group.muted = true;
        auto child = makeTrack(1);
        child.parentId = 2;
        child.audioOutputDevice = "track:2";
        group.childIds.push_back(1);

        Harness harness({child, group}, makeMaster());
        ConstantSource source(0.5f);
        harness.bindings.clipAudio[1] = &source;

        harness.prepareCleanly();
        harness.render();
        CHECK(harness.outputSample() == approx(0.0f));
    }

    SECTION("destination track") {
        auto destination = makeTrack(2, TrackType::Aux);
        destination.auxBusIndex = 0;
        destination.muted = true;
        auto source = makeTrack(1);
        source.audioOutputDevice = "track:2";

        Harness harness({source, destination}, makeMaster());
        ConstantSource level(0.5f);
        harness.bindings.clipAudio[1] = &level;

        harness.prepareCleanly();
        harness.render();
        CHECK(harness.outputSample() == approx(0.0f));
    }

    SECTION("a destination two routes away") {
        auto destination = makeTrack(3, TrackType::Aux);
        destination.auxBusIndex = 0;
        destination.muted = true;

        auto middle = makeTrack(2, TrackType::Aux);
        middle.auxBusIndex = 1;
        middle.audioOutputDevice = "track:3";

        auto source = makeTrack(1);
        source.audioOutputDevice = "track:2";

        Harness harness({source, middle, destination}, makeMaster());
        ConstantSource level(0.5f);
        harness.bindings.clipAudio[1] = &level;

        harness.prepareCleanly();
        harness.render();
        CHECK(harness.outputSample() == approx(0.0f));
    }
}

TEST_CASE("A send carries its level, and mute does not stop it", "[engine][exec]") {
    auto aux = makeTrack(2, TrackType::Aux);
    aux.auxBusIndex = 0;

    auto track = makeTrack(1);
    track.sends.push_back(SendInfo{0, 0.5f, false, 2});

    SECTION("audible source") {
        Harness harness({track, aux}, makeMaster());
        ConstantSource source(1.0f);
        harness.bindings.clipAudio[1] = &source;

        harness.prepareCleanly();
        harness.render();

        // Dry track plus the aux return at half level.
        CHECK(harness.outputSample() == approx(1.5f));
    }

    SECTION("muted source still feeds the aux") {
        track.muted = true;
        Harness harness({track, aux}, makeMaster());
        ConstantSource source(1.0f);
        harness.bindings.clipAudio[1] = &source;

        harness.prepareCleanly();
        harness.render();

        // The current engine only zeroes an aux send when the track's contents
        // are not being processed, and MAGDA processes muted tracks so their
        // meters stay alive. The dry path is silent, the send is not.
        CHECK(harness.outputSample() == approx(0.5f));
    }
}

TEST_CASE("A pre-fader send taps the signal before the fader", "[engine][exec]") {
    auto aux = makeTrack(2, TrackType::Aux);
    aux.auxBusIndex = 0;

    auto track = makeTrack(1);
    track.volume = 0.0f;
    track.sends.push_back(SendInfo{0, 1.0f, true, 2});

    Harness harness({track, aux}, makeMaster());
    ConstantSource source(0.5f);
    harness.bindings.clipAudio[1] = &source;

    harness.prepareCleanly();
    harness.render();

    // The fader is all the way down, so everything reaching the master came
    // through the pre-fader send.
    CHECK(harness.outputSample() == approx(0.5f));
}

TEST_CASE("A rack sums its chains through their faders and its own output law", "[engine][exec]") {
    auto rack = std::make_unique<RackInfo>();
    rack->id = 5;
    rack->volume = -6.0f;

    ChainInfo first;
    first.id = 10;
    ChainInfo second;
    second.id = 11;
    second.volume = -6.0f;
    rack->chains.push_back(std::move(first));
    rack->chains.push_back(std::move(second));

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(ChainElement{std::move(rack)});

    Harness harness({track}, makeMaster());
    ConstantSource source(1.0f);
    harness.bindings.clipAudio[1] = &source;

    harness.prepareCleanly();
    harness.render();

    const auto chainGain = 1.0f + juce::Decibels::decibelsToGain(-6.0f);
    CHECK(harness.outputSample() == approx(chainGain * juce::Decibels::decibelsToGain(-6.0f)));
}

TEST_CASE("Delta solo hears what the device added", "[engine][exec]") {
    auto effect = makeEffect(7);
    effect.deltaSolo = true;

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(effect));

    SECTION("a device that changes the signal leaves its difference behind") {
        Harness harness({track}, makeMaster());
        ConstantSource source(0.5f);
        GainDevice device(0.25f);
        harness.bindings.clipAudio[1] = &source;
        harness.bindings.devices[DeviceKey{7}] = &device;

        harness.prepareCleanly();
        harness.render();

        CHECK(harness.outputSample() == approx(0.5f * 0.25f - 0.5f));
    }

    SECTION("a device that changes nothing leaves nothing") {
        Harness harness({track}, makeMaster());
        ConstantSource source(0.5f);
        GainDevice device(1.0f);
        harness.bindings.clipAudio[1] = &source;
        harness.bindings.devices[DeviceKey{7}] = &device;

        harness.prepareCleanly();
        harness.render();

        CHECK(device.processedBlocks == 1);
        CHECK(harness.outputSample() == approx(0.0f));
    }
}

TEST_CASE("A delta solo's dry edge is delayed to meet the wet one", "[engine][exec]") {
    // A device that only delays adds nothing, so its delta is silence - but
    // only once the dry side has been held back to meet it. Compensate it
    // wrongly and the impulse comes out twice, once from each side.
    auto effect = makeEffect(7);
    effect.deltaSolo = true;

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(effect));

    Harness harness({track}, makeMaster());
    ImpulseSource source(1.0f);
    LatentDevice device(32);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[DeviceKey{7}] = &device;

    harness.prepareCleanly();

    const auto stream = renderStream(harness, 2);
    INFO(magda::engine::dumpPlan(harness.plan));
    CHECK(soundingSamples(stream).empty());
}

TEST_CASE("Delta solo turned on mid-render reads a dry line that has been running",
          "[engine][exec]") {
    // The point of compiling the subtract and its dry delay whether or not
    // anything is soloing a delta. A delay line is history: one that came into
    // being when the button was pressed would hand back its own length in
    // silence, so a device with any real latency would leak its wet signal for
    // that long and then step. The current engine keeps the same line running
    // for the same reason - PluginNode's deltaLatencyProcessor is built from
    // the plugin's latency alone and written every block, and only the read is
    // conditional (see the regression in tests/test_delta_solo.cpp).
    //
    // A device that only delays adds nothing, so its delta is silence. Reaching
    // that on the very first block after the toggle is possible only if the dry
    // line already holds the samples the wet side is now arriving with.
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    Harness harness({track}, makeMaster());
    RampSource source;
    LatentDevice device(kBlockSize);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[DeviceKey{7}] = &device;

    harness.prepareCleanly();
    INFO(magda::engine::dumpPlan(harness.plan));

    // Block 0 with the delta unheard: the device's own delay is still filling,
    // and nothing is taken off what comes out of it.
    harness.render(kBlockSize, 0);

    // Now the model says to hear it, which is a value away: the plan does not
    // change, so nothing is rebuilt and the line keeps its contents.
    getDevice(harness.tracks[0].chain.fxChainElements[0]).deltaSolo = true;
    harness.valueMessages = magda::engine::resolvePlanValues(harness.plan, harness.tracks,
                                                             harness.master, harness.values);
    REQUIRE(harness.valueMessages.empty());

    harness.render(kBlockSize, kBlockSize);
    for (int sample = 0; sample < kBlockSize; ++sample)
        CHECK(harness.outputSample(0, sample) == approx(0.0f));
}

TEST_CASE("Delta solo around a rack measures its output against its input", "[engine][exec]") {
    auto rack = std::make_unique<RackInfo>();
    rack->id = 5;
    rack->deltaSolo = true;

    ChainInfo chain;
    chain.id = 10;
    chain.elements.push_back(makeDeviceElement(makeEffect(7)));
    rack->chains.push_back(std::move(chain));

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(ChainElement{std::move(rack)});

    Harness harness({track}, makeMaster());
    ConstantSource source(1.0f);
    GainDevice device(0.5f);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[DeviceKey{7}] = &device;

    harness.prepareCleanly();
    harness.render();

    CHECK(harness.outputSample() == approx(0.5f - 1.0f));
}

TEST_CASE("A rack chain out of the mix contributes neither audio nor MIDI", "[engine][exec]") {
    auto rack = std::make_unique<RackInfo>();
    rack->id = 5;

    ChainInfo audible;
    audible.id = 10;
    ChainInfo silenced;
    silenced.id = 11;
    silenced.elements.push_back(makeDeviceElement(makeInstrument(8)));
    rack->chains.push_back(std::move(audible));
    rack->chains.push_back(std::move(silenced));

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(ChainElement{std::move(rack)});

    ConstantSource source(0.25f);
    NoteSource notes(64);
    NoteToDcInstrument instrument;
    const auto instrumentLevel = 64.0f / 127.0f;

    SECTION("both chains in the mix") {
        Harness harness({track}, makeMaster());
        harness.bindings.clipAudio[1] = &source;
        harness.bindings.clipMidi[1] = &notes;
        harness.bindings.devices[DeviceKey{8}] = &instrument;

        harness.prepareCleanly();
        harness.render();
        CHECK(harness.outputSample() == approx(0.25f + instrumentLevel));
    }

    SECTION("the second chain is muted") {
        auto muted = track;
        getRack(muted.chain.fxChainElements[0]).chains[1].muted = true;

        Harness harness({muted}, makeMaster());
        harness.bindings.clipAudio[1] = &source;
        harness.bindings.clipMidi[1] = &notes;
        harness.bindings.devices[DeviceKey{8}] = &instrument;

        harness.prepareCleanly();
        harness.render();

        // The instrument in the muted chain never ran, so the chain adds
        // nothing to the mix: it is out of the graph, not turned down.
        CHECK(harness.outputSample() == approx(0.25f));
        CHECK(harness.takeMeterFor(OpRole::DeviceMeter, 1) == approx(0.0f));
    }

    SECTION("a sibling chain is soloed") {
        auto soloed = track;
        getRack(soloed.chain.fxChainElements[0]).chains[0].solo = true;

        Harness harness({soloed}, makeMaster());
        harness.bindings.clipAudio[1] = &source;
        harness.bindings.clipMidi[1] = &notes;
        harness.bindings.devices[DeviceKey{8}] = &instrument;

        harness.prepareCleanly();
        harness.render();
        CHECK(harness.outputSample() == approx(0.25f));
        CHECK(harness.takeMeterFor(OpRole::DeviceMeter, 1) == approx(0.0f));
    }
}

TEST_CASE("A bypassed rack chain passes signal at unity", "[engine][exec]") {
    // Bypass wires the rack's input pins straight to its output pins, which
    // skips the chain's volume and pan along with its devices. The fader op is
    // still there, so that it keeps its identity when bypass comes off.
    auto rack = std::make_unique<RackInfo>();
    rack->id = 5;

    ChainInfo chain;
    chain.id = 10;
    chain.bypassed = true;
    chain.volume = -12.0f;
    chain.pan = 0.8f;
    chain.elements.push_back(makeDeviceElement(makeEffect(7)));
    rack->chains.push_back(std::move(chain));

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(ChainElement{std::move(rack)});

    Harness harness({track}, makeMaster());
    ConstantSource source(0.5f);
    harness.bindings.clipAudio[1] = &source;

    harness.prepareCleanly();
    harness.render();

    CHECK(harness.outputSample(0) == approx(0.5f));
    CHECK(harness.outputSample(1) == approx(0.5f));
}

TEST_CASE("A rack inside a chain out of the mix stops processing", "[engine][exec]") {
    // Its ops key on the nested rack, so the chain ID on an op cannot find
    // them. Left running they would advance their tails and publish meter
    // levels for a chain the current engine leaves disconnected.
    auto inner = std::make_unique<RackInfo>();
    inner->id = 6;
    ChainInfo innerChain;
    innerChain.id = 20;
    innerChain.elements.push_back(makeDeviceElement(makeEffect(7)));
    inner->chains.push_back(std::move(innerChain));

    auto outer = std::make_unique<RackInfo>();
    outer->id = 5;
    ChainInfo outerChain;
    outerChain.id = 10;
    outerChain.muted = true;
    outerChain.elements.push_back(ChainElement{std::move(inner)});
    outer->chains.push_back(std::move(outerChain));

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(ChainElement{std::move(outer)});

    Harness harness({track}, makeMaster());
    ConstantSource source(1.0f);
    GainDevice nested(1.0f);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[DeviceKey{7}] = &nested;

    harness.prepareCleanly();
    harness.render();

    CHECK(nested.processedBlocks == 0);
    CHECK(harness.takeMeterFor(OpRole::DeviceMeter, 1) == approx(0.0f));
    CHECK(harness.outputSample() == approx(0.0f));
}

TEST_CASE("Values resolved for another plan are not applied", "[engine][exec]") {
    // Swapping one device for another is the everyday structural edit that
    // keeps the op count and changes what each index means. Nothing about the
    // sizes says the table is stale, so identity has to be carried.
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    auto replaced = makeTrack(1);
    replaced.volume = 0.5f;
    replaced.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));

    Harness harness({track}, makeMaster());
    ConstantSource source(1.0f);
    GainDevice device(1.0f);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[DeviceKey{7}] = &device;
    harness.prepareCleanly();

    const std::vector<TrackInfo> otherTracks{replaced};
    const auto other = magda::engine::compileRenderPlan(otherTracks, makeMaster());
    PlanValues otherValues;
    magda::engine::resolvePlanValues(other, otherTracks, makeMaster(), otherValues);
    REQUIRE(otherValues.ops.size() == harness.values.ops.size());
    REQUIRE(otherValues.planFingerprint != harness.values.planFingerprint);

    harness.values = otherValues;
    harness.render();

    // Rendered at unity rather than with the other plan's fader value.
    CHECK(harness.outputSample() == approx(1.0f));
}

TEST_CASE("An instrument turns the track's MIDI into audio", "[engine][exec]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(8)));

    Harness harness({track}, makeMaster());
    ConstantSource audio(0.0f);
    NoteSource notes(64);
    NoteToDcInstrument instrument;
    harness.bindings.clipAudio[1] = &audio;
    harness.bindings.clipMidi[1] = &notes;
    harness.bindings.devices[DeviceKey{8}] = &instrument;

    harness.prepareCleanly();
    harness.render();

    CHECK(harness.outputSample() == approx(64.0f / 127.0f));
}

TEST_CASE("A MIDI-producing device's output reaches the device after it", "[engine][exec]") {
    auto arp = makeEffect(7);
    arp.deviceType = DeviceType::MIDI;
    arp.canReceiveMidi = true;
    arp.producesMidi = true;
    arp.midiInThru = false;

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(arp));
    track.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(8)));

    Harness harness({track}, makeMaster());
    ConstantSource audio(0.0f);
    NoteSource notes(40);
    ArpDevice arpDevice(72);
    NoteToDcInstrument instrument;
    harness.bindings.clipAudio[1] = &audio;
    harness.bindings.clipMidi[1] = &notes;
    harness.bindings.devices[DeviceKey{7}] = &arpDevice;
    harness.bindings.devices[DeviceKey{8}] = &instrument;

    harness.prepareCleanly();
    harness.render();

    // The arp replaces the incoming note rather than passing it through, so the
    // instrument sees only what the arp emitted.
    CHECK(harness.outputSample() == approx(72.0f / 127.0f));
}

TEST_CASE("A merge carries everything that reaches it", "[engine][exec]") {
    // Two dense sources into one merge. The port's reservation is summed
    // through the MIDI graph for exactly this: a flat per-port figure would be
    // outgrown here, and growing it happens on the audio thread.
    class BurstSource final : public EngineMidiSource {
      public:
        explicit BurstSource(int count, int firstNote) : count_(count), firstNote_(firstNote) {}

        void render(const BlockInfo&, juce::MidiBuffer& out) override {
            for (int event = 0; event < count_; ++event)
                out.addEvent(juce::MidiMessage::noteOn(1, (firstNote_ + event) % 128, 1.0f), 0);
        }

      private:
        int count_;
        int firstNote_;
    };

    class EventCounter final : public EngineDevice {
      public:
        void process(DeviceBlock& block) override {
            events = block.midiIn->getNumEvents();
            block.audio.clear();
        }

        int events = 0;
    };

    auto track = makeTrack(1);
    track.recordArmed = true;
    track.midiInputDevice = "midi-hardware";
    track.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(8)));

    Harness harness({track}, makeMaster());
    ConstantSource audio(0.0f);
    // Each source fills its own port to the byte budget, so the merge has to
    // hold both at once.
    constexpr auto perPort =
        magda::engine::kMaxMidiBytesPerPort / magda::engine::kMidiShortMessageBytes;
    BurstSource clips(perPort, 0);
    BurstSource live(perPort, 64);
    EventCounter counter;
    harness.bindings.clipAudio[1] = &audio;
    harness.bindings.clipMidi[1] = &clips;
    harness.bindings.midiInputs[1] = &live;
    harness.bindings.devices[DeviceKey{8}] = &counter;

    harness.prepareCleanly();
    harness.render();

    CHECK(counter.events == 2 * perPort);
}

TEST_CASE("An audio sidechain reaches the device that asked for it", "[engine][exec]") {
    class SidechainProbe final : public EngineDevice {
      public:
        void process(DeviceBlock& block) override {
            sidechainChannels = static_cast<int>(block.sidechain.getNumChannels());
            if (sidechainChannels > 0)
                sidechainLevel = block.sidechain.getSample(0, 0);
            block.audio.clear();
        }

        int sidechainChannels = -1;
        float sidechainLevel = 0.0f;
    };

    auto compressor = makeEffect(7);
    compressor.canSidechain = true;
    compressor.sidechain.type = SidechainConfig::Type::Audio;
    compressor.sidechain.sourceTrackId = 2;

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(compressor));

    Harness harness({makeTrack(2), track}, makeMaster());
    ConstantSource key(0.75f);
    ConstantSource main(1.0f);
    SidechainProbe probe;
    harness.bindings.clipAudio[2] = &key;
    harness.bindings.clipAudio[1] = &main;
    harness.bindings.devices[DeviceKey{7}] = &probe;

    harness.prepareCleanly();
    harness.render();

    CHECK(probe.sidechainChannels == 2);
    CHECK(probe.sidechainLevel == approx(0.75f));
}

TEST_CASE("Unbound ops are reported and render silence", "[engine][exec]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    Harness harness({track}, makeMaster());
    const auto messages = harness.prepare();

    REQUIRE(messages.size() == 2);
    CHECK(messages[0].find("no clip audio source bound for track 1") != std::string::npos);
    CHECK(messages[1].find("no device bound for D7") != std::string::npos);

    harness.render();
    CHECK(harness.outputSample() == approx(0.0f));
}

TEST_CASE("A device's latency becomes the plan's latency", "[engine][exec][pdc]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    Harness harness({track}, makeMaster());
    ImpulseSource source(0.5f);
    LatentDevice device(80);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[DeviceKey{7}] = &device;

    harness.prepareCleanly();
    CHECK(harness.executor.latencySamples() == 80);

    // Longer than a block, so the delay has to survive being read back across
    // one and the ring has to wrap.
    const auto sounding = soundingSamples(renderStream(harness, 4));
    REQUIRE(sounding.size() == 1);
    CHECK(sounding[0].first == 80);
    CHECK(sounding[0].second == approx(0.5f));
}

TEST_CASE("Paths that meet are aligned to the longest of them", "[engine][exec][pdc]") {
    // The current engine delays every input of a sum up to the longest one and
    // never pulls anything early, wherever that sum happens to be. These are
    // the places it happens: two tracks meeting in a mix, a group summing
    // children, a send landing on another track's input, a sidechain meeting
    // the chain it keys, and a rack's chains meeting each other.
    ImpulseSource loud(0.5f);
    ImpulseSource quiet(0.25f);
    LatentDevice latent(48);

    SECTION("two tracks summed into the master") {
        auto latentTrack = makeTrack(1);
        latentTrack.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

        Harness harness({latentTrack, makeTrack(2)}, makeMaster());
        harness.bindings.clipAudio[1] = &loud;
        harness.bindings.clipAudio[2] = &quiet;
        harness.bindings.devices[DeviceKey{7}] = &latent;

        harness.prepareCleanly();
        const auto sounding = soundingSamples(renderStream(harness, 3));

        REQUIRE(sounding.size() == 1);
        CHECK(sounding[0].first == 48);
        CHECK(sounding[0].second == approx(0.75f));
    }

    SECTION("a group summing children of unequal latency") {
        auto latentTrack = makeTrack(1);
        latentTrack.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
        latentTrack.audioOutputDevice = "track:3";

        auto cleanTrack = makeTrack(2);
        cleanTrack.audioOutputDevice = "track:3";

        Harness harness({latentTrack, cleanTrack, makeTrack(3, TrackType::Group)}, makeMaster());
        harness.bindings.clipAudio[1] = &loud;
        harness.bindings.clipAudio[2] = &quiet;
        harness.bindings.devices[DeviceKey{7}] = &latent;

        harness.prepareCleanly();
        const auto sounding = soundingSamples(renderStream(harness, 3));

        REQUIRE(sounding.size() == 1);
        CHECK(sounding[0].first == 48);
        CHECK(sounding[0].second == approx(0.75f));
    }

    SECTION("a send from a latent track landing on a track of its own") {
        auto latentTrack = makeTrack(1);
        latentTrack.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
        latentTrack.sends.push_back(SendInfo{0, 1.0f, false, 2});

        Harness harness({latentTrack, makeTrack(2)}, makeMaster());
        harness.bindings.clipAudio[1] = &loud;
        harness.bindings.clipAudio[2] = &quiet;
        harness.bindings.devices[DeviceKey{7}] = &latent;

        harness.prepareCleanly();
        const auto sounding = soundingSamples(renderStream(harness, 3));

        // Track 1 direct, the same signal again through the send, and track 2's
        // own, which the send's arrival is what delays.
        REQUIRE(sounding.size() == 1);
        CHECK(sounding[0].first == 48);
        CHECK(sounding[0].second == approx(1.25f));
    }

    SECTION("a sidechain meeting the chain it keys") {
        auto keyed = makeTrack(1);
        auto device = makeEffect(7);
        device.sidechain.type = SidechainConfig::Type::Audio;
        device.sidechain.sourceTrackId = 2;
        keyed.chain.fxChainElements.push_back(makeDeviceElement(device));

        auto source = makeTrack(2);
        source.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(8)));

        Harness harness({keyed, source}, makeMaster());
        SidechainSumDevice sum;
        harness.bindings.clipAudio[1] = &loud;
        harness.bindings.clipAudio[2] = &quiet;
        harness.bindings.devices[DeviceKey{7}] = &sum;
        harness.bindings.devices[DeviceKey{8}] = &latent;

        harness.prepareCleanly();
        const auto sounding = soundingSamples(renderStream(harness, 3));

        // Track 1's own signal is what moves: the key it is being summed with
        // arrives 48 samples late, so the plan delays the chain to meet it.
        REQUIRE(sounding.size() == 1);
        CHECK(sounding[0].first == 48);
        CHECK(sounding[0].second == approx(1.0f));
    }

    SECTION("parallel rack chains of unequal latency") {
        auto rack = std::make_unique<RackInfo>();
        rack->id = 5;

        ChainInfo latentChain;
        latentChain.id = 10;
        latentChain.elements.push_back(makeDeviceElement(makeEffect(7)));
        rack->chains.push_back(std::move(latentChain));

        ChainInfo cleanChain;
        cleanChain.id = 11;
        rack->chains.push_back(std::move(cleanChain));

        auto track = makeTrack(1);
        track.chain.fxChainElements.push_back(ChainElement{std::move(rack)});

        Harness harness({track}, makeMaster());
        harness.bindings.clipAudio[1] = &loud;
        harness.bindings.devices[DeviceKey{7}] = &latent;

        harness.prepareCleanly();
        const auto sounding = soundingSamples(renderStream(harness, 3));

        // Both chains carry the same signal, so aligned they sum to twice it.
        REQUIRE(sounding.size() == 1);
        CHECK(sounding[0].first == 48);
        CHECK(sounding[0].second == approx(1.0f));
    }
}

TEST_CASE("MIDI is aligned against the audio it travels with", "[engine][exec][pdc]") {
    // The current engine carries a plugin's MIDI in the same stream as its
    // audio, so a delay on that stream moves both. Here they are separate
    // ports, and the merge is where they meet: the chain's own MIDI is delayed
    // to where the device's arrives.
    auto arp = makeEffect(7);
    arp.deviceType = DeviceType::MIDI;
    arp.canReceiveMidi = true;
    arp.producesMidi = true;
    arp.midiInThru = true;

    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(arp));
    track.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(8)));

    Harness harness({track}, makeMaster());
    ConstantSource audio(0.0f);
    NoteSource notes(40);
    // 80 samples of latency at a 64 sample block: the arp's own note comes out
    // 16 samples into the following block, and the chain's has to be held over
    // the block boundary to land beside it.
    LatentArpDevice latentArp(72, 80, 16);
    MidiPositionProbe probe;
    harness.bindings.clipAudio[1] = &audio;
    harness.bindings.clipMidi[1] = &notes;
    harness.bindings.devices[DeviceKey{7}] = &latentArp;
    harness.bindings.devices[DeviceKey{8}] = &probe;

    harness.prepareCleanly();
    renderStream(harness, 4);

    // Every block's note-on from the source is held 80 samples; the arp's are
    // already where they claim to be.
    CHECK(probe.positionsOf(72) == std::vector<int>{16, 80, 144, 208});
    CHECK(probe.positionsOf(40) == std::vector<int>{80, 144, 208});
}

TEST_CASE("A malformed plan is refused, and the executor renders silence", "[engine][exec]") {
    RenderPlan plan;
    magda::engine::PlanOp op;
    op.kind = magda::engine::OpKind::Gain;
    op.inputs = {magda::engine::PortRef{4, 0}};  // reads an op that does not exist
    op.outputs = {magda::engine::SignalKind::Audio};
    plan.ops.push_back(op);

    PlanExecutor executor;
    const auto messages = executor.prepare(plan, PlanBindings{}, RenderContext{44100.0, 64, 2});

    REQUIRE_FALSE(messages.empty());
    CHECK(messages.front().find("plan is not well formed") != std::string::npos);
    CHECK_FALSE(executor.isPrepared());

    juce::AudioBuffer<float> output(2, 64);
    output.clear();
    executor.process(PlanValues{}, blockAt(0, 64), output);
    CHECK(output.getSample(0, 0) == approx(0.0f));
}

TEST_CASE("Block size does not change what is rendered", "[engine][exec]") {
    const auto renderTwice = [](int firstBlock, int secondBlock, std::vector<float>& out) {
        auto track = makeTrack(1);
        track.volume = 0.7f;
        track.pan = -0.3f;
        track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

        Harness harness({track}, makeMaster());
        RampSource source;
        GainDevice device(0.9f);
        harness.bindings.clipAudio[1] = &source;
        harness.bindings.devices[DeviceKey{7}] = &device;
        harness.prepareCleanly();

        harness.render(firstBlock, 0);
        for (int sample = 0; sample < firstBlock; ++sample)
            out.push_back(harness.outputSample(0, sample));

        harness.render(secondBlock, firstBlock);
        for (int sample = 0; sample < secondBlock; ++sample)
            out.push_back(harness.outputSample(0, sample));
    };

    // The same, with two tracks and one of them latent, so what is compared is
    // a delay line read back across a block boundary it does not line up with.
    const auto renderCompensatedTwice = [](int firstBlock, int secondBlock,
                                           std::vector<float>& out) {
        auto latentTrack = makeTrack(1);
        latentTrack.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

        Harness harness({latentTrack, makeTrack(2)}, makeMaster());
        RampSource ramp;
        ConstantSource steady(0.25f);
        LatentDevice device(37);
        harness.bindings.clipAudio[1] = &ramp;
        harness.bindings.clipAudio[2] = &steady;
        harness.bindings.devices[DeviceKey{7}] = &device;
        harness.prepareCleanly();

        harness.render(firstBlock, 0);
        for (int sample = 0; sample < firstBlock; ++sample)
            out.push_back(harness.outputSample(0, sample));

        harness.render(secondBlock, firstBlock);
        for (int sample = 0; sample < secondBlock; ++sample)
            out.push_back(harness.outputSample(0, sample));
    };

    std::vector<float> whole;
    std::vector<float> split;
    renderTwice(kBlockSize, 0, whole);
    renderTwice(kBlockSize / 4, kBlockSize - (kBlockSize / 4), split);

    REQUIRE(whole.size() == split.size());
    for (std::size_t sample = 0; sample < whole.size(); ++sample)
        REQUIRE(whole[sample] == approx(split[sample]));

    std::vector<float> compensatedWhole;
    std::vector<float> compensatedSplit;
    renderCompensatedTwice(kBlockSize, 0, compensatedWhole);
    renderCompensatedTwice(kBlockSize / 4, kBlockSize - (kBlockSize / 4), compensatedSplit);

    REQUIRE(compensatedWhole.size() == compensatedSplit.size());
    for (std::size_t sample = 0; sample < compensatedWhole.size(); ++sample)
        REQUIRE(compensatedWhole[sample] == approx(compensatedSplit[sample]));
}

TEST_CASE("Block size does not change where delayed MIDI lands", "[engine][exec][pdc]") {
    // A MIDI delay is the one thing here that holds state across blocks, so it
    // is the one thing a host cutting the same stretch of timeline into
    // different callbacks can pull apart. What it carries is budgeted over a
    // span of samples for exactly that reason: counted per callback, the
    // reservation would be short by whatever the host chose the block size to
    // be, and the short blocks below are what would find it.
    const auto renderNotes = [](const std::vector<int>& blockSizes) {
        auto track = makeTrack(1);
        track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
        track.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(8)));

        Harness harness({track}, makeMaster());
        ConstantSource audio(0.0f);
        // A period that lines up with no block boundary in the test, so notes
        // land all over the delay's window rather than at the edges of it.
        TimelineNoteSource notes(48, 23);
        LatentDevice latent(80);
        MidiPositionProbe probe;
        harness.bindings.clipAudio[1] = &audio;
        harness.bindings.clipMidi[1] = &notes;
        harness.bindings.devices[DeviceKey{7}] = &latent;
        harness.bindings.devices[DeviceKey{8}] = &probe;
        harness.prepareCleanly();

        std::int64_t timelineSample = 0;
        for (const auto numSamples : blockSizes) {
            harness.render(numSamples, timelineSample);
            timelineSample += numSamples;
        }
        return probe.positionsOf(48);
    };

    const auto whole = renderNotes({kBlockSize, kBlockSize, kBlockSize, kBlockSize});
    const auto split = renderNotes({1, 7, kBlockSize, 3, 1, 40, 20, kBlockSize, 60, 24});

    // The instrument's audio arrives 80 samples late, so its MIDI is held to
    // meet it: a note at t is seen at t + 80, however the blocks were cut.
    REQUIRE_FALSE(whole.empty());
    for (const auto position : whole)
        CHECK(position % 23 == 80 % 23);

    const auto common = std::min(whole.size(), split.size());
    REQUIRE(common > 4);
    for (std::size_t note = 0; note < common; ++note)
        CHECK(whole[note] == split[note]);

    SECTION("with the port carrying as much as its budget allows") {
        // Right up against kMaxMidiBytesPerPort for a span of kBlockSize
        // samples, which is the rate the delay's storage was reserved for. If
        // that reservation is ever computed from callbacks again, this is what
        // holds eighty samples of it in flight while the callbacks are short.
        constexpr int perSample = magda::engine::kMaxMidiBytesPerPort /
                                  (kBlockSize * magda::engine::kMidiShortMessageBytes);

        auto track = makeTrack(1);
        track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
        track.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(8)));

        Harness harness({track}, makeMaster());
        ConstantSource audio(0.0f);
        DenseNoteSource notes(48, perSample);
        LatentDevice latent(80);
        MidiPositionProbe probe;
        harness.bindings.clipAudio[1] = &audio;
        harness.bindings.clipMidi[1] = &notes;
        harness.bindings.devices[DeviceKey{7}] = &latent;
        harness.bindings.devices[DeviceKey{8}] = &probe;
        harness.prepareCleanly();

        std::int64_t timelineSample = 0;
        for (const auto numSamples : {1, 3, kBlockSize, 5, 40, kBlockSize, 11, kBlockSize}) {
            harness.render(numSamples, timelineSample);
            timelineSample += numSamples;
        }

        // Everything played more than the delay ago has arrived, and nothing
        // has arrived early.
        const auto arrived = probe.positionsOf(48);
        REQUIRE(arrived.size() == static_cast<std::size_t>((timelineSample - 80) * perSample));
        CHECK(arrived.front() == 80);
        CHECK(arrived.back() == static_cast<int>(timelineSample) - 1);

        // And it was all held in the room reserved for it, which is the part a
        // reservation counted in callbacks rather than samples gets wrong.
        CHECK(harness.executor.midiDelayOverflows() == 0);
    }
}

TEST_CASE("What is in flight survives the plan being replaced", "[engine][exec][diff]") {
    // Track 2 is compensated for track 1's latency, so at any moment there are
    // 80 samples of it inside a delay line. Recompiling used to be the end of
    // them: a new executor built a new line and those samples became silence.
    // This is the rebuild click in its smallest form, and the differ is what
    // answers it.
    auto latentTrack = makeTrack(1);
    latentTrack.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));
    const std::vector<TrackInfo> tracks{latentTrack, makeTrack(2)};
    const auto master = makeMaster();

    const auto plan = magda::engine::compileRenderPlan(tracks, master);
    PlanValues values;
    magda::engine::resolvePlanValues(plan, tracks, master, values);

    ImpulseSource impulse(0.5f);
    ConstantSource silent(0.0f);
    LatentDevice latent(80);
    PlanBindings bindings;
    bindings.clipAudio[1] = &silent;
    bindings.clipAudio[2] = &impulse;
    bindings.devices[DeviceKey{7}] = &latent;

    const RenderContext context{44100.0, kBlockSize, 2};
    prepareBindings(bindings, context);
    juce::AudioBuffer<float> output(2, kBlockSize);

    PlanExecutor first;
    REQUIRE(first.prepare(plan, bindings, context).empty());

    // Block 0 carries the impulse into the delay line and nothing out of it.
    first.process(values, blockAt(0, kBlockSize), output);
    CHECK(output.getSample(0, 0) == approx(0.0f));

    PlanExecutor second;

    SECTION("taking over from the epoch being replaced") {
        REQUIRE(second.prepare(plan, bindings, context, &first).empty());

        // The master's alignment of track 2, and the dry edge of the latent
        // device's own delta: both hold samples, and both carry.
        CHECK(second.carriedDelayLines() == 2);

        // Block 1, on the new executor: the impulse comes out where it would
        // have if nothing had happened.
        second.process(values, blockAt(kBlockSize, kBlockSize), output);
        CHECK(output.getSample(0, 80 - kBlockSize) == approx(0.5f));
    }

    SECTION("starting again, which is what it used to do") {
        REQUIRE(second.prepare(plan, bindings, context).empty());
        CHECK(second.carriedDelayLines() == 0);

        second.process(values, blockAt(kBlockSize, kBlockSize), output);
        CHECK(output.getSample(0, 80 - kBlockSize) == approx(0.0f));
    }

    SECTION("a line whose delay changed is rebuilt rather than reinterpreted") {
        // The samples in the old line are 80 apart from what they now have to
        // be aligned with. Handing them over would be a few milliseconds of
        // audio in the wrong place; starting again is a few milliseconds of
        // silence, and only one of those is recoverable.
        LatentDevice deeper(120);
        bindings.devices[DeviceKey{7}] = &deeper;

        REQUIRE(second.prepare(plan, bindings, context, &first).empty());
        CHECK(second.carriedDelayLines() == 0);
        CHECK(second.latencySamples() == 120);
    }
}

TEST_CASE("A chain out of the mix silences a nested rack's MIDI too", "[engine][exec]") {
    // The MIDI comes from a rack nested inside the muted chain, so its ops key
    // on the nested rack and nothing about them says which outer chain they
    // belong to. Both signals leave the chain through its fader, which is what
    // makes one silent flag enough.
    const auto build = [](bool muteOuterChain) {
        auto inner = std::make_unique<RackInfo>();
        inner->id = 6;
        ChainInfo innerChain;
        innerChain.id = 20;

        auto arp = makeEffect(7);
        arp.deviceType = DeviceType::MIDI;
        arp.canReceiveMidi = true;
        arp.producesMidi = true;
        arp.midiInThru = false;
        innerChain.elements.push_back(makeDeviceElement(arp));
        inner->chains.push_back(std::move(innerChain));

        auto outer = std::make_unique<RackInfo>();
        outer->id = 5;
        ChainInfo outerChain;
        outerChain.id = 10;
        outerChain.muted = muteOuterChain;
        outerChain.elements.push_back(ChainElement{std::move(inner)});
        outer->chains.push_back(std::move(outerChain));

        auto track = makeTrack(1);
        track.chain.fxChainElements.push_back(ChainElement{std::move(outer)});
        // Downstream of the rack, so it hears whatever MIDI the rack puts out.
        track.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(8)));
        return track;
    };

    ConstantSource audio(0.0f);
    NoteSource notes(40);
    ArpDevice arpDevice(72);
    NoteToDcInstrument instrument;

    SECTION("chain in the mix") {
        Harness harness({build(false)}, makeMaster());
        harness.bindings.clipAudio[1] = &audio;
        harness.bindings.clipMidi[1] = &notes;
        harness.bindings.devices[DeviceKey{7}] = &arpDevice;
        harness.bindings.devices[DeviceKey{8}] = &instrument;

        harness.prepareCleanly();
        harness.render();
        CHECK(harness.outputSample() == approx(72.0f / 127.0f));
    }

    SECTION("chain muted") {
        Harness harness({build(true)}, makeMaster());
        harness.bindings.clipAudio[1] = &audio;
        harness.bindings.clipMidi[1] = &notes;
        harness.bindings.devices[DeviceKey{7}] = &arpDevice;
        harness.bindings.devices[DeviceKey{8}] = &instrument;

        harness.prepareCleanly();
        harness.render();

        // No note reached the instrument, so it never left its initial silence.
        CHECK(harness.outputSample() == approx(0.0f));
    }
}

namespace {

/// Writes a different DC level to every output pair it has, so which pair a
/// track is reading is visible in that track's meter. The main pair carries
/// `base`, and pair n carries base * (n + 1).
class MultiOutInstrument final : public EngineDevice {
  public:
    explicit MultiOutInstrument(float base) : base_(base) {}

    void process(DeviceBlock& block) override {
        ++processedBlocks;
        sawExtraOutputs = static_cast<int>(block.extraOutputs.size());

        block.audio.fill(base_);
        for (std::size_t pair = 0; pair < block.extraOutputs.size(); ++pair)
            block.extraOutputs[pair].fill(base_ * static_cast<float>(pair + 2));
    }

    int processedBlocks = 0;
    int sawExtraOutputs = 0;

  private:
    float base_;
};

DeviceInfo makeMultiOutInstrument(DeviceId id, int pairs) {
    auto device = makeInstrument(id);
    device.multiOut.isMultiOut = true;
    device.multiOut.totalOutputChannels = pairs * 2;
    for (int pair = 0; pair < pairs; ++pair) {
        MultiOutOutputPair out;
        out.outputIndex = pair;
        out.firstPin = 1 + pair * 2;
        out.numChannels = 2;
        device.multiOut.outputPairs.push_back(out);
    }
    return device;
}

TrackInfo makeMultiOutTrack(TrackId id, TrackId sourceTrack, DeviceId deviceId, int pair) {
    auto track = makeTrack(id, TrackType::MultiOut);
    track.multiOutLink = MultiOutTrackLink{sourceTrack, deviceId, pair};
    return track;
}

}  // namespace

TEST_CASE("Each multi-out pair reaches the track that opened it", "[engine][exec]") {
    auto source = makeTrack(1);
    source.chain.fxChainElements.push_back(makeDeviceElement(makeMultiOutInstrument(7, 3)));

    Harness harness({source, makeMultiOutTrack(2, 1, 7, 1), makeMultiOutTrack(3, 1, 7, 2)},
                    makeMaster());
    ConstantSource silence(0.0f);
    NoteSource notes(64);
    MultiOutInstrument instrument(0.1f);
    harness.bindings.clipAudio[1] = &silence;
    harness.bindings.clipMidi[1] = &notes;
    harness.bindings.devices[DeviceKey{7}] = &instrument;

    harness.prepareCleanly();
    harness.render();

    // Two pairs past the main one, whether or not both were opened.
    CHECK(instrument.processedBlocks == 1);
    CHECK(instrument.sawExtraOutputs == 2);

    // Each track reads its own pair, and the source track keeps the main one.
    CHECK(harness.takeMeterFor(OpRole::TrackMeter, 1) == approx(0.1f));
    CHECK(harness.takeMeterFor(OpRole::TrackMeter, 2) == approx(0.2f));
    CHECK(harness.takeMeterFor(OpRole::TrackMeter, 3) == approx(0.3f));

    // All three land on the master, which is what a multi-out setup sounds
    // like with nothing else done to it.
    CHECK(harness.outputSample() == approx(0.6f));
}

TEST_CASE("A multi-out pair nobody opened is rendered and dropped", "[engine][exec]") {
    auto source = makeTrack(1);
    source.chain.fxChainElements.push_back(makeDeviceElement(makeMultiOutInstrument(7, 3)));

    Harness harness({source, makeMultiOutTrack(2, 1, 7, 1)}, makeMaster());
    ConstantSource silence(0.0f);
    NoteSource notes(64);
    MultiOutInstrument instrument(0.1f);
    harness.bindings.clipAudio[1] = &silence;
    harness.bindings.clipMidi[1] = &notes;
    harness.bindings.devices[DeviceKey{7}] = &instrument;

    harness.prepareCleanly();
    harness.render();

    // The device still writes both pairs: its output layout is its own, and
    // the plan decides what is read rather than what is produced.
    CHECK(instrument.sawExtraOutputs == 2);
    CHECK(harness.takeMeterFor(OpRole::TrackMeter, 2) == approx(0.2f));

    // Pair 2 reaches nothing, so the master carries the main pair and pair 1.
    CHECK(harness.outputSample() == approx(0.3f));
}

TEST_CASE("An ordinary device is handed no extra outputs", "[engine][exec]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeInstrument(7)));

    Harness harness({track}, makeMaster());
    ConstantSource silence(0.0f);
    NoteSource notes(64);
    MultiOutInstrument instrument(0.25f);
    harness.bindings.clipAudio[1] = &silence;
    harness.bindings.clipMidi[1] = &notes;
    harness.bindings.devices[DeviceKey{7}] = &instrument;

    harness.prepareCleanly();
    harness.render();

    CHECK(instrument.sawExtraOutputs == 0);
    CHECK(harness.outputSample() == approx(0.25f));
}

TEST_CASE("A multi-out pair the device stops writing goes silent", "[engine][exec]") {
    // Writes its pairs on the first block only, which is what any instrument
    // with nothing to play on a pair does. The pair has to go silent rather
    // than repeat the block it last had material for, and the only thing that
    // makes that true is the executor clearing the port before the call.
    class WritesOnceInstrument final : public EngineDevice {
      public:
        void process(DeviceBlock& block) override {
            block.audio.fill(0.1f);
            if (blocks++ > 0)
                return;
            for (std::size_t pair = 0; pair < block.extraOutputs.size(); ++pair)
                block.extraOutputs[pair].fill(0.2f);
        }

        int blocks = 0;
    };

    auto source = makeTrack(1);
    source.chain.fxChainElements.push_back(makeDeviceElement(makeMultiOutInstrument(7, 2)));

    Harness harness({source, makeMultiOutTrack(2, 1, 7, 1)}, makeMaster());
    ConstantSource silence(0.0f);
    NoteSource notes(64);
    WritesOnceInstrument instrument;
    harness.bindings.clipAudio[1] = &silence;
    harness.bindings.clipMidi[1] = &notes;
    harness.bindings.devices[DeviceKey{7}] = &instrument;

    harness.prepareCleanly();
    harness.render();
    REQUIRE(harness.takeMeterFor(OpRole::TrackMeter, 2) == approx(0.2f));

    harness.render(kBlockSize, kBlockSize);
    CHECK(instrument.blocks == 2);
    CHECK(harness.takeMeterFor(OpRole::TrackMeter, 2) == approx(0.0f));

    // The main pair is unaffected: it is written every block.
    CHECK(harness.takeMeterFor(OpRole::TrackMeter, 1) == approx(0.1f));
}
