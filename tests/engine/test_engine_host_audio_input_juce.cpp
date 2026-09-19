#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineHost.hpp"

/// @file Hardware audio input reaching a track through the real device callback (#2553).

namespace {

constexpr int kInputs = 4;
constexpr int kBlockSize = 480;

juce::BigInteger channels(std::initializer_list<int> set) {
    juce::BigInteger result;
    for (const auto channel : set)
        result.setBit(channel);
    return result;
}

/// What physical input @p channel carries: distinct per channel, so a wrong
/// channel reads as a wrong ratio.
float levelOf(int channel) {
    return 0.1f * static_cast<float>(channel + 1);
}

struct Output {
    float left = 0.0f;
    float right = 0.0f;
};

class InputPumpDevice final : public juce::AudioIODevice {
  public:
    InputPumpDevice() : juce::AudioIODevice("Input Pump", "Input Pump") {}

    juce::StringArray getOutputChannelNames() override {
        return {"Left", "Right"};
    }
    juce::StringArray getInputChannelNames() override {
        return {"In 1", "In 2", "Loopback 1", "Loopback 2"};
    }
    juce::Array<double> getAvailableSampleRates() override {
        return {48000.0, 96000.0};
    }
    juce::Array<int> getAvailableBufferSizes() override {
        return {kBlockSize};
    }
    int getDefaultBufferSize() override {
        return kBlockSize;
    }
    juce::String open(const juce::BigInteger& inputs, const juce::BigInteger&, double sampleRate,
                      int) override {
        active_ = inputs;
        sampleRate_ = sampleRate;
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
        return sampleRate_;
    }
    int getCurrentBitDepth() override {
        return 32;
    }
    juce::BigInteger getActiveOutputChannels() const override {
        return channels({0, 1});
    }
    juce::BigInteger getActiveInputChannels() const override {
        return active_;
    }
    int getOutputLatencyInSamples() override {
        return 0;
    }
    int getInputLatencyInSamples() override {
        if (++latencyReads_ == pumpAtLatencyRead_) {
            pumpAtLatencyRead_ = 0;
            juce::MessageManager::callAsync([this] { pump(); });
        }
        return inputLatencySamples;
    }

    void restart(const juce::BigInteger& active, double sampleRate = 48000.0) {
        if (callback_ != nullptr)
            callback_->audioDeviceStopped();
        active_ = active;
        sampleRate_ = sampleRate;
        if (callback_ != nullptr)
            callback_->audioDeviceAboutToStart(this);
    }

    /// Queue one block while the host is between the rebuild and its next
    /// async reconciliation. Two reads cover the outer restart and the
    /// callback installation performed by the rebuild itself.
    void pumpAfterFutureLatencyReads(int reads) {
        pumpAtLatencyRead_ = latencyReads_ + reads;
    }

    /// One callback, inputs packed as a driver packs its active channels.
    Output pump() {
        std::array<std::array<float, kBlockSize>, kInputs> inputs{};
        std::array<const float*, kInputs> packed{};
        auto count = 0;
        for (auto physical = 0; physical < kInputs; ++physical) {
            if (!active_[physical])
                continue;
            inputs[physical].fill(levelOf(physical));
            packed[static_cast<std::size_t>(count++)] = inputs[physical].data();
        }

        std::array<float, kBlockSize> left{}, right{};
        float* outputs[] = {left.data(), right.data()};
        if (callback_ != nullptr)
            callback_->audioDeviceIOCallbackWithContext(packed.data(), count, outputs, 2,
                                                        kBlockSize, {});
        return {left.back(), right.back()};
    }

    int inputLatencySamples = 0;

  private:
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::BigInteger active_;
    double sampleRate_ = 48000.0;
    int latencyReads_ = 0;
    int pumpAtLatencyRead_ = 0;
    bool open_ = false;
};

class InputPumpType final : public juce::AudioIODeviceType {
  public:
    explicit InputPumpType(InputPumpDevice*& device)
        : juce::AudioIODeviceType("Input Pump"), device_(device) {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override {
        return {"Input Pump"};
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
        if (output != "Input Pump")
            return nullptr;
        auto device = std::make_unique<InputPumpDevice>();
        device_ = device.get();
        return device.release();
    }

  private:
    InputPumpDevice*& device_;
};

class InputPumpManager final : public juce::AudioDeviceManager {
  public:
    InputPumpDevice* device = nullptr;

