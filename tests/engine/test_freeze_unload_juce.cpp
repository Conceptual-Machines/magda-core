// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaChorusCompiledPlugin.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineHost.hpp"
#include "magda/daw/engine/host/TrackFreeze.hpp"

/**
 * @file test_freeze_unload_juce.cpp
 * @brief Freezing lets a track's devices go, and unfreezing brings them back as they were (#2067).
 */

namespace {

namespace host = magda::daw::engine_host;

/** @brief The plugin state a project cannot see, which only its chunk carries. */
class RememberingPlugin final : public juce::AudioPluginInstance {
  public:
    RememberingPlugin()
        : AudioPluginInstance(BusesProperties()
                                  .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        ++alive;
        latest = this;
    }

    ~RememberingPlugin() override {
        --alive;
        if (latest == this)
            latest = nullptr;
    }

    inline static int alive = 0;
    inline static RememberingPlugin* latest = nullptr;
    inline static bool failsToSave = false;

    float remembered = 0.0f;

    static juce::PluginDescription description() {
        juce::PluginDescription described;
        described.name = "Remembering";
        described.descriptiveName = described.name;
        described.pluginFormatName = "Remembering Format";
        described.category = "Effect";
        described.manufacturerName = "MAGDA Tests";
        described.version = "1.0.0";
        described.fileOrIdentifier = "remembering";
        described.numInputChannels = 2;
        described.numOutputChannels = 2;
        described.uniqueId = 0x2067;
        return described;
    }

    const juce::String getName() const override {
        return "Remembering";
    }
    void fillInPluginDescription(juce::PluginDescription& described) const override {
        described = description();
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

    void getStateInformation(juce::MemoryBlock& destination) override {
        if (failsToSave)
            throw std::runtime_error("plugin state handler failed");
        destination.replaceAll(&remembered, sizeof(remembered));
    }

    void setStateInformation(const void* data, int size) override {
        if (size == static_cast<int>(sizeof(remembered)))
            std::memcpy(&remembered, data, sizeof(remembered));
    }
};

class RememberingFormat final : public juce::AudioPluginFormat {
  public:
    juce::String getName() const override {
        return "Remembering Format";
    }
    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>&,
                             const juce::String&) override {}
    bool fileMightContainThisPluginType(const juce::String&) override {
        return true;
    }
    juce::String getNameOfPluginFromIdentifier(const juce::String&) override {
        return "Remembering";
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

  private:
    void createPluginInstance(const juce::PluginDescription&, double, int,
                              PluginCreationCallback callback) override {
        callback(std::make_unique<RememberingPlugin>(), {});
    }
};

class PumpDevice final : public juce::AudioIODevice {
  public:
    PumpDevice() : juce::AudioIODevice("Pump", "Pump") {}

    juce::StringArray getOutputChannelNames() override {
        return {"Left", "Right"};
    }
    juce::StringArray getInputChannelNames() override {
        return {};
    }
    juce::Array<double> getAvailableSampleRates() override {
        return {48000.0};
    }
    juce::Array<int> getAvailableBufferSizes() override {
        return {480};
    }
    int getDefaultBufferSize() override {
        return 480;
    }
    juce::String open(const juce::BigInteger&, const juce::BigInteger& outputs, double,
                      int) override {
        outputs_ = outputs;
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
        return 480;
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
        return {};
    }
    int getOutputLatencyInSamples() override {
        return 0;
    }
    int getInputLatencyInSamples() override {
        return 0;
    }

    void pump() {
        if (callback_ == nullptr)
            return;
        std::array<float, 480> left{}, right{};
        float* outputs[] = {left.data(), right.data()};
        callback_->audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 480, {});
    }

  private:
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::BigInteger outputs_;
    bool open_ = false;
};

class PumpType final : public juce::AudioIODeviceType {
  public:
    explicit PumpType(PumpDevice*& device) : juce::AudioIODeviceType("Pump"), device_(device) {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool input) const override {
        return input ? juce::StringArray{} : juce::StringArray{"Pump"};
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
        if (output != "Pump")
            return nullptr;
        auto device = std::make_unique<PumpDevice>();
        device_ = device.get();
        return device.release();
    }

