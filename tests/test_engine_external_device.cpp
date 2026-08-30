#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "exec/EngineDevice.hpp"
#include "magda/daw/audio/plugin_manager/ExternalPluginState.hpp"
#include "magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "magda/daw/audio/plugins/engine/EngineExternalDevice.hpp"
#include "param/ParamBlock.hpp"
#include "transport/TempoMap.hpp"

/**
 * @file test_engine_external_device.cpp
 * @brief An external plugin under the native engine's Device op (#2241).
 *
 * The adapter's half of external plugin hosting, tested against a plugin
 * written here rather than against one that happens to be installed. That is
 * not a convenience: the assertions below are about what the host does with a
 * plugin -- which channels it hands it, which parameter a project's saved value
 * lands on, what it does with the wet/dry pair the plugin never declared -- and
 * a real plugin answers none of those questions any better than a stub that
 * reports exactly what it was given. What a real plugin is for is the corpus,
 * where the two engines run the same one (#2175).
 *
 * The stub is deliberately awkward in the ways real plugins are: a
 * non-automatable parameter in the middle of its list, a mono bus, a sidechain
 * bus, latency it reports rather than hides.
 */

namespace {

namespace adapter = magda::daw::audio::engine_adapter;

/**
 * @brief One parameter of the stub, automatable or not.
 *
 * The non-automatable one is what makes the slot numbering below worth
 * asserting: the fork's list skips it, so every parameter after it sits one slot
 * lower than its position in the plugin's own array. Real plugins are full of
 * them -- a bypass switch, a program selector, a meter reported as a parameter.
 */
class StubParameter final : public juce::AudioProcessorParameterWithID {
  public:
    StubParameter(const juce::String& id, const juce::String& name, float initial, bool automatable)
        : juce::AudioProcessorParameterWithID(
              juce::ParameterID{id, 1}, name,
              juce::AudioProcessorParameterWithIDAttributes().withAutomatable(automatable)),
          value_(initial) {}

    float getValue() const override {
        return value_;
    }

    void setValue(float newValue) override {
        value_ = newValue;
        ++writes;
    }

    float getDefaultValue() const override {
        return 0.0f;
    }

    float getValueForText(const juce::String& text) const override {
        return text.getFloatValue();
    }

    /// How many times the host wrote this parameter, which is how a test sees
    /// that an unchanged value is not written again.
    int writes = 0;

  private:
    float value_ = 0.0f;
};

/**
 * @brief A plugin that reports what it was handed and marks what it wrote.
 *
 * Its output is the input at the gain its first parameter says, plus a
 * per-channel marker, so a test can tell which of its channels ended up where
 * without inferring it from a level.
 */
class StubPlugin final : public juce::AudioPluginInstance {
  public:
    static juce::AudioChannelSet setFor(int channels) {
        return channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    }

    static BusesProperties busesFor(int inputs, int outputs, int sidechain, int extraPairs) {
        auto buses = BusesProperties()
                         .withInput("Input", setFor(inputs), true)
                         .withOutput("Output", setFor(outputs), true);

        if (sidechain > 0)
            buses = buses.withInput("Sidechain", setFor(sidechain), true);

        // Further stereo output buses, the way a multi-out sampler reports its
        // drum outs: one bus each, after the main one.
        for (int pair = 0; pair < extraPairs; ++pair)
            buses = buses.withOutput("Out " + juce::String(pair + 2),
                                     juce::AudioChannelSet::stereo(), true);

        return buses;
    }

    StubPlugin(int inputs, int outputs, int sidechain, int extraPairs = 0)
        : AudioPluginInstance(busesFor(inputs, outputs, sidechain, extraPairs)) {
        auto gainParameter = std::make_unique<StubParameter>("gain", "Gain", 1.0f, true);
        gain = gainParameter.get();
        addHostedParameter(std::move(gainParameter));

        auto fixedParameter = std::make_unique<StubParameter>("fixed", "Fixed", 0.0f, false);
        fixed = fixedParameter.get();
        addHostedParameter(std::move(fixedParameter));

        auto toneParameter = std::make_unique<StubParameter>("tone", "Tone", 0.25f, true);
        tone = toneParameter.get();
        addHostedParameter(std::move(toneParameter));
    }

    StubPlugin() : StubPlugin(2, 2, 0) {}

    const juce::String getName() const override {
        return "Stub";
    }

    void fillInPluginDescription(juce::PluginDescription& description) const override {
        description.name = "Stub";
        description.pluginFormatName = "VST3";
        description.manufacturerName = "MAGDA";
    }

