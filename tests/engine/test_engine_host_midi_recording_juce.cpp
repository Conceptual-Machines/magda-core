#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <array>
#include <memory>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/audio/midi/RecordingNoteQueue.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineHost.hpp"

namespace {

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
        return true;
    }

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
        magda::test::runWithCleanJuceState([this] { sessionSlotWaitsForTheNextBar(); });
        magda::test::runWithCleanJuceState([this] { sessionSlotsShareOneBoundary(); });
        magda::test::runWithCleanJuceState([this] { sessionAllInputSurvivesReconcile(); });
        magda::test::runWithCleanJuceState([this] { reclickFinishesSessionTakeOnce(); });
        magda::test::runWithCleanJuceState([this] { queuedSessionCancellationRetiresTake(); });
        magda::test::runWithCleanJuceState([this] { timerHarvestsFinishedSessionTake(); });
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

        tracks.setTrackRecordArmed(trackId, false);
        settle();
        expect(host.recordingPreviews().empty(), "disarm clears the preview");
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

        host.launchScene({trackId}, 1);
        devices.device->pump();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        expect(!host.isRecording());
        expect(!host.isSessionSlotRecordArmed(trackId, 0));
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