  private:
    PumpDevice*& device_;
};

class PumpManager final : public juce::AudioDeviceManager {
  public:
    PumpDevice* device = nullptr;

  protected:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override {
        types.add(new PumpType(device));
    }
};

magda::DeviceInfo rememberingDevice(magda::DeviceId id) {
    const auto described = RememberingPlugin::description();

    magda::DeviceInfo device;
    device.id = id;
    device.name = described.name;
    device.pluginId = described.createIdentifierString();
    device.manufacturer = described.manufacturerName;
    device.format = magda::PluginFormat::VST3;
    device.uniqueId = described.createIdentifierString();
    device.fileOrIdentifier = described.fileOrIdentifier;
    device.deviceType = magda::DeviceType::Effect;
    device.audioInputChannels = 2;
    device.audioOutputChannels = 2;
    return device;
}

magda::DeviceInfo chorus(magda::DeviceId id) {
    using Chorus = magda::daw::audio::compiled::MagdaChorusCompiledPlugin;

    magda::DeviceInfo device;
    device.id = id;
    device.name = "Chorus";
    device.pluginId = Chorus::xmlTypeName;
    device.deviceType = magda::DeviceType::Effect;
    device.format = magda::PluginFormat::Internal;
    device.audioInputChannels = 2;
    device.audioOutputChannels = 2;
    return device;
}

/** @brief A file for @p trackId to play once frozen, without rendering one. */
juce::File writeFreezeFile(magda::TrackId trackId) {
    const auto file = host::freezeFileFor(trackId);
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    constexpr auto samples = 4800;
    juce::AudioBuffer<float> silence(2, samples);
    silence.clear();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                  .withSampleRate(48000.0)
                                                  .withNumChannels(2)
                                                  .withBitsPerSample(32));
    writer->writeFromAudioSampleBuffer(silence, 0, samples);
    return file;
}

class FreezeUnloadTest final : public juce::UnitTest {
  public:
    FreezeUnloadTest() : juce::UnitTest("Freeze Unload", "magda") {}

