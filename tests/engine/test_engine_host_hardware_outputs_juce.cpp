#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/audio/io/AudioIOService.hpp"
#include "magda/daw/audio/io/HardwareRouteNames.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineHost.hpp"

namespace {

constexpr int kPhysicalOutputs = 64;
constexpr int kBlockSize = 480;

magda::DeviceInfo polySynth(magda::DeviceId id) {
    using PolySynth = magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;

    magda::DeviceInfo device;
    device.id = id;
    device.name = "Poly Synth";
    device.pluginId = PolySynth::xmlTypeName;
    device.deviceType = magda::DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    device.format = magda::PluginFormat::Internal;
    device.audioOutputChannels = 2;

    const PolySynth metadata;
    for (auto index = 0; index < metadata.parameterCount(); ++index) {
        auto info = metadata.parameterInfo(index);
        info.currentValue = info.defaultValue;
        device.parameters.push_back(std::move(info));
    }
    return device;
}

juce::BigInteger channels(std::initializer_list<int> set) {
    juce::BigInteger result;
    for (const auto channel : set)
        result.setBit(channel);
    return result;
}

class OutputPumpDevice final : public juce::AudioIODevice {
  public:
    OutputPumpDevice() : juce::AudioIODevice("Output Pump", "Output Pump") {}

    juce::StringArray getOutputChannelNames() override {
        juce::StringArray names;
        for (auto channel = 0; channel < kPhysicalOutputs; ++channel)
            names.add("Output " + juce::String(channel + 1));
        return names;
    }
    juce::StringArray getInputChannelNames() override {
        return {};
    }
    juce::Array<double> getAvailableSampleRates() override {
        return {48000.0};
    }
    juce::Array<int> getAvailableBufferSizes() override {
        return {kBlockSize};
    }
    int getDefaultBufferSize() override {
        return kBlockSize;
    }
    juce::String open(const juce::BigInteger&, const juce::BigInteger& outputs, double,
                      int) override {
        if (!outputs.isZero())
            active_ = outputs;
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
        return kBlockSize;
    }
    double getCurrentSampleRate() override {
        return 48000.0;
    }
    int getCurrentBitDepth() override {
        return 32;
    }
    juce::BigInteger getActiveOutputChannels() const override {
        return active_;
    }
    juce::BigInteger getActiveInputChannels() const override {
        return {};
    }
    int getOutputLatencyInSamples() override {
        return 0;
    }
    int getInputLatencyInSamples() override {
        return 0;
    }

    void restart(const juce::BigInteger& active) {
        if (callback_ != nullptr)
            callback_->audioDeviceStopped();
        active_ = active;
        if (callback_ != nullptr)
            callback_->audioDeviceAboutToStart(this);
    }

    std::array<float, kPhysicalOutputs> pump(int nullPhysicalChannel = -1) {
        for (auto& channel : buffers_)
            channel.fill(0.0f);

        std::array<float*, kPhysicalOutputs> packed{};
        auto count = 0;
        for (auto physical = 0; physical < kPhysicalOutputs; ++physical) {
            if (!active_[physical])
                continue;
            packed[static_cast<std::size_t>(count++)] =
                physical == nullPhysicalChannel ? nullptr : buffers_[physical].data();
        }
        if (callback_ != nullptr)
            callback_->audioDeviceIOCallbackWithContext(nullptr, 0, packed.data(), count,
                                                        kBlockSize, {});

        std::array<float, kPhysicalOutputs> peaks{};
        for (auto physical = 0; physical < kPhysicalOutputs; ++physical)
            for (const auto sample : buffers_[physical])
                peaks[physical] = std::max(peaks[physical], std::abs(sample));
        return peaks;
    }

