#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <limits>
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
constexpr int kInputLatency = 120;
constexpr int kOutputLatency = 240;

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
        return outputLatencySamples;
    }
    int getInputLatencyInSamples() override {
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
    int outputLatencySamples = 0;

  private:
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::BigInteger active_;
    double sampleRate_ = 48000.0;
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
        magda::test::runWithCleanJuceState([this] { deviceStopForceClosesPostRoll(); });
        magda::test::runWithCleanJuceState([this] { reportedLatencyIsBounded(); });
        magda::test::runWithCleanJuceState([this] { sameRateRestartContinuesRecording(); });
        magda::test::runWithCleanJuceState([this] { inputChangeContinuesAfterPostRoll(); });
        magda::test::runWithCleanJuceState([this] { deviceRestartSplitsRecording(); });
        magda::test::runWithCleanJuceState([this] { recordsIntoAnArmedSessionSlot(); });
        magda::test::runWithCleanJuceState([this] { globalStopKeepsSessionClipPlaying(); });
        magda::test::runWithCleanJuceState([this] { filledSessionSlotPreservesTake(); });
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

    static double beatsForSamples(const Host& host, InputPumpDevice& device, int samples) {
        const auto seconds = static_cast<double>(samples) / device.getCurrentSampleRate();
        const auto* tempo = host.tempoMap();
        return tempo->timeToBeat(seconds) - tempo->timeToBeat(0.0);
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

    static const magda::ClipInfo* onlySessionAudioClip(magda::TrackId trackId) {
        auto& clips = magda::ClipManager::getInstance();
        const auto ids = clips.getClipsOnTrack(trackId, magda::ClipView::Session);
        if (ids.size() != 1)
            return nullptr;
        const auto* clip = clips.getClip(ids.front());
        return clip != nullptr && clip->isAudio() ? clip : nullptr;
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
        beginTest("a restart repacks channels; missing names and disabled inputs are silent");

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

        // What enabling a track's audio input stores: the first pair open.
        tracks.setTrackAudioInput(track, "default");
        settle(host);
        const auto fallback = steady(*devices.device);
        expect(fallback.left > 0.001f, "default sounds");
        expectWithinAbsoluteError(fallback.right / fallback.left, levelOf(3) / levelOf(2), 0.001f,
                                  "from the first open pair");

        // Disabling every input in the device layer leaves nothing to read.
        catalog.enabledChannels = {};
        host.refreshHardwareInputs();
        settle(host);
        const auto disabled = steady(*devices.device);
        expectEquals(disabled.left, 0.0f);
        expectEquals(disabled.right, 0.0f);

        host.stop();
        devices.closeAudioDevice();
    }

    void recordsInputWithPreviewAndLatency() {
        beginTest("an armed audio input records callback channels with a live preview");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        devices.device->inputLatencySamples = kInputLatency;
        devices.device->outputLatencySamples = kOutputLatency;

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
        devices.device->pump();
        settle(host);
        expect(host.recordingPreviews().empty(), "the transient preview clears at stop");
        const auto* clip = onlyAudioClip(track);
        expect(clip != nullptr, "stop publishes the completed audio clip");
        if (clip != nullptr) {
            auto reader = readerFor(*clip);
            expect(reader != nullptr, "the recorded WAV can be read");
            if (reader != nullptr) {
                expectEquals(static_cast<int>(reader->numChannels), 2);
                expectEquals(static_cast<int>(reader->lengthInSamples), 4 * kBlockSize,
                             "post-roll replaces the round-trip correction removed at the head");
                juce::AudioBuffer<float> captured(2, 1);
                reader->read(&captured, 0, 1, 0, true, true);
                expectWithinAbsoluteError(captured.getSample(0, 0), levelOf(2), 0.0001f);
                expectWithinAbsoluteError(captured.getSample(1, 0), levelOf(3), 0.0001f);
            }
            expectWithinAbsoluteError(clip->placement.startBeat, 0.0, 0.0000001,
                                      "correction keeps the take on its record-start beat");
            expectWithinAbsoluteError(clip->placement.lengthBeats,
                                      beatsForSamples(host, *devices.device, 4 * kBlockSize),
                                      0.0000001, "timeline length matches the corrected file");
        }

        host.stopPlaying();
        host.locateSeconds(0.0);
        host.play();
        settle(host);
        const auto playback = devices.device->pump();
        expectWithinAbsoluteError(playback.left, levelOf(2), 0.0001f,
                                  "the recorded clip plays through the track");
        expectWithinAbsoluteError(playback.right, levelOf(3), 0.0001f,
                                  "recorded stereo reaches both outputs");

        host.stop();
        devices.closeAudioDevice();
    }

    void disarmFinalizesRecording() {
        beginTest("disarming finalizes an active audio take once");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        devices.device->inputLatencySamples = kInputLatency;
        devices.device->outputLatencySamples = kOutputLatency;
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
        devices.device->pump();
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

    void deviceStopForceClosesPostRoll() {
        beginTest("an audio-device stop force-closes a take already in post-roll");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        devices.device->inputLatencySamples = kInputLatency;
        devices.device->outputLatencySamples = kOutputLatency;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Stopped post-roll take");
        tracks.setTrackAudioInput(track, "Loopback 2");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        expect(host.startMidiRecording(0.0));
        devices.device->pump();
        devices.device->pump();

        host.stopMidiRecording();
        devices.device->stop();
        settle(host);
        expect(!host.isRecording());
        expect(onlyAudioClip(track) != nullptr,
               "the deferred take closes without waiting for a future device callback");

        host.stop();
        devices.closeAudioDevice();
    }

    void reportedLatencyIsBounded() {
        beginTest("reported latency is summed without overflow and bounded to three seconds");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        devices.device->inputLatencySamples = std::numeric_limits<int>::max();
        devices.device->outputLatencySamples = std::numeric_limits<int>::max();

        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Bounded latency");
        tracks.setTrackAudioInput(track, "Loopback 1");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        expect(host.startMidiRecording(0.0));

        // At 48 kHz, 300 blocks are exactly the three-second bound. The next
        // block must reach the file; an unbounded or overflowed sum will not.
        for (auto block = 0; block < 301; ++block)
            devices.device->pump();

        devices.device->stop();
        settle(host);
        const auto* clip = onlyAudioClip(track);
        expect(clip != nullptr);
        if (clip != nullptr) {
            auto reader = readerFor(*clip);
            expect(reader != nullptr);
            if (reader != nullptr)
                expectEquals(static_cast<int>(reader->lengthInSamples), kBlockSize);
        }

        host.stop();
        devices.closeAudioDevice();
    }

    void recordsIntoAnArmedSessionSlot() {
        beginTest("an armed Session slot records its selected audio input");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        devices.device->inputLatencySamples = kInputLatency;
        devices.device->outputLatencySamples = kOutputLatency;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Audio Session target");
        tracks.setTrackAudioInput(track, "Loopback 1");
        tracks.setTrackRecordArmed(track, true);
        tracks.getTrack(track)->midiInputDevice = "all";

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle(host);
        host.armSessionSlotRecording(track, 0);
        host.beginArmedSessionSlotRecordings(0.0);
        devices.device->pump();
        expect(host.isSessionSlotRecording(track, 0));
        for (auto block = 0; block < 3; ++block)
            devices.device->pump();

        const auto& previews = host.recordingPreviews();
        const auto preview = previews.find(track);
        expect(preview != previews.end());
        if (preview != previews.end()) {
            expect(preview->second.isAudioRecording,
                   "the selected audio input wins over the default MIDI-all route");
            expect(preview->second.target == magda::RecordingTargetKind::SessionSlot);
            expectEquals(preview->second.sceneIndex, 0);
            expect(!preview->second.audioPeaks.empty());
        }

        host.armSessionSlotRecording(track, 0);
        devices.device->pump();
        settle(host);
        expect(!host.isSessionSlotRecordArmed(track, 0));
        expect(!host.isSessionSlotRecording(track, 0));
        expect(onlyAudioClip(track) == nullptr, "Session recording creates no Arrangement clip");
        const auto* clip = onlySessionAudioClip(track);
        expect(clip != nullptr, "the completed file is published into the selected slot");
        if (clip != nullptr) {
            expectEquals(clip->sceneIndex, 0);
            expect(clip->loopEnabled);
            expect(clip->primaryEvent() != nullptr && clip->primaryEvent()->autoTempo,
                   "recorded Session audio follows project tempo");
            auto reader = readerFor(*clip);
            expect(reader != nullptr);
            if (reader != nullptr) {
                expectEquals(static_cast<int>(reader->numChannels), 1);
                expectEquals(static_cast<int>(reader->lengthInSamples), 4 * kBlockSize,
                             "Session captures the correction-sized post-roll too");
            }
            expectWithinAbsoluteError(clip->placement.startBeat, 0.0, 0.0000001);
            expectWithinAbsoluteError(
                clip->placement.lengthBeats, beatsForSamples(host, *devices.device, 4 * kBlockSize),
                0.0000001, "Session timeline length matches the corrected file");
        }

        devices.device->pump();
        host.processSessionStateEvents();
        if (clip != nullptr)
            expectEquals(static_cast<int>(host.sessionClipPlayState(clip->id)),
                         static_cast<int>(magda::SessionClipPlayState::Stopped),
                         "stopping a recording slot stops its materialized clip too");
        expect(tracks.getTrack(track)->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "deferred and immediate Session stops both return to Arrangement");

        host.stop();
        devices.closeAudioDevice();
    }

    void filledSessionSlotPreservesTake() {
        beginTest("an audio Session take falls back to Arrangement if its slot was filled");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Filled audio Session target");
        tracks.setTrackAudioInput(track, "Loopback 1");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        host.armSessionSlotRecording(track, 1);
        host.beginArmedSessionSlotRecordings(0.0);
        devices.device->pump();
        devices.device->pump();

        auto& clips = magda::ClipManager::getInstance();
        const auto occupant = clips.createMidiClipBeats(track, 0.0, 4.0, magda::ClipView::Session);
        clips.setClipSceneIndex(occupant, 1);
        host.stopMidiRecording();

        expectEquals(clips.getClipInSlot(track, 1), occupant);
        expect(onlyAudioClip(track) != nullptr,
               "the recorded file survives outside the occupied slot");
        expect(onlySessionAudioClip(track) == nullptr);
        expect(!host.isSessionSlotRecordArmed(track, 1));

        host.stop();
        devices.closeAudioDevice();
    }

    void globalStopKeepsSessionClipPlaying() {
        beginTest("Record-off lets a deferred audio Session clip keep playing");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        devices.device->inputLatencySamples = kInputLatency;
        devices.device->outputLatencySamples = kOutputLatency;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Continuing audio Session take");
        tracks.setTrackAudioInput(track, "Loopback 1");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        host.armSessionSlotRecording(track, 0);
        host.beginArmedSessionSlotRecordings(0.0);
        for (auto block = 0; block < 3; ++block)
            devices.device->pump();

        host.stopMidiRecording();
        devices.device->pump();
        settle(host);
        const auto* clip = onlySessionAudioClip(track);
        expect(clip != nullptr);
        devices.device->pump();
        host.processSessionStateEvents();
        if (clip != nullptr)
            expect(host.sessionClipPlayState(clip->id) == magda::SessionClipPlayState::Playing,
                   "global Record-off preserves a successful Session handover");
        expect(tracks.getTrack(track)->playbackMode == magda::TrackPlaybackMode::Session);

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

        devices.device->inputLatencySamples = kInputLatency;
        devices.device->outputLatencySamples = kOutputLatency;
        devices.device->restart(channels({0, 1, 2, 3}), 96000.0);
        settle(host);
        expect(host.isRecording());
        auto& clips = magda::ClipManager::getInstance();
        const auto beforeRestart = clips.getClipsOnTrack(track, magda::ClipView::Arrangement);
        expectEquals(static_cast<int>(beforeRestart.size()), 1,
                     "the old correction became one clip");
        const auto oldClipId =
            beforeRestart.empty() ? magda::INVALID_CLIP_ID : beforeRestart.front();
        if (const auto* oldClip = clips.getClip(oldClipId)) {
            auto reader = readerFor(*oldClip);
            expect(reader != nullptr);
            if (reader != nullptr)
                expectEquals(static_cast<int>(reader->lengthInSamples), 2 * kBlockSize,
                             "the old file keeps only its zero-correction epoch");
        }

        // The replacement take is open the moment the rebuild has reinstalled the
        // callback, so every block after it belongs to the new clip.
        devices.device->pump();
        devices.device->pump();
        devices.device->pump();
        host.stopMidiRecording();
        devices.device->pump();
        settle(host);
        const auto afterRestart = clips.getClipsOnTrack(track, magda::ClipView::Arrangement);
        expectEquals(static_cast<int>(afterRestart.size()), 2,
                     "the changed correction records into a second clip");
        for (const auto id : afterRestart) {
            if (id == oldClipId)
                continue;
            const auto* newClip = clips.getClip(id);
            expect(newClip != nullptr);
            if (newClip == nullptr)
                continue;
            auto reader = readerFor(*newClip);
            expect(reader != nullptr);
            if (reader != nullptr)
                expectEquals(static_cast<int>(reader->lengthInSamples), 3 * kBlockSize,
                             "the rebuilt take records every block after the rebuild");
        }

        host.stop();
        devices.closeAudioDevice();
    }

    void sameRateRestartContinuesRecording() {
        beginTest("a same-rate audio-device restart immediately opens a replacement take");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Same-rate restarted take");
        tracks.setTrackAudioInput(track, "Loopback 1");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        expect(host.startMidiRecording(0.0));
        devices.device->pump();
        devices.device->pump();

        devices.device->restart(channels({0, 1, 2, 3}));
        settle(host);
        expect(host.isRecording());
        expectEquals(static_cast<int>(magda::ClipManager::getInstance()
                                          .getClipsOnTrack(track, magda::ClipView::Arrangement)
                                          .size()),
                     1, "the old device generation materializes once");

        devices.device->pump();
        devices.device->pump();
        host.stopMidiRecording();
        settle(host);
        expectEquals(static_cast<int>(magda::ClipManager::getInstance()
                                          .getClipsOnTrack(track, magda::ClipView::Arrangement)
                                          .size()),
                     2, "the replacement keeps recording after the same-rate restart");

        host.stop();
        devices.closeAudioDevice();
    }

    void inputChangeContinuesAfterPostRoll() {
        beginTest("an input change opens its replacement after the old take's post-roll");

        InputPumpManager devices;
        expect(devices.initialise(kInputs, 2, nullptr, true).isEmpty());
        if (devices.device == nullptr)
            return;
        devices.device->inputLatencySamples = kInputLatency;
        devices.device->outputLatencySamples = kOutputLatency;
        auto& tracks = magda::TrackManager::getInstance();
        const auto track = tracks.createTrack("Changed input take");
        tracks.setTrackAudioInput(track, "Loopback 1");
        tracks.setTrackRecordArmed(track, true);

        Host host;
        host.setHardwareInputProvider([] { return loopbackCatalog(); });
        host.start(devices);
        settle(host);
        expect(host.startMidiRecording(0.0));
        devices.device->pump();
        devices.device->pump();

        tracks.setTrackAudioInput(track, "Loopback 2");
        settle(host);
        expect(host.isRecording(), "the eligible closing take keeps the Record gesture alive");
        devices.device->pump();
        settle(host);
        expect(host.isRecording(), "the replacement starts after deferred materialization");
        expectEquals(static_cast<int>(magda::ClipManager::getInstance()
                                          .getClipsOnTrack(track, magda::ClipView::Arrangement)
                                          .size()),
                     1);

        devices.device->pump();
        devices.device->pump();
        host.stopMidiRecording();
        devices.device->pump();
        settle(host);
        expectEquals(static_cast<int>(magda::ClipManager::getInstance()
                                          .getClipsOnTrack(track, magda::ClipView::Arrangement)
                                          .size()),
                     2, "the replacement route records into a second clip");

        host.stop();
        devices.closeAudioDevice();
    }
};

EngineHostAudioInputTest engineHostAudioInputTest;

}  // namespace
