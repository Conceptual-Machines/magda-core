#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "exec/EngineDevice.hpp"
#include "magda/daw/audio/Vst3Preset.hpp"
#include "magda/daw/audio/plugin_manager/ExternalPluginLookup.hpp"
#include "magda/daw/audio/plugin_manager/ExternalPluginState.hpp"
#include "magda/daw/audio/plugins/engine/DeviceControl.hpp"
#include "magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "magda/daw/audio/plugins/engine/EngineExternalDevice.hpp"
#include "magda/daw/audio/plugins/engine/PluginAssignments.hpp"
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

/// The class id a stub VST3's preset header carries, which is the identity
/// another host matches on and the one thing MAGDA reads out of the header.
constexpr const char* kStubVst3ClassId = "0123456789ABCDEF0123456789ABCDEF";

/// A .vstpreset-shaped block: Steinberg's header, then @p bytes of payload.
///
/// The header is what makes this a preset rather than a chunk -- the magic, the
/// class id at offset eight, the chunk-list offset at forty -- and a stub that
/// skipped it would let a bug that reads the wrong offset pass.
juce::MemoryBlock vst3PresetOf(const void* payload, size_t bytes) {
    constexpr size_t kHeaderBytes = 48;

    juce::MemoryBlock preset;
    preset.setSize(kHeaderBytes, true);

    auto* header = static_cast<char*>(preset.getData());
    std::memcpy(header, "VST3", 4);
    std::memcpy(header + magda::vst3::kVst3PresetClassIdOffset, kStubVst3ClassId,
                magda::vst3::kVst3PresetClassIdLength);

    preset.append(payload, bytes);
    return preset;
}

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
        return takesMidi;
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
        // What a real plugin spends time doing here, and what a test needs in
        // order to see two readers inside it at once if anything let them be.
        if (whileDescribingItself)
            whileDescribingItself();

        if (throwsSavingState)
            throw std::runtime_error("plugin state handler failed");

        // A plugin whose whole voice is its parameters has nothing to add, and
        // plenty of real ones are like that.
        if (savesNothing)
            return;

        const State state{.tone = tone->getValue(), .fixed = fixed->getValue()};
        destination.replaceAll(&state, sizeof(state));
    }

    void setStateInformation(const void* data, int size) override {
        if (throwsOnState) {
            // Half of a restore, then the throw. A plugin that got this far has
            // changed things no parameter write puts back.
            if (mutatesBeforeThrowing)
                tone->setValue(0.42f);

            throw std::runtime_error("plugin state handler failed");
        }

        if (size != static_cast<int>(sizeof(State)))
            return;

        State state{};
        std::memcpy(&state, data, sizeof(state));
        tone->setValue(state.tone);
        fixed->setValue(state.fixed);
        ++stateRestores;

        // A patch that changes the plugin's own topology, which real ones do:
        // a sampler whose preset turns its drum outs on, an instrument that
        // goes stereo for one program and mono for another. Anything that read
        // the width before the chunk was applied is now describing a layout
        // this instance no longer has.
        if (widensOutputOnState) {
            auto layout = getBusesLayout();
            layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
            setBusesLayout(layout);
        }
    }

    /**
     * @brief The stub as a VST3, for the one record that is not a chunk.
     *
     * A portable .vstpreset is reached through the format's own client rather
     * than through setStateInformation, and whether an instance answers this
     * visit at all is the only way a host can ask whether it is a VST3. So the
     * stub answers it or does not, and a test picks which.
     *
     * The patch it carries is the same State the chunk carries, behind a real
     * preset header: what is being asserted is which door the host went
     * through, not that two serialisations differ.
     */
    struct Vst3Extension final : juce::ExtensionsVisitor::VST3Client {
        explicit Vst3Extension(const StubPlugin& owner) : plugin(owner) {}

        Steinberg::Vst::IComponent* getIComponentPtr() const noexcept override {
            return nullptr;
        }

        juce::MemoryBlock getPreset() const override;
        bool setPreset(const juce::MemoryBlock& data) const override;

        const StubPlugin& plugin;
    };

    void getExtensions(juce::ExtensionsVisitor& visitor) const override {
        // Nothing is visited for a plugin of another format, which is how the
        // host tells a VST3 from an AU without being told.
        if (!isVst3)
            return;

        const Vst3Extension client(*this);
        visitor.visitVST3Client(client);
    }

    /// The per-channel offset the output carries, so a test can name which of
    /// the plugin's channels it is reading.
    static constexpr float kChannelMarker = 0.01f;

    StubParameter* gain = nullptr;
    StubParameter* tone = nullptr;
    StubParameter* fixed = nullptr;

    int stateRestores = 0;

    bool emitsMidi = false;

    /// Whether the processor advertises MIDI input at all. A real plugin can
    /// say no here while the incumbent engine still takes MIDI input for it.
    bool takesMidi = true;

    /// Whether restoring the chunk widens the main output bus (see
    /// setStateInformation).
    bool widensOutputOnState = false;

    /// Third-party code handed a chunk it cannot read. Some throw.
    bool throwsOnState = false;

    /// And some get part of the way through first.
    bool mutatesBeforeThrowing = false;

    /// And some throw describing themselves rather than reading a description.
    bool throwsSavingState = false;

    /// Run inside getStateInformation, for a test that has something to observe
    /// about when a read is in progress.
    std::function<void()> whileDescribingItself;

    /// And some have nothing beyond their parameters to describe.
    bool savesNothing = false;

    /// A VST3 that cannot write its patch out, which is the case a caller must
    /// not read as "not a VST3".
    bool savesNoPreset = false;

    /// Whether this stub is a VST3 at all (see getExtensions).
    bool isVst3 = false;

    /// Whether its VST3 client takes the preset it is handed. A real one
    /// refuses a patch that was written for a different plugin.
    bool acceptsPreset = true;

    /// And whether it has already changed something by the time it refuses.
    bool mutatesBeforeRefusing = false;

    /// How many presets reached it, which is how a test tells the portable
    /// record's door from the chunk's.
    mutable int presetApplies = 0;

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

juce::MemoryBlock StubPlugin::Vst3Extension::getPreset() const {
    if (plugin.savesNoPreset)
        return {};

    const State state{.tone = plugin.tone->getValue(), .fixed = plugin.fixed->getValue()};
    return vst3PresetOf(&state, sizeof(state));
}

bool StubPlugin::Vst3Extension::setPreset(const juce::MemoryBlock& data) const {
    if (!plugin.acceptsPreset) {
        // Half of it, then the refusal. Steinberg's loader restores the
        // component's state and then the controller's and returns false when the
        // second fails after the first, so this is what a real refusal can look
        // like from outside.
        if (plugin.mutatesBeforeRefusing)
            plugin.tone->setValue(0.42f);

        return false;
    }

    constexpr size_t kHeaderBytes = 48;
    if (data.getSize() != kHeaderBytes + sizeof(State))
        return false;

    State state{};
    std::memcpy(&state, static_cast<const char*>(data.getData()) + kHeaderBytes, sizeof(state));
    plugin.tone->setValue(state.tone);
    plugin.fixed->setValue(state.fixed);
    ++plugin.presetApplies;
    return true;
}

/// A real AudioPluginFormat seam for the asynchronous factory test. The
/// format manager still performs its normal lookup and message-thread delivery;
/// only the binary behind the description is local to the test.
class StubFormat final : public juce::AudioPluginFormat {
  public:
    juce::String getName() const override {
        return "StubFormat";
    }

    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>&,
                             const juce::String&) override {}

    bool fileMightContainThisPluginType(const juce::String&) override {
        return true;
    }

    juce::String getNameOfPluginFromIdentifier(const juce::String&) override {
        return "Stub";
    }

    bool pluginNeedsRescanning(const juce::PluginDescription&) override {
        return false;
    }

    bool doesPluginStillExist(const juce::PluginDescription&) override {
        return true;
    }

    bool canScanForPlugins() const override {
        return false;
    }

    bool isTrivialToScan() const override {
        return true;
    }

    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath&, bool, bool) override {
        return {};
    }

    juce::FileSearchPath getDefaultLocationsToSearch() override {
        return {};
    }

    bool requiresUnblockedMessageThreadDuringCreation(
        const juce::PluginDescription&) const override {
        return false;
    }

    juce::PluginDescription requested;

  private:
    void createPluginInstance(const juce::PluginDescription& description, double, int,
                              PluginCreationCallback callback) override {
        requested = description;
        callback(std::make_unique<StubPlugin>(2, 2, 0), {});
    }
};

magda::engine::RenderContext contextFor(int channels = 2, int blockSize = 64) {
    return {.sampleRate = 48000.0, .maxBlockSize = blockSize, .numChannels = channels};
}

/// A device whose parameters are the fork's list: the wrapper pair at zero and
/// one, then the plugin's automatable parameters.
/**
 * @brief Ask @p plane for a capture and wait for the answer (#2270).
 *
 * Every answer arrives on the plane's executor, which is not this thread, so a
 * test that read its result straight after the call would be reading it before
 * it was written. Waiting for it here is what a host with something else to do
 * would not have to do, and what a test that has nothing else to do must.
 */
