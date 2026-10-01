#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <type_traits>
#include <utility>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "audio/plugins/DeviceParameterHandle.hpp"
#include "audio/plugins/DevicePluginHandle.hpp"
#include "audio/plugins/MagdaDevice.hpp"
#include "audio/plugins/compiled/MagdaBellCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaClapCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaDjembeCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaFMCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaHatCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaKickCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaMarimbaCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaSnareCompiledPlugin.hpp"
#include "audio/plugins/compiled/MagdaTomCompiledPlugin.hpp"

namespace audio = magda::daw::audio;
namespace compiled = magda::daw::audio::compiled;

static_assert(std::is_base_of_v<audio::MagdaDevice, compiled::MagdaBellCompiledPlugin>);
static_assert(std::is_base_of_v<audio::MagdaDevice, compiled::MagdaClapCompiledPlugin>);
static_assert(std::is_base_of_v<audio::MagdaDevice, compiled::MagdaDjembeCompiledPlugin>);
static_assert(std::is_base_of_v<audio::MagdaDevice, compiled::MagdaFMCompiledPlugin>);
static_assert(std::is_base_of_v<audio::MagdaDevice, compiled::MagdaHatCompiledPlugin>);
static_assert(std::is_base_of_v<audio::MagdaDevice, compiled::MagdaKickCompiledPlugin>);
static_assert(std::is_base_of_v<audio::MagdaDevice, compiled::MagdaMarimbaCompiledPlugin>);
static_assert(std::is_base_of_v<audio::MagdaDevice, compiled::MagdaSnareCompiledPlugin>);
static_assert(std::is_base_of_v<audio::MagdaDevice, compiled::MagdaTomCompiledPlugin>);

TEST_CASE("DeviceParameterHandle delegates without exposing a host parameter type",
          "[device-sdk]") {
    struct Parameter {
        float base = 0.25f;
        float current = 0.5f;
    } parameter;

    const audio::DeviceParameterHandle handle{
        &parameter,
        [](const void* native) { return static_cast<const Parameter*>(native)->base; },
        [](const void* native) { return static_cast<const Parameter*>(native)->current; },
        [](void* native, float value) { static_cast<Parameter*>(native)->current = value; },
    };

    REQUIRE(handle);
    CHECK(handle.currentBaseValue() == 0.25f);
    CHECK(handle.currentValue() == 0.5f);

    handle.setValueFromHost(0.75f);
    CHECK(parameter.current == 0.75f);
}

TEST_CASE("MagdaDevice owns telemetry independently of a host plugin lifecycle", "[device-sdk]") {
    class TestTelemetry final : public audio::DeviceTelemetry {
      public:
        std::string_view telemetryKey() const override {
            return "test";
        }

        int value = 42;
    };

    class TestDevice final : public audio::MagdaDevice {
      public:
        audio::DeviceProperties properties() const override {
            return {.pluginId = "test", .name = "Test", .shortName = "Test"};
        }

        void process(audio::DeviceProcessContext& context) override {
            if (context.tempoMap != nullptr)
                lastBeat = context.tempoMap->beatsAtSeconds(context.timelineStartSeconds);
        }

        audio::DeviceTelemetry* telemetry(std::string_view key) override {
            return key == telemetry_.telemetryKey() ? &telemetry_ : nullptr;
        }

        const audio::DeviceTelemetry* telemetry(std::string_view key) const override {
            return key == telemetry_.telemetryKey() ? &telemetry_ : nullptr;
        }

        TestTelemetry telemetry_;
        double lastBeat = -1.0;
    };

    class TestTempoMap final : public magda::sdk::TempoMap {
      public:
        double beatsAtSeconds(double seconds) const override {
            return seconds * 2.0;
        }

        double bpmAtSeconds(double) const override {
            return 120.0;
        }
    };

    TestDevice device;
    TestTempoMap tempoMap;
    audio::DeviceProcessContext context;
    context.tempoMap = &tempoMap;
    context.timelineStartSeconds = 3.0;

    device.process(context);

    CHECK(device.lastBeat == 6.0);
    REQUIRE(device.telemetry("test") != nullptr);
    CHECK(static_cast<TestTelemetry*>(device.telemetry("test"))->value == 42);
    CHECK(device.telemetry("host-owned") == nullptr);
}

TEST_CASE("MagdaDevice lifecycle, state, parameters, audio, and MIDI need no host plugin",
          "[device-sdk]") {
    using MidiBuffer = magda::test::DeviceMidiBuffer;

    class TestDevice final : public audio::MagdaDevice {
      public:
        audio::DeviceProperties properties() const override {
            return {
                .pluginId = "neutral-lifecycle",
                .name = "Neutral lifecycle",
                .shortName = "Neutral",
                .takesMidiInput = true,
                .producesMidi = true,
            };
        }

        void prepare(const audio::DevicePrepareContext& context) override {
            sampleRate = context.sampleRate;
        }

        int latencySamples() const override {
            return static_cast<int>(sampleRate * 0.01);
        }

        void release() override {
            released = true;
        }

        void process(audio::DeviceProcessContext& context) override {
            auto audio = audio::juceAudio(context);
            audio.applyGain(0, context.numSamples(), parameter);
            if (context.midiOut != nullptr)
                context.midiOut->addEvent(magda::sdk::MidiEvent::noteOn(1, 60, 100, 0));
        }

        int parameterCount() const override {
            return 1;
        }

        magda::ParameterInfo parameterInfo(int index) const override {
            if (index != 0)
                return {};
            magda::ParameterInfo info;
            info.paramIndex = 0;
            info.stableId = "gain";
            info.name = "Gain";
            info.defaultValue = 0.5f;
            return info;
        }

        float parameterValue(int index) const override {
            return index == 0 ? parameter : 0.0f;
        }

        void setParameterValue(int index, float value) override {
            if (index == 0)
                parameter = value;
        }

        magda::sdk::RestoreResult restoreState(const magda::sdk::StateNode& state) override {
            deviceOwnedState = state.getInt("deviceOwned");
            return magda::sdk::RestoreResult::success();
        }

        double sampleRate = 0.0;
        float parameter = 1.0f;
        int deviceOwnedState = 0;
        bool released = false;
    };

    TestDevice device;
    device.prepare({.sampleRate = 48000.0, .maximumBlockSize = 16});
    CHECK(device.sampleRate == 48000.0);
    CHECK(device.latencySamples() == 480);
    CHECK(device.parameterInfo(0).stableId == "gain");

    device.setParameterValue(0, 0.5f);
    juce::AudioBuffer<float> audioBuffer(1, 4);
    audioBuffer.clear();
    audioBuffer.addFrom(0, 0, std::array{1.0f, 1.0f, 1.0f, 1.0f}.data(), 4);
    MidiBuffer in;
    MidiBuffer out;
    magda::test::DeviceTestBlock block(audioBuffer);
    auto& context = block.context;
    context.midiIn = &in;
    context.midiOut = &out;
    device.process(context);

    CHECK(audioBuffer.getSample(0, 0) == 0.5f);
    REQUIRE(out.size() == 1);
    CHECK(out.message(0).isNoteOn());

    // The host owns the document and the device restores it.
    magda::sdk::StateNode state;
    state.setInt("deviceOwned", 17);
    CHECK(device.restoreState(state).ok);
    CHECK(device.deviceOwnedState == 17);

    device.release();
    CHECK(device.released);
}
