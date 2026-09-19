#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/audio/midi/RecordingNoteQueue.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineHost.hpp"

namespace {

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

    bool pump() {
        if (callback_ == nullptr)
            return false;
        std::array<float, 480> left{}, right{};
        float* outputs[] = {left.data(), right.data()};
        callback_->audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 480, {});
        lastMagnitude = 0.0f;
        for (const auto sample : left)
            lastMagnitude = std::max(lastMagnitude, std::abs(sample));
        return true;
    }

    float lastMagnitude = 0.0f;

  private:
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::BigInteger outputs_;
    bool open_ = false;
};

class PumpDeviceType final : public juce::AudioIODeviceType {
  public:
    explicit PumpDeviceType(PumpDevice*& device)
        : juce::AudioIODeviceType("Pump"), device_(device) {}

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

class PumpDeviceManager final : public juce::AudioDeviceManager {
  public:
    PumpDevice* device = nullptr;

  protected:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override {
        types.add(new PumpDeviceType(device));
    }
};

class EngineHostMidiRecordingTest final : public juce::UnitTest {
  public:
    EngineHostMidiRecordingTest() : juce::UnitTest("Engine Host MIDI Recording", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { recordsFromTheRealDeviceCallback(); });
        magda::test::runWithCleanJuceState([this] { immediateRecordFlushesArmAndMonitorAuto(); });
        magda::test::runWithCleanJuceState([this] { immediateRecordFlushesArmWithMonitorIn(); });
        magda::test::runWithCleanJuceState([this] { allInputDoesNotRecordAudition(); });
        magda::test::runWithCleanJuceState([this] { disarmingClosesTheTakeOnce(); });
        magda::test::runWithCleanJuceState([this] { rollingPunchOutLeavesPlaybackRunning(); });
        magda::test::runWithCleanJuceState([this] { deviceStopClosesTheTake(); });
        magda::test::runWithCleanJuceState([this] { loopPassesAndHeldNoteAreMaterialized(); });
        magda::test::runWithCleanJuceState([this] { projectBoundaryDropsTheOldTake(); });
        magda::test::runWithCleanJuceState([this] { previewTracksTheCallbackAndFinalClip(); });
        magda::test::runWithCleanJuceState([this] { previewReplacesLoopPassAndClears(); });
        magda::test::runWithCleanJuceState([this] { recordsIntoAnArmedSessionSlot(); });
        magda::test::runWithCleanJuceState(
            [this] { standaloneSlotIgnoresPastArrangementPunchOut(); });
        magda::test::runWithCleanJuceState([this] { sessionRecordingOwnershipLifecycle(); });
        magda::test::runWithCleanJuceState([this] { discardedSessionTakeReleasesOwnership(); });
        magda::test::runWithCleanJuceState([this] { recordingSupersedesRetainedClipIntent(); });
        magda::test::runWithCleanJuceState([this] { sessionSlotWaitsForTheNextBar(); });
        magda::test::runWithCleanJuceState([this] { sessionSlotsShareOneBoundary(); });
        magda::test::runWithCleanJuceState([this] { sessionAllInputSurvivesReconcile(); });
        magda::test::runWithCleanJuceState([this] { reclickFinishesSessionTakeOnce(); });
        magda::test::runWithCleanJuceState([this] { queuedSessionCancellationRetiresTake(); });
        magda::test::runWithCleanJuceState([this] { timerHarvestsFinishedSessionTake(); });
        magda::test::runWithCleanJuceState([this] { countInIsNotPartOfTheTake(); });
        magda::test::runWithCleanJuceState([this] { sessionSlotCountsInWhileStopped(); });
        magda::test::runWithCleanJuceState([this] { punchWindowClipsHeldNoteInCallback(); });
        magda::test::runWithCleanJuceState([this] { livePunchMarkerMovesCaptureWindow(); });
        magda::test::runWithCleanJuceState([this] { livePunchToggleOpensCaptureWindow(); });
        magda::test::runWithCleanJuceState([this] { punchOutWinsBeforeLoopWrap(); });
        magda::test::runWithCleanJuceState([this] { punchOutAtLoopWrapClosesCapture(); });
        magda::test::runWithCleanJuceState([this] { punchOutAtCallbackEndClosesCapture(); });
        magda::test::runWithCleanJuceState([this] { punchRestartBeforeCallbackUsesNewWindow(); });
        magda::test::runWithCleanJuceState([this] { armedSessionSlotFollowsNativePunchWindow(); });
        magda::test::runWithCleanJuceState([this] { enablingPunchOutStopsOngoingRecording(); });
        magda::test::runWithCleanJuceState([this] { movedPunchInReschedulesWaitingSlot(); });
        magda::test::runWithCleanJuceState([this] { punchOutCancelsSlotWaitingForNextBar(); });
    }

  private:
    static bool open(PumpDeviceManager& devices) {
        return devices.initialise(0, 2, nullptr, true).isEmpty() && devices.device != nullptr;
    }

    static void settle() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    static std::vector<magda::ClipInfo> clipsOn(magda::TrackId trackId) {
        auto& manager = magda::ClipManager::getInstance();
        std::vector<magda::ClipInfo> result;
        for (const auto clipId : manager.getClipsOnTrack(trackId, magda::ClipView::Arrangement))
            if (const auto* clip = manager.getClip(clipId))
                result.push_back(*clip);
        return result;
    }