adapter::CaptureOutcome capture(adapter::DeviceControlPlane& plane, magda::engine::DeviceKey key) {
    std::promise<adapter::CaptureOutcome> answer;
    auto answered = answer.get_future();

    plane.captureState(
        key, [&answer](adapter::CaptureOutcome outcome) { answer.set_value(std::move(outcome)); });

    return answered.get();
}

/// A registry over one device, which is what a runtime hands a plane (#2270).
///
/// It owns the device, the way a runtime owns the ones it runs, and hands out a
/// lease rather than a pointer: what keeps a device alive through a capture is
/// the lease, so this can let go of its own copy mid-operation and the capture
/// carries on with the plugin it was already reading.
class OneDeviceRegistry final : public adapter::DeviceRegistry {
  public:
    /// @p asked counts the lookups, for the cases about whether there should
    /// have been one. It belongs to the test rather than to this object,
    /// because the case that cares is the one where this object is gone.
    OneDeviceRegistry(magda::engine::DeviceKey key,
                      std::shared_ptr<adapter::EngineExternalDevice> device,
                      std::atomic<int>* asked = nullptr)
        : key_(key), device_(std::move(device)), asked_(asked) {}

    std::shared_ptr<adapter::EngineExternalDevice> find(
        magda::engine::DeviceKey key) const override {
        if (asked_ != nullptr)
            ++(*asked_);

        return key == key_ ? device_ : nullptr;
    }

    /// Let go of the device while keeping the registry, which is the state a
    /// chain edit leaves behind: the runtime is still there and the device is
    /// not its any more.
    void release() {
        device_.reset();
    }

  private:
    magda::engine::DeviceKey key_;
    std::shared_ptr<adapter::EngineExternalDevice> device_;
    std::atomic<int>* asked_ = nullptr;
};

/// A registry with nothing in it.
class EmptyRegistry final : public adapter::DeviceRegistry {
  public:
    std::shared_ptr<adapter::EngineExternalDevice> find(magda::engine::DeviceKey) const override {
        return nullptr;
    }
};

/// The device an adaptation produced, owned the way a runtime owns one.
///
/// The factory hands back the base type by unique_ptr, and what a registry
/// lends out is a lease on the external device underneath it, so the cast and
/// the change of ownership happen once here rather than in every case.
std::shared_ptr<adapter::EngineExternalDevice> ownedExternalDevice(
    adapter::ExternalDeviceResult& result) {
    auto* external = dynamic_cast<adapter::EngineExternalDevice*>(result.device.get());
    if (external == nullptr)
        return nullptr;

    result.device.release();
    return std::shared_ptr<adapter::EngineExternalDevice>(external);
}

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

juce::PluginDescription descriptionFor(const magda::DeviceInfo& device) {
    auto description = magda::describeSavedPlugin(device);
    description.uniqueId = device.name.hashCode();
    return description;
}

/// The key a device holds while these tests keep it in the main FX section.
magda::engine::DeviceKey keyFor(const magda::DeviceInfo& device) {
    return {magda::ChainSegment::Fx, device.id};
}

/// Read a plugin and write what it holds into @p device, which is what a caller
/// does with the two halves when it has nothing to check between them.
bool captureInto(juce::AudioPluginInstance& instance, magda::DeviceInfo& device) {
    const auto snapshot = magda::captureExternalPluginState(instance);
    if (!snapshot.has_value())
        return false;

    magda::applyCapturedPluginState(device, *snapshot);
    return true;
}