    void prepareToPlay(double rate, int blockSize) override {
        preparedRate = rate;
        preparedBlockSize = blockSize;
        ++prepareCount;
        nonRealtimeAtPrepare = isNonRealtime();
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        channelsSeen = buffer.getNumChannels();
        samplesSeen = buffer.getNumSamples();
        inputSeen.makeCopyOf(buffer);
        midiSeen.clear();
        midiSeen.addEvents(midi, 0, buffer.getNumSamples(), 0);

        if (auto* head = getPlayHead())
            if (auto position = head->getPosition())
                positionSeen = *position;

        // Only the channels it has outputs for, which is what a real plugin
        // does: JUCE hands it a buffer as wide as its inputs or its outputs,
        // whichever is more, and a mono-out plugin leaves everything past its
        // one output holding the input it was given.
        const auto written = std::min(buffer.getNumChannels(), getTotalNumOutputChannels());

        const auto level = gain->getValue();
        for (int channel = 0; channel < written; ++channel) {
            auto* samples = buffer.getWritePointer(channel);
            const auto marker = static_cast<float>(channel + 1) * kChannelMarker;

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                samples[sample] = (samples[sample] * level) + marker;
        }

        if (emitsMidi) {
            midi.clear();
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
        }
    }

    double getTailLengthSeconds() const override {
        return 0.0;
    }

    bool acceptsMidi() const override {
        return true;
    }

    bool producesMidi() const override {
        return emitsMidi;
    }

    juce::AudioProcessorEditor* createEditor() override {
        return nullptr;
    }

    bool hasEditor() const override {
        return false;
    }

    int getNumPrograms() override {
        return 1;
    }

    int getCurrentProgram() override {
        return 0;
    }

    void setCurrentProgram(int) override {}

    const juce::String getProgramName(int) override {
        return {};
    }

    void changeProgramName(int, const juce::String&) override {}

    /// The state a real plugin carries and a parameter array cannot: here, the
    /// values of both parameters, including the one no host may automate.
    struct State {
        float tone = 0.0f;
        float fixed = 0.0f;
    };

    void getStateInformation(juce::MemoryBlock& destination) override {
        const State state{.tone = tone->getValue(), .fixed = fixed->getValue()};
        destination.replaceAll(&state, sizeof(state));
    }

    void setStateInformation(const void* data, int size) override {
        if (throwsOnState)
            throw std::runtime_error("plugin state handler failed");

        if (size != static_cast<int>(sizeof(State)))
            return;

        State state{};
        std::memcpy(&state, data, sizeof(state));
        tone->setValue(state.tone);
        fixed->setValue(state.fixed);
        ++stateRestores;
    }

    /// The per-channel offset the output carries, so a test can name which of
    /// the plugin's channels it is reading.
    static constexpr float kChannelMarker = 0.01f;

    StubParameter* gain = nullptr;
    StubParameter* tone = nullptr;
    StubParameter* fixed = nullptr;

    int stateRestores = 0;

    bool emitsMidi = false;

    /// Third-party code handed a chunk it cannot read. Some throw.
    bool throwsOnState = false;

    double preparedRate = 0.0;
    int preparedBlockSize = 0;
    int prepareCount = 0;
    bool nonRealtimeAtPrepare = false;

    int channelsSeen = 0;
    int samplesSeen = 0;
    juce::AudioBuffer<float> inputSeen;
    juce::MidiBuffer midiSeen;
    juce::AudioPlayHead::PositionInfo positionSeen;
};

magda::engine::RenderContext contextFor(int channels = 2, int blockSize = 64) {
    return {.sampleRate = 48000.0, .maxBlockSize = blockSize, .numChannels = channels};
}

/// A device whose parameters are the fork's list: the wrapper pair at zero and
/// one, then the plugin's automatable parameters.
magda::DeviceInfo externalDevice() {
    magda::DeviceInfo device;
    device.id = 7;
    device.name = "Stub";
    device.format = magda::PluginFormat::VST3;

    const auto normalised = [](int index, const juce::String& name) {
        magda::ParameterInfo info;
        info.paramIndex = index;
        info.name = name;
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.currentValue = 0.0f;
        return info;
    };

    auto dry = normalised(0, "Dry Level");
    dry.wrapperRole = magda::WrapperRole::DryGain;
    auto wet = normalised(1, "Wet Level");
    wet.wrapperRole = magda::WrapperRole::WetGain;

    device.wrapperParameters = {dry, wet};
    device.parameters = {normalised(2, "Gain"), normalised(3, "Tone")};

    return device;
}

/// The same device, with the knob positions a project saved for the plugin's
/// two parameters. What the plan resolves for an untouched parameter is this,
/// which is what tells "the project left it here" from "something moved it".
magda::DeviceInfo externalDeviceSaving(float gain, float tone) {
    auto device = externalDevice();
    device.parameters[0].currentValue = gain;
    device.parameters[1].currentValue = tone;
    return device;
}

/**
 * @brief One block, and the parameter values the plan resolved for it.
 *
 * The table is built by hand rather than compiled, because what is being
 * asserted is what the adapter does with a resolved value and not how it came
 * to be resolved. The values are held as the flat arena DeviceParams reads,
 * one segment per parameter.
 */
class ParamArena {
  public:
    explicit ParamArena(const std::vector<float>& values) {
        for (const auto value : values) {
            segments_.push_back({.startSample = 0, .startValue = value, .endValue = value});
            counts_.push_back(1);
            domains_.push_back(
                {.scale = magda::ParameterScale::Linear, .minValue = 0.0f, .maxValue = 1.0f});
        }
    }

