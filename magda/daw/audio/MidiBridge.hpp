#pragma once

/**
 * @file MidiBridge.hpp
 * @brief MIDI devices, routing and monitoring, owned by the app rather than an engine (#2759).
 */

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core/MidiTypes.hpp"
#include "../core/TypeIds.hpp"
#include "midi/MidiEventQueue.hpp"
#include "midi/RecordingNoteQueue.hpp"

namespace magda {

// Forward declarations
class AudioBridge;
struct TrackMeters;

// ============================================================================
// RawMidiListener
// ============================================================================

/**
 * @brief Receives raw MIDI messages from every open input device.
 *
 * Called at the top of MidiBridge::handleIncomingMidiMessage, before routing
 * or monitoring logic. Intended for ControllerRouter to tap controller ports
 * without interfering with the existing track-routing pipeline.
 *
 * Called from the MIDI callback thread -- implementations must be lock-free.
 */
struct RawMidiListener {
    virtual ~RawMidiListener() = default;
    // deviceId: JUCE identifier (opaque, OS-specific format).
    // deviceName: human-readable display name (also provided so listeners can
    //             match via magda::midi::matches without a separate lookup).
    virtual void onRawMidi(const juce::String& deviceId, const juce::String& deviceName,
                           const juce::MidiMessage& msg) = 0;
};

/// What the fork's virtual keyboard device is called.
inline constexpr const char* kQwertyMidiDeviceName = "QWERTY Keyboard";

/// The keyboard's device ID on both engines: what the fork's virtual device
/// reports (tracktion_DeviceManager.cpp:248 derives it from the name), so a
/// track routed to it on one engine is routed to it on the other.
inline juce::String qwertyMidiDeviceId() {
    return "vmidiin_" + juce::String::toHexString(juce::String(kQwertyMidiDeviceName).hashCode());
}

/**
 * @brief Where live MIDI goes when something other than the fork renders (#2579).
 *
 * Called from the MIDI callback thread and the message thread; never the audio thread.
 */
class LiveMidiSink {
  public:
    virtual ~LiveMidiSink() = default;
    virtual void pushMidi(const juce::String& deviceId, const juce::MidiMessage& message) = 0;
    virtual void audition(TrackId trackId, const juce::MidiMessage& message) = 0;
};

// ============================================================================
// MidiBridge
// ============================================================================

/**
 * @brief The MIDI devices, the routes onto tracks, and the activity the UI draws.
 *
 * The app's, not an engine's (#2759): the ports stay open across an engine switch, and the
 * routing menus, the controller layer, the QWERTY keyboard and the MIDI monitor all ask
 * here rather than asking whichever engine renders. Whichever engine that is attaches
 * through @ref useEngine and leaves through @ref forgetEngine.
 *
 * The one instance lives as long as the process and is never destroyed; @ref getInstance
 * says why.
 */
class MidiBridge : public juce::MidiInputCallback {
  public:
    static MidiBridge& getInstance();

    MidiBridge(MidiBridge&&) = delete;
    MidiBridge& operator=(MidiBridge&&) = delete;

    /**
     * @brief The engine that renders is attaching, offering @p virtualInputs of its own.
     *
     * Tracktion's enabled virtual devices under the fork, the QWERTY keyboard under the
     * native engine: devices the system's MIDI list never holds but a track can route to.
     *
     * @p owner is an opaque token for whoever may hand the service back. A later attach
     * replaces it, which is how the native engine layers over the fork it holds.
     */
    void useEngine(const void* owner, std::function<std::vector<MidiDeviceInfo>()> virtualInputs);

    /** @brief Whether @p owner is the engine attached, for a teardown shared with others. */
    bool isAttachedTo(const void* owner) const {
        return owner != nullptr && owner_ == owner;
    }

    /**
     * @brief That engine is going away: stop the inputs and forget what it lent.
     *
     * Does nothing unless @p owner is the engine attached, so a wrapper that never
     * attached, or one another has since replaced, cannot unwind the live engine's MIDI.
     *
     * Inputs stop here rather than at destruction, because a callback arriving after the
     * engine is gone has nowhere to route.
     */
    void forgetEngine(const void* owner);

    /**
     * @brief Set the AudioBridge reference used for triggering MIDI activity
     * and track lookup. Must be called after AudioBridge is created.
     */
    void setAudioBridge(AudioBridge* audioBridge);

    /**
     * @brief Clear the AudioBridge pointer before it's destroyed, to avoid a
     * dangling pointer between shutdown steps.
     */
    void clearAudioBridge() {
        audioBridge_ = nullptr;
    }

    /**
     * @brief Set the shared meters object that feeds the MIDI activity light.
     *
     * Lets the activity light work with no AudioBridge, under the magda engine.
     */
    void setMeters(TrackMeters* meters) {
        meters_ = meters;
    }

    /**
     * @brief Set where live MIDI goes under the magda engine, or clear it.
     *
     * Clearing (nullptr) waits for any in-flight handleIncomingMidiMessage
     * call to drain, the way stopAllInputs waits on activeCallbacks_.
     */
    void setLiveSink(LiveMidiSink* sink);

