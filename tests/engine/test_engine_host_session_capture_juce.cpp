#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineHost.hpp"

namespace {

class CapturePumpDevice final : public juce::AudioIODevice {
  public:
    CapturePumpDevice() : juce::AudioIODevice("Capture Pump", "Capture Pump") {}

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

class CapturePumpType final : public juce::AudioIODeviceType {
  public:
    explicit CapturePumpType(CapturePumpDevice*& device)
        : juce::AudioIODeviceType("Capture Pump"), device_(device) {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool input) const override {
        return input ? juce::StringArray{} : juce::StringArray{"Capture Pump"};
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
        if (output != "Capture Pump")
            return nullptr;
        auto device = std::make_unique<CapturePumpDevice>();
        device_ = device.get();
        return device.release();
    }

  private:
    CapturePumpDevice*& device_;
};

class CapturePumpManager final : public juce::AudioDeviceManager {
  public:
    CapturePumpDevice* device = nullptr;

  protected:
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override {
        types.add(new CapturePumpType(device));
    }
};

class EngineHostSessionCaptureTest final : public juce::UnitTest {
  public:
    EngineHostSessionCaptureTest()
        : juce::UnitTest("Engine Host Session Arrangement Capture", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { launchWithoutRecordCreatesNothing(); });
        magda::test::runWithCleanJuceState([this] { unarmedTrackCapturesAcrossTransportLoop(); });
        magda::test::runWithCleanJuceState([this] { recordBoundaryPreservesSourcePhase(); });
        magda::test::runWithCleanJuceState([this] { punchWindowCapturesExactSessionSpan(); });
        magda::test::runWithCleanJuceState([this] { seekPunchesOutOnce(); });
        magda::test::runWithCleanJuceState([this] { handoverAndIndividualReturnCloseRuns(); });
        magda::test::runWithCleanJuceState([this] { sceneAndGlobalReturnShareBoundaries(); });
        magda::test::runWithCleanJuceState([this] { replacementKeepsBothImmutableSources(); });
        magda::test::runWithCleanJuceState([this] { slotReplacementKeepsBothSources(); });
        magda::test::runWithCleanJuceState([this] { projectBoundaryForgetsOldSources(); });
    }

  private:
    static bool open(CapturePumpManager& devices) {
        return devices.initialise(0, 2, nullptr, true).isEmpty() && devices.device != nullptr;
    }

    static void settle() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }

    static void pump(CapturePumpDevice& device, int blocks) {
        for (auto block = 0; block < blocks; ++block)
            device.pump();
    }

    static magda::ClipId sessionMidi(magda::TrackId track, int scene, int note) {
        auto& clips = magda::ClipManager::getInstance();
        const auto clip = clips.createMidiClipBeats(track, 0.0, 4.0, magda::ClipView::Session);
        clips.setClipSceneIndex(clip, scene);
        clips.setClipLaunchQuantize(clip, magda::LaunchQuantize::None);
        clips.addMidiNote(
            clip, {.noteNumber = note, .velocity = 100, .startBeat = 0.0, .lengthBeats = 4.0});
        return clip;
    }

