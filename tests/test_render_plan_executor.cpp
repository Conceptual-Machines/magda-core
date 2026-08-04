#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
                static_cast<float>((block.timelineSample + sample) % 97) * (1.0f / 97.0f);
            for (std::size_t channel = 0; channel < out.getNumChannels(); ++channel)
                out.setSample(static_cast<int>(channel), sample, value);
        }
    }
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
        return executor.prepare(plan, bindings, RenderContext{44100.0, kBlockSize, 2});
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
        executor.process(values, BlockInfo{numSamples, timelineSample, true}, output);
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

    float meterFor(OpRole role, TrackId trackId) const {
        return executor.meterLevels()[static_cast<std::size_t>(opWithRole(role, trackId))];
    }

    std::vector<TrackInfo> tracks;
    TrackInfo master;
    RenderPlan plan;
    PlanBindings bindings;
    PlanValues values;
    std::vector<std::string> valueMessages;
    PlanExecutor executor;
    juce::AudioBuffer<float> output;
};

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-5);
}

}  // namespace

TEST_CASE("A source renders through a device into the master output", "[engine][exec]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    Harness harness({track}, makeMaster());
    ConstantSource source(0.5f);
    GainDevice device(0.5f);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[7] = &device;

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
    harness.bindings.devices[7] = &device;

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
    CHECK(harness.meterFor(OpRole::TrackMeter, 1) == approx(0.8f));
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
        harness.bindings.devices[8] = &instrument;

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
        harness.bindings.devices[8] = &instrument;

        harness.prepareCleanly();
        harness.render();

        // The instrument in the muted chain never ran, so the chain adds
        // nothing to the mix: it is out of the graph, not turned down.
        CHECK(harness.outputSample() == approx(0.25f));
        CHECK(harness.meterFor(OpRole::DeviceMeter, 1) == approx(0.0f));
    }

    SECTION("a sibling chain is soloed") {
        auto soloed = track;
        getRack(soloed.chain.fxChainElements[0]).chains[0].solo = true;

        Harness harness({soloed}, makeMaster());
        harness.bindings.clipAudio[1] = &source;
        harness.bindings.clipMidi[1] = &notes;
        harness.bindings.devices[8] = &instrument;

        harness.prepareCleanly();
        harness.render();
        CHECK(harness.outputSample() == approx(0.25f));
        CHECK(harness.meterFor(OpRole::DeviceMeter, 1) == approx(0.0f));
    }
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
    harness.bindings.devices[8] = &instrument;

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
    harness.bindings.devices[7] = &arpDevice;
    harness.bindings.devices[8] = &instrument;

    harness.prepareCleanly();
    harness.render();

    // The arp replaces the incoming note rather than passing it through, so the
    // instrument sees only what the arp emitted.
    CHECK(harness.outputSample() == approx(72.0f / 127.0f));
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
    harness.bindings.devices[7] = &probe;

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
    CHECK(messages[1].find("no device bound for device 7") != std::string::npos);

    harness.render();
    CHECK(harness.outputSample() == approx(0.0f));
}

TEST_CASE("Device latency is reported rather than silently ignored", "[engine][exec]") {
    auto track = makeTrack(1);
    track.chain.fxChainElements.push_back(makeDeviceElement(makeEffect(7)));

    Harness harness({track}, makeMaster());
    ConstantSource source(1.0f);
    GainDevice device(1.0f, 128);
    harness.bindings.clipAudio[1] = &source;
    harness.bindings.devices[7] = &device;

    const auto messages = harness.prepare();
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].find("128 samples of latency") != std::string::npos);
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
    executor.process(PlanValues{}, BlockInfo{64, 0, true}, output);
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
        harness.bindings.devices[7] = &device;
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
}

TEST_CASE("A nested rack inside a chain that is out of the mix is reported", "[engine][exec]") {
    auto inner = std::make_unique<RackInfo>();
    inner->id = 6;
    ChainInfo innerChain;
    innerChain.id = 20;
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
    harness.bindings.clipAudio[1] = &source;
    harness.prepare();

    REQUIRE(harness.valueMessages.size() == 1);
    CHECK(harness.valueMessages.front().find("keys its ops on itself") != std::string::npos);

    // Audio is still correct: everything in the chain passes through the
    // chain's fader on its way out.
    harness.render();
    CHECK(harness.outputSample() == approx(0.0f));
}