/// The device registered as live, and the request a load for it carries.
///
/// Registration is the caller's job in production too: a device that becomes
/// live is assigned a handle, and nothing about the DeviceInfo grants one.
adapter::RequestedPlugin requestFor(
    adapter::PluginAssignments& assignments, const magda::DeviceInfo& device,
    const std::optional<juce::PluginDescription>& resolved = std::nullopt) {
    const auto description = resolved.value_or(descriptionFor(device));
    const auto key = keyFor(device);
    assignments.ensureAssignment(key);
    return {.assignment = assignments.request(key),
            .displayName = device.name,
            .resolvedIsInstrument = description.isInstrument};
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
        info.seconds.end = context.maxBlockSize / context.sampleRate;
        info.beats.end = info.seconds.end * 2.0;
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

TEST_CASE("A plugin with no MIDI of its own hands none back", "[engine][external][2348]") {
    // JUCE's AU path only clears the shared buffer under wantsMidiMessages, so
    // a plugin that neither accepts nor produces MIDI leaves the input sitting
    // in it. Writing that back doubles every note behind a ChainMidiMerge.
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();
    raw->takesMidi = false;
    raw->emitsMidi = false;

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

    CHECK(out.getNumEvents() == 0);
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
    block.info.beats.start = 7.0;  // the second beat of the third bar, in 3/4
    block.info.beats.end = 7.5;
    block.info.seconds.start = tempo.beatToTime(7.0);
    block.info.seconds.end = tempo.beatToTime(7.5);
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

TEST_CASE("The plugin is told the section the block renders in", "[engine][external][2336]") {
    // A musical position within a hundredth of a sample of the cursor is the
    // cursor as far as the transport is concerned, so a block can open that
    // close before a section boundary while every sample it renders is past it
    // (BlockInfo::openingBeat). A plugin is told one bpm and one signature for
    // the whole call, and taking them from the block's first beat would run the
    // call on the section it had already left.
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    // 120 to 60 at beat 4, and four four to three four with it. A step is two
    // changes at the one beat, which is how it is written rather than a ramp.
    const magda::engine::TempoMap tempo({{.startBeat = 0.0, .bpm = 120.0},
                                         {.startBeat = 4.0, .bpm = 120.0},
                                         {.startBeat = 4.0, .bpm = 60.0}},
                                        {{.startBeat = 0.0, .numerator = 4, .denominator = 4},
                                         {.startBeat = 4.0, .numerator = 3, .denominator = 4}});

    // A hundredth of a sample before the boundary, at 120 bpm and 44.1 kHz.
    constexpr auto kNudge = 0.01 / 22050.0;

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);
    block.info.beats.start = 4.0 - kNudge;
    block.info.beats.end = block.info.beats.start + 0.25;
    block.info.seconds.start = tempo.beatToTime(block.info.beats.start);
    block.info.seconds.end = tempo.beatToTime(block.info.beats.end);
    block.info.tempo = &tempo;

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    const auto& position = raw->positionSeen;

    // The section the block is in, not the one its first beat is in.
    REQUIRE(position.getBpm().hasValue());
    CHECK(*position.getBpm() == Catch::Approx(60.0));
    REQUIRE(position.getTimeSignature().hasValue());
    CHECK(position.getTimeSignature()->numerator == 3);
    CHECK(position.getTimeSignature()->denominator == 4);

    // And the bar that section starts, rather than the four four bar the
    // block's first beat is still a whole bar into.
    REQUIRE(position.getPpqPositionOfLastBarStart().hasValue());
    CHECK(*position.getPpqPositionOfLastBarStart() == Catch::Approx(4.0));

    // Where the block is stays its own first sample, which is what the plugin
    // is being handed.
    REQUIRE(position.getPpqPosition().hasValue());
    CHECK(*position.getPpqPosition() == Catch::Approx(block.info.beats.start));
    REQUIRE(position.getTimeInSeconds().hasValue());
    CHECK(*position.getTimeInSeconds() == Catch::Approx(block.info.seconds.start));
}

TEST_CASE("A block straddling a bar line reports the bar it began in", "[engine][external][2336]") {
    // The bar a block's first sample is in is what the plugin is told, and a
    // block is not cut at bar lines, so a long one straddles one. Reporting the
    // bar its middle is in would make what the plugin hears depend on how the
    // host sized the callback: the same first sample, two answers.
    auto plugin = std::make_unique<StubPlugin>();
    auto* raw = plugin.get();

    adapter::EngineExternalDevice device(std::move(plugin), externalDevice(), false);
    const auto context = contextFor();
    device.prepare(context);

    const magda::engine::TempoMap tempo({{.startBeat = 0.0, .bpm = 90.0}},
                                        {{.startBeat = 0.0, .numerator = 3, .denominator = 4}});

    ParamArena arena({0.0f, 1.0f, 1.0f, 0.0f});
    Block block(context, 2);

    // Opens just before the bar line at beat 6 and runs past it.
    block.info.beats.start = 5.99;
    block.info.beats.end = 6.25;
    block.info.seconds.start = tempo.beatToTime(block.info.beats.start);
    block.info.seconds.end = tempo.beatToTime(block.info.beats.end);
    block.info.tempo = &tempo;

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    device.process(deviceBlock);

    const auto& position = raw->positionSeen;

    // The bar the block began in, which is the one before the line it crosses.
    REQUIRE(position.getPpqPositionOfLastBarStart().hasValue());
    CHECK(*position.getPpqPositionOfLastBarStart() == Catch::Approx(3.0));
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

    // And which plugin went without, because a caller collecting these has a
    // project's worth of them.
    CHECK(result.failure.contains(externalDevice().name));
}

TEST_CASE("Resolution returns planning facts without rewriting the saved assignment",
          "[engine][external]") {
    auto model = externalDevice();
    model.name = "My imported synth";  // user/import display label
    model.pluginId = "saved-plugin-key";
    model.uniqueId = "saved-preference-key";
    model.manufacturer = "Saved vendor";
    model.fileOrIdentifier = "/Library/Stub.vst3";
    model.audioInputChannels = 2;
    model.audioOutputChannels = 2;

    auto installed = descriptionFor(model);
    installed.name = "Scanner's canonical name";
    installed.manufacturerName = "Installed vendor";
    installed.pluginFormatName = "UnknownFutureFormat";
    installed.isInstrument = true;
    installed.numInputChannels = 0;
    installed.numOutputChannels = 0;

    juce::KnownPluginList known;
    known.addType(installed);
    juce::AudioPluginFormatManager formats;
    const adapter::ExternalPluginServices services{
        .formats = &formats, .knownPlugins = &known, .context = contextFor()};

    const auto resolved = adapter::resolveEngineExternalPlugin(model, services);
    REQUIRE(resolved);

    // The persistent assignment is untouched, including preference keys,
    // display text, placement role and widths last observed from a live host.
    CHECK(model.name == "My imported synth");
    CHECK(model.pluginId == "saved-plugin-key");
    CHECK(model.uniqueId == "saved-preference-key");
    CHECK(model.manufacturer == "Saved vendor");
    CHECK_FALSE(model.isInstrument);
    CHECK(model.deviceType == magda::DeviceType::Effect);
    CHECK(model.audioInputChannels == 2);
    CHECK(model.audioOutputChannels == 2);

    // The transient copy carries only the role needed for placement validation
    // and plan compilation. Zero/default scan buses are not treated as live.
    CHECK(resolved.planDevice.name == model.name);
    CHECK(resolved.planDevice.pluginId == model.pluginId);
    CHECK(resolved.planDevice.uniqueId == model.uniqueId);
    CHECK(resolved.planDevice.format == model.format);
    CHECK(resolved.planDevice.isInstrument);
    CHECK(resolved.planDevice.deviceType == magda::DeviceType::Instrument);
    CHECK(resolved.planDevice.audioInputChannels == 2);
    CHECK(resolved.planDevice.audioOutputChannels == 2);

    auto analysis = model;
    analysis.deviceType = magda::DeviceType::Analysis;
    const auto resolvedAnalysis = adapter::resolveEngineExternalPlugin(analysis, services);
    REQUIRE(resolvedAnalysis);
    CHECK(resolvedAnalysis.planDevice.deviceType == magda::DeviceType::Analysis);
    CHECK_FALSE(resolvedAnalysis.planDevice.isInstrument);
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

TEST_CASE("A plugin that throws restoring its state is not published", "[engine][external]") {
    // A plugin's state handler is third-party code, and a corrupt chunk is the
    // input it is least likely to have been tested against. Catching the throw
    // keeps the host alive and tells us nothing about the plugin: it may have
    // loaded half a preset, switched program and swapped a sample first, and no
    // amount of writing parameters puts any of that back.
    //
    // So the instance is dropped rather than published. What must not happen is
    // the third step running over it, because that would hand the caller a half
    // restored plugin's values as the ones to write into the project.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->throwsOnState = true;
    raw->mutatesBeforeThrowing = true;

    const StubPlugin::State chunkVoice{.tone = 0.9f, .fixed = 0.8f};
    juce::MemoryBlock chunk(&chunkVoice, sizeof(chunkVoice));

    auto model = externalDeviceSaving(1.0f, 0.7f);
    model.pluginState = chunk.toBase64Encoding();

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("failed while restoring"));
    CHECK(result.restoredParameters.empty());
}

TEST_CASE("The value a half restored plugin was left on never reaches the model",
          "[engine][external]") {
    // The same case, said from the model's side, which is where the damage
    // would be: a snapshot of a plugin that threw partway would be written into
    // the project as the corrected values and saved over the real ones.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->throwsOnState = true;
    raw->mutatesBeforeThrowing = true;

    const StubPlugin::State chunkVoice{.tone = 0.9f, .fixed = 0.8f};
    juce::MemoryBlock chunk(&chunkVoice, sizeof(chunkVoice));

    auto model = externalDeviceSaving(1.0f, 0.7f);
    model.pluginState = chunk.toBase64Encoding();

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    magda::applyRestoredParameters(model, result.restoredParameters);

    // 0.42 is where the plugin was left. The project still says 0.7.
    CHECK(model.parameters[1].currentValue == Catch::Approx(0.7f));
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

TEST_CASE("Successful adaptation reports live buses and MIDI capabilities", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 1, 2);
    plugin->emitsMidi = true;

    auto model = externalDevice();
    model.audioInputChannels = 9;
    model.audioOutputChannels = 9;

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    REQUIRE(result.device != nullptr);
    REQUIRE(result.resolvedDevice.has_value());
    CHECK(result.resolvedDevice->audioInputChannels == 3);
    CHECK(result.resolvedDevice->audioOutputChannels == 6);
    CHECK(result.resolvedDevice->canReceiveMidi);
    CHECK(result.resolvedDevice->producesMidi);

    // Publication belongs to the caller and only happens after success.
    CHECK(model.audioInputChannels == 9);
    CHECK(model.audioOutputChannels == 9);
}

TEST_CASE("A plugin that does not advertise MIDI input cannot clear the project's flag",
          "[engine][external]") {
    // The incumbent engine takes MIDI input for plugins whose AudioProcessor
    // says no, so the model's true is evidence the instance does not have.
    // Assigning over it would leave PlanCompiler routing no MIDI to a device
    // that had been receiving it.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    plugin->takesMidi = false;

    auto model = externalDevice();
    model.canReceiveMidi = true;

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    REQUIRE(result.device != nullptr);
    REQUIRE(result.resolvedDevice.has_value());
    CHECK(result.resolvedDevice->canReceiveMidi);
}

TEST_CASE("An instrument keeps its MIDI flag off however the instance answers",
          "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto model = externalDevice();
    model.isInstrument = true;

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    REQUIRE(result.resolvedDevice.has_value());
    CHECK_FALSE(result.resolvedDevice->canReceiveMidi);
}

TEST_CASE("Channel widths are read after the plugin's own state is restored",
          "[engine][external]") {
    // A chunk is free to change the layout it is restored into. The widths the
    // plan compiles from have to be the ones the instance ends up with, since
    // EngineExternalDevice::prepare() goes on to process that layout.
    auto plugin = std::make_unique<StubPlugin>(2, 1, 0);
    plugin->widensOutputOnState = true;
    auto* raw = plugin.get();

    const StubPlugin::State saved{.tone = 0.5f, .fixed = 0.5f};
    juce::MemoryBlock chunk(&saved, sizeof(saved));

    auto model = externalDevice();
    model.pluginState = chunk.toBase64Encoding();

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    REQUIRE(result.device != nullptr);
    REQUIRE(result.resolvedDevice.has_value());
    CHECK(raw->stateRestores == 1);
    CHECK(raw->getTotalNumOutputChannels() == 2);
    CHECK(result.resolvedDevice->audioOutputChannels == 2);
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

// ============================================================================
// The portable record: a .vstpreset a project was imported with (#2244)
// ============================================================================

TEST_CASE("An imported project's .vstpreset is the overlay", "[engine][external]") {
    // A DAWproject carries a VST3's patch as a .vstpreset, because no other
    // host has MAGDA's chunk format. The device it lands on therefore has a
    // portable record and no native one, and a load that only knew about chunks
    // would render the plugin's initialised voice.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->isVst3 = true;

    const StubPlugin::State imported{.tone = 0.9f, .fixed = 0.8f};
    const auto preset = vst3PresetOf(&imported, sizeof(imported));

    auto model = externalDevice();
    model.vst3Preset = juce::Base64::toBase64(preset.getData(), preset.getSize());

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    // Through the format's own door, not the chunk's.
    CHECK(raw->presetApplies == 1);
    CHECK(raw->stateRestores == 0);
    CHECK(raw->fixed->getValue() == Catch::Approx(0.8f));
}

TEST_CASE("An applied preset is spent rather than kept", "[engine][external]") {
    // The patch is the instance's now, and the next save writes it out as a
    // chunk. A project that kept the preset would apply it again on the load
    // after that, over whatever had been saved in between.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    plugin->isVst3 = true;

    const StubPlugin::State imported{.tone = 0.9f, .fixed = 0.8f};
    const auto preset = vst3PresetOf(&imported, sizeof(imported));

    auto model = externalDevice();
    model.vst3Preset = juce::Base64::toBase64(preset.getData(), preset.getSize());

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);
    REQUIRE(result.resolvedDevice.has_value());
    CHECK(result.resolvedDevice->vst3Preset.isEmpty());

    // And the model the caller was handed is the only thing that changed: a
    // load that never got published leaves the project holding its preset.
    CHECK(model.vst3Preset.isNotEmpty());
}

TEST_CASE("A refused preset with nothing behind it is not published", "[engine][external]") {
    // Steinberg's loader restores the component's state before the controller's
    // and returns false when the second fails after the first, so a refusal is
    // not a no-op: the plugin can be holding half the imported patch. An import
    // is exactly the project with no chunk to overwrite that with, and the
    // parameter array underneath describes what the project believed rather than
    // what the instance now is.
    //
    // So it is refused the same way a chunk that threw is, rather than published
    // as a baseline nobody can describe.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->isVst3 = true;
    raw->acceptsPreset = false;
    raw->mutatesBeforeRefusing = true;

    const StubPlugin::State imported{.tone = 0.9f, .fixed = 0.8f};
    const auto preset = vst3PresetOf(&imported, sizeof(imported));

    auto model = externalDevice();
    model.vst3Preset = juce::Base64::toBase64(preset.getData(), preset.getSize());

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("restoring its own saved state"));

    // And the value it was left on never reached the model.
    CHECK(result.restoredParameters.empty());
}