    /**
     * @brief Enable/disable forwarding MIDI to instrument plugins.
     *
     * When enabled, incoming MIDI is injected into Tracktion tracks.
     * @param enabled True to forward MIDI to plugins
     */
    void setMidiToPluginsEnabled(bool enabled) {
        forwardMidiToPlugins_ = enabled;
    }

    // =========================================================================
    // MIDI Device Enumeration
    // =========================================================================

    /**
     * @brief Get all available MIDI input devices
     * @return Vector of device info (id, name, enabled status)
     */
    std::vector<MidiDeviceInfo> getAvailableMidiInputs() const;

    /**
     * @brief Notify listeners that the MIDI device list has changed.
     * Call after creating/destroying virtual devices so routing selectors refresh.
     */
    void notifyMidiDeviceListChanged() {
        midiDeviceListListeners_.call([](Listener& l) { l.midiDeviceListChanged(); });
    }

    /**
     * @brief Audio Settings changed which inputs are active: reopen, relist and reroute.
     *
     * @ref onActiveInputsChanged is the rendering engine's rerouting; the bridge only lists
     * and opens.
     */
    void activeInputsChanged();
    std::function<void()> onActiveInputsChanged;

    struct Listener {
        virtual ~Listener() = default;
        virtual void midiDeviceListChanged() = 0;
    };

    void addMidiDeviceListListener(Listener* l) {
        midiDeviceListListeners_.add(l);
    }
    void removeMidiDeviceListListener(Listener* l) {
        midiDeviceListListeners_.remove(l);
    }

    /**
     * @brief Get all available MIDI output devices
     * @return Vector of device info
     */
    static std::vector<MidiDeviceInfo> getAvailableMidiOutputs();

    // =========================================================================
    // MIDI Output (host → device)
    // =========================================================================

    /**
     * @brief Send a MIDI message to an output device, opening it lazily on
     *        first use.
     *
     * `deviceNameOrId` matches against either the device's display name
     * (what scripts see in `e.port`) or its JUCE identifier. The output
     * stays open until the engine is forgotten, so subsequent sends are cheap.
     *
     * Thread-safe. Returns false if the device cannot be found or opened.
     */
    bool sendMidi(const juce::String& deviceNameOrId, const juce::MidiMessage& msg);

    /**
     * @brief Convenience: build a SysEx message from a byte payload (without
     *        F0/F7 framing) and dispatch via sendMidi.
     */
    bool sendSysEx(const juce::String& deviceNameOrId, const juce::uint8* data, size_t numBytes);

    /** Inject a message into one track's live plugin chain. Used by controller
     *  script User/Note modes whose physical input is otherwise surface-only. */
    bool injectMidiToTrack(TrackId trackId, const juce::MidiMessage& msg);

    // =========================================================================
    // MIDI Device Enable/Disable
    // =========================================================================

    /**
     * @brief Enable a MIDI input device globally
     * @param deviceId Device identifier from MidiDeviceInfo
     */
    void enableMidiInput(const juce::String& deviceId);

    /**
     * @brief Disable a MIDI input device globally
     * @param deviceId Device identifier
     */
    void disableMidiInput(const juce::String& deviceId);

    // =========================================================================
    // Track MIDI Routing
    // =========================================================================

    /**
     * @brief Set MIDI input source for a track
     * @param trackId MAGDA track ID
     * @param midiDeviceId MIDI device ID (empty string = no input)
     */
    void setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId);

    /**
     * @brief Get current MIDI input source for a track
     * @return Device ID, or empty string if no input
     */
    juce::String getTrackMidiInput(TrackId trackId) const;

    /**
     * @brief Clear MIDI input routing for a track
     */
    void clearTrackMidiInput(TrackId trackId);

    // =========================================================================
    // MIDI Monitoring (for visualization)
    // =========================================================================

    /**
     * @brief Callback when MIDI note event received on a track
     * Parameters: (trackId, noteEvent)
     * Called from audio thread - keep handlers lightweight!
     */
    std::function<void(TrackId, const MidiNoteEvent&)> onNoteEvent;

    /**
     * @brief Callback when MIDI CC event received on a track
     * Parameters: (trackId, ccEvent)
     * Called from audio thread - keep handlers lightweight!
     */
    std::function<void(TrackId, const MidiCCEvent&)> onCCEvent;

    /**
     * @brief Start monitoring MIDI events for a track
     * Enables callbacks for note/CC events
     */
    void startMonitoring(TrackId trackId);

    /**
     * @brief Stop monitoring MIDI events for a track
     */
    void stopMonitoring(TrackId trackId);

    /**
     * @brief Check if monitoring is active for a track
     */
    bool isMonitoring(TrackId trackId) const;

    /**
     * @brief Get the global MIDI event queue for the debug monitor
     * Audio thread pushes, UI thread reads.
     */
    MidiEventQueue& getGlobalEventQueue() {
        return globalEventQueue_;
    }

