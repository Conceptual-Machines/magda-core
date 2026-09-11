#pragma once

#include <functional>
#include <memory>

#include "../../core/ChainNodePath.hpp"
#include "../../core/TypeIds.hpp"

namespace juce {
class AudioDeviceManager;
class AudioPluginFormatManager;
class KnownPluginList;
class MidiMessage;
class String;
}  // namespace juce

namespace magda {
class TempoMap;
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

    /**
     * @brief Where external plugins are found and what can open them (#2566).
     *
     * Both are the fork's, which owns the scan; the engine has no catalog of
     * its own for a plugin that is a file on a machine rather than a class this
     * build contains. Before @ref start, or the first publish goes without
     * them.
     */
    void setPluginServices(juce::AudioPluginFormatManager& formats,
                           const juce::KnownPluginList& knownPlugins);

    /**
     * @brief Where the levels this renders go (#2570).
     *
     * On the message thread at meter rate, per track and for the master, with
     * the peak since the last call. Set before @ref start; without one nothing
     * reads the taps, which costs only the meters.
     */
    using MeterSink = std::function<void(TrackId trackId, float peakL, float peakR)>;
    void meterInto(MeterSink sink);

    /// Take the callback back off the device and stop following the model.
    /// Safe to call twice, and called by the destructor.
    void stop();

    // ===== Live MIDI (#2579) =====
    //
    // Both queue one message for the next callback, from the message thread or
    // from a MIDI callback thread. Never from the audio thread.

    /// A note played at @p trackId itself: the piano roll, the chord panel and
    /// the drum pads, which preview whatever the track is monitoring.
    void audition(TrackId trackId, const juce::MidiMessage& message);

    /// A message from @p deviceId, for whichever tracks are routed to it.
    void pushMidi(const juce::String& deviceId, const juce::MidiMessage& message);

    /// Name a device the system's MIDI list does not hold -- the QWERTY
    /// keyboard -- so a track routed to "all" is bound to it at the next
    /// publish and stays bound to it. Message thread.
    void registerVirtualMidiSource(const juce::String& deviceId);

    // ===== Plugin state =====
    //
    // All three run on the message thread and return once the plugin has been
    // read or written, because a save writes the file immediately after.

    /** @brief Read every external plugin this renders back into the model. */
    void captureExternalPluginStates();

    /**
     * @brief Read the plugin at @p devicePath, plus any on its pads.
     *
     * Callers use this before copying, removing or snapshotting one slot.
     * Pass a Drum Grid's own path to reach its pads: pad plugins have no
     * path of their own (#2207).
     */
    void captureExternalPluginStateAt(const ChainNodePath& devicePath);

    /**
     * @brief Write the model's state for @p devicePath into the plugin (#2573).
     *
     * The model is then updated from the plugin, because a state chunk can
     * change parameters the model's array does not list.
     */
    void applyExternalPluginStateAt(const ChainNodePath& devicePath);

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

    /** @brief The loop, as the one value the transport is published with. */
    struct LoopState {
        bool enabled = false;
        double startBeat = 0.0;
        double endBeat = 0.0;
    };
    LoopState loop() const;

    /// The tempo and signature above as the app's beats<->seconds facade.
    /// Never null, and valid until this host is destroyed.
    const magda::TempoMap* tempoMap() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::daw::engine_host