TEST_CASE("A plugin of another format refusing nothing is still a baseline", "[engine][external]") {
    // The other side of the case above. A project can carry a portable preset
    // and be opened against an AU, where nothing is asked and nothing moves, and
    // that device still has its parameter array to stand on.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->isVst3 = false;

    const StubPlugin::State imported{.tone = 0.9f, .fixed = 0.8f};
    const auto preset = vst3PresetOf(&imported, sizeof(imported));

    auto model = externalDeviceSaving(1.0f, 0.7f);
    model.vst3Preset = juce::Base64::toBase64(preset.getData(), preset.getSize());

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);

    REQUIRE(result.device != nullptr);
    CHECK(raw->tone->getValue() == Catch::Approx(0.7f));
}

TEST_CASE("A preset the plugin refuses falls through to the chunk", "[engine][external]") {
    // A patch written for another plugin, handed to this one. The project's own
    // native record is right there and is the only thing that describes this
    // device, so refusing the preset must not also discard it.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->isVst3 = true;
    raw->acceptsPreset = false;

    const StubPlugin::State imported{.tone = 0.9f, .fixed = 0.8f};
    const auto preset = vst3PresetOf(&imported, sizeof(imported));

    const StubPlugin::State saved{.tone = 0.3f, .fixed = 0.2f};
    juce::MemoryBlock chunk(&saved, sizeof(saved));

    auto model = externalDevice();
    model.vst3Preset = juce::Base64::toBase64(preset.getData(), preset.getSize());
    model.pluginState = chunk.toBase64Encoding();

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    CHECK(raw->presetApplies == 0);
    CHECK(raw->stateRestores == 1);
    CHECK(raw->fixed->getValue() == Catch::Approx(0.2f));

    // Nothing was consumed, so the project keeps the preset for a machine whose
    // copy of the plugin does take it.
    REQUIRE(result.resolvedDevice.has_value());
    CHECK(result.resolvedDevice->vst3Preset.isNotEmpty());
}

TEST_CASE("A plugin that is not a VST3 never sees a preset", "[engine][external]") {
    // Format is not something a host reads off a description here: an instance
    // either answers the VST3 extension or it does not, and one that does not
    // has only the chunk.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->isVst3 = false;

    const StubPlugin::State imported{.tone = 0.9f, .fixed = 0.8f};
    const auto preset = vst3PresetOf(&imported, sizeof(imported));

    auto model = externalDeviceSaving(1.0f, 0.7f);
    model.vst3Preset = juce::Base64::toBase64(preset.getData(), preset.getSize());

    const auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    CHECK(raw->presetApplies == 0);
    CHECK(raw->stateRestores == 0);
    CHECK(raw->tone->getValue() == Catch::Approx(0.7f));  // the array, and nothing over it
}

// ============================================================================
// Saving: what a project keeps about a plugin the native engine holds (#2244)
// ============================================================================

TEST_CASE("A block that arrives during a state read does not reach the plugin",
          "[engine][external][state]") {
    // Suspension is worth nothing unless the host honours it. A save reads the
    // plugin's chunk, its parameters and its preset from the message thread with
    // the plugin suspended, and a block that ran the plugin anyway would have
    // third-party DSP and a third-party state handler in the same instance at
    // the same time.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), externalDevice());
    REQUIRE(result.device != nullptr);

    const auto context = contextFor();
    result.device->prepare(context);

    // Suspended, the way captureExternalPluginState() suspends it for the duration
    // of its read.
    raw->suspendProcessing(true);

    const auto gainWrites = raw->gain->writes;
    const auto toneWrites = raw->tone->writes;
    const auto gainBefore = raw->gain->getValue();

    // Values the plugin is not already holding, so a write that got through
    // would be a write the parameter records. Supplying what it already has
    // would let this pass with the gate around processBlock alone, which is
    // where the hole was: writeParameters() runs before it.
    ParamArena arena({0.0f, 1.0f, 0.3f, 0.7f});
    Block block(context, 2);
    block.fill(0.5f);

    auto deviceBlock = block.deviceBlock(arena.params(context.maxBlockSize));
    result.device->process(deviceBlock);

    // Nothing plugin-facing happened. Not its DSP, and not its parameters
    // either: a capture reads the chunk and then the parameter values, and a
    // block that moved one between the two saves a pair that disagrees with
    // itself.
    CHECK(raw->samplesSeen == 0);
    CHECK(raw->gain->writes == gainWrites);
    CHECK(raw->tone->writes == toneWrites);
    CHECK(raw->gain->getValue() == Catch::Approx(gainBefore));

    // And the block came out as it went in, which is the passthrough the plan
    // already gives a Device op with no instance bound. The stub would have
    // added its per-channel marker had it run.
    CHECK(block.buffer.getSample(0, 0) == Catch::Approx(0.5f));

    raw->suspendProcessing(false);
}

TEST_CASE("Reading a plugin does not write the project", "[engine][external][state]") {
    // The two halves are separate so a caller can put a question between them.
    // The one it has to ask is whether the device it read is still the device it
    // is about to write, and a capture that wrote the model on the way past
    // would have answered it too late. The same seam is what a plugin living in
    // another process would need: a snapshot can cross, a plugin cannot.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->gain->setValue(0.6f);

    auto model = externalDeviceSaving(0.1f, 0.2f);
    model.pluginState = "the project as it was";

    const auto snapshot = magda::captureExternalPluginState(*raw);
    REQUIRE(snapshot.has_value());

    // The plugin has been read and the project has not been touched.
    CHECK(model.pluginState == "the project as it was");
    CHECK(model.parameters[0].currentValue == Catch::Approx(0.1f));

    // And everything read is in the snapshot, waiting for a caller that wants it.
    CHECK(snapshot->pluginState.isNotEmpty());

    magda::applyCapturedPluginState(model, *snapshot);
    CHECK(model.pluginState != "the project as it was");
    CHECK(model.parameters[0].currentValue == Catch::Approx(0.6f));
}

TEST_CASE("A save writes the chunk the incumbent would have written", "[engine][external][state]") {
    // The round trip that matters during the dual-engine release: a project
    // saved while the native engine held the instance has to open under the
    // fork, which means the same base64 in the same field.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->tone->setValue(0.9f);
    raw->fixed->setValue(0.8f);

    auto model = externalDevice();
    REQUIRE(captureInto(*raw, model));

    // What the fork writes for the same instance: getStateInformation, in
    // juce::MemoryBlock's own base64.
    juce::MemoryBlock expected;
    raw->getStateInformation(expected);
    CHECK(model.pluginState == expected.toBase64Encoding());

    // And a second instance restored from it holds what the first one held,
    // including the parameter no host may automate.
    auto reopened = std::make_unique<StubPlugin>(2, 2, 0);
    auto* reopenedRaw = reopened.get();
    const auto result = adapter::adaptExternalPluginInstance(std::move(reopened), model);
    REQUIRE(result.device != nullptr);
    CHECK(reopenedRaw->tone->getValue() == Catch::Approx(0.9f));
    CHECK(reopenedRaw->fixed->getValue() == Catch::Approx(0.8f));
}

TEST_CASE("A save writes the parameter array beside the chunk", "[engine][external][state]") {
    // The two records have to agree. The array is the baseline the chunk
    // overlays on the next load, and it is also what the UI draws and what
    // automation addresses, so a save that only wrote the chunk would leave the
    // project describing the voice before the last knob move.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->gain->setValue(0.6f);
    raw->tone->setValue(0.9f);

    auto model = externalDeviceSaving(0.1f, 0.2f);
    REQUIRE(captureInto(*raw, model));

    CHECK(model.parameters[0].currentValue == Catch::Approx(0.6f));
    CHECK(model.parameters[1].currentValue == Catch::Approx(0.9f));
}