    void setRecordingQueue(RecordingNoteQueue* queue, std::atomic<double>* transportPos);

    // =========================================================================
    // Raw MIDI listener (for ControllerRouter)
    // =========================================================================

    /**
     * @brief Subscribe to raw MIDI from every open input device.
     *
     * Thread-safe. The listener is called from the MIDI callback thread;
     * implementations must be lock-free.
     */
    void addRawMidiListener(RawMidiListener* listener);
    void removeRawMidiListener(RawMidiListener* listener);

    /**
     * @brief Fan a QWERTY-synthesized note out to the UI layer.
     *
     * Mirrors the physical-MIDI fan-out in handleIncomingMidiMessage:
     *   - Fires MIDI activity + note-on/off UI on EVERY track whose MIDI
     *     input routes this virtual device (or "all"), regardless of
     *     record-arm state.
     *   - Pushes to the recording preview queue ONLY for armed tracks.
     *
     * @param sourceDeviceId TE device ID of the virtual device that produced
     *                       the note (typically the QWERTY keyboard).
     */
    void broadcastSynthesizedNote(const juce::String& sourceDeviceId, int noteNumber, int velocity,
                                  bool isNoteOn);

    /**
     * @brief Play a QWERTY-keyboard note through whichever engine is live, and
     * fan it out to the UI via broadcastSynthesizedNote.
     */
    void playQwertyNote(int note, int velocity, bool isNoteOn);

    /// Enables or disables the QWERTY device on the fork, and for the sink path.
    void setQwertyEnabled(bool enabled);

    /// What the native engine's virtual-input list reads, having no fork device to ask.
    bool isQwertyEnabled() const {
        return qwertyEnabled_;
    }

    void resetTestState();

  private:
    MidiBridge();

    /// Stop all MIDI inputs and drain in-flight callbacks, so none races the teardown.
    void stopAllInputs();

    // MidiInputCallback implementation
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    // Shared note-event fan-out for both real MIDI (handleIncomingMidiMessage)
    // and synthesized QWERTY notes (broadcastSynthesizedNote): fires onNoteEvent
    // only when the track is being monitored. Caller must hold routingLock_.
    void notifyNoteEventIfMonitored(TrackId trackId, int noteNumber, int velocity, bool isNoteOn);

    // Whoever attached, and what they offer beside the system's inputs (#2759).
    const void* owner_ = nullptr;
    std::function<std::vector<MidiDeviceInfo>()> virtualInputs_;

    // AudioBridge reference for triggering MIDI activity (not owned)
    AudioBridge* audioBridge_ = nullptr;

    // Shared MIDI-activity monitor; not owned (#2579).
    TrackMeters* meters_ = nullptr;

    // Where live MIDI goes under the magda engine; not owned (#2579).
    std::atomic<LiveMidiSink*> liveSink_{nullptr};

    // QWERTY enable state for the sink path, where there is no fork device to ask.
    bool qwertyEnabled_ = false;

    // Track MIDI input routing (trackId → MIDI device ID)
    std::unordered_map<TrackId, juce::String> trackMidiInputs_;

    // Tracks being monitored for MIDI activity
    std::unordered_set<TrackId> monitoredTracks_;

    // Active MIDI input listeners (deviceId → MidiInput)
    std::unordered_map<juce::String, std::unique_ptr<juce::MidiInput>> activeMidiInputs_;

    // Active MIDI outputs (JUCE identifier → MidiOutput). Opened lazily by
    // sendMidi() and kept alive until the engine is forgotten.
    std::unordered_map<juce::String, std::unique_ptr<juce::MidiOutput>> activeMidiOutputs_;

    // Synchronization for UI thread access
    mutable juce::CriticalSection routingLock_;

    // Whether to forward MIDI to instrument plugins
    bool forwardMidiToPlugins_ = true;

    // Global MIDI event queue for debug monitor (audio thread → UI thread)
    MidiEventQueue globalEventQueue_;

    // Recording note queue for real-time MIDI preview (not owned)
    RecordingNoteQueue* recordingQueue_ = nullptr;
    std::atomic<double>* transportPosition_ = nullptr;

    // Shutdown guard: prevents CoreMIDI callbacks from accessing destroyed state
    std::atomic<bool> isShuttingDown_{false};
    std::atomic<int> activeCallbacks_{0};

    /** @brief Open what a route names once it is plugged in, and close what was unplugged. */
    void refreshMidiInputs();

    // Only while an engine is attached: it registers with a JUCE global that must be let
    // go of on the message thread, and this static outlives the message manager (#2759).
    // Last, so it disconnects before anything its callback reads is destroyed.
    juce::MidiDeviceListConnection deviceList_;

    juce::ListenerList<Listener> midiDeviceListListeners_;

    // Raw MIDI listeners (for ControllerRouter)
    juce::Array<RawMidiListener*> rawMidiListeners_;
    juce::CriticalSection rawMidiListenersLock_;

    // No leak detector: the one instance outlives it on purpose, and would be reported.
    JUCE_DECLARE_NON_COPYABLE(MidiBridge)
};

}  // namespace magda
