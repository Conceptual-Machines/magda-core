#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../command.hpp"
#include "../interfaces/clip_interface.hpp"
#include "../interfaces/mixer_interface.hpp"
#include "../interfaces/track_interface.hpp"
#include "../interfaces/transport_interface.hpp"
#include "AudioEngine.hpp"

namespace magica {

/**
 * @brief Tracktion Engine implementation of AudioEngine
 *
 * This class bridges our command-based interface with the actual Tracktion Engine,
 * providing real audio functionality to our multi-agent DAW system.
 *
 * Inherits from AudioEngine (which includes AudioEngineListener) so it can:
 * - Be used as a generic audio engine
 * - Receive state change notifications from TimelineController
 */
class TracktionEngineWrapper : public AudioEngine,
                               public TransportInterface,
                               public TrackInterface,
                               public ClipInterface,
                               public MixerInterface {
  public:
    TracktionEngineWrapper();
    ~TracktionEngineWrapper();

    // Initialize the engine
    bool initialize() override;
    void shutdown() override;

    // Process commands from MCP agents
    CommandResponse processCommand(const Command& command);

    // TransportInterface implementation
    void play() override;
    void stop() override;
    void pause() override;
    void record() override;
    void locate(double position_seconds) override;
    void locateMusical(int bar, int beat, int tick = 0) override;
    double getCurrentPosition() const override;
    void getCurrentMusicalPosition(int& bar, int& beat, int& tick) const override;
    bool isPlaying() const override;
    bool isRecording() const override;
    void setTempo(double bpm) override;
    double getTempo() const override;
    void setTimeSignature(int numerator, int denominator) override;
    void getTimeSignature(int& numerator, int& denominator) const override;
    void setLooping(bool enabled) override;
    void setLoopRegion(double start_seconds, double end_seconds) override;
    bool isLooping() const override;

    // Metronome/click track control
    void setMetronomeEnabled(bool enabled) override;
    bool isMetronomeEnabled() const override;

    // Device management
    juce::AudioDeviceManager* getDeviceManager() override;

    // AudioEngineListener implementation (receives state changes from UI)
    void onTransportPlay(double position) override;
    void onTransportStop(double returnPosition) override;
    void onTransportPause() override;
    void onTransportRecord(double position) override;
    void onEditPositionChanged(double position) override;
    void onTempoChanged(double bpm) override;
    void onTimeSignatureChanged(int numerator, int denominator) override;
    void onLoopRegionChanged(double startTime, double endTime, bool enabled) override;
    void onLoopEnabledChanged(bool enabled) override;

    // TrackInterface implementation
    std::string createAudioTrack(const std::string& name) override;
    std::string createMidiTrack(const std::string& name) override;
    void deleteTrack(const std::string& track_id) override;
    void setTrackName(const std::string& track_id, const std::string& name) override;
    std::string getTrackName(const std::string& track_id) const override;
    void setTrackMuted(const std::string& track_id, bool muted) override;
    bool isTrackMuted(const std::string& track_id) const override;
    void setTrackSolo(const std::string& track_id, bool solo) override;
    bool isTrackSolo(const std::string& track_id) const override;
    void setTrackArmed(const std::string& track_id, bool armed) override;
    bool isTrackArmed(const std::string& track_id) const override;
    void setTrackColor(const std::string& track_id, int r, int g, int b) override;
    std::vector<std::string> getAllTrackIds() const override;
    bool trackExists(const std::string& track_id) const override;

    // ClipInterface implementation - fixed method signatures
    std::string addMidiClip(const std::string& track_id, double start_time, double length,
                            const std::vector<MidiNote>& notes) override;
    std::string addAudioClip(const std::string& track_id, double start_time,
                             const std::string& audio_file_path) override;
    void deleteClip(const std::string& clip_id) override;
    void moveClip(const std::string& clip_id, double new_start_time) override;
    void resizeClip(const std::string& clip_id, double new_length) override;
    double getClipStartTime(const std::string& clip_id) const override;
    double getClipLength(const std::string& clip_id) const override;
    void addNoteToMidiClip(const std::string& clip_id, const MidiNote& note) override;
    void removeNotesFromMidiClip(const std::string& clip_id, double start_time,
                                 double end_time) override;
    std::vector<MidiNote> getMidiClipNotes(const std::string& clip_id) const override;
    std::vector<std::string> getTrackClips(const std::string& track_id) const override;
    bool clipExists(const std::string& clip_id) const override;

    // MixerInterface implementation - fixed to use double instead of float
    void setTrackVolume(const std::string& track_id, double volume) override;
    double getTrackVolume(const std::string& track_id) const override;
    void setTrackPan(const std::string& track_id, double pan) override;
    double getTrackPan(const std::string& track_id) const override;
    void setMasterVolume(double volume) override;
    double getMasterVolume() const override;
    std::string addEffect(const std::string& track_id, const std::string& effect_name) override;
    void removeEffect(const std::string& effect_id) override;
    void setEffectParameter(const std::string& effect_id, const std::string& parameter_name,
                            double value) override;
    double getEffectParameter(const std::string& effect_id,
                              const std::string& parameter_name) const override;
    void setEffectEnabled(const std::string& effect_id, bool enabled) override;
    bool isEffectEnabled(const std::string& effect_id) const override;
    std::vector<std::string> getAvailableEffects() const override;
    std::vector<std::string> getTrackEffects(const std::string& track_id) const override;

  private:
    // Tracktion Engine components
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<tracktion::Edit> currentEdit_;

    // Helper methods
    tracktion::Track* findTrackById(const std::string& track_id) const;
    tracktion::Clip* findClipById(const std::string& clip_id) const;
    std::string generateTrackId();
    std::string generateClipId();
    std::string generateEffectId();

    // State tracking
    std::map<std::string, tracktion::Track::Ptr> trackMap_;
    std::map<std::string, tracktion::Clip::Ptr> clipMap_;
    std::map<std::string, void*> effectMap_;  // For tracking effects
    int nextTrackId_ = 1;
    int nextClipId_ = 1;
    int nextEffectId_ = 1;
};

}  // namespace magica
