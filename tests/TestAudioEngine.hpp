#pragma once

#include <cstdlib>

#include "magda/daw/audio/DeviceMeters.hpp"
#include "magda/daw/audio/TrackMeters.hpp"
#include "magda/daw/engine/AudioEngine.hpp"

namespace magda::test {
class TestAudioEngine : public AudioEngine {
  public:
    bool initialize() override {
        return true;
    }

    void shutdown() override {}

    bool hasActiveEdit() const override {
        return true;
    }

    BeatDuration getEditLengthBeats() const override {
        return {};
    }

    juce::File getEditFile() const override {
        return {};
    }

    void play() override {
        playing = true;
    }

    void stop() override {
        ++stopCalls;
        playing = false;
        recording = false;
    }

    void pause() override {
        stop();
    }

    void record() override {
        recording = true;
    }

    void locate(double positionSeconds) override {
        ++locateCalls;
        position = positionSeconds;
    }

    double getCurrentPosition() const override {
        return position;
    }

    bool isPlaying() const override {
        return playing;
    }

    bool isRecording() const override {
        return recording;
    }

    double getSessionPlayheadPosition() const override {
        return -1.0;
    }

    ClipId getSessionPlayheadClipId() const override {
        return INVALID_CLIP_ID;
    }

    std::unordered_map<ClipId, double> getActiveClipPlayheadPositions() const override {
        return {};
    }

    SessionClipPlayState getSessionClipPlayState(ClipId) const override {
        return SessionClipPlayState::Stopped;
    }

    void stopSessionTrack(TrackId) override {}

    bool isSessionTrackStopPending(TrackId) const override {
        return false;
    }

    double getAudioThreadTransportSeconds() const override {
        return -1.0;
    }

    void deactivateAllSessionClips() override {
        ++deactivateCalls;
    }

    void launchSessionScene(const std::vector<TrackId>&, int) override {}

    void setTempo(double bpm) override {
        tempo = bpm;
    }

    double getTempo() const override {
        return tempo;
    }

    const TempoMap* tempoMap() const override {
        return nullptr;
    }

    void setTimeSignature(int numerator, int denominator) override {
        timeSigNumerator = numerator;
        timeSigDenominator = denominator;
    }

    void getTimeSignature(int& numerator, int& denominator) const override {
        numerator = timeSigNumerator;
        denominator = timeSigDenominator;
    }

    void setLooping(bool enabled) override {
        ++setLoopingCalls;
        looping = enabled;
    }

    void setLoopRegionBeats(BeatRange range) override {
        loopStart = range.start.value;
        loopEnd = range.end.value;
    }

    bool isLooping() const override {
        return looping;
    }

    BeatRange getLoopRegionBeats() const override {
        return {{loopStart}, {loopEnd}};
    }

    void setMetronomeEnabled(bool enabled) override {
        metronome = enabled;
    }

    bool isMetronomeEnabled() const override {
        return metronome;
    }

    void setCountInMode(int mode) override {
        countInMode = mode;
    }

    int getCountInMode() const override {
        return countInMode;
    }

    void updateTriggerState() override {}
    void processSessionStateEvents() override {}

    AudioIOControl* getAudioIO() override {
        return nullptr;
    }

    void setMidiDevicesReadyCallback(std::function<void()>) override {}

    TrackMeters& meters() override {
        return meters_;
    }

    const TrackMeters& meters() const override {
        return meters_;
    }

    DeviceMeters& deviceMeters() override {
        return deviceMeters_;
    }

    const DeviceMeters& deviceMeters() const override {
        return deviceMeters_;
    }

    bool showDeviceEditor(const ChainNodePath&) override {
        return false;
    }

    bool hideDeviceEditor(const ChainNodePath&) override {
        return false;
    }

    bool toggleDeviceEditor(const ChainNodePath&) override {
        return false;
    }

    bool isDeviceEditorOpen(const ChainNodePath&) const override {
        return false;
    }

    MagdaApi& getMagdaApi() override {
        std::abort();
    }

    InsertRenderCapture* getInsertRenderCapture() override {
        return nullptr;
    }

    std::unique_ptr<OfflineRenderSession> createOfflineRenderSession(bool) override {
        return nullptr;
    }
    void setTrackFrozen(TrackId, bool) override {}

    void previewNoteOnTrack(const std::string&, int, int, bool) override {}

    void onTransportPlay(double positionSeconds) override {
        locate(positionSeconds);
        play();
    }

    void onTransportStop(double returnPosition) override {
        stop();
        locate(returnPosition);
    }

    void onTransportPause() override {
        pause();
    }

    void onTransportRecord(double positionSeconds) override {
        locate(positionSeconds);
        record();
    }

    void onTransportStopRecording() override {
        recording = false;
    }

    void onEditPositionChanged(double positionSeconds) override {
        locate(positionSeconds);
    }

    void onTempoChanged(double bpm) override {
        setTempo(bpm);
    }

    void onTimeSignatureChanged(int numerator, int denominator) override {
        setTimeSignature(numerator, denominator);
    }

    void onLoopRegionChanged(double startTime, double endTime, bool enabled) override {
        loopStart = startTime;
        loopEnd = endTime;
        setLooping(enabled);
    }

    void onLoopEnabledChanged(bool enabled) override {
        setLooping(enabled);
    }

    int stopCalls = 0;
    int deactivateCalls = 0;
    int setLoopingCalls = 0;
    int locateCalls = 0;
    bool playing = true;
    bool recording = true;
    bool looping = true;
    bool metronome = false;
    int countInMode = 0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    double tempo = 120.0;
    double position = 12.0;
    double loopStart = 0.0;
    double loopEnd = 0.0;
    TrackMeters meters_;
    DeviceMeters deviceMeters_;
};

}  // namespace magda::test