  protected:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override {
        types.add(new InputPumpType(device));
    }
};

class EngineHostAudioInputTest final : public juce::UnitTest {
  public:
    EngineHostAudioInputTest() : juce::UnitTest("Engine Host Audio Input", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { monitoredInputReachesTheTrack(); });
        magda::test::runWithCleanJuceState([this] { unmonitoredInputStaysSilent(); });
        magda::test::runWithCleanJuceState([this] { restartRepacksAndMissingIsSilent(); });
        magda::test::runWithCleanJuceState([this] { recordsInputWithPreviewAndLatency(); });
        magda::test::runWithCleanJuceState([this] { disarmFinalizesRecording(); });
        magda::test::runWithCleanJuceState([this] { deviceStopFinalizesRecording(); });
        magda::test::runWithCleanJuceState([this] { deviceRestartSplitsRecording(); });
        magda::test::runWithCleanJuceState([this] { sessionTargetDoesNotRecordArrangement(); });
    }

  private:
    using Host = magda::daw::engine_host::EngineHost;

    static void settle(magda::daw::engine_host::EngineHost& host) {
        for (auto tick = 0; tick < 400 && !host.isSettled(); ++tick)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }

    static Host::HardwareChannelCatalog loopbackCatalog() {
        return {.enabledChannels = channels({0, 1, 2, 3}),
                .namesByChannel = {{0, "In 1"}, {1, "In 2"}, {2, "Loopback 1"}, {3, "Loopback 2"}}};
    }

    /// The output once the gate's ramp has settled.
    static Output steady(InputPumpDevice& device) {
        Output out;
        for (auto block = 0; block < 4; ++block)
            out = device.pump();
        return out;
    }

    /// The track meter's next reading after @p device has rendered.
    static float meterAfter(std::map<magda::TrackId, float>& meters, magda::TrackId track,
                            InputPumpDevice& device) {
        meters.erase(track);
        for (auto tick = 0; tick < 50 && !meters.contains(track); ++tick) {
            device.pump();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }
        return meters.contains(track) ? meters[track] : -1.0f;
    }

    static const magda::ClipInfo* onlyAudioClip(magda::TrackId trackId) {
        auto& clips = magda::ClipManager::getInstance();
        const auto ids = clips.getClipsOnTrack(trackId, magda::ClipView::Arrangement);
        const magda::ClipInfo* result = nullptr;
        for (const auto id : ids) {
            const auto* clip = clips.getClip(id);
            if (clip == nullptr || !clip->isAudio())
                continue;
            if (result != nullptr)
                return nullptr;
            result = clip;
        }
        return result;
    }

    static std::unique_ptr<juce::AudioFormatReader> readerFor(const magda::ClipInfo& clip) {
        const auto* event = clip.primaryEvent();
        if (event == nullptr)
            return nullptr;
        static juce::AudioFormatManager formats;
        static const auto registered = [] {
            formats.registerBasicFormats();
            return true;
        }();
        juce::ignoreUnused(registered);
        return std::unique_ptr<juce::AudioFormatReader>(
            formats.createReaderFor(juce::File(event->sourceFilePath())));
    }

    void monitoredInputReachesTheTrack() {
        beginTest("a monitored track hears and meters its hardware input");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Loopback");
        tracks.setTrackAudioInput(track, "stereo:Loopback 1");
        tracks.setTrackInputMonitor(track, magda::InputMonitorMode::In);

        std::map<magda::TrackId, float> meters;
        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.meterInto([&meters](magda::TrackId id, float left, float right) {
            meters[id] = std::max(left, right);
        });
        host.start(devices);
        settle(host);

        const auto stereo = steady(*devices.device);
        expect(stereo.left > 0.001f, "the pair's first channel reaches the left");
        expectWithinAbsoluteError(stereo.right / stereo.left, levelOf(3) / levelOf(2), 0.001f,
                                  "each side reads its own channel");
        expect(meterAfter(meters, track, *devices.device) > 0.001f, "the track meters its input");

        tracks.setTrackAudioInput(track, "Loopback 2");
        settle(host);
        const auto mono = steady(*devices.device);
        expect(mono.left > 0.001f, "a mono input sounds");
        expectWithinAbsoluteError(mono.left, mono.right, 0.000001f, "in both ears");

        host.stop();
        devices.closeAudioDevice();
    }