    magda::engine::DeviceParams params(int numSamples) const {
        return {segments_, counts_, domains_, 1, numSamples};
    }

  private:
    std::vector<magda::engine::ParamSegment> segments_;
    std::vector<int> counts_;
    std::vector<magda::ParameterUtils::ParameterDomain> domains_;
};

/// A block of audio, its MIDI ports, and the description of where it is.
struct Block {
    Block(const magda::engine::RenderContext& context, int channels)
        : buffer(channels, context.maxBlockSize) {
        buffer.clear();
        info.numSamples = context.maxBlockSize;
        info.playing = true;
        info.endSeconds = context.maxBlockSize / context.sampleRate;
        info.endBeat = info.endSeconds * 2.0;
    }

    void fill(float value) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), value,
                                              buffer.getNumSamples());
    }

    magda::engine::DeviceBlock deviceBlock(const magda::engine::DeviceParams& params) {
        magda::engine::DeviceBlock block;
        block.audio = juce::dsp::AudioBlock<float>(buffer);
        block.params = params;
        block.block = info;
        return block;
    }

    juce::AudioBuffer<float> buffer;
    magda::engine::BlockInfo info;
};

}  // namespace

TEST_CASE("External device reports the instance's latency", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    plugin->setLatencySamples(128);
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    device.prepare(contextFor());

    CHECK(device.latencySamples() == 128);
    CHECK(raw->preparedRate == 48000.0);
    CHECK(raw->preparedBlockSize == 64);
    CHECK(raw->prepareCount == 1);
}

TEST_CASE("A bounce tells the plugin it is not realtime", "[engine][external]") {
    auto live = std::make_unique<StubPlugin>();
    auto* liveRaw = live.get();
    adapter::EngineExternalDevice liveDevice(std::move(live), externalDevice(), false);
    liveDevice.prepare(contextFor());

    auto offline = std::make_unique<StubPlugin>();
    auto* offlineRaw = offline.get();
    adapter::EngineExternalDevice offlineDevice(std::move(offline), externalDevice(), true);
    offlineDevice.prepare(contextFor());

    CHECK_FALSE(liveRaw->nonRealtimeAtPrepare);
    CHECK(offlineRaw->nonRealtimeAtPrepare);
}

TEST_CASE("A plan slot addresses the fork's parameter, not the plugin's", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    // Slot two is the plugin's first automatable parameter and slot three is
    // its second: the non-automatable one between them is not in the fork's
    // list, so it takes no slot.
    ParamArena arena({0.0f, 1.0f, 0.75f, 0.5f});
    Block block(context, 2);
    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    CHECK(raw->gain->getValue() == Catch::Approx(0.75f));
    CHECK(raw->tone->getValue() == Catch::Approx(0.5f));
}

TEST_CASE("A stereo plugin processes the block in place", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    block.fill(0.5f);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    CHECK(raw->channelsSeen == 2);
    CHECK(raw->inputSeen.getSample(0, 0) == Catch::Approx(0.5f));

    // Its own output, marked per channel: nothing between the plugin and the
    // block to lose a channel in.
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(0.5f + StubPlugin::kChannelMarker));
    CHECK(block.buffer.getSample(1, 0) == Catch::Approx(0.5f + (2 * StubPlugin::kChannelMarker)));
}

