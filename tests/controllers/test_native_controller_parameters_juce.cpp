#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <optional>
#include <ranges>
#include <vector>

#include "JuceTestStateGuard.hpp"
#include "magda/agents/generic_sound_design_agent.hpp"
#include "magda/daw/api/osc_command_sink_live.hpp"
#include "magda/daw/audio/DeviceParameterList.hpp"
#include "magda/daw/audio/controllers/ControllerParamReader.hpp"
#include "magda/daw/audio/controllers/ControllerParamWriter.hpp"
#include "magda/daw/audio/controllers/ControllerRouter.hpp"
#include "magda/daw/audio/plugins/engine/EngineExternalDevice.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/aliases/ChainContext.hpp"
#include "magda/daw/core/controllers/BindingRegistry.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#include "param/ParamBlock.hpp"

using namespace magda;
namespace {
namespace adapter = magda::daw::audio::engine_adapter;
namespace engine = magda::engine;

class StubParameter final : public juce::AudioProcessorParameterWithID {
  public:
    StubParameter(juce::String id, juce::String name)
        : AudioProcessorParameterWithID(juce::ParameterID{std::move(id), 1}, std::move(name)) {}
    float getValue() const override {
        return value_;
    }
    void setValue(float v) override {
        value_ = v;
    }
    float getDefaultValue() const override {
        return 0.0f;
    }
    float getValueForText(const juce::String& text) const override {
        return text.getFloatValue();
    }

  private:
    float value_ = 0.0f;
};

class StubPlugin final : public juce::AudioPluginInstance {
  public:
    StubPlugin()
        : AudioPluginInstance(BusesProperties()
                                  .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        for (const auto* name : {"Gain", "Tone", "Drive"}) {
            auto p = std::make_unique<StubParameter>(juce::String(name).toLowerCase(), name);
            parameters.push_back(p.get());
            addHostedParameter(std::move(p));
        }
    }
    const juce::String getName() const override {
        return "Controller Stub";
    }
    void fillInPluginDescription(juce::PluginDescription& d) const override {
        d.name = getName();
        d.pluginFormatName = "VST3";
    }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override {
        return 0.0;
    }
    bool acceptsMidi() const override {
        return false;
    }
    bool producesMidi() const override {
        return false;
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
    std::vector<StubParameter*> parameters;
};

class FixtureEngine final : public TracktionEngineWrapper {
  public:
    explicit FixtureEngine(adapter::EngineExternalDevice& d) : device(d) {}
    AudioBridge* getAudioBridge() override {
        return nullptr;
    }
    const AudioBridge* getAudioBridge() const override {
        return nullptr;
    }
    HostParameters describeDeviceParameters(const ChainNodePath&) const override {
        return device.describeParameters();
    }
    EditReceipt editHostedParameter(const ChainNodePath&, int slot, float value, EditOrigin,
                                    std::function<void(EditCompletion)> = {}) override {
        return {.status = device.queueParameterEdit(slot, value) ? EditStatus::Accepted
                                                                 : EditStatus::UnknownParameter,
                .requested = value};
    }