    static juce::File silentWav() {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("magda-session-capture", ".wav");
        juce::AudioBuffer<float> audio(1, 96000);
        audio.clear();
        juce::WavAudioFormat format;
        JUCE_BEGIN_IGNORE_WARNINGS_MSVC(4996)
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 16, {}, 0));
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        JUCE_END_IGNORE_WARNINGS_MSVC
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
        return file;
    }

    static magda::ClipId sessionAudio(magda::TrackId track, int scene, const juce::File& file) {
        auto& clips = magda::ClipManager::getInstance();
        const auto clip = clips.createAudioClipBeats(track, 0.0, 4.0, file.getFullPathName(),
                                                     magda::ClipView::Session, 120.0);
        clips.setClipSceneIndex(clip, scene);
        clips.setClipLaunchQuantize(clip, magda::LaunchQuantize::None);
        return clip;
    }

    static std::vector<magda::ClipInfo> arrangement(magda::TrackId track) {
        auto& clips = magda::ClipManager::getInstance();
        std::vector<magda::ClipInfo> result;
        for (const auto id : clips.getClipsOnTrack(track, magda::ClipView::Arrangement))
            if (const auto* clip = clips.getClip(id))
                result.push_back(*clip);
        return result;
    }

    void launchWithoutRecordCreatesNothing() {
        beginTest("Session launches without Arrangement record create no clips");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        const auto track = magda::TrackManager::getInstance().createTrack("Playback only");
        const auto clip = sessionMidi(track, 0, 60);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();
        host.launchClip(clip);
        pump(*devices.device, 20);
        host.stopSessionTrack(track);
        devices.device->pump();
        host.processSessionStateEvents();

        expect(arrangement(track).empty());
        host.stop();
        devices.closeAudioDevice();
    }

    void unarmedTrackCapturesAcrossTransportLoop() {
        beginTest("unarmed Session material captures while transport loops and punch-out rolls on");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        const auto track = magda::TrackManager::getInstance().createTrack("Session");
        const auto clip = sessionMidi(track, 0, 60);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();

        host.setLoop(true, 0.0, 0.25);
        expect(host.startMidiRecording(0.0), "Session material makes Arrangement record eligible");
        host.launchClip(clip);
        pump(*devices.device, 50);
        magda::TrackManager::getInstance().createTrack("Unrelated plan swap");
        settle();
        host.stopMidiRecording();

        expect(host.isPlaying(), "record punch-out leaves transport playing");
        const auto captured = arrangement(track);
        expectEquals(static_cast<int>(captured.size()), 1);
        if (captured.size() == 1) {
            expectWithinAbsoluteError(captured[0].startBeats, 0.0, 0.000001);
            expectWithinAbsoluteError(captured[0].lengthBeats, 1.0, 0.03);
        }

        host.stop();
        devices.closeAudioDevice();
    }

    void recordBoundaryPreservesSourcePhase() {
        beginTest("recording a run already sounding starts at punch-in with its source phase");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        const auto track = magda::TrackManager::getInstance().createTrack("Punch in");
        const auto clip = sessionMidi(track, 0, 62);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();
        host.launchClip(clip);
        pump(*devices.device, 25);

        expect(host.startMidiRecording(0.0));
        pump(*devices.device, 25);
        host.stopMidiRecording();

        auto captured = arrangement(track);
        expectEquals(static_cast<int>(captured.size()), 1);
        if (captured.size() == 1) {
            expectWithinAbsoluteError(captured[0].startBeats, 0.5, 0.03);
            expectWithinAbsoluteError(captured[0].lengthBeats, 0.5, 0.03);
            expectWithinAbsoluteError(captured[0].midiOffset, 0.5, 0.03);
        }

        pump(*devices.device, 10);
        expect(host.startMidiRecording(99.0), "rolling re-arm uses the live boundary");
        pump(*devices.device, 10);
        host.stopMidiRecording();
        captured = arrangement(track);
        expectEquals(static_cast<int>(captured.size()), 2);
        if (captured.size() == 2) {
            expectWithinAbsoluteError(captured[1].startBeats, 1.2, 0.03);
            expectWithinAbsoluteError(captured[1].lengthBeats, 0.2, 0.03);
            expectWithinAbsoluteError(captured[1].midiOffset, 1.2, 0.03);
        }

        host.stop();
        devices.closeAudioDevice();
    }

    void punchWindowCapturesExactSessionSpan() {
        beginTest("punch markers bound Session capture on the audio callback");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        const auto track = magda::TrackManager::getInstance().createTrack("Session punch");
        const auto clip = sessionMidi(track, 0, 67);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();
        host.launchClip(clip);
        pump(*devices.device, 10);

        host.setPunch(0.4, 0.6, true, true);
        expect(host.startMidiRecording(0.1));
        pump(*devices.device, 30);
        settle();

        expect(!host.isRecording(), "punch-out finalizes while playback continues");
        const auto captured = arrangement(track);
        expectEquals(static_cast<int>(captured.size()), 1);
        if (captured.size() == 1) {
            expectWithinAbsoluteError(captured[0].startBeats, 0.4, 0.000001);
            expectWithinAbsoluteError(captured[0].lengthBeats, 0.2, 0.000001);
            expectWithinAbsoluteError(captured[0].midiOffset, 0.4, 0.000001);
        }

        host.stop();
        devices.closeAudioDevice();
    }

    void seekPunchesOutOnce() {
        beginTest("locating closes Session capture once while playback continues");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        const auto track = magda::TrackManager::getInstance().createTrack("Locate");
        const auto clip = sessionMidi(track, 0, 63);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();
        expect(host.startMidiRecording(0.0));
        host.launchClip(clip);
        pump(*devices.device, 10);

        host.locateSeconds(1.0);
        expect(!host.isRecording());
        expect(host.isPlaying());
        pump(*devices.device, 10);
        host.processSessionStateEvents();
        expectEquals(static_cast<int>(arrangement(track).size()), 1,
                     "post-seek playback cannot duplicate the closed span");

        expect(host.startMidiRecording(0.0));
        pump(*devices.device, 10);
        host.stopPlaying();
        expect(!host.isPlaying());
        expectEquals(static_cast<int>(arrangement(track).size()), 2,
                     "transport stop closes the re-armed span once");

        host.stop();
        devices.closeAudioDevice();
    }

    void handoverAndIndividualReturnCloseRuns() {
        beginTest("slot handover and individual Back to Arrangement close exact runs");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        const auto track = magda::TrackManager::getInstance().createTrack("Handover");
        const auto first = sessionMidi(track, 0, 60);
        const auto second = sessionMidi(track, 1, 64);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();
        expect(host.startMidiRecording(0.0));

        host.launchClip(first);
        pump(*devices.device, 10);
        host.launchClip(second);
        pump(*devices.device, 10);
        host.stopSessionTrack(track);
        devices.device->pump();
        host.processSessionStateEvents();
        host.stopMidiRecording();

        const auto captured = arrangement(track);
        expectEquals(static_cast<int>(captured.size()), 2);
        if (captured.size() == 2) {
            std::set<int> notes;
            for (const auto& span : captured)
                if (!span.midiNotes.empty())
                    notes.insert(span.midiNotes.front().noteNumber);
            expect(notes == std::set<int>{60, 64});
        }

        host.stop();
        devices.closeAudioDevice();
    }

    void sceneAndGlobalReturnShareBoundaries() {
        beginTest("scene launch and global Back to Arrangement capture all tracks");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        const auto first = tracks.createTrack("Scene one");
        const auto second = tracks.createTrack("Scene two");
        const auto audioFile = silentWav();
        sessionMidi(first, 0, 65);
        sessionAudio(second, 0, audioFile);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();
        expect(host.startMidiRecording(0.0));

        host.launchScene({first, second}, 0);
        pump(*devices.device, 20);
        host.stopAllSessionClips();
        devices.device->pump();
        host.processSessionStateEvents();
        host.stopMidiRecording();

        const auto a = arrangement(first);
        const auto b = arrangement(second);
        expectEquals(static_cast<int>(a.size()), 1);
        expectEquals(static_cast<int>(b.size()), 1);
        if (a.size() == 1 && b.size() == 1) {
            expectWithinAbsoluteError(a[0].startBeats, b[0].startBeats, 0.000001);
            expectWithinAbsoluteError(a[0].lengthBeats, b[0].lengthBeats, 0.000001);
            expect(a[0].isMidi());
            expect(b[0].isAudio());
        }

        host.stop();
        devices.closeAudioDevice();
        audioFile.deleteFile();
    }

    void replacementKeepsBothImmutableSources() {
        beginTest("a sounding slot edit splits capture across immutable source snapshots");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        const auto track = magda::TrackManager::getInstance().createTrack("Replacement");
        const auto clip = sessionMidi(track, 0, 60);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();
        expect(host.startMidiRecording(0.0));
        host.launchClip(clip);
        pump(*devices.device, 10);

        auto& clips = magda::ClipManager::getInstance();
        clips.clearMidiNotes(clip);
        clips.addMidiNote(
            clip, {.noteNumber = 72, .velocity = 100, .startBeat = 0.0, .lengthBeats = 4.0});
        settle();
        pump(*devices.device, 10);
        host.stopMidiRecording();

        const auto captured = arrangement(track);
        expectEquals(static_cast<int>(captured.size()), 2);
        std::set<int> notes;
        for (const auto& span : captured)
            if (!span.midiNotes.empty())
                notes.insert(span.midiNotes.front().noteNumber);
        expect(notes == std::set<int>{60, 72}, "each span keeps the source that actually played");

        host.stop();
        devices.closeAudioDevice();
    }

    void slotReplacementKeepsBothSources() {
        beginTest("replacing a sounding slot keeps both clip-ID source snapshots");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        const auto track = magda::TrackManager::getInstance().createTrack("Refill");
        const auto oldClip = sessionMidi(track, 0, 55);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();
        expect(host.startMidiRecording(0.0));
        host.launchClip(oldClip);
        pump(*devices.device, 10);

        auto& clips = magda::ClipManager::getInstance();
        clips.deleteClip(oldClip);
        sessionMidi(track, 0, 79);
        settle();
        pump(*devices.device, 10);
        host.stopMidiRecording();

        const auto captured = arrangement(track);
        expectEquals(static_cast<int>(captured.size()), 2);
        std::set<int> notes;
        for (const auto& span : captured)
            if (!span.midiNotes.empty())
                notes.insert(span.midiNotes.front().noteNumber);
        expect(notes == std::set<int>{55, 79});

        host.stop();
        devices.closeAudioDevice();
    }

    void projectBoundaryForgetsOldSources() {
        beginTest("project teardown cannot capture an old source into reused model IDs");
        CapturePumpManager devices;
        expect(open(devices));
        if (devices.device == nullptr)
            return;

        auto& tracks = magda::TrackManager::getInstance();
        auto& clips = magda::ClipManager::getInstance();
        const auto oldTrack = tracks.createTrack("Old project");
        const auto oldClip = sessionMidi(oldTrack, 0, 48);
        magda::daw::engine_host::EngineHost host;
        host.start(devices);
        settle();
        expect(host.startMidiRecording(0.0));
        host.launchClip(oldClip);
        pump(*devices.device, 10);

        host.forgetProject();
        clips.clearAllClips();
        tracks.clearAllTracks();
        const auto newTrack = tracks.createTrack("New project");
        expectEquals(newTrack, oldTrack);
        const auto newClip = sessionMidi(newTrack, 0, 76);
        settle();
        expect(host.startMidiRecording(0.0));
        host.launchClip(newClip);
        pump(*devices.device, 10);
        host.stopMidiRecording();

        const auto captured = arrangement(newTrack);
        expectEquals(static_cast<int>(captured.size()), 1);
        if (captured.size() == 1 && !captured[0].midiNotes.empty())
            expectEquals(captured[0].midiNotes.front().noteNumber, 76);

        host.stop();
        devices.closeAudioDevice();
    }
};

EngineHostSessionCaptureTest engineHostSessionCaptureTest;

}  // namespace
