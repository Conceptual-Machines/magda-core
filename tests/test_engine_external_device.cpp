#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "exec/EngineDevice.hpp"
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

    static BusesProperties busesFor(int inputs, int outputs, int sidechain) {
        auto buses = BusesProperties()
                         .withInput("Input", setFor(inputs), true)
                         .withOutput("Output", setFor(outputs), true);

        if (sidechain > 0)
            buses = buses.withInput("Sidechain", setFor(sidechain), true);

        return buses;
    }

    StubPlugin(int inputs, int outputs, int sidechain)
        : AudioPluginInstance(busesFor(inputs, outputs, sidechain)) {
        auto gainParameter = std::make_unique<StubParameter>("gain", "Gain", 1.0f, true);
        gain = gainParameter.get();
        addHostedParameter(std::move(gainParameter));

        addHostedParameter(std::make_unique<StubParameter>("fixed", "Fixed", 0.0f, false));

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

        const auto level = gain->getValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
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

    void getStateInformation(juce::MemoryBlock&) override {}

    void setStateInformation(const void*, int) override {}

    /// The per-channel offset the output carries, so a test can name which of
    /// the plugin's channels it is reading.
    static constexpr float kChannelMarker = 0.01f;

    StubParameter* gain = nullptr;
    StubParameter* tone = nullptr;

    bool emitsMidi = false;

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

    for (int pass = 0; pass < 3; ++pass) {
        auto deviceBlock = block.deviceBlock(held.params(context.maxBlockSize));
        device.process(deviceBlock);
    }

    // Once, on the block that moved it. The fork writes a plugin parameter only
    // when the value differs from what the plugin already reports, because a
    // plugin is entitled to treat every write as a gesture: one that rebuilds a
    // filter or repaints an editor on each would do it every block on a
    // parameter nobody touched.
    CHECK(raw->gain->writes == 1);

    ParamArena moved({0.0f, 1.0f, 0.7f, 0.0f});
    auto movedBlock = block.deviceBlock(moved.params(context.maxBlockSize));
    device.process(movedBlock);

    CHECK(raw->gain->writes == 2);
}

TEST_CASE("A parameter the table does not carry is left where it was", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();
    raw->tone->setValue(0.9f);

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    // A window that stops before the tone parameter: a project saved against a
    // build of the plugin that did not have it.
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