TEST_CASE("A mono plugin is fed the average and answers on both sides", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(1, 1, 0);
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    juce::FloatVectorOperations::fill(block.buffer.getWritePointer(0), 1.0f,
                                      block.buffer.getNumSamples());
    juce::FloatVectorOperations::fill(block.buffer.getWritePointer(1), 0.0f,
                                      block.buffer.getNumSamples());

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    // The average of one and nothing, which is the fork's rule and not the left
    // channel.
    CHECK(raw->channelsSeen == 1);
    CHECK(raw->inputSeen.getSample(0, 0) == Catch::Approx(0.5f));

    // One output channel spread over both sides, so what follows reads two.
    const auto expected = 0.5f + StubPlugin::kChannelMarker;
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(expected));
    CHECK(block.buffer.getSample(1, 0) == Catch::Approx(expected));
}

TEST_CASE("A stereo plugin in a mono slot is handed the channel twice", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor(1);
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 1);
    block.fill(0.25f);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    CHECK(raw->channelsSeen == 2);
    CHECK(raw->inputSeen.getSample(0, 0) == Catch::Approx(0.25f));
    CHECK(raw->inputSeen.getSample(1, 0) == Catch::Approx(0.25f));

    // The slot is one channel wide, so only the plugin's first comes back.
    CHECK(block.buffer.getNumChannels() == 1);
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(0.25f + StubPlugin::kChannelMarker));
}

TEST_CASE("A sidechain key lands on the bus after the plugin's own", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 2);
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    block.fill(0.5f);

    juce::AudioBuffer<float> key(2, context.maxBlockSize);
    for (int channel = 0; channel < key.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(key.getWritePointer(channel), -0.75f,
                                          key.getNumSamples());

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    deviceBlock.sidechain = juce::dsp::AudioBlock<const float>(key);
    device.process(deviceBlock);

    REQUIRE(raw->channelsSeen == 4);
    CHECK(raw->inputSeen.getSample(0, 0) == Catch::Approx(0.5f));
    CHECK(raw->inputSeen.getSample(2, 0) == Catch::Approx(-0.75f));
    CHECK(raw->inputSeen.getSample(3, 0) == Catch::Approx(-0.75f));

    // Only the plugin's own pair comes back to the chain.
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(0.5f + StubPlugin::kChannelMarker));
}

TEST_CASE("An unconnected sidechain is silence rather than the last block's key",
          "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 2);
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    block.fill(0.5f);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    REQUIRE(raw->channelsSeen == 4);
    CHECK(raw->inputSeen.getSample(2, 0) == Catch::Approx(0.0f));
    CHECK(raw->inputSeen.getSample(3, 0) == Catch::Approx(0.0f));
}

TEST_CASE("The wrapper pair mixes the plugin against what it was handed", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    // Half dry, half wet, and a plugin that halves what it is given: the output
    // is half the input plus half of the plugin's answer.
    ParamArena arena({0.5f, 0.5f, 0.5f, 0.0f});
    Block block(context, 2);
    block.fill(1.0f);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    const auto wet = ((1.0f * 0.5f) + StubPlugin::kChannelMarker) * 0.5f;
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(wet + (1.0f * 0.5f)));
}

TEST_CASE("A fully dry slot still runs the plugin", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({1.0f, 0.0f, 1.0f, 0.0f});
    Block block(context, 2);
    block.fill(0.25f);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    // The plugin ran -- its tail keeps building even at no wet level, which is
    // what makes a wet-level automation lane usable at all.
    CHECK(raw->channelsSeen == 2);
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(0.25f));
}

TEST_CASE("MIDI reaches the plugin and what it answers reaches the port", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();
    raw->emitsMidi = true;

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);

    juce::MidiBuffer in;
    in.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 12);
    juce::MidiBuffer out;

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    deviceBlock.midiIn = &in;
    deviceBlock.midiOut = &out;
    device.process(deviceBlock);

    REQUIRE(raw->midiSeen.getNumEvents() == 1);
    for (const auto event : raw->midiSeen) {
        CHECK(event.getMessage().isNoteOn());
        CHECK(event.getMessage().getNoteNumber() == 60);
        CHECK(event.samplePosition == 12);
    }

    REQUIRE(out.getNumEvents() == 1);
    for (const auto event : out)
        CHECK(event.getMessage().getNoteNumber() == 64);
}