TEST_CASE("A plugin with nothing to say saves no chunk at all", "[engine][external][state]") {
    // The fork removes the property rather than storing a zero-length chunk,
    // and a project that stored one would come back as a baseline anyway. What
    // matters is that a previous save's chunk does not survive the plugin
    // ceasing to have one.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->savesNothing = true;

    auto model = externalDevice();
    model.pluginState = "some earlier chunk";

    REQUIRE(captureInto(*raw, model));
    CHECK(model.pluginState.isEmpty());
}

TEST_CASE("A plugin that throws describing itself leaves the last good save",
          "[engine][external][state]") {
    // Neither record may be written on its own: the array is the baseline the
    // chunk overlays, and a fresh array under a stale chunk restores the stale
    // voice and calls it the project's. So a save that cannot write both writes
    // neither.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->gain->setValue(0.6f);
    raw->throwsSavingState = true;

    auto model = externalDeviceSaving(0.1f, 0.2f);
    model.pluginState = "the last chunk that saved cleanly";

    CHECK_FALSE(captureInto(*raw, model));
    CHECK(model.pluginState == "the last chunk that saved cleanly");
    CHECK(model.parameters[0].currentValue == Catch::Approx(0.1f));
}

TEST_CASE("A save is not left holding the plugin suspended", "[engine][external][state]") {
    // The fork suspends the plugin across the read, and a plugin left suspended
    // by its own throw would render silence for the rest of the session.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->throwsSavingState = true;

    auto model = externalDevice();
    CHECK_FALSE(captureInto(*raw, model));
    CHECK_FALSE(raw->isSuspended());
}

TEST_CASE("A save refreshes the portable VST3 records", "[engine][external][state]") {
    // The .vstpreset a DAWproject export writes is the current patch, not the
    // one the project was imported with, so it is read again on every save. The
    // class id beside it is the plugin's identity rather than its patch, and is
    // written once.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->isVst3 = true;
    raw->tone->setValue(0.9f);

    auto model = externalDevice();
    REQUIRE(captureInto(*raw, model));

    CHECK(model.vst3ClassId == kStubVst3ClassId);
    REQUIRE(model.vst3Preset.isNotEmpty());

    juce::MemoryOutputStream decoded;
    REQUIRE(juce::Base64::convertFromBase64(decoded, model.vst3Preset));
    CHECK(magda::vst3::classIdFromPreset(decoded.getMemoryBlock()) == kStubVst3ClassId);
}

TEST_CASE("A VST3 that cannot write its patch out does not keep the old one",
          "[engine][external][state]") {
    // The dangerous half of the case below. An empty read from a VST3 and an
    // empty read from an AU are the same block and are not the same event: the
    // AU was never asked, and the VST3 failed to answer. Keeping the project's
    // existing preset here would leave a record of an older patch beside the
    // chunk this very save just wrote -- and the portable record is the overlay
    // on the next load, so the older patch would win over the newer one.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->isVst3 = true;
    raw->savesNoPreset = true;

    auto model = externalDevice();
    model.vst3ClassId = kStubVst3ClassId;
    model.vst3Preset = "the patch this project was imported with";

    REQUIRE(captureInto(*raw, model));

    CHECK(model.vst3Preset.isEmpty());
    CHECK(model.pluginState.isNotEmpty());

    // The identity survives. It is the plugin rather than its patch, and the one
    // the project was authored against is the one another host matches on.
    CHECK(model.vst3ClassId == kStubVst3ClassId);
}

TEST_CASE("A save leaves a non-VST3 device's portable records alone", "[engine][external][state]") {
    // An AU cannot restate the identity a project was imported with, and
    // clearing it would lose the one thing another host matches on.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->isVst3 = false;

    auto model = externalDevice();
    model.vst3ClassId = "FEDCBA9876543210FEDCBA9876543210";
    model.vst3Preset = "what the project was imported with";

    REQUIRE(captureInto(*raw, model));
    CHECK(model.vst3ClassId == "FEDCBA9876543210FEDCBA9876543210");
    CHECK(model.vst3Preset == "what the project was imported with");
}

TEST_CASE("A save reads the plugin through the device that owns it", "[engine][external][state]") {
    // The seam a host saves through (#2270). The adapter owns the instance and
    // nothing else can reach it: a save asks the device for what its plugin
    // holds, which is what makes the read and the blocks around it two things
    // one object is serialising rather than two callers observing a convention.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto model = externalDevice();
    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    // The patch moves after the device was built, the way a knob moved during a
    // session does.
    raw->tone->setValue(0.75f);

    auto* external = dynamic_cast<adapter::EngineExternalDevice*>(result.device.get());
    REQUIRE(external != nullptr);

    const auto snapshot = external->captureState();
    REQUIRE(snapshot.has_value());
    magda::applyCapturedPluginState(model, *snapshot);

    auto reopened = std::make_unique<StubPlugin>(2, 2, 0);
    auto* reopenedRaw = reopened.get();
    const auto reloaded = adapter::adaptExternalPluginInstance(std::move(reopened), model);
    REQUIRE(reloaded.device != nullptr);
    CHECK(reopenedRaw->tone->getValue() == Catch::Approx(0.75f));
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

    adapter::PluginAssignments assignments;
    assignments.ensureAssignment(keyFor(missing));

    bool called = false;
    adapter::ExternalDeviceResult delivered;
    adapter::createEngineExternalDeviceAsync(
        missing, keyFor(missing), services, false, assignments,
        [&](magda::engine::DeviceKey) { return &missing; },
        [&](adapter::ExternalDeviceResult result) {
            called = true;
            delivered = std::move(result);
        });

    REQUIRE(called);
    CHECK(delivered.device == nullptr);
    CHECK(delivered.failure.contains("Massive X"));
    CHECK(delivered.restoredParameters.empty());
}

TEST_CASE("The asynchronous entry point resolves once and completes an installed plugin",
          "[engine][external]") {
    juce::ScopedJuceInitialiser_GUI juce;

    auto model = externalDevice();
    model.name = "My Stub";
    model.fileOrIdentifier = "stub.plugin";
    model.isInstrument = false;
    model.deviceType = magda::DeviceType::Effect;
    model.audioInputChannels = 1;
    model.audioOutputChannels = 1;

    auto installed = descriptionFor(model);
    installed.name = "Installed Stub";
    installed.pluginFormatName = "StubFormat";
    installed.fileOrIdentifier = model.fileOrIdentifier;
    installed.isInstrument = true;
    installed.numInputChannels = 0;
    installed.numOutputChannels = 0;

    juce::KnownPluginList known;
    known.addType(installed);
    juce::AudioPluginFormatManager formats;
    auto stubFormat = std::make_unique<StubFormat>();
    auto* rawFormat = stubFormat.get();
    formats.addFormat(std::move(stubFormat));

    const adapter::ExternalPluginServices services{
        .formats = &formats, .knownPlugins = &known, .context = contextFor()};

    adapter::PluginAssignments assignments;
    assignments.ensureAssignment(keyFor(model));

    bool called = false;
    adapter::ExternalDeviceResult delivered;
    const auto resolved = adapter::createEngineExternalDeviceAsync(
        model, keyFor(model), services, false, assignments,
        [&](magda::engine::DeviceKey) { return &model; },
        [&](adapter::ExternalDeviceResult result) {
            called = true;
            delivered = std::move(result);
        });

    REQUIRE(resolved);
    CHECK(resolved.planDevice.isInstrument);
    CHECK(resolved.planDevice.deviceType == magda::DeviceType::Instrument);
    CHECK(resolved.planDevice.audioInputChannels == 1);
    CHECK(resolved.planDevice.audioOutputChannels == 1);

    // Starting the request does not rewrite a placed model or publish scan bus
    // defaults before JUCE has made an instance.
    CHECK_FALSE(model.isInstrument);
    CHECK(model.deviceType == magda::DeviceType::Effect);
    CHECK(model.audioInputChannels == 1);
    CHECK(model.audioOutputChannels == 1);

    for (int attempts = 0; !called && attempts < 100; ++attempts)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

    REQUIRE(called);
    REQUIRE(delivered.device != nullptr);
    REQUIRE(delivered.resolvedDevice.has_value());
    CHECK(delivered.resolvedDevice->isInstrument);
    CHECK(delivered.resolvedDevice->deviceType == magda::DeviceType::Instrument);
    CHECK(delivered.resolvedDevice->audioInputChannels == 2);
    CHECK(delivered.resolvedDevice->audioOutputChannels == 2);
    CHECK(rawFormat->requested.createIdentifierString() == installed.createIdentifierString());
}

