#include <juce_audio_devices/juce_audio_devices.h>

#include "magda/daw/audio/io/HardwareRouteNames.hpp"
#include "magda/daw/engine/MagdaEngineBehaviour.hpp"
#include "magda/daw/engine/TracktionAudioIO.hpp"

/// @file Audio Settings driving the Tracktion engine's interface through its wave devices (#2749).

namespace {

constexpr int kChannels = 4;

juce::BigInteger channels(std::initializer_list<int> set) {
    juce::BigInteger result;
    for (const auto channel : set)
        result.setBit(channel);
    return result;
}

juce::StringArray numbered(const juce::String& prefix) {
    juce::StringArray names;
    for (auto channel = 1; channel <= kChannels; ++channel)
        names.add(prefix + " " + juce::String(channel));
    return names;
}

class QuadInterface final : public juce::AudioIODevice {
  public:
    QuadInterface() : juce::AudioIODevice("Quad", "Fake") {}

    juce::StringArray getOutputChannelNames() override {
        return numbered("Out");
    }
    juce::StringArray getInputChannelNames() override {
        return numbered("In");
    }
    juce::Array<double> getAvailableSampleRates() override {
        return {48000.0};
    }
    juce::Array<int> getAvailableBufferSizes() override {
        return {512};
    }
    int getDefaultBufferSize() override {
        return 512;
    }
    juce::String open(const juce::BigInteger& inputs, const juce::BigInteger& outputs, double,
                      int) override {
        inputs_ = inputs;
        outputs_ = outputs;
        open_ = true;
        return {};
    }
    void close() override {
        open_ = false;
    }
    bool isOpen() override {
        return open_;
    }
    void start(juce::AudioIODeviceCallback* callback) override {
        callback_ = callback;
        if (callback_ != nullptr)
            callback_->audioDeviceAboutToStart(this);
    }
    void stop() override {
        if (callback_ != nullptr)
            callback_->audioDeviceStopped();
        callback_ = nullptr;
    }
    bool isPlaying() override {
        return callback_ != nullptr;
    }
    juce::String getLastError() override {
        return {};
    }
    int getCurrentBufferSizeSamples() override {
        return 512;
    }
    double getCurrentSampleRate() override {
        return 48000.0;
    }
    int getCurrentBitDepth() override {
        return 32;
    }
    juce::BigInteger getActiveOutputChannels() const override {
        return outputs_;
    }
    juce::BigInteger getActiveInputChannels() const override {
        return inputs_;
    }
    int getOutputLatencyInSamples() override {
        return 0;
    }
    int getInputLatencyInSamples() override {
        return 0;
    }

  private:
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::BigInteger inputs_, outputs_;
    bool open_ = false;
};

class QuadBackend final : public juce::AudioIODeviceType {
  public:
    QuadBackend() : juce::AudioIODeviceType("Fake") {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool = false) const override {
        return {"Quad"};
    }
    int getDefaultDeviceIndex(bool) const override {
        return 0;
    }
    int getIndexOfDevice(juce::AudioIODevice* device, bool) const override {
        return device != nullptr && device->getName() == "Quad" ? 0 : -1;
    }
    bool hasSeparateInputsAndOutputs() const override {
        return true;
    }
    juce::AudioIODevice* createDevice(const juce::String& output,
                                      const juce::String& input) override {
        return output == "Quad" || input == "Quad" ? new QuadInterface() : nullptr;
    }
};

/** @brief A settings folder of the test's own, removed once the engine has saved into it. */
struct ScratchFolder {
    ~ScratchFolder() {
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile(appName)
            .deleteRecursively();
    }

    juce::String appName = "MAGDA-test-" + juce::Uuid().toDashedString();
};

/** @brief A Tracktion engine on the quad backend, with a Settings.xml of its own. */
struct Rig {
    Rig() {
        auto& devices = engine.getDeviceManager();
        devices.deviceManager.addAudioDeviceType(std::make_unique<QuadBackend>());
        devices.initialise(256, 256);
        devices.dispatchPendingUpdates();
    }

    ~Rig() {
        audioIO.reset();
        engine.getDeviceManager().closeDevices();
    }

    ScratchFolder scratch;
    tracktion::Engine engine{std::make_unique<tracktion::PropertyStorage>(scratch.appName), nullptr,
                             std::make_unique<magda::MagdaEngineBehaviour>()};
    std::unique_ptr<magda::TracktionAudioIO> audioIO =
        std::make_unique<magda::TracktionAudioIO>(engine.getDeviceManager());
};

}  // namespace

class TracktionAudioIOTest final : public juce::UnitTest {
  public:
    TracktionAudioIOTest() : juce::UnitTest("Tracktion Audio IO", "magda") {}

    void runTest() override {
        Rig rig;

        beginTest("the chosen channels are the wave devices enabled, over every channel open");
        {
            const auto error = rig.audioIO->apply({.backend = "Fake",
                                                   .inputInterface = "Quad",
                                                   .outputInterface = "Quad",
                                                   .sampleRate = 48000.0,
                                                   .bufferSize = 512,
                                                   .inputChannels = {1},
                                                   .outputChannels = {2, 3}});
            expect(error.isEmpty(), error);

            const auto setup = rig.engine.getDeviceManager().deviceManager.getAudioDeviceSetup();
            expect(setup.outputChannels == channels({0, 1, 2, 3}));
            expect(setup.inputChannels == channels({0, 1, 2, 3}));
            expect(rig.audioIO->outputs().open == channels({2, 3}));
            expect(rig.audioIO->inputs().open == channels({1}));
            expect(rig.audioIO->chosen().outputChannels == std::vector<int>{2, 3});
        }

        beginTest("Tracktion names channels as the native engine does");
        {
            expect(rig.audioIO
                       ->apply({.backend = "Fake",
                                .inputInterface = "Quad",
                                .outputInterface = "Quad",
                                .sampleRate = 48000.0,
                                .bufferSize = 512,
                                .inputChannels = {0, 1, 2, 3},
                                .outputChannels = {0, 1, 2, 3}})
                       .isEmpty());

            for (const auto inputs : {false, true}) {
                const auto direction = inputs ? rig.audioIO->inputs() : rig.audioIO->outputs();
                expect(direction.routeNames == magda::routeNamesByChannel(direction.channelNames,
                                                                          direction.open, inputs),
                       inputs ? "input names" : "output names");
            }
        }
    }
};

namespace {
TracktionAudioIOTest tracktionAudioIOTest;
}  // namespace
