#pragma once

#include <memory>

namespace juce {
class AudioDeviceManager;
}

/**
 * @file EngineHost.hpp
 * @brief The app's end of magda::engine: a device callback and a publisher.
 *
 * Everything the engine can do it has so far done offline, driven by a harness
 * (#2551). This is the first thing to drive it from an audio device: it owns an
 * EngineSession, renders it from the callback, and republishes the project
 * whenever the model moves.
 *
 * The engine's own types stay out of this header on purpose. MagdaAudioEngine
 * is compiled into magda_daw, which does not see magda_engine's includes; what
 * crosses that line is one pointer.
 *
 * Threading: everything but the callback runs on the message thread, which is
 * the session's one publishing thread. Model edits are coalesced through an
 * async update rather than published where they are noticed, so a drag that
 * fires a hundred property changes publishes once.
 */

namespace magda::daw::engine_host {

class EngineHost {
  public:
    EngineHost();
    ~EngineHost();

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    /**
     * @brief Render into @p devices, and follow the model from here on.
     *
     * The device decides the sample rate and block size, so the session behind
     * this is built on the first callback rather than here: until then the
     * output is silence and the cursor stands still, which is what a session
     * with no plan renders anyway (EngineSession::process).
     */
    void start(juce::AudioDeviceManager& devices);

    /// Take the callback back off the device and stop following the model.
    /// Safe to call twice, and called by the destructor.
    void stop();

    // ===== Transport =====
    //
    // Play, stop and locate are a request the clock applies once, so each of
    // these publishes a transport rather than reaching into the audio thread.

    void play();
    void stopPlaying();
    void locateSeconds(double seconds);
    bool isPlaying() const;

    /// Where the cursor is. Readable from any thread; what a playhead is drawn
    /// from.
    double positionBeats() const;
    double positionSeconds() const;

    // ===== What the transport is published with =====

    void setTempo(double bpm);
    double tempo() const;
    void setTimeSignature(int numerator, int denominator);
    void getTimeSignature(int& numerator, int& denominator) const;
    void setLoop(bool enabled, double startBeat, double endBeat);
    void setMetronomeEnabled(bool enabled);
    bool isMetronomeEnabled() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::daw::engine_host