TEST_CASE("A load that finishes late restores from the model as it is now", "[engine][external]") {
    // A plugin takes seconds to load and a project does not stop while it does.
    // If the user turns a knob or applies a preset in the meantime, completing
    // the load from the values the request was made with would put them back,
    // and the caller would then be handed those as the values to write into the
    // model: the edit undone twice over.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto model = externalDeviceSaving(1.0f, 0.25f);
    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, model);

    // The edit, made while the plugin was loading. It is an edit to this
    // assignment, so nothing about the assignment changes.
    model.parameters[1].currentValue = 0.6f;

    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    REQUIRE(result.device != nullptr);
    CHECK(raw->tone->getValue() == Catch::Approx(0.6f));

    const auto tone =
        std::find_if(result.restoredParameters.begin(), result.restoredParameters.end(),
                     [](const magda::RestoredParameter& p) { return p.paramIndex == 3; });
    REQUIRE(tone != result.restoredParameters.end());
    CHECK(tone->value == Catch::Approx(0.6f));
}

TEST_CASE("State applied while a plugin loads is the state that gets restored",
          "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto model = externalDeviceSaving(1.0f, 0.25f);
    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, model);

    // A preset selected after the load began. Its chunk is the current model's
    // authority at completion, not the empty state on the request-time copy.
    const StubPlugin::State preset{.tone = 0.8f, .fixed = 0.7f};
    const juce::MemoryBlock chunk(&preset, sizeof(preset));
    model.pluginState = chunk.toBase64Encoding();

    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    REQUIRE(result.device != nullptr);
    CHECK(raw->stateRestores == 1);
    CHECK(raw->tone->getValue() == Catch::Approx(0.8f));
    CHECK(raw->fixed->getValue() == Catch::Approx(0.7f));
}

TEST_CASE("A device deleted while its plugin loaded is not completed", "[engine][external]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);

    const auto device = externalDevice();
    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, device);

    // Deletion releases the assignment, which is what expires the request.
    assignments.release(keyFor(device));

    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested,
        [](magda::engine::DeviceKey) -> const magda::DeviceInfo* { return nullptr; });

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("removed while"));
}

TEST_CASE("A device that changed plugin while loading is not completed", "[engine][external]") {
    // The answer arrived for a question the device is no longer asking.
    // Restoring here would put one plugin's saved patch onto another.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);

    auto model = externalDevice();
    model.name = "Massive X";
    model.fileOrIdentifier = "/Library/MassiveX.vst3";
    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, model);

    // The slot keeps its id and is assigned again: a new assignment on a key
    // that had one.
    assignments.replaceAssignment(keyFor(model));
    model.name = "Something Else";
    model.fileOrIdentifier = "/Library/SomethingElse.vst3";

    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("changed plugin"));
}

TEST_CASE("A load that failed reports the loader's reason", "[engine][external]") {
    auto model = externalDevice();
    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, model);

    const auto result =
        adapter::completeExternalPluginLoad(nullptr, "the file was not there", requested,
                                            [&](magda::engine::DeviceKey) { return &model; });

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("the file was not there"));
}

TEST_CASE("A scanned plugin's own identifier is not read as a change", "[engine][external]") {
    // The identity a project saves for a device added from the browser is
    // JUCE's identifier string for the scanned plugin, and that string carries
    // the plugin's numeric uid. A description rebuilt from the device's saved
    // name, format and file has no uid to put in it, so comparing one form
    // against the other finds every unchanged plugin changed -- and refuses
    // every load that ever succeeds.
    juce::PluginDescription scanned;
    scanned.name = "Massive X";
    scanned.manufacturerName = "NI";
    scanned.fileOrIdentifier = "/Library/MassiveX.vst3";
    scanned.pluginFormatName = "VST3";
    scanned.uniqueId = 0x4d617373;  // what a scan reports and a rebuild cannot

    auto model = externalDeviceSaving(1.0f, 0.5f);
    model.name = scanned.name;
    model.manufacturer = scanned.manufacturerName;
    model.fileOrIdentifier = scanned.fileOrIdentifier;
    model.uniqueId = scanned.createIdentifierString();

    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, model, scanned);

    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    REQUIRE(result.device != nullptr);
    CHECK(result.failure.isEmpty());
    CHECK(raw->tone->getValue() == Catch::Approx(0.5f));
}

TEST_CASE("Resolved metadata gained while loading does not change the assignment",
          "[engine][external]") {
    // Resolution may correct every mutable identity-looking field. They are
    // newly learned facts about this assignment, not the identity boundary.
    auto model = externalDeviceSaving(1.0f, 0.5f);
    model.name = "Serum";
    model.manufacturer = {};
    model.fileOrIdentifier = "/old/Serum.vst3";
    model.isInstrument = false;  // DAWproject role was wrong
    model.uniqueId = {};

    auto resolved = descriptionFor(model);
    resolved.manufacturerName = "Xfer Records";
    resolved.fileOrIdentifier = "/Library/Serum.vst3";
    resolved.isInstrument = true;
    resolved.uniqueId = 0x53657275;
    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, model, resolved);

    model.manufacturer = "Xfer Records";
    model.fileOrIdentifier = "/Library/Serum.vst3";
    model.isInstrument = true;
    model.deviceType = magda::DeviceType::Instrument;
    model.uniqueId = "VST3-Serum-1234abcd-4d617373";

    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    REQUIRE(result.device != nullptr);
    CHECK(result.failure.isEmpty());
}

TEST_CASE("An imported device swapped to the other half of its bundle is a change",
          "[engine][external]") {
    // The assignment distinguishes the two even where JUCE's compact identifier
    // and all bundle-level metadata do not.
    auto model = externalDeviceSaving(1.0f, 0.5f);
    model.name = "Kontakt";
    model.manufacturer = "NI";
    model.fileOrIdentifier = "/Library/Kontakt.vst3";
    model.isInstrument = false;
    model.uniqueId = {};  // imported: nothing scanned it

    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, model);

    assignments.replaceAssignment(keyFor(model));
    model.isInstrument = true;

    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("changed plugin"));
}

TEST_CASE("An imported device swapped to another vendor's plugin is a change",
          "[engine][external]") {
    // The same gap from the other side: display metadata is not asked to carry
    // request lifetime identity.
    auto model = externalDeviceSaving(1.0f, 0.5f);
    model.name = "Saturator";
    model.manufacturer = "One";
    model.fileOrIdentifier = "/Library/Saturator.vst3";
    model.uniqueId = {};

    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, model);

    assignments.replaceAssignment(keyFor(model));
    model.manufacturer = "Another";
    model.fileOrIdentifier = "/Another/Saturator.vst3";

    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("changed plugin"));
}

TEST_CASE("A duplicate cannot accept the load its source asked for", "[engine][external]") {
    // The copy is a different device with an id of its own, and there is
    // nothing in the DeviceInfo it was copied from that could have made it look
    // like the original. It is registered as the placement it is.
    auto source = externalDeviceSaving(1.0f, 0.5f);
    source.id = 1;

    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, source);

    auto copy = source;  // duplicate, paste, preset import, undo reinsertion
    copy.id = 2;
    assignments.replaceAssignment(keyFor(copy));

    // The load arrives while only the copy is still in the project.
    assignments.release(keyFor(source));

    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &copy; });

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("removed while"));
}

TEST_CASE("A device in another section cannot accept the load", "[engine][external]") {
    // DeviceId is allocated per section, so the post-FX device below is also
    // device 1. A bare id would have named both.
    auto model = externalDeviceSaving(1.0f, 0.5f);
    model.id = 1;

    adapter::PluginAssignments assignments;
    assignments.replaceAssignment({magda::ChainSegment::PostFx, model.id});

    const adapter::RequestedPlugin requested{
        .assignment = assignments.request({magda::ChainSegment::PostFx, model.id}),
        .displayName = model.name,
        .resolvedIsInstrument = false};

    // The main-FX device with the same integer is the one still live.
    assignments.release({magda::ChainSegment::PostFx, model.id});
    assignments.replaceAssignment({magda::ChainSegment::Fx, model.id});

    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    CHECK(result.device == nullptr);
    // "removed", not "changed plugin": the live main-FX device is a different
    // key, so it was not offered as a stand-in for the one that went away.
    CHECK(result.failure.contains("removed while"));
}

TEST_CASE("A load nobody registered is refused rather than completed", "[engine][external]") {
    // The failure direction. The device is in the model and the plugin loaded
    // fine; what is missing is the registration, and the answer is no.
    auto model = externalDeviceSaving(1.0f, 0.5f);

    const adapter::PluginAssignments assignments;
    const adapter::RequestedPlugin requested{.assignment = assignments.request(keyFor(model)),
                                             .displayName = model.name,
                                             .resolvedIsInstrument = false};

    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    CHECK(result.device == nullptr);
    CHECK(result.failure.isNotEmpty());
    CHECK(result.restoredParameters.empty());
}

TEST_CASE("A load outliving the runtime that started it is refused", "[engine][external]") {
    // The project was closed while a plugin was still loading, and the thing
    // that owned the assignments went with it. The completion still arrives,
    // and it has to answer without reaching into any of that.
    auto model = externalDeviceSaving(1.0f, 0.5f);

    adapter::RequestedPlugin requested;
    {
        adapter::PluginAssignments assignments;
        requested = requestFor(assignments, model);
        REQUIRE(requested.assignment.isStillWanted());
    }

    CHECK_FALSE(requested.assignment.isStillWanted());

    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    CHECK(result.device == nullptr);
    CHECK(result.failure.contains("removed while"));
}

