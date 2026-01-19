#pragma once

#include "../ui/state/TransportStateListener.hpp"

namespace juce {
class AudioDeviceManager;
}

namespace magica {

/**
 * @brief Abstract audio engine interface
 *
 * This provides a clean abstraction over the actual audio engine implementation.
 * Concrete implementations (e.g., TracktionEngineWrapper) inherit from this.
 *
 * Also inherits from AudioEngineListener so the TimelineController can notify
 * the audio engine of state changes via the observer pattern.
 */
class AudioEngine : public AudioEngineListener {
  public:
    virtual ~AudioEngine() = default;

    // ===== Lifecycle =====
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // ===== Transport =====
    virtual void play() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void record() = 0;
    virtual void locate(double positionSeconds) = 0;
    virtual double getCurrentPosition() const = 0;
    virtual bool isPlaying() const = 0;
    virtual bool isRecording() const = 0;

    // ===== Tempo =====
    virtual void setTempo(double bpm) = 0;
    virtual double getTempo() const = 0;
    virtual void setTimeSignature(int numerator, int denominator) = 0;

    // ===== Loop =====
    virtual void setLooping(bool enabled) = 0;
    virtual void setLoopRegion(double startSeconds, double endSeconds) = 0;
    virtual bool isLooping() const = 0;

    // ===== Metronome =====
    virtual void setMetronomeEnabled(bool enabled) = 0;
    virtual bool isMetronomeEnabled() const = 0;

    // ===== Device Management =====
    virtual juce::AudioDeviceManager* getDeviceManager() = 0;
};

}  // namespace magica