TEST_CASE("The plugin is told where the transport is", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    const magda::engine::TempoMap tempo({{.startBeat = 0.0, .bpm = 90.0}},
                                        {{.startBeat = 0.0, .numerator = 3, .denominator = 4}});

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    block.info.startBeat = 7.0;  // the second beat of the third bar, in 3/4
    block.info.endBeat = 7.5;
    block.info.startSeconds = tempo.beatToTime(7.0);
    block.info.endSeconds = tempo.beatToTime(7.5);
    block.info.tempo = &tempo;

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    const auto& position = raw->positionSeen;
    REQUIRE(position.getBpm().hasValue());
    CHECK(*position.getBpm() == Catch::Approx(90.0));
    CHECK(position.getIsPlaying());
    REQUIRE(position.getPpqPosition().hasValue());
    CHECK(*position.getPpqPosition() == Catch::Approx(7.0));
    REQUIRE(position.getPpqPositionOfLastBarStart().hasValue());
    CHECK(*position.getPpqPositionOfLastBarStart() == Catch::Approx(6.0));
    REQUIRE(position.getTimeSignature().hasValue());
    CHECK(position.getTimeSignature()->numerator == 3);
    CHECK(position.getTimeSignature()->denominator == 4);
    REQUIRE(position.getTimeInSeconds().hasValue());
    CHECK(*position.getTimeInSeconds() == Catch::Approx(tempo.beatToTime(7.0)));
}

TEST_CASE("A parameter that did not move is not written again", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena held({0.0f, 1.0f, 0.6f, 0.0f});
    Block block(context, 2);

    // From here: applying the project's saved parameter array is itself a
    // write, and what this is about is how many the blocks add.
    const auto writesAfterConstruction = raw->gain->writes;

    for (int pass = 0; pass < 3; ++pass) {
        auto deviceBlock = block.deviceBlock(held.params(context.maxBlockSize));
        device.process(deviceBlock);
    }

    // Once, on the block that moved it. The fork writes a plugin parameter only
    // when the value differs from what the plugin already reports, because a
    // plugin is entitled to treat every write as a gesture: one that rebuilds a
    // filter or repaints an editor on each would do it every block on a
    // parameter nobody touched.
    CHECK(raw->gain->writes == writesAfterConstruction + 1);

    ParamArena moved({0.0f, 1.0f, 0.7f, 0.0f});
    auto movedBlock = block.deviceBlock(moved.params(context.maxBlockSize));
    device.process(movedBlock);

    CHECK(raw->gain->writes == writesAfterConstruction + 2);
}

TEST_CASE("A parameter the table does not carry is left where it was", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();
    raw->tone->setValue(0.9f);

    // A project saved against a build of the plugin that did not have the tone
    // parameter: the model does not describe it, so nothing seeds it and the
    // plan's window stops before it.
    auto model = externalDevice();
    model.parameters.pop_back();

    adapter::EngineExternalDevice device(std::move(plugin), model, false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 0.4f});
    Block block(context, 2);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    CHECK(raw->gain->getValue() == Catch::Approx(0.4f));
    CHECK(raw->tone->getValue() == Catch::Approx(0.9f));
}

TEST_CASE("A plugin the scan never saw is refused with a reason", "[engine][external]") {
    juce::KnownPluginList empty;
    juce::AudioPluginFormatManager formats;

    magda::DeviceInfo missing = externalDevice();
    missing.name = "Massive X";
    missing.fileOrIdentifier = "/Library/MassiveX.vst3";

    const adapter::ExternalPluginServices services{
        .formats = &formats, .knownPlugins = &empty, .context = contextFor()};

    const auto result = adapter::createEngineExternalDevice(missing, services);

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("Massive X"));
    CHECK(result.failure.contains("not installed"));
    CHECK_FALSE(adapter::isInstalledExternalPlugin(missing, empty));
}

TEST_CASE("A host that gave the engine no formats is told so", "[engine][external]") {
    const adapter::ExternalPluginServices nothing{};

    const auto result = adapter::createEngineExternalDevice(externalDevice(), nothing);

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("no plugin formats"));
}

TEST_CASE("A plugin with more inputs than outputs fills the slot from its outputs",
          "[engine][external]") {
    // Stereo in, mono out, and a mono sidechain after them: processed at three
    // channels, of which the plugin writes one. The two channels past its
    // output hold what was copied in -- the right half of the input, and the
    // key -- and reading those back as output hands the chain its own input in
    // place of an answer.
    auto plugin = std::make_unique<StubPlugin>(2, 1, 1);
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    juce::FloatVectorOperations::fill(block.buffer.getWritePointer(0), 1.0f,
                                      block.buffer.getNumSamples());
    juce::FloatVectorOperations::fill(block.buffer.getWritePointer(1), -1.0f,
                                      block.buffer.getNumSamples());

    juce::AudioBuffer<float> key(1, context.maxBlockSize);
    juce::FloatVectorOperations::fill(key.getWritePointer(0), 0.5f, key.getNumSamples());

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    deviceBlock.sidechain = juce::dsp::AudioBlock<const float>(key);
    device.process(deviceBlock);

    REQUIRE(raw->channelsSeen == 3);

    // The one output, on both sides. Not the input's right channel and not the
    // key, which are what channels one and two of the plugin's buffer hold.
    const auto expected = 1.0f + StubPlugin::kChannelMarker;
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(expected));
    CHECK(block.buffer.getSample(1, 0) == Catch::Approx(expected));
}