TEST_CASE("Registering a device that is already live does not expire its load",
          "[engine][external]") {
    // Ordinary registration: a plan prepared again, the project walked again.
    // Minting a new assignment for that would abandon a load that is still
    // wanted, over and over for anything that re-registers on every recompile.
    auto model = externalDeviceSaving(1.0f, 0.5f);

    adapter::PluginAssignments assignments;
    const auto requested = requestFor(assignments, model);

    for (int again = 0; again < 3; ++again)
        assignments.ensureAssignment(keyFor(model));

    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    const auto result = adapter::completeExternalPluginLoad(
        std::move(plugin), {}, requested, [&](magda::engine::DeviceKey) { return &model; });

    REQUIRE(result.device != nullptr);
    CHECK(result.failure.isEmpty());
    CHECK(raw->tone->getValue() == Catch::Approx(0.5f));
}

TEST_CASE("A runtime torn down before dispatch is never called back", "[engine][external]") {
    // The other half of the shutdown case, through the entry point a session
    // actually uses. JUCE stores the completion and runs it a turn or more
    // later, and in production that callback is the runtime's own -- it
    // publishes the device into the project. So the load must be dropped
    // before it is invoked, not refused inside it.
    juce::ScopedJuceInitialiser_GUI juce;

    auto model = externalDevice();
    model.name = "My Stub";
    model.fileOrIdentifier = "stub.plugin";

    auto installed = descriptionFor(model);
    installed.name = "Installed Stub";
    installed.pluginFormatName = "StubFormat";
    installed.fileOrIdentifier = model.fileOrIdentifier;

    juce::KnownPluginList known;
    known.addType(installed);
    juce::AudioPluginFormatManager formats;
    formats.addFormat(std::make_unique<StubFormat>());

    const adapter::ExternalPluginServices services{
        .formats = &formats, .knownPlugins = &known, .context = contextFor()};

    bool called = false;
    {
        adapter::PluginAssignments assignments;
        assignments.ensureAssignment(keyFor(model));

        const auto resolved = adapter::createEngineExternalDeviceAsync(
            model, keyFor(model), services, false, assignments,
            [&](magda::engine::DeviceKey) { return &model; },
            [&](adapter::ExternalDeviceResult) { called = true; });

        REQUIRE(resolved);
        // Nothing has dispatched yet: the creation is queued, not done.
        REQUIRE_FALSE(called);
    }

    // The project closed. Everything the completion would have published into
    // is gone, and the plugin only arrives now.
    for (int attempts = 0; attempts < 20; ++attempts)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

    CHECK_FALSE(called);
}

// =============================================================================
// The control plane (#2270)
// =============================================================================

TEST_CASE("A capture through the control plane answers with what the plugin holds",
          "[engine][external][control]") {
    // The endpoint a host asks rather than the instance it used to be handed.
    // What it answers with is data: a snapshot that outlives the call, which is
    // what lets the caller check between reading and writing, and what a plugin
    // in another process could send back at all.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto model = externalDevice();
    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    // Moved after the device was built, the way a knob moved during a session
    // is: what a capture is for is the difference between what the project
    // holds and what the plugin holds now.
    raw->tone->setValue(0.4f);

    const auto external = ownedExternalDevice(result);
    REQUIRE(external != nullptr);

    const magda::engine::DeviceKey key{magda::ChainSegment::Fx, model.id};
    const auto registry = std::make_shared<const OneDeviceRegistry>(key, external);
    adapter::LocalDeviceControlPlane plane(std::make_shared<adapter::SerialControlThread>(),
                                           registry);

    const auto answered = capture(plane, key);
    REQUIRE(answered.ok());
    CHECK(answered.failure().isEmpty());

    magda::applyCapturedPluginState(model, answered.snapshot());
    CHECK(model.parameters[1].currentValue == Catch::Approx(0.4f));
}

TEST_CASE("A key with no device bound is a failure rather than an empty state",
          "[engine][external][control]") {
    // The two are different findings and one reads like the other: a slot whose
    // plugin has not arrived, against a plugin that answered with nothing. A
    // caller told the second would write that nothing into the project and lose
    // the patch the slot is about to load.
    const auto registry = std::make_shared<const EmptyRegistry>();
    adapter::LocalDeviceControlPlane plane(std::make_shared<adapter::SerialControlThread>(),
                                           registry);

    const auto answered = capture(plane, {magda::ChainSegment::PostFx, 12});
    CHECK_FALSE(answered.ok());
    CHECK(answered.failure().contains("no plugin is bound"));

    // And it says which slot, because a host with a project's worth of devices
    // is holding a failure that has to name one of them.
    CHECK(answered.failure().contains("12"));
}

TEST_CASE("A plugin that will not describe itself is a failure with a reason",
          "[engine][external][control]") {
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();
    raw->throwsSavingState = true;

    auto model = externalDevice();
    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    const auto external = ownedExternalDevice(result);
    REQUIRE(external != nullptr);

    const auto registry = std::make_shared<const OneDeviceRegistry>(
        magda::engine::DeviceKey{magda::ChainSegment::Fx, model.id}, external);
    adapter::LocalDeviceControlPlane plane(std::make_shared<adapter::SerialControlThread>(),
                                           registry);

    const auto answered = capture(plane, {magda::ChainSegment::Fx, model.id});
    CHECK_FALSE(answered.ok());
    CHECK(answered.failure().contains("could not describe itself"));
}

TEST_CASE("A snapshot is written onto the assignment it was read from",
          "[engine][external][control]") {
    adapter::PluginAssignments assignments;
    const magda::engine::DeviceKey key{magda::ChainSegment::Fx, 7};
    assignments.replaceAssignment(key);

    const auto request = assignments.request(key);

    magda::ExternalPluginSnapshot snapshot;
    snapshot.pluginState = "what the plugin was holding";

    auto model = externalDevice();
    CHECK(adapter::commitCapturedState(request, snapshot, [&model](magda::engine::DeviceKey asked) {
        return asked.deviceId == model.id ? &model : nullptr;
    }));
    CHECK(model.pluginState == "what the plugin was holding");
}

TEST_CASE("A snapshot read from an assignment that has been replaced is not written",
          "[engine][external][control]") {
    // The reason a capture is two steps with only data between them. Between
    // asking a plugin what it holds and writing that down, the slot can have
    // been given a different plugin -- and the patch of the one that answered
    // would land on the one that did not.
    adapter::PluginAssignments assignments;
    const magda::engine::DeviceKey key{magda::ChainSegment::Fx, 7};
    assignments.replaceAssignment(key);

    const auto request = assignments.request(key);
    assignments.replaceAssignment(key);

    magda::ExternalPluginSnapshot snapshot;
    snapshot.pluginState = "the plugin that used to be in this slot";

    auto model = externalDevice();
    model.pluginState = "what the slot holds now";

    CHECK_FALSE(adapter::commitCapturedState(
        request, snapshot, [&model](magda::engine::DeviceKey) { return &model; }));
    CHECK(model.pluginState == "what the slot holds now");
}

TEST_CASE("A snapshot outliving the runtime that read it is not written",
          "[engine][external][control]") {
    // The other way a commit arrives too late, and the one an out-of-process
    // capture will meet first: the project was closed while the answer was in
    // flight. There is nothing to write onto and nobody waiting to hear it.
    adapter::AssignmentRequest request;
    {
        adapter::PluginAssignments assignments;
        assignments.replaceAssignment({magda::ChainSegment::Fx, 7});
        request = assignments.request({magda::ChainSegment::Fx, 7});
        CHECK(request.isStillWanted());
    }

    magda::ExternalPluginSnapshot snapshot;
    snapshot.pluginState = "read before the project closed";

    auto model = externalDevice();
    CHECK_FALSE(adapter::commitCapturedState(
        request, snapshot, [&model](magda::engine::DeviceKey) { return &model; }));
    CHECK(model.pluginState.isEmpty());
}

TEST_CASE("A snapshot is written onto the device its own token names",
          "[engine][external][control]") {
    // The half a token check does not do on its own. Validating one device and
    // writing another would pass every test above it and still put one plugin's
    // patch onto another's slot, so the device is resolved here from the
    // request's own key rather than handed in beside it: what was checked and
    // what is written are the same device by construction.
    adapter::PluginAssignments assignments;
    const magda::engine::DeviceKey key{magda::ChainSegment::Fx, 7};
    assignments.replaceAssignment(key);

    const auto request = assignments.request(key);

    magda::ExternalPluginSnapshot snapshot;
    snapshot.pluginState = "what the plugin was holding";

    auto onTheSlot = externalDevice();
    auto somewhereElse = externalDevice();
    somewhereElse.id = 9;

    std::vector<magda::engine::DeviceKey> asked;
    CHECK(adapter::commitCapturedState(
        request, snapshot, [&](magda::engine::DeviceKey key) -> magda::DeviceInfo* {
            asked.push_back(key);
            return key.deviceId == onTheSlot.id ? &onTheSlot : &somewhereElse;
        }));

    REQUIRE(asked.size() == 1);
    CHECK(asked.front() == key);
    CHECK(onTheSlot.pluginState == "what the plugin was holding");
    CHECK(somewhereElse.pluginState.isEmpty());
}

