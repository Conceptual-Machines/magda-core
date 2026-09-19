#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

#include "magda/daw/audio/io/AudioIOService.hpp"
#include "magda/daw/core/Config.hpp"

/// @file AudioIOService opening only the channels asked for (#2746).

namespace {

constexpr int kVirtualChannels = 128;
constexpr int kInputLatency = 96;

juce::BigInteger channels(std::initializer_list<int> set) {
    juce::BigInteger result;
    for (const auto channel : set)
        result.setBit(channel);
    return result;
}

juce::StringArray numbered(const juce::String& prefix, int count) {
    juce::StringArray names;
    for (auto channel = 1; channel <= count; ++channel)
        names.add(prefix + " " + juce::String(channel));
    return names;
}

/** @brief What the fake interfaces were last asked to open, which is what the driver sees. */
struct OpenRequest {
    juce::String backend;
    juce::String interfaceName;
    juce::BigInteger inputs;
    juce::BigInteger outputs;
    double sampleRate = 0.0;
    int bufferSize = 0;
    int opens = 0;
};

class FakeInterface final : public juce::AudioIODevice {
  public:
    FakeInterface(const juce::String& name, const juce::String& backend, int channelCount,
                  std::shared_ptr<OpenRequest> request)
        : juce::AudioIODevice(name, backend),
          channelCount_(channelCount),
          request_(std::move(request)) {}

    juce::StringArray getOutputChannelNames() override {
        return numbered("Out", channelCount_);
    }
    juce::StringArray getInputChannelNames() override {
        return numbered("In", channelCount_);
    }
    juce::Array<double> getAvailableSampleRates() override {
        return {44100.0, 48000.0};
    }
    juce::Array<int> getAvailableBufferSizes() override {
        return {256, 512};
    }
    int getDefaultBufferSize() override {
        return 512;
    }
    juce::String open(const juce::BigInteger& inputs, const juce::BigInteger& outputs,
                      double sampleRate, int bufferSize) override {
        if (getName() == "Broken")
            return "The interface refused to open";

        *request_ = {.backend = getTypeName(),
                     .interfaceName = getName(),
                     .inputs = inputs,
                     .outputs = outputs,
                     .sampleRate = sampleRate,
                     .bufferSize = bufferSize,
                     .opens = request_->opens + 1};
        inputs_ = inputs;
        outputs_ = outputs;
        sampleRate_ = sampleRate;
        bufferSize_ = bufferSize;
        open_ = true;
        return {};
    }
    void close() override {
        stop();
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
        return bufferSize_;
    }
    double getCurrentSampleRate() override {
        return sampleRate_;
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
        return kInputLatency;
    }

  private:
    int channelCount_;
    std::shared_ptr<OpenRequest> request_;
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::BigInteger inputs_, outputs_;
    double sampleRate_ = 44100.0;
    int bufferSize_ = 512;
    bool open_ = false;
};

/** @brief Its default, "Virtual", has 128 channels each way, like #2528's ALSA endpoint. */
class FakeBackend final : public juce::AudioIODeviceType {
  public:
    FakeBackend(const juce::String& name, std::shared_ptr<OpenRequest> request)
        : juce::AudioIODeviceType(name), request_(std::move(request)) {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool = false) const override {
        return {"Virtual", "Stereo", "Broken"};
    }
    int getDefaultDeviceIndex(bool) const override {
        return 0;
    }
    int getIndexOfDevice(juce::AudioIODevice* device, bool) const override {
        return device != nullptr ? getDeviceNames().indexOf(device->getName()) : -1;
    }
    bool hasSeparateInputsAndOutputs() const override {
        return true;
    }
    juce::AudioIODevice* createDevice(const juce::String& output,
                                      const juce::String& input) override {
        const auto name = output.isNotEmpty() ? output : input;
        if (!getDeviceNames().contains(name))
            return nullptr;
        return new FakeInterface(name, getTypeName(), name == "Virtual" ? kVirtualChannels : 2,
                                 request_);
    }

  private:
    std::shared_ptr<OpenRequest> request_;
};

/** @brief A service on the fake backend, starting from @p saved and no Tracktion settings. */
struct Rig {
    explicit Rig(std::optional<magda::AudioIOSettings> saved) {
        magda::Config::getInstance().setAudioIO(std::move(saved));

        std::vector<std::unique_ptr<juce::AudioIODeviceType>> backends;
        // The same interfaces on both, as Windows Audio's shared and exclusive modes have.
        backends.push_back(std::make_unique<FakeBackend>("Fake", request));
        backends.push_back(std::make_unique<FakeBackend>("Fake Exclusive", request));
        service = std::make_unique<magda::AudioIOService>(std::move(backends),
                                                          tracktionSettings.getFile());
    }