  private:
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::BigInteger active_ = channels({0, 1, 2, 3});
    std::array<std::array<float, kBlockSize>, kPhysicalOutputs> buffers_{};
    bool open_ = false;
};

class OutputPumpType final : public juce::AudioIODeviceType {
  public:
    explicit OutputPumpType(OutputPumpDevice*& device)
        : juce::AudioIODeviceType("Output Pump"), device_(device) {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool input) const override {
        return input ? juce::StringArray{} : juce::StringArray{"Output Pump"};
    }
    int getDefaultDeviceIndex(bool) const override {
        return 0;
    }
    int getIndexOfDevice(juce::AudioIODevice* device, bool) const override {
        return device == device_ ? 0 : -1;
    }
    bool hasSeparateInputsAndOutputs() const override {
        return true;
    }
    juce::AudioIODevice* createDevice(const juce::String& output, const juce::String&) override {
        if (output != "Output Pump")
            return nullptr;
        auto device = std::make_unique<OutputPumpDevice>();
        device_ = device.get();
        return device.release();
    }

  private:
    OutputPumpDevice*& device_;
};

class OutputPumpManager final : public juce::AudioDeviceManager {
  public:
    OutputPumpDevice* device = nullptr;

  protected:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override {
        types.add(new OutputPumpType(device));
    }
};

class EngineHostHardwareOutputTest final : public juce::UnitTest {
  public:
    EngineHostHardwareOutputTest() : juce::UnitTest("Engine Host Hardware Outputs", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { routesToPackedHardwareChannels(); });
        magda::test::runWithCleanJuceState([this] { followsTheAudioInterface(); });
    }

  private:
    static void settle(magda::daw::engine_host::EngineHost& host) {
        for (auto tick = 0; tick < 400 && !host.isSettled(); ++tick)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }

    static std::array<float, kPhysicalOutputs> sound(magda::daw::engine_host::EngineHost& host,
                                                     OutputPumpDevice& device, magda::TrackId track,
                                                     int note = 60) {
        host.audition(track, juce::MidiMessage::noteOn(1, note, (juce::uint8)100));
        std::array<float, kPhysicalOutputs> peaks{};
        for (auto block = 0; block < 8; ++block) {
            const auto next = device.pump();
            for (auto channel = 0; channel < kPhysicalOutputs; ++channel)
                peaks[channel] = std::max(peaks[channel], next[channel]);
        }
        host.audition(track, juce::MidiMessage::noteOff(1, note));
        device.pump();
        return peaks;
    }

    void expectOnly(const std::array<float, kPhysicalOutputs>& peaks,
                    std::initializer_list<int> sounding) {
        for (auto channel = 0; channel < kPhysicalOutputs; ++channel) {
            const auto wanted = std::ranges::find(sounding, channel) != sounding.end();
            if (wanted)
                expect(peaks[channel] > 0.0001f,
                       "physical output " + juce::String(channel + 1) + " should sound");
            else
                expectWithinAbsoluteError(peaks[channel], 0.0f, 0.000001f,
                                          "unexpected signal on physical output " +
                                              juce::String(channel + 1));
        }
    }