TEST_CASE("A capture runs and answers on the plane's executor", "[engine][external][control]") {
    // Asked from any thread, run on one. That is the whole of the execution
    // contract: a caller does not have to be anywhere in particular, and what
    // it gets back arrives on the thread the control side of a device is
    // allowed to be on -- which is where it is entitled to write a project's
    // model from, whichever thread asked and whichever process answered.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto model = externalDevice();
    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    const auto external = ownedExternalDevice(result);
    REQUIRE(external != nullptr);

    auto executor = std::make_shared<adapter::SerialControlThread>();
    const auto registry = std::make_shared<const OneDeviceRegistry>(
        magda::engine::DeviceKey{magda::ChainSegment::Fx, model.id}, external);
    adapter::LocalDeviceControlPlane plane(executor, registry);

    std::promise<bool> onTheExecutor;
    auto answered = onTheExecutor.get_future();

    // Asked from a thread that is neither the executor's nor this one, which is
    // the case the endpoint used to refuse and now simply serves.
    std::thread worker([&] {
        plane.captureState({magda::ChainSegment::Fx, model.id},
                           [&executor, &onTheExecutor](adapter::CaptureOutcome outcome) {
                               onTheExecutor.set_value(outcome.ok() && executor->isCurrent());
                           });
    });
    worker.join();

    CHECK(answered.get());
}

TEST_CASE("A capture asked for on the executor is queued behind what asked for it",
          "[engine][external][control]") {
    // Never inline, and that is the rule rather than a missed optimisation: an
    // operation that has suspended a plugin and asks for a capture must not
    // have the capture run inside it. It would resume the plugin and let audio
    // back in halfway through the first transaction, which is what the executor
    // is for (ControlExecutor.hpp).
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto model = externalDevice();
    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    const auto external = ownedExternalDevice(result);
    REQUIRE(external != nullptr);

    auto executor = std::make_shared<adapter::SerialControlThread>();
    const auto registry = std::make_shared<const OneDeviceRegistry>(
        magda::engine::DeviceKey{magda::ChainSegment::Fx, model.id}, external);
    adapter::LocalDeviceControlPlane plane(executor, registry);

    std::atomic<bool> answered{false};
    std::promise<bool> answeredInside;
    auto asked = answeredInside.get_future();

    executor->run([&](adapter::ExecutionState) {
        plane.captureState(
            {magda::ChainSegment::Fx, model.id},
            [&answered](adapter::CaptureOutcome outcome) { answered = outcome.ok(); });

        // Read after captureState returned and while this work is still the one
        // running: an answer here would be a second transaction inside this one.
        answeredInside.set_value(answered.load());
    });

    CHECK_FALSE(asked.get());

    // And it arrives in its turn, which is the other half.
    std::promise<void> drained;
    auto emptied = drained.get_future();
    executor->run([&drained](adapter::ExecutionState) { drained.set_value(); });
    emptied.wait();

    CHECK(answered);
}

TEST_CASE("A device let go of mid-capture is still the one being read",
          "[engine][external][control]") {
    // What the lease is for, and what holding the registry alive never proved.
    // A registry can carry on while a device leaves it -- a chain edited, a
    // slot emptied -- and a capture that had been handed a bare pointer would
    // be reading a plugin that had gone. So find() hands over the device rather
    // than pointing at it, and the operation holds it until it is finished.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto model = externalDevice();
    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    auto external = ownedExternalDevice(result);
    REQUIRE(external != nullptr);

    const magda::engine::DeviceKey key{magda::ChainSegment::Fx, model.id};
    auto registry = std::make_shared<OneDeviceRegistry>(key, external);

    // From here the registry is the only owner, so what happens next is the
    // device being destroyed rather than merely unregistered.
    std::weak_ptr<adapter::EngineExternalDevice> watch = external;
    external.reset();

    // Let go of halfway through describing itself, which is the moment a bare
    // pointer would become stale in.
    raw->whileDescribingItself = [&registry] { registry->release(); };

    auto executor = std::make_shared<adapter::SerialControlThread>();
    adapter::LocalDeviceControlPlane plane(executor, registry);

    const auto answered = capture(plane, key);
    CHECK(answered.ok());

    // And once the operation is over, so is the lease. Drained by asking for
    // one more thing, because the work holding it is destroyed at the end of
    // its own turn rather than when its callback fired.
    std::promise<void> drained;
    auto emptied = drained.get_future();
    REQUIRE(executor->run([&drained](adapter::ExecutionState) { drained.set_value(); }));
    emptied.wait();

    CHECK(watch.expired());
}

TEST_CASE("A capture queued before its registry went is answered rather than run",
          "[engine][external][control]") {
    // The hazard of being genuinely asynchronous. Between queueing a capture
    // and running it, the project can close -- and what finds a device is a
    // registry the runtime owns, so asking one whose runtime has gone is
    // reaching into something that has been deleted rather than being told
    // there is no device.
    //
    // So the plane holds the registry weakly and takes it before it asks
    // anything, which is the relationship a token standing beside a lookup
    // could not have had. The same shape the asynchronous load path uses for
    // the same problem (PluginAssignments.hpp).
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto model = externalDevice();
    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    const auto external = ownedExternalDevice(result);
    REQUIRE(external != nullptr);

    auto executor = std::make_shared<adapter::SerialControlThread>();
    // Counted outside the registry, because the point of this case is that the
    // registry is gone by the time the question is asked.
    std::atomic<int> asked{0};
    auto registry = std::make_shared<const OneDeviceRegistry>(
        magda::engine::DeviceKey{magda::ChainSegment::Fx, model.id}, external, &asked);

    adapter::LocalDeviceControlPlane plane(executor, registry);

    // The worker is held while the capture is queued behind it, so the project
    // can be closed in between the way a person would close it.
    std::promise<void> holdingOn;
    std::shared_future<void> release = holdingOn.get_future();
    std::promise<void> started;
    auto running = started.get_future();

    REQUIRE(executor->run([&started, release](adapter::ExecutionState) {
        started.set_value();
        release.wait();
    }));
    running.wait();

    std::promise<adapter::CaptureOutcome> answered;
    auto answer = answered.get_future();
    REQUIRE(plane.captureState(
        {magda::ChainSegment::Fx, model.id},
        [&answered](adapter::CaptureOutcome outcome) { answered.set_value(std::move(outcome)); }));

    registry.reset();
    holdingOn.set_value();

    const auto outcome = answer.get();
    CHECK_FALSE(outcome.ok());
    CHECK(outcome.failure().contains("is gone"));
    CHECK(asked == 0);
}

TEST_CASE("Two captures of one device do not overlap", "[engine][external][control]") {
    // The rule the executor exists for. A capture suspends the plugin and
    // resumes it afterwards, so two of them overlapping would have the first to
    // finish resuming it while the second was still reading -- and the next
    // block let straight back into a plugin halfway through describing itself.
    //
    // A headless host has whatever threads it has, and four of them ask at once
    // here. What makes that safe is not a thread check but the executor: one
    // piece of control work at a time, for every operation and not only this
    // one. The stub counts how many readers are inside it at once.
    auto plugin = std::make_unique<StubPlugin>(2, 2, 0);
    auto* raw = plugin.get();

    auto model = externalDevice();
    auto result = adapter::adaptExternalPluginInstance(std::move(plugin), model);
    REQUIRE(result.device != nullptr);

    const auto external = ownedExternalDevice(result);
    REQUIRE(external != nullptr);

    std::atomic<int> inside{0};
    std::atomic<int> mostAtOnce{0};
    raw->whileDescribingItself = [&inside, &mostAtOnce] {
        const auto now = ++inside;
        auto highest = mostAtOnce.load();
        while (now > highest && !mostAtOnce.compare_exchange_weak(highest, now)) {
        }

        // Long enough that a second reader arriving unserialised would be seen
        // rather than missed by luck.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        --inside;
    };

    const auto registry = std::make_shared<const OneDeviceRegistry>(
        magda::engine::DeviceKey{magda::ChainSegment::Fx, model.id}, external);
    adapter::LocalDeviceControlPlane plane(std::make_shared<adapter::SerialControlThread>(),
                                           registry);

    std::atomic<int> taken{0};
    std::vector<std::thread> askers;
    for (int index = 0; index < 4; ++index)
        askers.emplace_back([&plane, &taken, &model] {
            if (capture(plane, {magda::ChainSegment::Fx, model.id}).ok())
                ++taken;
        });

    for (auto& asker : askers)
        asker.join();

    CHECK(taken == 4);
    CHECK(mostAtOnce == 1);
}