TEST_CASE("A stereo-in mono-out plugin does not leave its input in the right channel",
          "[engine][external]") {
    // The same rule where the widths happen to agree: two channels in, one
    // written, and the block is two channels wide. Taking the fast path here
    // would leave the input sitting in the right channel, which is neither
    // silence nor the plugin's answer.
    auto plugin = std::make_unique<StubPlugin>(2, 1, 0);

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    block.fill(0.5f);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    const auto expected = 0.5f + StubPlugin::kChannelMarker;
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(expected));
    CHECK(block.buffer.getSample(1, 0) == Catch::Approx(expected));
}

TEST_CASE("A second prepare at the same settings does not prepare the plugin again",
          "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    device.prepare(contextFor());
    device.prepare(contextFor());

    // Once. The fork does not release a plugin's resources before re-preparing
    // it -- with VST3 that shuts down the MIDI input buses for good -- so a
    // device retained across a re-prepare at settings that did not move is left
    // alone rather than torn down and rebuilt.
    CHECK(raw->prepareCount == 1);

    device.prepare(contextFor(2, 128));
    CHECK(raw->prepareCount == 2);
    CHECK(raw->preparedBlockSize == 128);
}

TEST_CASE("A multi-out plugin's further pairs reach the ports opened for them",
          "[engine][external]") {
    // Three drum outs past the main pair, which is what a multi-out sampler on
    // a MultiOut track is. The plan opens a port for each and the executor
    // hands over a cleared block for it; a device that writes only the main
    // pair leaves every one of those tracks silent.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0, 3);
    auto* raw = plugin.get();

    auto model = externalDevice();
    model.multiOut.isMultiOut = true;
    model.multiOut.totalOutputChannels = 8;
    for (int pair = 0; pair < 4; ++pair)
        model.multiOut.outputPairs.push_back({.outputIndex = pair,
                                              .name = "Out " + juce::String(pair + 1),
                                              .firstPin = (pair * 2) + 1,
                                              .numChannels = 2});

    adapter::EngineExternalDevice device(std::move(plugin), model, false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    block.fill(0.25f);

    // One block per pair past the main one, cleared, exactly as the executor
    // hands them over.
    std::vector<juce::AudioBuffer<float>> pairBuffers;
    std::vector<juce::dsp::AudioBlock<float>> pairs;
    pairBuffers.reserve(3);
    for (int pair = 0; pair < 3; ++pair) {
        pairBuffers.emplace_back(2, context.maxBlockSize);
        pairBuffers.back().clear();
    }
    for (auto& buffer : pairBuffers)
        pairs.emplace_back(buffer);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    deviceBlock.extraOutputs = {pairs.data(), pairs.size()};
    device.process(deviceBlock);

    REQUIRE(raw->channelsSeen == 8);

    // The main pair still goes to the chain, and each further pair carries the
    // plugin's own channels for it: pair one is channels two and three.
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(0.25f + StubPlugin::kChannelMarker));

    for (int pair = 0; pair < 3; ++pair) {
        const auto firstChannel = (pair + 1) * 2;
        for (int channel = 0; channel < 2; ++channel) {
            const auto expected =
                static_cast<float>(firstChannel + channel + 1) * StubPlugin::kChannelMarker;
            CHECK(pairBuffers[static_cast<std::size_t>(pair)].getSample(channel, 0) ==
                  Catch::Approx(expected));
        }
    }
}