    ~Rig() {
        service.reset();
        magda::Config::getInstance().setAudioIO(std::nullopt);
    }

    std::shared_ptr<OpenRequest> request = std::make_shared<OpenRequest>();
    juce::TemporaryFile tracktionSettings{".xml"};
    std::unique_ptr<magda::AudioIOService> service;
};

magda::AudioIOSettings savedOn(const std::string& interfaceName, std::vector<int> inputs,
                               std::vector<int> outputs) {
    return {.backend = "Fake",
            .inputInterface = interfaceName,
            .outputInterface = interfaceName,
            .sampleRate = 48000.0,
            .bufferSize = 256,
            .inputChannels = std::move(inputs),
            .outputChannels = std::move(outputs)};
}

}  // namespace

class AudioIOServiceTest final : public juce::UnitTest {
  public:
    AudioIOServiceTest() : juce::UnitTest("Audio IO Service", "magda") {}

    void runTest() override {
        beginTest("two outputs selected on 128 advertised opens two outputs and no inputs");
        {
            Rig rig(savedOn("Virtual", {}, {0, 1}));
            rig.service->open();

            expect(rig.request->interfaceName == "Virtual");
            expect(rig.request->outputs == channels({0, 1}));
            expect(rig.request->inputs.isZero());

            const auto active = rig.service->getActiveConfiguration();
            expect(active.outputChannels == channels({0, 1}));
            expect(active.inputChannels.isZero());
            expectEquals(active.inputInterface, juce::String());
        }

        beginTest("a fresh configuration opens stereo out and no inputs");
        {
            Rig rig(std::nullopt);
            rig.service->open();

            expect(rig.request->interfaceName == "Virtual");
            expect(rig.request->outputs == channels({0, 1}));
            expect(rig.request->inputs.isZero());
            expect(!magda::Config::getInstance().getAudioIO().has_value(),
                   "nothing was chosen, so nothing is saved");
        }

        beginTest("saved channels the interface does not have are dropped");
        {
            Rig rig(savedOn("Stereo", {0, 5}, {0, 1, 2, 3, 7}));
            rig.service->open();

            expect(rig.request->interfaceName == "Stereo");
            expect(rig.request->outputs == channels({0, 1}));
            expect(rig.request->inputs == channels({0}));
            expectEquals(rig.request->sampleRate, 48000.0);
            expectEquals(rig.request->bufferSize, 256);
        }

        beginTest("an interface that is gone falls back to the default at stereo out");
        {
            Rig rig(savedOn("Unplugged", {0}, {4, 5}));
            rig.service->open();

            expect(rig.request->interfaceName == "Virtual");
            expect(rig.request->outputs == channels({0, 1}));
            expect(rig.request->inputs.isZero());
            expect(magda::Config::getInstance().getAudioIO() == savedOn("Unplugged", {0}, {4, 5}),
                   "the choice outlives the interface being away");
        }

        beginTest("a backend that is gone falls back to the default one");
        {
            auto saved = savedOn("Stereo", {0}, {0, 1});
            saved.backend = "Uninstalled";
            Rig rig(saved);
            rig.service->open();

            expect(rig.request->interfaceName == "Virtual");
            expect(rig.request->outputs == channels({0, 1}));
            expect(rig.request->inputs.isZero());
        }

        beginTest("a failed open leaves nothing open and the service usable");
        {
            Rig rig(savedOn("Broken", {}, {0, 1}));
            rig.service->open();

            expectEquals(rig.request->opens, 0);
            expect(rig.service->getActiveConfiguration().backend.isEmpty());

            expect(rig.service->apply(savedOn("Stereo", {}, {1})).isEmpty());
            expect(rig.request->outputs == channels({1}));
        }

        beginTest("apply opens exactly what it is given and saves it");
        {
            Rig rig(std::nullopt);
            rig.service->open();

            const auto chosen = savedOn("Virtual", {2, 3}, {4, 5});
            expect(rig.service->apply(chosen).isEmpty());
            expect(rig.request->inputs == channels({2, 3}));
            expect(rig.request->outputs == channels({4, 5}));
            expect(magda::Config::getInstance().getAudioIO() == chosen);
        }

        beginTest("switching backend reopens an interface of the same name on the new one");
        {
            Rig rig(savedOn("Stereo", {}, {0, 1}));
            rig.service->open();
            expectEquals(rig.request->backend, juce::String("Fake"));

            auto exclusive = savedOn("Stereo", {}, {0, 1});
            exclusive.backend = "Fake Exclusive";
            expect(rig.service->apply(exclusive).isEmpty());

            expectEquals(rig.request->backend, juce::String("Fake Exclusive"));
            expectEquals(rig.request->opens, 2);
            expectEquals(rig.service->getActiveConfiguration().backend,
                         juce::String("Fake Exclusive"));
        }

        beginTest("two of 128 outputs chosen are the two that reopen after a restart");
        {
            Rig before(std::nullopt);
            before.service->open();
            expect(before.service->apply(savedOn("Virtual", {}, {6, 7})).isEmpty());
            expect(before.request->outputs == channels({6, 7}));

            // A second service over the same settings, as the next launch builds one.
            std::vector<std::unique_ptr<juce::AudioIODeviceType>> backends;
            backends.push_back(std::make_unique<FakeBackend>("Fake", before.request));
            magda::AudioIOService after(std::move(backends), juce::File());
            before.service.reset();
            after.open();
            expect(before.request->outputs == channels({6, 7}));
            expect(before.request->inputs.isZero());
            expect(after.chosen() == savedOn("Virtual", {}, {6, 7}));
        }

        beginTest("the active configuration reports what the interface opened");
        {
            Rig rig(savedOn("Virtual", {6, 7}, {0, 1}));
            rig.service->open();

            const auto active = rig.service->getActiveConfiguration();
            expectEquals(active.backend, juce::String("Fake"));
            expectEquals(active.inputInterface, juce::String("Virtual"));
            expectEquals(active.outputInterface, juce::String("Virtual"));
            expectEquals(active.sampleRate, 48000.0);
            expectEquals(active.bufferSize, 256);
            expect(active.inputChannels == channels({6, 7}));
            expectEquals(active.inputChannelNames.size(), kVirtualChannels);
            expectEquals(active.inputChannelNames[6], juce::String("In 7"));
            expectEquals(active.outputChannelNames[1], juce::String("Out 2"));
            expectEquals(active.inputLatencySamples, kInputLatency);
        }

        beginTest("enumeration names backends, interfaces and channels without opening them");
        {
            Rig rig(std::nullopt);

            expect(rig.service->getBackendNames() == juce::StringArray{"Fake", "Fake Exclusive"});
            expect(rig.service->getInterfaceNames("Fake", false) ==
                   juce::StringArray{"Virtual", "Stereo", "Broken"});
            expectEquals(rig.service->getChannelNames("Fake", "Stereo", true).size(), 2);
            expectEquals(rig.request->opens, 0);
        }

        beginTest("what Tracktion saved opens and is saved as masks");
        {
            Rig rig(std::nullopt);
            const auto allOn = juce::String::repeatedString("1", 256);
            expect(rig.tracktionSettings.getFile().replaceWithText(
                R"(<?xml version="1.0" encoding="UTF-8"?>
<PROPERTIES>
  <VALUE name="audio_device_setup">
    <DEVICESETUP deviceType="Fake" audioOutputDeviceName="Stereo" audioInputDeviceName="Stereo"
                 audioDeviceRate="48000.0" audioDeviceBufferSize="256"/>
  </VALUE>
  <VALUE name="audiosettings_Fake">
    <AUDIODEVICE outEnabled=")" +
                allOn + R"(" inEnabled="10" monoChansOut="0" stereoChansIn="0"/>
  </VALUE>
</PROPERTIES>
)"));
            rig.service->open();

            expect(rig.request->interfaceName == "Stereo");
            expect(rig.request->inputs == channels({1}));
            expect(rig.request->outputs == channels({0, 1}));
            expectEquals(rig.request->sampleRate, 48000.0);
            expectEquals(rig.request->bufferSize, 256);
            expect(magda::Config::getInstance().getAudioIO() == savedOn("Stereo", {1}, {0, 1}),
                   "saved cut to the channels the interface has");
        }

        beginTest("listeners hear the interface change");
        {
            struct Heard final : magda::HardwareChannels::Listener {
                void hardwareChannelsChanged() override {
                    ++changes;
                }
                int changes = 0;
            } heard;

            Rig rig(std::nullopt);
            rig.service->addListener(&heard);
            rig.service->open();

            for (auto tick = 0; tick < 200 && heard.changes == 0; ++tick)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(5);

            expect(heard.changes > 0);
            rig.service->removeListener(&heard);
        }
    }
};

static AudioIOServiceTest audioIOServiceTest;