    void initialise() override {
        formats_.addFormat(std::make_unique<RememberingFormat>());
        known_.addType(RememberingPlugin::description());
    }

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { unfreezeRestoresWhatTheFreezeRead(); });
        magda::test::runWithCleanJuceState([this] { pluginThatCannotBeReadStaysLoaded(); });
        magda::test::runWithCleanJuceState([this] { freezeWithoutUnloadKeepsThePlugin(); });
        magda::test::runWithCleanJuceState([this] { reopenedFrozenProjectLoadsNothing(); });
        magda::test::runWithCleanJuceState([this] { onlyWhatTheFileHoldsIsReleased(); });
        magda::test::runWithCleanJuceState([this] { chainFrozenFollowsTheOutputRoute(); });
    }

  private:
    using Host = host::EngineHost;

    static void settle(Host& engineHost, PumpDevice& device) {
        for (auto tick = 0; tick < 400; ++tick) {
            device.pump();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
            if (engineHost.isSettled())
                return;
        }
    }

    /** @brief Pump until @p done, since a hosted plugin loads over several message-loop turns. */
    static bool settleUntil(Host& engineHost, PumpDevice& device,
                            const std::function<bool()>& done) {
        for (auto tick = 0; tick < 400 && !done(); ++tick)
            settle(engineHost, device);
        return done();
    }

    bool start(Host& engineHost, PumpManager& devices) {
        expect(devices.initialise(0, 2, nullptr, true).isEmpty() && devices.device != nullptr,
               "The device opens");
        if (devices.device == nullptr)
            return false;

        engineHost.setPluginServices(formats_, known_);
        engineHost.start(devices);
        return true;
    }

    /** @brief A track holding the plugin, with that plugin loaded and a freeze file on disk. */
    magda::TrackId trackWithLoadedPlugin(Host& engineHost, PumpDevice& device) {
        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Frozen");
        tracks.getTrack(trackId)->chain.fxChainElements.emplace_back(rememberingDevice(1));
        tracks.notifyTrackDevicesChanged(trackId);
        writeFreezeFile(trackId);

        expect(settleUntil(engineHost, device, [] { return RememberingPlugin::alive == 1; }),
               "The plugin loads");
        return trackId;
    }

    void unfreezeRestoresWhatTheFreezeRead() {
        beginTest("Freezing closes a plugin, and unfreezing brings back what it held");
        PumpManager devices;
        Host engineHost;
        if (!start(engineHost, devices))
            return;

        auto& device = *devices.device;
        const auto trackId = trackWithLoadedPlugin(engineHost, device);
        if (RememberingPlugin::latest == nullptr)
            return;
        RememberingPlugin::latest->remembered = 0.7f;

        auto& tracks = magda::TrackManager::getInstance();
        engineHost.unloadDevicesBakedInto(trackId);
        tracks.setTrackFrozen(trackId, true);

        expect(settleUntil(engineHost, device, [] { return RememberingPlugin::alive == 0; }),
               "The instance is destroyed");
        const auto* frozen = tracks.getTrack(trackId);
        expect(frozen->chain.fxChainElements.size() == 1, "The model keeps its entry");
        expect(magda::getDevice(frozen->chain.fxChainElements.front()).pluginState.isNotEmpty(),
               "The model holds what the plugin held");

        tracks.setTrackFrozen(trackId, false);
        expect(settleUntil(engineHost, device, [] { return RememberingPlugin::alive == 1; }),
               "Unfreezing loads it again");
        if (RememberingPlugin::latest != nullptr)
            expectWithinAbsoluteError(RememberingPlugin::latest->remembered, 0.7f, 1.0e-6f,
                                      "It comes back as it was");

        engineHost.stop();
        devices.closeAudioDevice();
        expectEquals(RememberingPlugin::alive, 0, "Nothing outlives the host");
    }

    void pluginThatCannotBeReadStaysLoaded() {
        beginTest("A plugin whose state cannot be read stays loaded");
        PumpManager devices;
        Host engineHost;
        if (!start(engineHost, devices))
            return;

        auto& device = *devices.device;
        const auto trackId = trackWithLoadedPlugin(engineHost, device);

        RememberingPlugin::failsToSave = true;
        engineHost.unloadDevicesBakedInto(trackId);
        magda::TrackManager::getInstance().setTrackFrozen(trackId, true);
        settle(engineHost, device);
        RememberingPlugin::failsToSave = false;

        expectEquals(RememberingPlugin::alive, 1, "Its instance is the only copy left");

        engineHost.stop();
        devices.closeAudioDevice();
    }

    void freezeWithoutUnloadKeepsThePlugin() {
        beginTest("A freeze that does not unload keeps the plugin for the session");
        PumpManager devices;
        Host engineHost;
        if (!start(engineHost, devices))
            return;

        auto& device = *devices.device;
        const auto trackId = trackWithLoadedPlugin(engineHost, device);
        const auto* instance = RememberingPlugin::latest;

        magda::TrackManager::getInstance().setTrackFrozen(trackId, true);
        settle(engineHost, device);

        expect(RememberingPlugin::alive == 1 && RememberingPlugin::latest == instance,
               "The same instance is still there");

        engineHost.stop();
        devices.closeAudioDevice();
    }

    void reopenedFrozenProjectLoadsNothing() {
        beginTest("A project opened with a frozen track loads none of its plugins");
        auto& tracks = magda::TrackManager::getInstance();
        magda::TrackId trackId = magda::INVALID_TRACK_ID;

        {
            PumpManager devices;
            Host engineHost;
            if (!start(engineHost, devices))
                return;

            trackId = trackWithLoadedPlugin(engineHost, *devices.device);
            if (RememberingPlugin::latest != nullptr)
                RememberingPlugin::latest->remembered = 0.3f;
            engineHost.captureExternalPluginStates();
            engineHost.stop();
            devices.closeAudioDevice();
        }

        expectEquals(RememberingPlugin::alive, 0, "The first session is gone");
        tracks.getTrack(trackId)->frozen = true;

        PumpManager devices;
        Host engineHost;
        if (!start(engineHost, devices))
            return;

        auto& device = *devices.device;
        settle(engineHost, device);
        expectEquals(RememberingPlugin::alive, 0, "Nothing is loaded for a frozen track");

        tracks.setTrackFrozen(trackId, false);
        expect(settleUntil(engineHost, device, [] { return RememberingPlugin::alive == 1; }),
               "Unfreezing loads it for the first time");
        if (RememberingPlugin::latest != nullptr)
            expectWithinAbsoluteError(RememberingPlugin::latest->remembered, 0.3f, 1.0e-6f,
                                      "From what the project saved");

        engineHost.stop();
        devices.closeAudioDevice();
    }

    void onlyWhatTheFileHoldsIsReleased() {
        beginTest("Only what the file holds is released, and it all comes back");
        auto& tracks = magda::TrackManager::getInstance();
        const auto frozenId = tracks.createTrack("Frozen");
        const auto feedingId = tracks.createTrack("Feeding");
        const auto otherId = tracks.createTrack("Other");

        auto* frozen = tracks.getTrack(frozenId);
        frozen->chain.fxChainElements.emplace_back(chorus(1));
        frozen->chain.postFxPostFader = true;
        frozen->chain.postFxChainElements.push_back({chorus(2)});

        auto* feeding = tracks.getTrack(feedingId);
        feeding->audioOutputDevice = "track:" + juce::String(frozenId);
        feeding->chain.fxChainElements.emplace_back(chorus(3));

        tracks.getTrack(otherId)->chain.fxChainElements.emplace_back(chorus(4));
        writeFreezeFile(frozenId);

        PumpManager devices;
        Host engineHost;
        if (!start(engineHost, devices))
            return;
        auto& device = *devices.device;
        settle(engineHost, device);

        const auto chainPath = magda::ChainNodePath::topLevelDevice(frozenId, 1);
        const auto postPath = magda::ChainNodePath::postFxDevice(frozenId, 2);
        const auto feedingPath = magda::ChainNodePath::topLevelDevice(feedingId, 3);
        const auto otherPath = magda::ChainNodePath::topLevelDevice(otherId, 4);

        std::weak_ptr<magda::daw::audio::MagdaDevice> chainDevice =
            engineHost.renderedDevice(chainPath);
        std::weak_ptr<magda::daw::audio::MagdaDevice> feedingDevice =
            engineHost.renderedDevice(feedingPath);
        expect(!chainDevice.expired() && !feedingDevice.expired(), "Everything starts out built");

        engineHost.unloadDevicesBakedInto(frozenId);
        tracks.setTrackFrozen(frozenId, true);
        settle(engineHost, device);

        expect(chainDevice.expired(), "The chain before the fader is destroyed");
        expect(feedingDevice.expired(), "So are the devices on a track feeding it");
        expect(engineHost.renderedDevice(postPath) != nullptr, "After the fader still plays");
        expect(engineHost.renderedDevice(otherPath) != nullptr, "A track apart is untouched");

        tracks.setTrackFrozen(frozenId, false);
        settle(engineHost, device);

        expect(engineHost.renderedDevice(chainPath) != nullptr, "The chain is built again");
        expect(engineHost.renderedDevice(feedingPath) != nullptr, "And the track feeding it");

        engineHost.stop();
        devices.closeAudioDevice();
    }

    void chainFrozenFollowsTheOutputRoute() {
        beginTest("A chain is frozen when its track is, or when its output reaches one that is");
        auto& tracks = magda::TrackManager::getInstance();
        const auto busId = tracks.createTrack("Bus");
        const auto feedingId = tracks.createTrack("Feeding");
        const auto furtherId = tracks.createTrack("Further");
        const auto otherId = tracks.createTrack("Other");

        tracks.getTrack(feedingId)->audioOutputDevice = "track:" + juce::String(busId);
        tracks.getTrack(furtherId)->audioOutputDevice = "track:" + juce::String(feedingId);
        tracks.setTrackFrozen(busId, true);

        expect(tracks.isChainFrozen(busId));
        expect(tracks.isChainFrozen(feedingId));
        expect(tracks.isChainFrozen(furtherId));
        expect(!tracks.isChainFrozen(otherId));
        expect(!tracks.isChainFrozen(magda::INVALID_TRACK_ID));

        tracks.setTrackFrozen(busId, false);
        tracks.getTrack(busId)->audioOutputDevice = "track:" + juce::String(furtherId);
        expect(!tracks.isChainFrozen(busId), "A routing loop ends the walk");
    }

    juce::AudioPluginFormatManager formats_;
    juce::KnownPluginList known_;
};

FreezeUnloadTest freezeUnloadTest;

}  // namespace