TEST_CASE("A pair the plugin does not have stays silent", "[engine][external]") {
    // A model that recorded four pairs against a build of the plugin that has
    // two. The block for the pair with nothing behind it arrives cleared and
    // has to stay that way rather than being handed whatever sits at that index.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0, 1);

    auto model = externalDevice();
    model.multiOut.isMultiOut = true;
    for (int pair = 0; pair < 3; ++pair)
        model.multiOut.outputPairs.push_back(
            {.outputIndex = pair, .name = "Out", .firstPin = (pair * 2) + 1, .numChannels = 2});

    adapter::EngineExternalDevice device(std::move(plugin), model, false);
    const auto context = contextFor();
    device.prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    block.fill(0.25f);

    std::vector<juce::AudioBuffer<float>> pairBuffers;
    std::vector<juce::dsp::AudioBlock<float>> pairs;
    pairBuffers.reserve(2);
    for (int pair = 0; pair < 2; ++pair) {
        pairBuffers.emplace_back(2, context.maxBlockSize);
        pairBuffers.back().clear();
    }
    for (auto& buffer : pairBuffers)
        pairs.emplace_back(buffer);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    deviceBlock.extraOutputs = {pairs.data(), pairs.size()};
    device.process(deviceBlock);

    // Pair one is the plugin's second bus and carries audio; pair two has no
    // channels behind it.
    CHECK(pairBuffers[0].getSample(0, 0) == Catch::Approx(3 * StubPlugin::kChannelMarker));
    CHECK(pairBuffers[1].getSample(0, 0) == Catch::Approx(0.0f));
    CHECK(pairBuffers[1].getSample(1, 0) == Catch::Approx(0.0f));
}

TEST_CASE("A project's saved plugin state reaches the plugin", "[engine][external]") {
    // The chunk carries what the parameter array cannot. Here that is the value
    // of a parameter no host may automate, which stands for a sampler's loaded
    // samples and a synth's current program: a plugin created without the chunk
    // renders its initialised voice, which is a different project rather than a
    // quieter one.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    const StubPlugin::State saved{.tone = 0.9f, .fixed = 0.8f};
    juce::MemoryBlock chunk(&saved, sizeof(saved));

    auto model = externalDevice();
    model.pluginState = chunk.toBase64Encoding();

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);
    CHECK(raw->stateRestores == 1);
    CHECK(raw->fixed->getValue() == Catch::Approx(0.8f));
}

TEST_CASE("A stale saved parameter array is corrected rather than replayed", "[engine][external]") {
    // Every project MAGDA saved before the restore was fixed has this shape:
    // the chunk holds the voice the user heard, and the parameter array beside
    // it holds the defaults the host read at construction. The incumbent lets
    // the chunk win and test_external_plugin_state_restore_juce.cpp pins that.
    //
    // Under the native engine the plan writes every parameter it resolves
    // before each block, and it resolves them from the model. So the chunk
    // winning on the instance is not enough on its own: what the restoration
    // leaves behind has to reach the model, or the next block puts the stale
    // value back. That correction is what the result carries.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    const StubPlugin::State chunkVoice{.tone = 0.9f, .fixed = 0.8f};
    juce::MemoryBlock chunk(&chunkVoice, sizeof(chunkVoice));

    auto model = externalDeviceSaving(1.0f, 0.25f);  // stale: the chunk says 0.9
    model.pluginState = chunk.toBase64Encoding();

    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    // The chunk won on the instance.
    CHECK(raw->tone->getValue() == Catch::Approx(0.9f));
    CHECK(raw->fixed->getValue() == Catch::Approx(0.8f));

    // And the correction came back, addressed the way the project addresses it.
    const auto tone =
        std::find_if(result.restoredParameters.begin(), result.restoredParameters.end(),
                     [](const magda::RestoredParameter& p) { return p.paramIndex == 3; });
    REQUIRE(tone != result.restoredParameters.end());
    CHECK(tone->value == Catch::Approx(0.9f));

    // Applied by the host, on the model, which is the only place it may be
    // written. From here the plan resolves the chunk's value and every block
    // asserts it rather than fighting it.
    magda::applyRestoredParameters(model, result.restoredParameters);
    CHECK(model.parameters[1].currentValue == Catch::Approx(0.9f));

    const auto context = contextFor();
    result.device->prepare(context);

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.9f});
    Block block(context, 2);
    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    result.device->process(deviceBlock);

    CHECK(raw->tone->getValue() == Catch::Approx(0.9f));
}

TEST_CASE("With no chunk the saved parameter array is what the plugin gets", "[engine][external]") {
    // The baseline is not a formality. A plugin that stores nothing, or whose
    // chunk this build refuses, has only the array, and skipping it would leave
    // it on its factory defaults with the project's own values unused.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    REQUIRE(raw->tone->getValue() == Catch::Approx(0.25f));  // the plugin's own default

    const auto result =
        adapter::adaptExternalPluginInstance(std::move(plugin), externalDeviceSaving(1.0f, 0.7f));

    REQUIRE(result.device != nullptr);
    CHECK(raw->tone->getValue() == Catch::Approx(0.7f));
    CHECK(raw->stateRestores == 0);
}

TEST_CASE("A chunk that is not base64 leaves the baseline standing", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto model = externalDeviceSaving(1.0f, 0.7f);
    model.pluginState = "not base64 at all !!";

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    REQUIRE(result.device != nullptr);
    CHECK(raw->stateRestores == 0);
    CHECK(raw->tone->getValue() == Catch::Approx(0.7f));
}