    static std::vector<magda::ClipInfo> sessionClipsOn(magda::TrackId trackId) {
        auto& manager = magda::ClipManager::getInstance();
        std::vector<magda::ClipInfo> result;
        for (const auto clipId : manager.getClipsOnTrack(trackId, magda::ClipView::Session))
            if (const auto* clip = manager.getClip(clipId))
                result.push_back(*clip);
        return result;
    }

    void preparePunchTrack(magda::TrackId trackId) {
        auto& tracks = magda::TrackManager::getInstance();
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::Off);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
    }

    void punchWindowClipsHeldNoteInCallback() {
        beginTest("native punch boundaries split callbacks and close a held note at punch-out");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Punch window");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setPunch(0.01, 0.05, true, true);
        expect(host.startMidiRecording(0.0));

        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
        devices.device->pump();
        devices.device->pump();
        expect(host.isRecording(), "punch-out cleanup remains asynchronous");
        devices.device->pump();
        settle();

        expect(!host.isRecording(), "audio-clock punch-out closes the recording gesture");
        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.startBeat, 0.01, 0.001);
            expectWithinAbsoluteError(clips[0].placement.lengthBeats, 0.04, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1) {
                expectEquals(clips[0].midiNotes[0].noteNumber, 60);
                expectWithinAbsoluteError(clips[0].midiNotes[0].lengthBeats, 0.03, 0.001);
            }
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void livePunchMarkerMovesCaptureWindow() {
        beginTest("moving live punch markers changes the pending audio-clock window");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Moving punch");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setPunch(1.0, 2.0, true, true);
        expect(host.startMidiRecording(0.0));
        devices.device->pump();

        host.setPunch(0.02, 0.06, true, true);
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 62, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 62));
        devices.device->pump();
        devices.device->pump();
        settle();

        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.startBeat, 0.02, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void livePunchToggleOpensCaptureWindow() {
        beginTest("disabling live punch-in starts capture at the next audio block");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Punch toggle");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setPunch(1.0, 2.0, true, true);
        expect(host.startMidiRecording(0.0));
        devices.device->pump();

        host.setPunch(1.0, 2.0, false, true);
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 64, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 64));
        devices.device->pump();
        host.stopMidiRecording();

        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1)
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
        host.stop();
        devices.closeAudioDevice();
    }

    void punchOutWinsBeforeLoopWrap() {
        beginTest("punch-out inside a callback closes capture before the later loop wrap");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Punch loop");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setLoop(true, 0.0, 0.06);
        host.setPunch(0.0, 0.05, true, true);
        expect(host.startMidiRecording(0.0));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 67, (juce::uint8)100));
        devices.device->pump();
        devices.device->pump();
        devices.device->pump();
        settle();

        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.lengthBeats, 0.05, 0.001);
            expect(clips[0].midi().takes.empty(),
                   "loop wrap cannot turn the punch window into a multi-pass take");
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1) {
                expectEquals(clips[0].midiNotes[0].noteNumber, 67);
                expectWithinAbsoluteError(clips[0].midiNotes[0].lengthBeats, 0.05, 0.001);
            }
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void punchOutAtLoopWrapClosesCapture() {
        beginTest("punch-out at loop end closes capture before the transport wraps");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Punch at loop wrap");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setLoop(true, 0.0, 0.06);
        host.setPunch(0.0, 0.06, true, true);
        expect(host.startMidiRecording(0.0));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 69, (juce::uint8)100));
        devices.device->pump();
        devices.device->pump();
        devices.device->pump();
        settle();

        expect(!host.isRecording(), "the wrap cannot hide the coincident punch-out edge");
        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.lengthBeats, 0.06, 0.001);
            expect(clips[0].midi().takes.empty());
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1)
                expectWithinAbsoluteError(clips[0].midiNotes[0].lengthBeats, 0.06, 0.001);
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void punchOutAtCallbackEndClosesCapture() {
        beginTest("punch-out at callback end closes capture without a following callback");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Punch at callback end");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setPunch(0.0, 0.04, true, true);
        expect(host.startMidiRecording(0.0));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 71, (juce::uint8)100));
        devices.device->pump();
        devices.device->pump();
        settle();

        expect(!host.isRecording(), "punch-out does not require a block opening past its edge");
        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.lengthBeats, 0.04, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1)
                expectWithinAbsoluteError(clips[0].midiNotes[0].lengthBeats, 0.04, 0.001);
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void punchRestartBeforeCallbackUsesNewWindow() {
        beginTest("stop and restart before a callback uses only the new punch generation");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Punch restart");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setPunch(0.0, 0.02, true, true);
        expect(host.startMidiRecording(0.0));
        host.stopMidiRecording();
        host.setPunch(0.04, 0.08, true, true);
        expect(host.startMidiRecording(0.0));

        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 61, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 62, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 62));
        devices.device->pump();
        settle();

        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.startBeat, 0.04, 0.001);
            expectWithinAbsoluteError(clips[0].placement.lengthBeats, 0.04, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1,
                         "events before the replacement punch-in stay outside the take");
            if (clips[0].midiNotes.size() == 1)
                expectEquals(clips[0].midiNotes[0].noteNumber, 62);
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void armedSessionSlotFollowsNativePunchWindow() {
        beginTest("armed Session slot starts and ends at the native punch boundaries");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Punched Session slot");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.armSessionSlotRecording(trackId, 0);
        host.setPunch(0.02, 0.06, true, true);
        expect(host.startPunchRecording(0.0, 0.02));

        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 72, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 72));
        devices.device->pump();
        expect(host.isRecording(), "message-thread harvest has not run after punch-out");
        devices.device->pump();
        settle();

        expect(!host.isRecording());
        expect(!host.isSessionSlotRecording(trackId, 0));
        expect(clipsOn(trackId).empty(), "the punched slot does not leak into Arrangement");
        const auto clips = sessionClipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.lengthBeats, 0.04, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1) {
                expectEquals(clips[0].midiNotes[0].noteNumber, 72);
                expectWithinAbsoluteError(clips[0].midiNotes[0].lengthBeats, 0.02, 0.001);
            }
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void enablingPunchOutStopsOngoingRecording() {
        beginTest("enabling punch-out while recording activates the live end marker");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Live punch-out toggle");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setPunch(0.0, 0.06, true, false);
        expect(host.startPunchRecording(0.0, std::nullopt));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 74, (juce::uint8)100));
        devices.device->pump();

        host.setPunch(0.0, 0.06, true, true);
        devices.device->pump();
        devices.device->pump();
        settle();

        expect(!host.isRecording(), "the newly enabled punch-out closes the active gesture");
        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.lengthBeats, 0.06, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1)
                expectWithinAbsoluteError(clips[0].midiNotes[0].lengthBeats, 0.06, 0.001);
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void movedPunchInReschedulesWaitingSlot() {
        beginTest("moving punch-in reschedules a slot-only recording before capture begins");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Moved slot punch");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.armSessionSlotRecording(trackId, 0);
        host.setPunch(0.04, 0.12, true, true);
        expect(host.startPunchRecording(0.0, 0.04));
        devices.device->pump();

        host.setPunch(0.08, 0.12, true, true);
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
        devices.device->pump();
        expect(!host.isSessionSlotRecording(trackId, 0),
               "the slot does not launch at the superseded punch marker");
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 60));
        devices.device->pump();

        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 62, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 62));
        devices.device->pump();
        settle();

        const auto clips = sessionClipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.lengthBeats, 0.04, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1,
                         "only input after the moved punch-in is captured");
            if (clips[0].midiNotes.size() == 1)
                expectEquals(clips[0].midiNotes[0].noteNumber, 62);
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void punchOutCancelsSlotWaitingForNextBar() {
        beginTest("punch-out cancels a rolling slot whose next-bar launch is later");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Slot after punch-out");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.play();
        host.armSessionSlotRecording(trackId, 0);
        host.setPunch(0.0, 0.06, false, true);
        expect(host.startPunchRecording(0.0, std::nullopt));

        for (int block = 0; block < 200; ++block)
            devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 76, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 76));
        devices.device->pump();
        settle();

        expect(!host.isRecording());
        expect(!host.isSessionSlotRecording(trackId, 0));
        expect(sessionClipsOn(trackId).empty(),
               "the next-bar boundary cannot revive a take cancelled at punch-out");
        expect(clipsOn(trackId).empty());
        host.stop();
        devices.closeAudioDevice();
    }

    void recordsFromTheRealDeviceCallback() {
        beginTest("armed named input records through AudioIODevice callback and punches out");

        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Recorded");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::Off);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);

        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();

        expect(host.startMidiRecording(1.0), "eligible armed track starts recording");
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 64, (juce::uint8)100));
        expect(devices.device->pump(), "audio callback accepts note-on");
        for (int i = 0; i < 9; ++i)
            devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 64));
        devices.device->pump();
        host.stopMidiRecording();

        expect(!host.isRecording(), "punch-out closes the take");
        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1, "one complete clip is materialized");
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.startBeat, 2.0, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1) {
                expectEquals(clips[0].midiNotes[0].noteNumber, 64);
                expectWithinAbsoluteError(clips[0].midiNotes[0].lengthBeats, 0.2, 0.001);
            }
        }

        host.stop();
        devices.closeAudioDevice();
    }

    void immediateRecord(magda::InputMonitorMode monitor, const juce::String& label) {
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Immediate");
        tracks.setTrackInputMonitor(trackId, monitor);
        tracks.setTrackMidiInput(trackId, "keyboard");
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();

        tracks.setTrackRecordArmed(trackId, true);
        expect(host.startMidiRecording(0.0), label + " starts before the pending publish runs");
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 67, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 67));
        devices.device->pump();
        host.stopMidiRecording();

        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1)
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
        host.stop();
        devices.closeAudioDevice();
    }

    void immediateRecordFlushesArmAndMonitorAuto() {
        beginTest("Record flushes an immediate arm and Monitor Auto topology change");
        immediateRecord(magda::InputMonitorMode::Auto, "Monitor Auto");
    }

    void immediateRecordFlushesArmWithMonitorIn() {
        beginTest("Record flushes an immediate arm when Monitor In topology is unchanged");
        immediateRecord(magda::InputMonitorMode::In, "Monitor In");
    }

    void allInputDoesNotRecordAudition() {
        beginTest("all-device recording excludes the track audition source");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("All");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::Off);
        tracks.setTrackMidiInput(trackId, "all");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();

        expect(host.startMidiRecording(0.0));
        host.audition(trackId, juce::MidiMessage::noteOn(1, 72, (juce::uint8)100));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
        devices.device->pump();
        host.audition(trackId, juce::MidiMessage::noteOff(1, 72));
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 60));
        devices.device->pump();
        host.stopMidiRecording();

        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1)
                expectEquals(clips[0].midiNotes[0].noteNumber, 60);
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void disarmingClosesTheTakeOnce() {
        beginTest("disarming during Monitor In recording closes exactly one take");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Disarm");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::In);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        expect(host.startMidiRecording(0.0));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 55, (juce::uint8)100));
        devices.device->pump();

        tracks.setTrackRecordArmed(trackId, false);
        settle();
        expect(!host.isRecording(), "last eligible route ending ends the recording gesture");
        host.stopMidiRecording();
        expectEquals(static_cast<int>(clipsOn(trackId).size()), 1,
                     "a later punch-out does not materialize it twice");
        host.stop();
        devices.closeAudioDevice();
    }

    void rollingPunchOutLeavesPlaybackRunning() {
        beginTest("recording while rolling starts at the cursor and punch-out keeps playing");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Rolling");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::Off);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.play();
        for (int i = 0; i < 10; ++i)
            devices.device->pump();

        expect(host.startMidiRecording(9.0), "rolling record ignores the locate argument");
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 62, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 62));
        devices.device->pump();
        host.stopMidiRecording();

        expect(host.isPlaying(), "punch-out leaves playback running");
        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1)
            expect(clips[0].placement.startBeat < 1.0,
                   "rolling take begins at the cursor rather than the requested locate");
        host.stopPlaying();
        expectEquals(static_cast<int>(clipsOn(trackId).size()), 1,
                     "stopping after punch-out does not close the take again");
        host.stop();
        devices.closeAudioDevice();
    }

    void deviceStopClosesTheTake() {
        beginTest("an audio-device stop closes an active take without another callback");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Device stop");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::In);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        expect(host.startMidiRecording(0.0));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 50, (juce::uint8)100));
        devices.device->pump();

        devices.closeAudioDevice();
        settle();
        expect(!host.isRecording());
        expectEquals(static_cast<int>(clipsOn(trackId).size()), 1,
                     "the stopped device still materializes its rolled take");
        host.stop();
    }

    void loopPassesAndHeldNoteAreMaterialized() {
        beginTest("loop passes become takes and a held note is clipped at the wrap once");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Loop");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::Off);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setLoop(true, 0.0, 1.0);
        expect(host.startMidiRecording(0.0));

        for (int i = 0; i < 40; ++i)
            devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 69, (juce::uint8)100));
        devices.device->pump();
        for (int i = 0; i < 19; ++i)
            devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 69));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 71, (juce::uint8)100));
        devices.device->pump();
        for (int i = 0; i < 8; ++i)
            devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 71));
        devices.device->pump();
        for (int i = 0; i < 30; ++i)
            devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 74, (juce::uint8)100));
        devices.device->pump();
        for (int i = 0; i < 9; ++i)
            devices.device->pump();
        host.stopMidiRecording();

        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            const auto& midi = clips[0].midi();
            expectEquals(static_cast<int>(midi.takes.size()), 3,
                         "two complete passes and the partial final pass are retained");
            expectEquals(midi.currentTakeIndex, 1, "the last complete pass is active");
            expectWithinAbsoluteError(clips[0].placement.startBeat, 0.0, 0.001);
            expectWithinAbsoluteError(clips[0].placement.lengthBeats, 1.0, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1)
                expectEquals(clips[0].midiNotes[0].noteNumber, 71);
            if (!midi.takes.empty()) {
                expectEquals(static_cast<int>(midi.takes[0].notes.size()), 1);
                if (midi.takes[0].notes.size() == 1) {
                    expectWithinAbsoluteError(midi.takes[0].notes[0].startBeat, 0.8, 0.001);
                    expectWithinAbsoluteError(midi.takes[0].notes[0].lengthBeats, 0.2, 0.001);
                }
            }
            if (midi.takes.size() == 3) {
                expectEquals(static_cast<int>(midi.takes[2].notes.size()), 1);
                if (midi.takes[2].notes.size() == 1) {
                    expectEquals(midi.takes[2].notes[0].noteNumber, 74);
                    expectWithinAbsoluteError(midi.takes[2].notes[0].lengthBeats, 0.2, 0.001,
                                              "the final held note closes at Stop");
                }
            }
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void projectBoundaryDropsTheOldTake() {
        beginTest("a project boundary cannot materialize an old take into reused track IDs");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto oldTrackId = tracks.createTrack("Old project");
        tracks.setTrackInputMonitor(oldTrackId, magda::InputMonitorMode::In);
        tracks.setTrackMidiInput(oldTrackId, "keyboard");
        tracks.setTrackRecordArmed(oldTrackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        expect(host.startMidiRecording(0.0));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 48, (juce::uint8)100));
        devices.device->pump();

        host.forgetProject();
        tracks.clearAllTracks();
        const auto newTrackId = tracks.createTrack("New project");
        expectEquals(newTrackId, oldTrackId, "the fixture reproduces ID reuse across projects");
        host.stopMidiRecording();
        expect(clipsOn(newTrackId).empty(), "the outgoing take is not attached to the new track");
        host.stop();
        devices.closeAudioDevice();
    }

    void previewTracksTheCallbackAndFinalClip() {
        beginTest("recording preview appears on the callback, grows, and matches punch-out");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Preview");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::Off);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        expect(host.startMidiRecording(1.0));
        expect(host.recordingPreviews().empty(), "no preview is invented before audio runs");

        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 65, (juce::uint8)100));
        devices.device->pump();
        auto previews = host.recordingPreviews();
        expectEquals(static_cast<int>(previews.size()), 1,
                     "the first actual callback publishes the preview");
        if (!previews.contains(trackId))
            return;
        expectWithinAbsoluteError(previews.at(trackId).startBeat, 2.0, 0.001);
        expectEquals(static_cast<int>(previews.at(trackId).notes.size()), 1);
        const auto firstLength = previews.at(trackId).notes[0].lengthBeats;
        expect(firstLength > 0.0, "a held note has visible length immediately");

        for (int i = 0; i < 9; ++i)
            devices.device->pump();
        previews = host.recordingPreviews();
        const auto heldLength = previews.at(trackId).notes[0].lengthBeats;
        expect(heldLength > firstLength, "the held note grows with callback time");
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 65));
        devices.device->pump();
        previews = host.recordingPreviews();
        const auto released = previews.at(trackId).notes[0];
        for (int i = 0; i < 5; ++i)
            devices.device->pump();
        expectWithinAbsoluteError(host.recordingPreviews().at(trackId).notes[0].lengthBeats,
                                  released.lengthBeats, 0.000001,
                                  "a released note keeps its fixed length");

        host.stopMidiRecording();
        expect(host.recordingPreviews().empty(), "punch-out clears the live preview");
        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1 && clips[0].midiNotes.size() == 1) {
            expectEquals(clips[0].midiNotes[0].noteNumber, released.noteNumber);
            expectWithinAbsoluteError(clips[0].midiNotes[0].startBeat, released.startBeat,
                                      0.000001);
            expectWithinAbsoluteError(clips[0].midiNotes[0].lengthBeats, released.lengthBeats,
                                      0.000001);
        }
        host.stop();
        devices.closeAudioDevice();
    }

    void previewReplacesLoopPassAndClears() {
        beginTest("loop preview replaces the old pass and lifecycle boundaries clear it");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Preview loop");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::In);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setLoop(true, 0.0, 1.0);
        expect(host.startMidiRecording(0.0));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
        devices.device->pump();
        expectEquals(host.recordingPreviews().at(trackId).notes[0].noteNumber, 60);
        for (int i = 0; i < 50; ++i)
            devices.device->pump();

        const auto& wrapped = host.recordingPreviews().at(trackId);
        expectWithinAbsoluteError(wrapped.startBeat, 0.0, 0.001);
        expect(wrapped.currentLengthBeats < 0.1, "the preview location restarted at the wrap");
        expect(wrapped.notes.empty(), "the prior pass note does not remain in the new pass");
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 62, (juce::uint8)100));
        devices.device->pump();
        expectEquals(host.recordingPreviews().at(trackId).notes[0].noteNumber, 62);

        host.processSessionStateEvents();
        tracks.setTrackRecordArmed(trackId, false);
        settle();
        expect(host.recordingPreviews().empty(), "disarm clears the preview");
        expect(!host.isRecording(), "an empty Session capture cannot retain recording");
        tracks.setTrackRecordArmed(trackId, true);
        expect(host.startMidiRecording(0.0));
        devices.device->pump();
        expect(host.recordingPreviews().contains(trackId), "a new recording owns a new preview");
        host.forgetProject();
        expect(host.recordingPreviews().empty(), "project teardown cannot expose stale preview");
        host.stop();
        devices.closeAudioDevice();
    }

    void recordsIntoAnArmedSessionSlot() {
        beginTest("armed Session slot records one MIDI take without an Arrangement clip");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Session MIDI");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::Off);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);

        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();

        host.armSessionSlotRecording(trackId, 2);
        expect(host.isSessionSlotRecordArmed(trackId, 2));
        host.beginArmedSessionSlotRecordings(1.0);
        expect(!host.isSessionSlotRecording(trackId, 2),
               "queued slot is not active before the callback reaches its boundary");
        devices.device->pump();
        expect(host.isSessionSlotRecording(trackId, 2));
        expectWithinAbsoluteError(host.recordingPreviews().at(trackId).startBeat, 2.0, 0.001,
                                  "stopped transport records from the requested cursor");

        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 69, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::controllerEvent(1, 1, 96));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::pitchWheel(1, 9000));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 69));
        devices.device->pump();
        expect(host.recordingPreviews().at(trackId).target ==
               magda::RecordingTargetKind::SessionSlot);
        host.stopMidiRecording();

        expect(!host.isSessionSlotRecordArmed(trackId, 2));
        expect(!host.isSessionSlotRecording(trackId, 2));
        expect(tracks.getTrack(trackId)->playbackMode == magda::TrackPlaybackMode::Session,
               "the materialized slot keeps the recording handle's ownership");
        host.processSessionStateEvents();
        expect(tracks.getTrack(trackId)->activeSessionClipId != magda::INVALID_CLIP_ID,
               "the materialized clip takes over the recording target's run");
        expect(clipsOn(trackId).empty(), "Session recording creates no Arrangement clip");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::In);
        settle();
        expect(clipsOn(trackId).empty(), "later model edits do not begin Arrangement recording");
        const auto clips = sessionClipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectEquals(clips[0].sceneIndex, 2);
            expect(clips[0].loopEnabled);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            expectEquals(static_cast<int>(clips[0].midiCCData.size()), 1);
            expectEquals(static_cast<int>(clips[0].midiPitchBendData.size()), 1);
        }

        host.stopAllSessionClips();
        devices.device->pump();
        host.processSessionStateEvents();
        expect(tracks.getTrack(trackId)->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "global Back to Arrangement releases the materialized recording");

        host.stop();
        devices.closeAudioDevice();
    }

    void standaloneSlotIgnoresPastArrangementPunchOut() {
        beginTest("standalone Session recording ignores a past Arrangement punch-out marker");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Session beyond punch-out");
        preparePunchTrack(trackId);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setPunch(0.0, 1.0, false, true);
        host.armSessionSlotRecording(trackId, 0);
        host.beginArmedSessionSlotRecordings(1.0);
        devices.device->pump();

        expect(host.isSessionSlotRecording(trackId, 0));
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 77, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 77));
        devices.device->pump();
        host.stopMidiRecording();

        const auto clips = sessionClipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);
            if (clips[0].midiNotes.size() == 1)
                expectEquals(clips[0].midiNotes[0].noteNumber, 77);
        }
        expect(clipsOn(trackId).empty());
        host.stop();
        devices.closeAudioDevice();
    }

    void sessionRecordingOwnershipLifecycle() {
        beginTest("Session recording owns only its tracks and releases individually or globally");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto first = tracks.createTrack("Recording one");
        const auto second = tracks.createTrack("Recording two");
        const auto arrangement = tracks.createTrack("Arrangement");
        tracks.getTrack(first)->chain.fxChainElements.emplace_back(polySynth(1));
        tracks.getTrack(arrangement)->chain.fxChainElements.emplace_back(polySynth(2));
        auto& clips = magda::ClipManager::getInstance();
        const auto arrangementClip =
            clips.createMidiClipBeats(first, 0.0, 16.0, magda::ClipView::Arrangement);
        clips.addMidiNote(
            arrangementClip,
            {.noteNumber = 60, .velocity = 100, .startBeat = 0.0, .lengthBeats = 16.0});
        const auto unaffectedClip =
            clips.createMidiClipBeats(arrangement, 0.0, 16.0, magda::ClipView::Arrangement);
        clips.addMidiNote(
            unaffectedClip,
            {.noteNumber = 67, .velocity = 100, .startBeat = 0.0, .lengthBeats = 16.0});
        for (const auto trackId : {first, second}) {
            tracks.setTrackMidiInput(trackId, "keyboard");
            tracks.setTrackRecordArmed(trackId, true);
        }

        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.armSessionSlotRecording(first, 0);
        host.armSessionSlotRecording(second, 0);
        host.beginArmedSessionSlotRecordings();

        expect(tracks.getTrack(first)->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "a queued recording preserves Arrangement until its boundary");
        expect(tracks.getTrack(second)->playbackMode == magda::TrackPlaybackMode::Arrangement);
        devices.device->pump();
        host.processSessionStateEvents();
        expect(tracks.getTrack(first)->playbackMode == magda::TrackPlaybackMode::Session);
        expect(tracks.getTrack(second)->playbackMode == magda::TrackPlaybackMode::Session);
        expect(tracks.getTrack(arrangement)->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "recording leaves unrelated tracks in Arrangement");
        auto unaffectedPeak = 0.0f;
        for (auto block = 0; block < 8; ++block) {
            devices.device->pump();
            unaffectedPeak = std::max(unaffectedPeak, devices.device->lastMagnitude);
        }
        expect(unaffectedPeak > 0.0f, "unrelated Arrangement material keeps sounding");

        tracks.setTrackMuted(arrangement, true);
        clips.createMidiClipBeats(arrangement, 8.0, 1.0, magda::ClipView::Arrangement);
        settle();
        host.stopSessionTrack(first);
        auto arrangementPeak = 0.0f;
        for (auto block = 0; block < 8; ++block) {
            devices.device->pump();
            arrangementPeak = std::max(arrangementPeak, devices.device->lastMagnitude);
        }
        host.processSessionStateEvents();
        expect(tracks.getTrack(first)->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "individual Back to Arrangement releases only its recording target");
        expect(arrangementPeak > 0.0f,
               "Arrangement resumes after a clip republish while Session held the track");
        expect(tracks.getTrack(second)->playbackMode == magda::TrackPlaybackMode::Session);

        host.stopAllSessionClips();
        devices.device->pump();
        host.processSessionStateEvents();
        expect(tracks.getTrack(second)->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "global Back to Arrangement releases the remaining recording target");
        expect(!tracks.isAnyTrackInSessionMode());

        host.stop();
        devices.closeAudioDevice();
    }

    void discardedSessionTakeReleasesOwnership() {
        beginTest("discarding a held Session take releases its recording handle");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Empty recording");
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();

        host.armSessionSlotRecording(trackId, 0);
        host.beginArmedSessionSlotRecordings();
        devices.device->pump();
        host.processSessionStateEvents();
        expect(host.isSessionSlotRecording(trackId, 0));
        expect(tracks.getTrack(trackId)->playbackMode == magda::TrackPlaybackMode::Session);

        host.forgetProject();
        devices.device->pump();
        host.processSessionStateEvents();

        expect(sessionClipsOn(trackId).empty());
        expect(tracks.getTrack(trackId)->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "a discarded take cannot leave Session owning its track");
        host.stop();
        devices.closeAudioDevice();
    }

    void recordingSupersedesRetainedClipIntent() {
        beginTest("Session recording supersedes a clip retained across transport stop");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Clip to recording");
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        auto& clips = magda::ClipManager::getInstance();
        const auto clipId = clips.createMidiClipBeats(trackId, 0.0, 4.0, magda::ClipView::Session);
        clips.setClipSceneIndex(clipId, 0);
        clips.addMidiNote(
            clipId, {.noteNumber = 60, .velocity = 100, .startBeat = 0.0, .lengthBeats = 4.0});

        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.launchClip(clipId);
        devices.device->pump();
        host.processSessionStateEvents();
        expectEquals(tracks.getTrack(trackId)->activeSessionClipId, clipId);

        host.stopPlaying();
        devices.device->pump();
        expectEquals(tracks.getTrack(trackId)->activeSessionClipId, clipId,
                     "transport stop retains the clip intent for restart");
        host.armSessionSlotRecording(trackId, 1);
        host.beginArmedSessionSlotRecordings();
        devices.device->pump();
        host.processSessionStateEvents();

        expect(host.isSessionSlotRecording(trackId, 1));
        expectEquals(tracks.getTrack(trackId)->activeSessionClipId, magda::INVALID_CLIP_ID,
                     "the transport edge cannot relaunch the superseded clip");
        expect(tracks.getTrack(trackId)->playbackMode == magda::TrackPlaybackMode::Session);
        host.stopSessionTrack(trackId);
        devices.device->pump();
        host.stop();
        devices.closeAudioDevice();
    }

    void sessionSlotWaitsForTheNextBar() {
        beginTest("rolling Session slot recording opens on the next bar boundary");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Quantized Session MIDI");
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.play();
        devices.device->pump();

        host.armSessionSlotRecording(trackId, 1);
        host.beginArmedSessionSlotRecordings();
        for (int i = 0; i < 190; ++i)
            devices.device->pump();
        expect(!host.isSessionSlotRecording(trackId, 1), "take remains queued before beat four");
        for (int i = 0; i < 10; ++i)
            devices.device->pump();
        expect(host.isSessionSlotRecording(trackId, 1), "take opens at beat four");

        host.stopMidiRecording();
        expectEquals(static_cast<int>(sessionClipsOn(trackId).size()), 1);
        host.stop();
        devices.closeAudioDevice();
    }

    void countInIsNotPartOfTheTake() {
        beginTest("Record while stopped counts in, and the take starts where the count-in ends");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Counted in");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::Off);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setCountInMode(1);  // one bar, four beats in 4/4

        expect(host.startMidiRecording(4.0), "eligible armed track starts recording");
        devices.device->pump();
        expectWithinAbsoluteError(host.positionBeats(), 4.02, 0.001,
                                  "the cursor rolls in from a bar before beat eight");

        // Played during the count-in: heard, never part of the take.
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 60));
        for (int i = 0; i < 400 && host.positionBeats() < 8.0; ++i)
            devices.device->pump();

        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 64, (juce::uint8)100));
        for (int i = 0; i < 10; ++i)
            devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 64));
        devices.device->pump();
        host.stopMidiRecording();

        const auto clips = clipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1, "one take");
        if (clips.size() == 1) {
            expectWithinAbsoluteError(clips[0].placement.startBeat, 8.0, 0.001);
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1,
                         "only what was played after the count-in");
            if (clips[0].midiNotes.size() == 1)
                expectEquals(clips[0].midiNotes[0].noteNumber, 64);
        }

        host.stop();
        devices.closeAudioDevice();
    }

    void sessionSlotCountsInWhileStopped() {
        beginTest("a Session slot armed while stopped starts recording where the count-in ends");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Counted in Session");
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::Off);
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.setCountInMode(4);  // one beat

        host.armSessionSlotRecording(trackId, 0);
        host.beginArmedSessionSlotRecordings(1.0);
        for (int i = 0; i < 40; ++i)
            devices.device->pump();
        expect(!host.isSessionSlotRecording(trackId, 0), "still counting in");
        for (int i = 0; i < 20; ++i)
            devices.device->pump();
        expect(host.isSessionSlotRecording(trackId, 0), "recording once the count-in ends");
        expectWithinAbsoluteError(host.recordingPreviews().at(trackId).startBeat, 2.0, 0.001,
                                  "the take starts on the play position");

        host.stopMidiRecording();
        host.stop();
        devices.closeAudioDevice();
    }

    void sessionSlotsShareOneBoundary() {
        beginTest("multiple armed Session slots open on one callback boundary");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto first = tracks.createTrack("Session one");
        const auto second = tracks.createTrack("Session two");
        for (const auto trackId : {first, second}) {
            tracks.setTrackMidiInput(trackId, "keyboard");
            tracks.setTrackRecordArmed(trackId, true);
        }
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.play();
        devices.device->pump();
        host.armSessionSlotRecording(first, 0);
        host.armSessionSlotRecording(second, 0);
        host.beginArmedSessionSlotRecordings();
        for (int i = 0; i < 200; ++i)
            devices.device->pump();

        expect(host.isSessionSlotRecording(first, 0));
        expect(host.isSessionSlotRecording(second, 0));
        const auto& previews = host.recordingPreviews();
        expectWithinAbsoluteError(previews.at(first).startBeat, previews.at(second).startBeat,
                                  0.000001);
        host.stopMidiRecording();
        host.stop();
        devices.closeAudioDevice();
    }

    void sessionAllInputSurvivesReconcile() {
        beginTest("Session all-input recording survives reconciliation and excludes audition");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Session all inputs");
        tracks.setTrackMidiInput(trackId, "all");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("z-keyboard");
        host.registerVirtualMidiSource("a-keyboard");
        host.start(devices);
        settle();
        host.armSessionSlotRecording(trackId, 0);
        host.beginArmedSessionSlotRecordings();
        devices.device->pump();

        host.pushMidi("z-keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
        host.pushMidi("a-keyboard", juce::MidiMessage::noteOn(1, 64, (juce::uint8)100));
        host.audition(trackId, juce::MidiMessage::noteOn(1, 72, (juce::uint8)100));
        devices.device->pump();
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::In);
        settle();
        expect(host.isSessionSlotRecording(trackId, 0),
               "an unrelated model update keeps the normalized all-input route alive");
        host.pushMidi("z-keyboard", juce::MidiMessage::noteOff(1, 60));
        host.pushMidi("a-keyboard", juce::MidiMessage::noteOff(1, 64));
        host.audition(trackId, juce::MidiMessage::noteOff(1, 72));
        devices.device->pump();
        host.stopMidiRecording();

        const auto clips = sessionClipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1) {
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 2,
                         "both device sources record, while audition does not");
        }
        expect(clipsOn(trackId).empty());
        host.stop();
        devices.closeAudioDevice();
    }

    void reclickFinishesSessionTakeOnce() {
        beginTest("re-clicking an active Session record slot finishes and disarms it once");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Session re-click");
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.armSessionSlotRecording(trackId, 0);
        host.beginArmedSessionSlotRecordings();
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 60));
        devices.device->pump();

        host.armSessionSlotRecording(trackId, 0);
        expect(!host.isSessionSlotRecordArmed(trackId, 0));
        expect(!host.isSessionSlotRecording(trackId, 0));
        expectEquals(static_cast<int>(sessionClipsOn(trackId).size()), 1);
        expect(clipsOn(trackId).empty());
        tracks.setTrackInputMonitor(trackId, magda::InputMonitorMode::In);
        settle();
        expectEquals(static_cast<int>(sessionClipsOn(trackId).size()), 1,
                     "later reconciliation does not materialize the take twice");
        expect(clipsOn(trackId).empty(), "later edits cannot start Arrangement recording");

        host.stop();
        devices.closeAudioDevice();
    }

    void queuedSessionCancellationRetiresTake() {
        beginTest("cancelling a queued Session take retires it before its boundary");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Queued Session cancellation");
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.play();
        devices.device->pump();
        host.armSessionSlotRecording(trackId, 0);
        host.beginArmedSessionSlotRecordings();
        devices.device->pump();
        expect(!host.isSessionSlotRecording(trackId, 0), "the take is waiting for the bar");
        host.processSessionStateEvents();
        expect(tracks.getTrack(trackId)->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "a queued target does not take ownership before its boundary");

        host.launchScene({trackId}, 1);
        devices.device->pump();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        host.processSessionStateEvents();
        expect(!host.isRecording());
        expect(!host.isSessionSlotRecordArmed(trackId, 0));
        expect(tracks.getTrack(trackId)->playbackMode == magda::TrackPlaybackMode::Arrangement,
               "cancelling before launch preserves Arrangement ownership");
        expect(sessionClipsOn(trackId).empty(), "a take that never opened creates no clip");
        for (int i = 0; i < 400; ++i)
            devices.device->pump();
        expect(!host.isSessionSlotRecording(trackId, 0), "the cancelled boundary cannot restart");
        expect(sessionClipsOn(trackId).empty());

        host.stop();
        devices.closeAudioDevice();
    }

    void timerHarvestsFinishedSessionTake() {
        beginTest("the host timer harvests a finished Session take without a preview read");
        PumpDeviceManager devices;
        expect(open(devices), "fake device opens");
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto trackId = tracks.createTrack("Timer Session harvest");
        tracks.setTrackMidiInput(trackId, "keyboard");
        tracks.setTrackRecordArmed(trackId, true);
        magda::daw::engine_host::EngineHost host;
        host.registerVirtualMidiSource("keyboard");
        host.start(devices);
        settle();
        host.armSessionSlotRecording(trackId, 0);
        host.beginArmedSessionSlotRecordings();
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOn(1, 65, (juce::uint8)100));
        devices.device->pump();
        host.pushMidi("keyboard", juce::MidiMessage::noteOff(1, 65));
        devices.device->pump();

        host.launchScene({trackId}, 1);
        devices.device->pump();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        expect(!host.isRecording());
        expect(!host.isSessionSlotRecordArmed(trackId, 0));
        const auto clips = sessionClipsOn(trackId);
        expectEquals(static_cast<int>(clips.size()), 1);
        if (clips.size() == 1)
            expectEquals(static_cast<int>(clips[0].midiNotes.size()), 1);

        host.stop();
        devices.closeAudioDevice();
    }
};

EngineHostMidiRecordingTest engineHostMidiRecordingTest;

}  // namespace