  private:
    adapter::EngineExternalDevice& device;
};

struct Rig {
    static constexpr int blockSize = 64;
    Rig() : external(makeExternal()), engine(external) {
        external.prepare({.sampleRate = 48000.0, .maxBlockSize = blockSize, .numChannels = 2});
        external.setRendered(true);
        auto& tm = TrackManager::getInstance();
        tm.setAudioEngine(&engine);
        trackId = tm.createTrack("Native Controller Target");
        auto* track = tm.getTrack(trackId);
        DeviceInfo model;
        model.id = DeviceId{731};
        model.name = "Hosted Filter";
        model.pluginId = "test.hosted.filter";
        model.format = PluginFormat::VST3;
        auto tone = describedAt(3);
        tone.minValue = 20.0f;
        tone.maxValue = 20000.0f;
        tone.unit = "Hz";
        tone.currentValue = 0.2f;
        tone.displayText.reset();
        model.parameters.push_back(tone);
        track->chain.fxChainElements.emplace_back(model);
        path = ChainNodePath::topLevelDevice(trackId, model.id);
    }
    ~Rig() {
        TrackManager::getInstance().setAudioEngine(nullptr);
    }
    static adapter::EngineExternalDevice makeExternal() {
        auto p = std::make_unique<StubPlugin>();
        plugin = p.get();
        return adapter::EngineExternalDevice(std::move(p), DeviceInfo{}, false);
    }
    ParameterInfo describedAt(int slot) const {
        auto list = external.describeParameters().parameters;
        auto found = std::ranges::find(list, slot, &ParameterInfo::paramIndex);
        jassert(found != list.end());
        return *found;
    }
    void render(std::optional<std::pair<int, float>> carried = {}) {
        std::vector<engine::ParamSegment> segments;
        std::vector<int> counts, slots;
        std::vector<ParameterUtils::ParameterDomain> domains;
        std::vector<std::uint8_t> driven;
        if (carried) {
            segments.push_back(
                {.startSample = 0, .startValue = carried->second, .endValue = carried->second});
            counts.push_back(1);
            slots.push_back(carried->first);
            driven.push_back(0);
            domains.push_back(
                {.scale = ParameterScale::Linear, .minValue = 0.0f, .maxValue = 1.0f});
        }
        engine::DeviceParams params{segments, counts, domains, slots, driven, 1, blockSize};
        juce::AudioBuffer<float> audio(2, blockSize);
        engine::DeviceBlock block;
        block.audio = juce::dsp::AudioBlock<float>(audio);
        block.params = params;
        block.block.numSamples = blockSize;
        external.process(block);
    }
    static inline StubPlugin* plugin = nullptr;
    adapter::EngineExternalDevice external;
    FixtureEngine engine;
    TrackId trackId = INVALID_TRACK_ID;
    ChainNodePath path;
};

Binding bindingFor(const Rig& rig, BindingSourceKind kind) {
    Binding b;
    b.id = juce::Uuid();
    b.source.kind = kind;
    b.source.portKey = "native-controller-port";
    b.source.msgType = BindingMsgType::CC;
    b.source.channel = 1;
    b.source.number = 21;
    b.source.oscAddress = "/native/tone";
    b.target = ControlTarget::pluginParam(rig.path, 3);
    return b;
}
}  // namespace

class NativeControllerParametersTest final : public juce::UnitTest {
  public:
    NativeControllerParametersTest()
        : juce::UnitTest("Native Controller Parameter Tests", "magda") {}
    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testMidi(); });
        magda::test::runWithCleanJuceState([this] { testOsc(); });
        magda::test::runWithCleanJuceState([this] { testSoundDesign(); });
    }

  private:
    void testMidi() {
        beginTest("A MIDI binding updates the native hosted parameter");
        Rig rig;
        const auto binding = bindingFor(rig, BindingSourceKind::Midi);
        BindingRegistry::getInstance().add(BindingScope::Project, binding);
        ControllerRouter::getInstance().setParamWriter(
            std::make_unique<DefaultControllerParamWriter>());
        ControllerRouter::getInstance().injectMessageForTest(
            "native-controller-port", juce::MidiMessage::controllerEvent(1, 21, 96));
        const auto* model = TrackManager::getInstance().getDeviceInChainByPath(rig.path);
        const auto modelValue = model->findParameterByIndex(3)->currentValue;
        expectWithinAbsoluteError(modelValue, 96.0f / 127.0f, 0.001f,
                                  "MIDI updates the model-owned base");
        rig.render(std::pair{3, modelValue});
        expectWithinAbsoluteError(Rig::plugin->parameters[1]->getValue(), 96.0f / 127.0f, 0.001f);
        BindingRegistry::getInstance().remove(BindingScope::Project, binding.id);
    }
    void testOsc() {
        beginTest("An OSC binding writes and reads the native hosted parameter");
        Rig rig;
        const auto binding = bindingFor(rig, BindingSourceKind::Osc);
        OscBindingSinkLive sink{std::make_unique<DefaultControllerParamWriter>()};
        sink.apply(binding, 0.61f);
        const auto* model = TrackManager::getInstance().getDeviceInChainByPath(rig.path);
        const auto modelValue = model->findParameterByIndex(3)->currentValue;
        expectWithinAbsoluteError(modelValue, 0.61f, 0.001f, "OSC updates the model-owned base");
        rig.render(std::pair{3, modelValue});
        expectWithinAbsoluteError(Rig::plugin->parameters[1]->getValue(), 0.61f, 0.001f);
        ResolveResult resolved{.target = ControlTarget::pluginParam(rig.path, 3), .resolved = true};
        const auto feedback = DefaultControllerParamReader{}.read(resolved);
        expect(feedback.has_value());
        if (feedback)
            expectWithinAbsoluteError(*feedback, 0.61f, 0.001f);

        const auto liveParameters = DefaultChainContext{}.parametersAt(rig.path);
        expect(std::ranges::any_of(liveParameters,
                                   [](const ParameterInfo& p) { return p.paramIndex == 4; }),
               "Alias resolution sees an unmirrored live hosted slot");

        auto unmirrored = binding;
        unmirrored.id = juce::Uuid();
        unmirrored.target = ControlTarget::pluginParam(rig.path, 4);
        sink.apply(unmirrored, 0.35f);
        expectWithinAbsoluteError(Rig::plugin->parameters[2]->getValue(), 0.0f, 0.001f,
                                  "The OSC edit waits in the render mailbox");
        expect(model->findParameterByIndex(4) == nullptr,
               "The live-catalog write does not fill the sparse model");

        rig.render();
        expectWithinAbsoluteError(Rig::plugin->parameters[2]->getValue(), 0.35f, 0.001f,
                                  "The queued OSC edit reaches the hosted parameter");
        ResolveResult unmirroredResolved{.target = ControlTarget::pluginParam(rig.path, 4),
                                         .resolved = true};
        const auto unmirroredFeedback = DefaultControllerParamReader{}.read(unmirroredResolved);
        expect(unmirroredFeedback.has_value(), "Feedback reads the live hosted catalog fallback");
        if (unmirroredFeedback)
            expectWithinAbsoluteError(*unmirroredFeedback, 0.35f, 0.001f);
        expect(model->findParameterByIndex(4) == nullptr,
               "Reading live feedback also leaves the model sparse");
    }
    void testSoundDesign() {
        beginTest("Sound design reaches an unaddressed native hosted parameter");
        Rig rig;
        const auto* model = TrackManager::getInstance().getDeviceInChainByPath(rig.path);
        const auto snapshot =
            sound_design_detail::snapshotParameters(deviceParameterList(*model, rig.path), {4});
        int skipped = 0;
        const auto writes = sound_design_detail::resolveParameterWrites(
            snapshot, {{"Drive", juce::var(0.83)}}, skipped);
        expect(writes.size() == 1 && skipped == 0);
        if (writes.empty())
            return;
        TrackManager::getInstance().setDeviceParameterValue(rig.path, writes[0].first,
                                                            writes[0].second);
        expectWithinAbsoluteError(Rig::plugin->parameters[2]->getValue(), 0.0f, 0.001f,
                                  "The hosted edit waits in the render mailbox");
        rig.render();
        expectWithinAbsoluteError(Rig::plugin->parameters[2]->getValue(), 0.83f, 0.001f);
        expect(model->findParameterByIndex(4) == nullptr,
               "A one-off hosted edit must not fill the sparse model");
    }
};
static NativeControllerParametersTest nativeControllerParametersTest;