TEST_CASE("A chunk the plugin declines leaves the baseline standing", "[engine][external]") {
    // Valid base64 of something this build of the plugin cannot read, which is
    // what a chunk written by another version of it looks like.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto model = externalDeviceSaving(1.0f, 0.7f);
    const juce::MemoryBlock nonsense("wrong size entirely", 19);
    model.pluginState = nonsense.toBase64Encoding();

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    REQUIRE(result.device != nullptr);
    CHECK(raw->tone->getValue() == Catch::Approx(0.7f));
}

TEST_CASE("A plugin that throws restoring its state does not take the host with it",
          "[engine][external]") {
    // A plugin's state handler is third-party code, and a corrupt chunk is the
    // input it is least likely to have been tested against.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->throwsOnState = true;

    const StubPlugin::State chunkVoice{.tone = 0.9f, .fixed = 0.8f};
    juce::MemoryBlock chunk(&chunkVoice, sizeof(chunkVoice));

    auto model = externalDeviceSaving(1.0f, 0.7f);
    model.pluginState = chunk.toBase64Encoding();

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    REQUIRE(result.device != nullptr);
    CHECK(raw->tone->getValue() == Catch::Approx(0.7f));
}

TEST_CASE("Automation moves a parameter the chunk set", "[engine][external]") {
    // A lane, a macro or a modifier takes the parameter away from whatever the
    // plugin's state left there, and keeps it: a value the lane passes through
    // on its way back is still the lane's, not the plugin's.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    const StubPlugin::State chunkVoice{.tone = 0.9f, .fixed = 0.8f};
    juce::MemoryBlock chunk(&chunkVoice, sizeof(chunkVoice));

    auto model = externalDeviceSaving(1.0f, 0.25f);
    model.pluginState = chunk.toBase64Encoding();

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    const auto context = contextFor();
    result.device->prepare(context);
    Block block(context, 2);

    // A lane holding the parameter somewhere else.
    ParamArena automated({0.0f, 1.0f, 1.0f, 0.5f});
    auto moved = block.deviceBlock(automated.params(context.maxBlockSize));
    result.device->process(moved);

    CHECK(raw->tone->getValue() == Catch::Approx(0.5f));

    // And back to where the project saved it, which is a position the lane
    // passes through rather than a parameter nobody has touched: it must arrive
    // there rather than stay at 0.5.
    ParamArena returned({0.0f, 1.0f, 1.0f, 0.25f});
    auto back = block.deviceBlock(returned.params(context.maxBlockSize));
    result.device->process(back);

    CHECK(raw->tone->getValue() == Catch::Approx(0.25f));
}

TEST_CASE("A device with no saved state keeps the plugin's defaults", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), externalDevice());

    REQUIRE(result.device != nullptr);
    CHECK(raw->stateRestores == 0);
}

TEST_CASE("Saved state that is not a chunk this plugin understands is refused",
          "[engine][external]") {
    // A chunk written by another plugin, or by another version of this one. The
    // plugin is handed it and declines; what it must not do is take half of it.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto model = externalDevice();
    model.pluginState = "not base64 at all !!";

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    REQUIRE(result.device != nullptr);
    CHECK(raw->stateRestores == 0);
    CHECK(raw->fixed->getValue() == Catch::Approx(0.0f));
}

TEST_CASE("The asynchronous entry point reports a missing plugin the same way",
          "[engine][external]") {
    // Both entry points end at the same transaction, which is what the cases
    // above drive; what is its own here is that a failure reaches the caller
    // through the callback rather than through a return value, so a session
    // that asked for a plugin it does not have is told rather than left
    // waiting.
    juce::KnownPluginList empty;
    juce::AudioPluginFormatManager formats;

    magda::DeviceInfo missing = externalDevice();
    missing.name = "Massive X";
    missing.fileOrIdentifier = "/Library/MassiveX.vst3";

    const adapter::ExternalPluginServices services{
        .formats = &formats, .knownPlugins = &empty, .context = contextFor()};

    bool called = false;
    adapter::ExternalDeviceResult delivered;
    adapter::createEngineExternalDeviceAsync(missing, services, false,
                                             [&](adapter::ExternalDeviceResult result) {
                                                 called = true;
                                                 delivered = std::move(result);
                                             });

    REQUIRE(called);
    CHECK(delivered.device == nullptr);
    CHECK(delivered.failure.contains("Massive X"));
    CHECK(delivered.restoredParameters.empty());
}