    void routesToPackedHardwareChannels() {
        beginTest("named, legacy, mono, missing and remapped hardware outputs");

        OutputPumpManager devices;
        expect(devices.initialise(0, kPhysicalOutputs, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;

        auto catalog = magda::daw::engine_host::EngineHost::HardwareChannelCatalog{
            .enabledChannels = channels({0, 1, 2, 3}),
            .namesByChannel = {{0, "Main"}, {1, "Main"}, {2, "Cue"}, {3, "Cue"}}};

        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Hardware routed");
        tracks.getTrack(track)->chain.fxChainElements.emplace_back(polySynth(1));
        tracks.setTrackAudioOutput(track, "stereo:Cue");

        magda::daw::engine_host::EngineHost host;
        host.setHardwareOutputProvider([&catalog] { return catalog; });
        host.start(devices);
        settle(host);

        expectOnly(sound(host, *devices.device, track), {2, 3});

        tracks.setTrackAudioOutput(track, "Cue");
        settle(host);
        expectOnly(sound(host, *devices.device, track, 62), {2, 3});

        catalog = {.enabledChannels = channels({2, 3, 4, 5}),
                   .namesByChannel = {{2, "Cue"}, {3, "Cue"}, {4, "Aux 5"}, {5, "Mono 6"}}};
        devices.device->restart(catalog.enabledChannels);
        expectOnly(devices.device->pump(), {});
        settle(host);
        expectOnly(sound(host, *devices.device, track, 64), {2, 3});

        tracks.setTrackAudioOutput(track, "stereo:Out 3");
        settle(host);
        expectOnly(sound(host, *devices.device, track, 65), {2, 3});

        tracks.setTrackAudioOutput(track, "Mono 6");
        settle(host);
        expectOnly(sound(host, *devices.device, track, 67), {5});
        devices.device->pump(5);

        catalog.enabledChannels = channels({2, 3});
        host.refreshHardwareOutputs();
        expectOnly(devices.device->pump(), {});
        settle(host);
        expectOnly(sound(host, *devices.device, track, 68), {});

        catalog.enabledChannels = {};
        host.refreshHardwareOutputs();
        settle(host);
        expectOnly(sound(host, *devices.device, track, 68), {});

        catalog.enabledChannels = channels({2, 3, 4, 5});
        host.refreshHardwareOutputs();
        expectOnly(devices.device->pump(), {});
        settle(host);
        expectOnly(sound(host, *devices.device, track, 68), {5});

        tracks.setTrackAudioOutput(track, "stereo:Unavailable");
        settle(host);
        expectOnly(sound(host, *devices.device, track, 69), {});

        juce::BigInteger wideChannels;
        wideChannels.setRange(0, kPhysicalOutputs, true);
        catalog = {.enabledChannels = channels({62, 63}),
                   .namesByChannel = {{62, "Out 63 + 64"}, {63, "Out 63 + 64"}}};
        tracks.setTrackAudioOutput(track, "stereo:Out 63 + 64");
        devices.device->restart(wideChannels);
        expectOnly(devices.device->pump(), {});
        settle(host);
        expectOnly(sound(host, *devices.device, track, 71), {62, 63});

        host.stop();
        devices.closeAudioDevice();
    }

    /** @brief The native engine's wiring: routes named as saved, masks from what is open. */
    struct AudioIORefresh final : magda::AudioIOService::Listener {
        explicit AudioIORefresh(magda::daw::engine_host::EngineHost& host) : host(host) {}
        void audioIOChanged() override {
            host.refreshHardwareOutputs();
        }
        magda::daw::engine_host::EngineHost& host;
    };

    static magda::AudioIOSettings pumpOutputs(std::vector<int> outputs) {
        return {.backend = "Output Pump",
                .inputInterface = {},
                .outputInterface = "Output Pump",
                .sampleRate = 48000.0,
                .bufferSize = kBlockSize,
                .inputChannels = {},
                .outputChannels = std::move(outputs)};
    }

    void followsTheAudioInterface() {
        beginTest("native outputs follow what AudioIOService opens, across a reopen");

        OutputPumpDevice* device = nullptr;
        std::vector<std::unique_ptr<juce::AudioIODeviceType>> backends;
        backends.push_back(std::make_unique<OutputPumpType>(device));
        magda::AudioIOService audioIO(std::move(backends), juce::File());
        magda::Config::getInstance().setAudioIO(pumpOutputs({2, 3}));
        audioIO.open();
        expect(device != nullptr, "the service opened the pump");
        if (device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Hardware routed");
        tracks.getTrack(track)->chain.fxChainElements.emplace_back(polySynth(1));
        tracks.setTrackAudioOutput(track, "stereo:Output 3 + 4");

        magda::daw::engine_host::EngineHost host;
        host.setHardwareOutputProvider([&audioIO] {
            const auto active = audioIO.getActiveConfiguration();
            return magda::daw::engine_host::EngineHost::HardwareChannelCatalog{
                .enabledChannels = active.outputChannels,
                .namesByChannel = magda::routeNamesByChannel(active.outputChannelNames,
                                                             active.outputChannels, false)};
        });
        AudioIORefresh refresh(host);
        audioIO.addListener(&refresh);
        host.start(audioIO.getDeviceManager());
        settle(host);

        expectOnly(sound(host, *device, track), {2, 3});

        expect(audioIO.apply(pumpOutputs({4, 5})).isEmpty());
        tracks.setTrackAudioOutput(track, "stereo:Output 5 + 6");
        settle(host);
        expectOnly(sound(host, *device, track, 62), {4, 5});

        audioIO.removeListener(&refresh);
        host.stop();
        magda::Config::getInstance().setAudioIO(std::nullopt);
    }
};

EngineHostHardwareOutputTest engineHostHardwareOutputTest;

}  // namespace