    void unmonitoredInputStaysSilent() {
        beginTest("an unmonitored input is silent until the track is armed");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Unmonitored");
        tracks.setTrackAudioInput(track, "stereo:Loopback 1");
        tracks.setTrackInputMonitor(track, magda::InputMonitorMode::Off);

        std::map<magda::TrackId, float> meters;
        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.meterInto([&meters](magda::TrackId id, float left, float right) {
            meters[id] = std::max(left, right);
        });
        host.start(devices);
        settle(host);

        const auto silent = steady(*devices.device);
        expectEquals(silent.left, 0.0f);
        expectEquals(silent.right, 0.0f);
        expectEquals(meterAfter(meters, track, *devices.device), 0.0f);

        tracks.setTrackRecordArmed(track, true);
        settle(host);
        expect(steady(*devices.device).left > 0.001f, "arming counts as monitoring");

        host.stop();
        devices.closeAudioDevice();
    }

    void restartRepacksAndMissingIsSilent() {
        beginTest("a restart repacks the input's channels, and a missing name is silence");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;

        auto catalog = loopbackCatalog();
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Repacked");
        tracks.setTrackAudioInput(track, "stereo:Loopback 1");
        tracks.setTrackInputMonitor(track, magda::InputMonitorMode::In);

        Host host;
        host.setHardwareInputProvider([&catalog] { return catalog; });
        host.start(devices);
        settle(host);

        // Only the loopback pair open: it is now the callback's first two inputs.
        catalog.enabledChannels = channels({2, 3});
        devices.device->restart(channels({2, 3}));
        settle(host);

        const auto repacked = steady(*devices.device);
        expect(repacked.left > 0.001f, "the pair still sounds");
        expectWithinAbsoluteError(repacked.right / repacked.left, levelOf(3) / levelOf(2), 0.001f,
                                  "from the same physical channels");

        tracks.setTrackAudioInput(track, "stereo:Missing");
        settle(host);
        const auto missing = steady(*devices.device);
        expectEquals(missing.left, 0.0f);
        expectEquals(missing.right, 0.0f);

        host.stop();
        devices.closeAudioDevice();
    }

    void recordsInputWithPreviewAndLatency() {
        beginTest("an armed audio input records callback channels with a live preview");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        devices.device->inputLatencySamples = kBlockSize;

        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Recorded loopback");
        tracks.setTrackAudioInput(track, "stereo:Loopback 1");
        tracks.setTrackInputMonitor(track, magda::InputMonitorMode::Off);
        tracks.setTrackRecordArmed(track, true);
        // Loaded and transitional model states can carry both routes even
        // though the current setters make them mutually exclusive.
        tracks.getTrack(track)->midiInputDevice = "all";

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle(host);
        expect(host.startMidiRecording(0.0), "the app-facing Record entry accepts audio input");

        for (auto block = 0; block < 4; ++block)
            devices.device->pump();

        const auto& previews = host.recordingPreviews();
        const auto preview = previews.find(track);
        expect(preview != previews.end(), "the callback publishes a recording preview");
        if (preview != previews.end()) {
            expect(preview->second.isAudioRecording,
                   "the default all-MIDI take cannot replace the waveform");
            expect(preview->second.currentLengthBeats > 0.0);
            expect(!preview->second.audioPeaks.empty());
            if (!preview->second.audioPeaks.empty()) {
                expectWithinAbsoluteError(preview->second.audioPeaks.front().peakL, levelOf(2),
                                          0.0001f);
                expectWithinAbsoluteError(preview->second.audioPeaks.front().peakR, levelOf(3),
                                          0.0001f);
            }
        }

        tracks.setTrackMidiInput(track, {});
        settle(host);
        expect(previews.contains(track) && previews.at(track).isAudioRecording,
               "closing the parallel MIDI take keeps the waveform visible");

        host.stopMidiRecording();
        expect(host.recordingPreviews().empty(), "the transient preview clears at stop");
        const auto* clip = onlyAudioClip(track);
        expect(clip != nullptr, "stop publishes the completed audio clip");
        if (clip != nullptr) {
            auto reader = readerFor(*clip);
            expect(reader != nullptr, "the recorded WAV can be read");
            if (reader != nullptr) {
                expectEquals(static_cast<int>(reader->numChannels), 2);
                expectEquals(static_cast<int>(reader->lengthInSamples), 3 * kBlockSize,
                             "device input latency is removed from the head");
                juce::AudioBuffer<float> captured(2, 1);
                reader->read(&captured, 0, 1, 0, true, true);
                expectWithinAbsoluteError(captured.getSample(0, 0), levelOf(2), 0.0001f);
                expectWithinAbsoluteError(captured.getSample(1, 0), levelOf(3), 0.0001f);
            }
        }

        host.stop();
        devices.closeAudioDevice();
    }

    void disarmFinalizesRecording() {
        beginTest("disarming finalizes an active audio take once");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Disarmed take");
        tracks.setTrackAudioInput(track, "Loopback 1");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        expect(host.startMidiRecording(0.0));
        devices.device->pump();
        devices.device->pump();

        tracks.setTrackRecordArmed(track, false);
        settle(host);
        expect(onlyAudioClip(track) != nullptr);
        expect(host.recordingPreviews().empty());
        host.stopMidiRecording();
        expectEquals(static_cast<int>(magda::ClipManager::getInstance()
                                          .getClipsOnTrack(track, magda::ClipView::Arrangement)
                                          .size()),
                     1);

        host.stop();
        devices.closeAudioDevice();
    }

    void deviceStopFinalizesRecording() {
        beginTest("an audio-device stop finalizes an active audio take");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Stopped device take");
        tracks.setTrackAudioInput(track, "Loopback 2");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        expect(host.startMidiRecording(0.0));
        devices.device->pump();
        devices.device->pump();

        devices.device->stop();
        settle(host);
        expect(!host.isRecording());
        expect(onlyAudioClip(track) != nullptr);
        expect(host.recordingPreviews().empty());

        host.stop();
        devices.closeAudioDevice();
    }

    void sessionTargetDoesNotRecordArrangement() {
        beginTest("an unsupported audio Session target does not become an Arrangement take");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Audio Session target");
        tracks.setTrackAudioInput(track, "Loopback 1");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        host.armSessionSlotRecording(track, 0);
        expect(!host.startPunchRecording(0.0, {}),
               "the unsupported target cannot start an Arrangement recording");
        devices.device->pump();
        expect(onlyAudioClip(track) == nullptr);
        expect(host.recordingPreviews().empty());

        host.stop();
        devices.closeAudioDevice();
    }

    void deviceRestartSplitsRecording() {
        beginTest("an audio-device restart closes the old take and continues recording");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Restarted device take");
        tracks.setTrackAudioInput(track, "Loopback 1");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        expect(host.startMidiRecording(0.0));
        devices.device->pump();
        devices.device->pump();

        devices.device->pumpAfterFutureLatencyReads(2);
        devices.device->restart(channels({0, 1, 2, 3}), 96000.0);
        settle(host);
        expect(host.isRecording());
        expectEquals(static_cast<int>(magda::ClipManager::getInstance()
                                          .getClipsOnTrack(track, magda::ClipView::Arrangement)
                                          .size()),
                     1, "the old device epoch became one clip");

        devices.device->pump();
        devices.device->pump();
        host.stopMidiRecording();
        expectEquals(static_cast<int>(magda::ClipManager::getInstance()
                                          .getClipsOnTrack(track, magda::ClipView::Arrangement)
                                          .size()),
                     2, "the restarted device recorded a second clip");

        host.stop();
        devices.closeAudioDevice();
    }
};

EngineHostAudioInputTest engineHostAudioInputTest;

}  // namespace
